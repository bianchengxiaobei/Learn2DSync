param(
    [Parameter(Mandatory = $true)][string]$BinDir,
    [int]$Port = 40301,
    [int]$DurationSeconds = 60,
    [int]$ClientCount = 10,
    [int]$RttMs = 0,
    [int]$JitterMs = 0,
    [int]$LossPercent = 0,
    [int]$DuplicatePercent = 0,
    [int]$BurstLength = 1,
    [int]$SpikeMs = 0,
    [int]$SpikeIntervalMs = 0,
    [int]$SpikeDurationMs = 0,
    [int]$InputDelay = 16,
    [int]$MatchDurationSeconds = 180,
    [int]$MatchEndDelayMs = 500,
    [int]$SampleSeconds = 10,
    [int]$MaxMemoryGrowthMB = 128,
    [int]$MaxHandleGrowth = 512,
    [switch]$ClientLag,
    [switch]$FixedStepScheduler,
    [string]$MapPath = "",
    [string]$TraceFile = ""
)

$ErrorActionPreference = "Stop"
$serverLog = Join-Path $env:TEMP "nsync_soak_server_$Port.log"
$clientLogs = @()
for ($i = 0; $i -lt $ClientCount; ++$i) {
    $clientLogs += Join-Path $env:TEMP "nsync_soak_client$($i)_$Port.log"
}

$serverArgs = @(
    "--net",
    "--port", "$Port",
    "--players", "$ClientCount",
    "--lobby-min-humans", "$ClientCount",
    "--ticks", "$(($DurationSeconds + 30) * 60)",
    "--log-level", "info"
)
if ($MatchDurationSeconds -gt 0) {
    $serverArgs += @("--match-duration-ticks", "$($MatchDurationSeconds * 60)")
}
if ($MatchEndDelayMs -ge 0) {
    $serverArgs += @("--match-end-delay-ms", "$MatchEndDelayMs")
}
$clientArgs = @(
    "--client",
    "--host", "127.0.0.1",
    "--port", "$Port",
    "--ticks", "$($DurationSeconds * 60)",
    "--input-delay", "$InputDelay",
    "--rtt-ms", "$RttMs",
    "--jitter-ms", "$JitterMs",
    "--loss-percent", "$LossPercent",
    "--duplicate-percent", "$DuplicatePercent",
    "--burst-length", "$BurstLength",
    "--spike-ms", "$SpikeMs",
    "--spike-interval-ms", "$SpikeIntervalMs",
    "--spike-duration-ms", "$SpikeDurationMs",
    "--auto-rematch",
    "--log-level", "warn"
)
if ($TraceFile -ne "") {
    $clientArgs += @("--trace-file", $TraceFile)
}
if ($ClientLag) {
    $clientArgs += "--client-lag"
}
if ($FixedStepScheduler) {
    $clientArgs += "--fixed-step-scheduler"
}
if ($MapPath -ne "") {
    $serverArgs += @("--map", $MapPath)
    $clientArgs += @("--map", $MapPath)
}

function Get-ProcessTotals {
    param([object[]]$Processes)
    $ws = [int64]0
    $handles = [int64]0
    $alive = 0
    foreach ($process in $Processes) {
        if ($null -eq $process) {
            continue
        }
        $current = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
        if ($null -eq $current) {
            continue
        }
        $ws += $current.WorkingSet64
        $handles += $current.HandleCount
        ++$alive
    }
    return [pscustomobject]@{
        WorkingSetMB = [math]::Round($ws / 1MB, 2)
        Handles = $handles
        Alive = $alive
    }
}

