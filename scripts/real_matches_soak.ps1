param(
    [Parameter(Mandatory = $true)][string]$BinDir,
    [int]$MatchCount = 10,
    [int]$BasePort = 41100,
    [int]$ClientsPerMatch = 2,
    [int]$Ticks = 900,
    [int]$InputDelay = 16,
    [int]$RttMs = 0,
    [int]$JitterMs = 0,
    [int]$LossPercent = 0,
    [int]$DuplicatePercent = 0,
    [int]$BurstLength = 1,
    [int]$SpikeMs = 0,
    [int]$SpikeIntervalMs = 0,
    [int]$SpikeDurationMs = 0,
    [int]$TimeoutSeconds = 180,
    [int]$SampleSeconds = 5,
    [int]$MaxMemoryGrowthMB = 256,
    [int]$MaxHandleGrowth = 2048,
    [switch]$ClientLag,
    [switch]$FixedStepScheduler,
    [string]$MapPath = "",
    [string]$TraceFile = ""
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $BinDir)) {
    throw "binary directory not found: $BinDir"
}
$serverExe = Join-Path $BinDir "nsync_server.exe"
$clientExe = Join-Path $BinDir "nsync_net_real.exe"
if (-not (Test-Path -LiteralPath $serverExe) -or
    -not (Test-Path -LiteralPath $clientExe)) {
    throw "nsync_server.exe/nsync_net_real.exe not found in $BinDir"
}