$server = $null
$clients = @()
$samples = @()
$ok = $true
$start = Get-Date
try {
    $server = Start-Process `
        -FilePath (Join-Path $BinDir "nsync_server.exe") `
        -ArgumentList $serverArgs `
        -WorkingDirectory $BinDir `
        -RedirectStandardOutput $serverLog `
        -RedirectStandardError "$serverLog.err" `
        -PassThru

    Start-Sleep -Milliseconds 600

    for ($i = 0; $i -lt $ClientCount; ++$i) {
        $clients += Start-Process `
            -FilePath (Join-Path $BinDir "nsync_net_real.exe") `
            -ArgumentList $clientArgs `
            -WorkingDirectory $BinDir `
            -RedirectStandardOutput $clientLogs[$i] `
            -RedirectStandardError "$($clientLogs[$i]).err" `
            -PassThru
    }

    $nextSample = 0
    while ((Get-Date) - $start -lt [TimeSpan]::FromSeconds($DurationSeconds + 120)) {
        Start-Sleep -Seconds 1
        $elapsed = [int]((Get-Date) - $start).TotalSeconds
        if ($elapsed -ge $nextSample) {
            $processes = @($server) + $clients
            $totals = Get-ProcessTotals -Processes $processes
            $sample = [pscustomobject]@{
                Seconds = $elapsed
                WorkingSetMB = $totals.WorkingSetMB
                Handles = $totals.Handles
                Alive = $totals.Alive
            }
            $samples += $sample
            Write-Output ("soak sample s={0} ws_mb={1} handles={2} alive={3}" -f `
                $sample.Seconds, $sample.WorkingSetMB, $sample.Handles, $sample.Alive)
            $nextSample = $elapsed + $SampleSeconds
        }

        $runningClients = @($clients | Where-Object {
            -not $_.HasExited
        })
        if ($server.HasExited -or $runningClients.Count -eq 0) {
            break
        }
    }

    foreach ($client in $clients) {
        if (-not $client.HasExited) {
            $client.WaitForExit(30000) | Out-Null
        }
    }
    if ($server.HasExited -and $null -ne $server.ExitCode -and
        $server.ExitCode -ne 0) {
        Write-Error "server failed with exit code $($server.ExitCode)"
        $ok = $false
    }

    for ($i = 0; $i -lt $clients.Count; ++$i) {
        if (-not $clients[$i].HasExited) {
            Write-Error "client $i did not finish within the soak window"
            $ok = $false
            continue
        }
        $clientExit = $clients[$i].ExitCode
        $clientOutput = Get-Content $clientLogs[$i] -Raw -ErrorAction SilentlyContinue
        if ($null -ne $clientExit -and $clientExit -ne 0) {
            Write-Error "client $i failed with exit code $clientExit"
            $ok = $false
        }
        if ($null -eq $clientOutput -or
            $clientOutput -notmatch "client slot=" -or
            $clientOutput -match "connect failed") {
            Write-Error "client $i did not report a successful session"
            $ok = $false
            continue
        }
        if ($clientOutput -match "corrections=[1-9]" -or
            $clientOutput -match "desyncs=[1-9]") {
            Write-Error "client $i reported corrections/desync"
            $ok = $false
        }
        $match = [regex]::Match($clientOutput, "match=([0-9]+)")
        if (-not $match.Success -or [int]$match.Groups[1].Value -lt 2) {
            Write-Error "client $i did not complete a rematch during soak"
            $ok = $false
        }
    }

    $elapsed = [int]((Get-Date) - $start).TotalSeconds
    if ($elapsed -lt $DurationSeconds) {
        Write-Error "soak ended early after $elapsed s, expected $DurationSeconds s"
        $ok = $false
    }

    if ($samples.Count -gt 0) {
        $baseline = $samples[0]
        $final = $samples[$samples.Count - 1]
        $memoryGrowth = [math]::Round($final.WorkingSetMB - $baseline.WorkingSetMB, 2)
        $handleGrowth = $final.Handles - $baseline.Handles
        $peakMemory = ($samples | Measure-Object -Property WorkingSetMB -Maximum).Maximum
        $peakHandles = ($samples | Measure-Object -Property Handles -Maximum).Maximum
        $result = ("soak result: ok={0} elapsed_s={1} clients={2} rtt_ms={3} " +
            "ws_start_mb={4} ws_end_mb={5} ws_growth_mb={6} max_mb={7} " +
            "handles_start={8} handles_end={9} handle_growth={10} max_handles={11}") -f `
            $ok, $elapsed, $ClientCount, $RttMs, $baseline.WorkingSetMB, `
            $final.WorkingSetMB, $memoryGrowth, $peakMemory, $baseline.Handles, `
            $final.Handles, $handleGrowth, $peakHandles
        Write-Output $result
        if ($memoryGrowth -gt $MaxMemoryGrowthMB) {
            Write-Error "memory grew by $memoryGrowth MB, limit is $MaxMemoryGrowthMB MB"
            $ok = $false
        }
        if ($handleGrowth -gt $MaxHandleGrowth) {
            Write-Error "handles grew by $handleGrowth, limit is $MaxHandleGrowth"
            $ok = $false
        }
    } else {
        Write-Error "no soak samples collected"
        $ok = $false
    }

    if (-not $ok) {
        Get-Content $serverLog -ErrorAction SilentlyContinue
        Get-Content "$serverLog.err" -ErrorAction SilentlyContinue
        foreach ($log in $clientLogs) {
            Get-Content $log -ErrorAction SilentlyContinue
            Get-Content "$log.err" -ErrorAction SilentlyContinue
        }
        exit 1
    }
} finally {
    foreach ($client in $clients) {
        if (-not $client.HasExited) {
            Stop-Process -Id $client.Id -Force -ErrorAction SilentlyContinue
        }
    }
    if ($null -ne $server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
    }
}
exit 0