$servers = @()
$clients = @()
$clientRecords = @()
$samples = @()
$ok = $true
$start = Get-Date
try {
    for ($m = 0; $m -lt $MatchCount; ++$m) {
        $port = $BasePort + $m
        $serverLog = Join-Path $env:TEMP "nsync_multi_server_$port.log"
        $serverArgs = @(
            "--net",
            "--port", "$port",
            "--players", "$ClientsPerMatch",
            "--lobby-min-humans", "$ClientsPerMatch",
            "--ticks", "$($Ticks + 600)",
            "--match-duration-ticks", "$Ticks",
            "--match-end-delay-ms", "500",
            "--log-level", "warn"
        )
        if ($MapPath -ne "") {
            $serverArgs += @("--map", $MapPath)
        }
        $server = Start-Process `
            -FilePath $serverExe `
            -ArgumentList $serverArgs `
            -WorkingDirectory $BinDir `
            -RedirectStandardOutput $serverLog `
            -RedirectStandardError "$serverLog.err" `
            -PassThru
        $servers += [pscustomobject]@{
            Index = $m
            Port = $port
            Process = $server
            Log = $serverLog
        }
    }

    Start-Sleep -Milliseconds 1000

    for ($m = 0; $m -lt $servers.Count; ++$m) {
        $port = $servers[$m].Port
        for ($c = 0; $c -lt $ClientsPerMatch; ++$c) {
            $clientLog = Join-Path $env:TEMP "nsync_multi_client${m}_$($c)_$($port).log"
            $clientArgs = @(
                "--client",
                "--host", "127.0.0.1",
                "--port", "$port",
                "--ticks", "$Ticks",
                "--input-delay", "$InputDelay",
                "--rtt-ms", "$RttMs",
                "--jitter-ms", "$JitterMs",
                "--loss-percent", "$LossPercent",
                "--duplicate-percent", "$DuplicatePercent",
                "--burst-length", "$BurstLength",
                "--spike-ms", "$SpikeMs",
                "--spike-interval-ms", "$SpikeIntervalMs",
                "--spike-duration-ms", "$SpikeDurationMs",
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
                $clientArgs += @("--map", $MapPath)
            }
            $client = Start-Process `
                -FilePath $clientExe `
                -ArgumentList $clientArgs `
                -WorkingDirectory $BinDir `
                -RedirectStandardOutput $clientLog `
                -RedirectStandardError "$clientLog.err" `
                -PassThru
            $clients += $client
            $clientRecords += [pscustomobject]@{
                Match = $m
                Slot = $c
                Port = $port
                Process = $client
                Log = $clientLog
            }
            Start-Sleep -Milliseconds 50
        }
    }

    $nextSample = 0
    while (((Get-Date) - $start).TotalSeconds -lt $TimeoutSeconds) {
        Start-Sleep -Seconds 1
        $elapsed = [int]((Get-Date) - $start).TotalSeconds
        if ($elapsed -ge $nextSample) {
            $processes = @()
            foreach ($server in $servers) {
                $processes += $server.Process
            }
            foreach ($client in $clients) {
                $processes += $client
            }
            $ws = [int64]0
            $handles = [int64]0
            $alive = 0
            foreach ($process in $processes) {
                $current = Get-Process -Id $process.Id -ErrorAction SilentlyContinue
                if ($null -eq $current) {
                    continue
                }
                $ws += $current.WorkingSet64
                $handles += $current.HandleCount
                ++$alive
            }
            $sample = [pscustomobject]@{
                Seconds = $elapsed
                WorkingSetMB = [math]::Round($ws / 1MB, 2)
                Handles = $handles
                Alive = $alive
            }
            $samples += $sample
            Write-Output ("multi sample s={0} ws_mb={1} handles={2} alive={3}" -f `
                $sample.Seconds, $sample.WorkingSetMB, $sample.Handles, $sample.Alive)
            $nextSample = $elapsed + $SampleSeconds
        }
        $runningClients = @($clients | Where-Object { -not $_.HasExited })
        if ($runningClients.Count -eq 0) {
            break
        }
    }

    foreach ($client in $clients) {
        if (-not $client.HasExited) {
            $client.WaitForExit(30000) | Out-Null
        }
    }

    foreach ($record in $clientRecords) {
        $label = "match $($record.Match) client $($record.Slot) port $($record.Port)"
        if (-not $record.Process.HasExited) {
            Write-Error "$label did not finish within the test window"
            $ok = $false
            continue
        }
        $exitCode = $record.Process.ExitCode
        $output = Get-Content $record.Log -Raw -ErrorAction SilentlyContinue
        if ($null -ne $exitCode -and $exitCode -ne 0) {
            Write-Error "$label failed with exit code $exitCode"
            $ok = $false
        }
        if ($null -eq $output -or
            $output -notmatch "client slot=" -or
            $output -match "connect failed") {
            Write-Error "$label did not report a successful session"
            $ok = $false
            continue
        }
        if ($output -match "corrections=[1-9]" -or
            $output -match "desyncs=[1-9]") {
            Write-Error "$label reported corrections/desync"
            $ok = $false
        }
        $match = [regex]::Match($output, "match=([0-9]+)")
        if (-not $match.Success -or [int]$match.Groups[1].Value -lt 1) {
            Write-Error "$label did not start a match"
            $ok = $false
        }
    }

    $elapsed = [int]((Get-Date) - $start).TotalSeconds
    if ($samples.Count -gt 0) {
        $baseline = $samples[0]
        $final = $samples[$samples.Count - 1]
        $memoryGrowth = [math]::Round($final.WorkingSetMB - $baseline.WorkingSetMB, 2)
        $handleGrowth = $final.Handles - $baseline.Handles
        $peakMemory = ($samples | Measure-Object -Property WorkingSetMB -Maximum).Maximum
        $peakHandles = ($samples | Measure-Object -Property Handles -Maximum).Maximum
        $result = ("multi result: ok={0} elapsed_s={1} matches={2} clients={3} " +
            "ws_start_mb={4} ws_end_mb={5} ws_growth_mb={6} max_mb={7} " +
            "handles_start={8} handles_end={9} handle_growth={10} max_handles={11}") -f `
            $ok, $elapsed, $MatchCount, $clients.Count, $baseline.WorkingSetMB, `
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
        Write-Error "no samples collected"
        $ok = $false
    }

    if (-not $ok) {
        foreach ($server in $servers) {
            Get-Content $server.Log -ErrorAction SilentlyContinue
            Get-Content "$($server.Log).err" -ErrorAction SilentlyContinue
        }
        foreach ($record in $clientRecords) {
            Get-Content $record.Log -ErrorAction SilentlyContinue
            Get-Content "$($record.Log).err" -ErrorAction SilentlyContinue
        }
        exit 1
    }
} finally {
    foreach ($client in $clients) {
        if (-not $client.HasExited) {
            Stop-Process -Id $client.Id -Force -ErrorAction SilentlyContinue
        }
    }
    foreach ($server in $servers) {
        if (-not $server.Process.HasExited) {
            Stop-Process -Id $server.Process.Id -Force -ErrorAction SilentlyContinue
        }
    }
}
