param(
    [Parameter(Mandatory = $true)][string]$BinDir,
    [int]$Port = 40191,
    [int]$Ticks = 240,
    [int]$ClientCount = 2,
    [int]$RttMs = 50,
    [int]$JitterMs = 0,
    [int]$LossPercent = 0,
    [int]$DuplicatePercent = 0,
    [int]$BurstLength = 1,
    [int]$SpikeMs = 0,
    [int]$SpikeIntervalMs = 0,
    [int]$SpikeDurationMs = 0,
    [int]$InputDelay = 16,
    [int]$ServerTicks = 600,
    [int]$MatchDurationTicks = 0,
    [int]$MatchEndDelayMs = -1,
    [int]$SyncTimeToStepMs = 0,
    [int]$ClientTimeoutMs = 45000,
    [int]$MinMatches = 1,
    [switch]$AutoRematch,
    [switch]$LateInputReplay,
    [switch]$ClientLag,
    [switch]$FixedStepScheduler,
    [switch]$RequireWanSimulation,
    [switch]$RequireSpike,
    [switch]$RequireTrace,
    [string]$MapPath = "",
    [string]$TraceFile = ""
)

$ErrorActionPreference = "Stop"
$serverLog = Join-Path $env:TEMP "nsync_real_server_$Port.log"
$clientLogs = @()
for ($i = 0; $i -lt $ClientCount; ++$i) {
    $clientLogs += Join-Path $env:TEMP "nsync_real_client$($i)_$Port.log"
}

$serverArgs = @(
    "--net",
    "--port", "$Port",
    "--players", "$ClientCount",
    "--lobby-min-humans", "$ClientCount",
    "--ticks", "$ServerTicks",
    "--log-level", $(if ($MinMatches -gt 1) { "info" } else { "warn" })
)
if ($LateInputReplay) {
    $serverArgs += "--late-input-replay"
}
if ($MatchDurationTicks -gt 0) {
    $serverArgs += @("--match-duration-ticks", "$MatchDurationTicks")
}
if ($MatchEndDelayMs -ge 0) {
    $serverArgs += @("--match-end-delay-ms", "$MatchEndDelayMs")
}
if ($SyncTimeToStepMs -ne 0) {
    $serverArgs += @("--sync-time-to-step-ms", "$SyncTimeToStepMs")
}
$clientArgs = @(
    "--client",
    "--host", "127.0.0.1",
    "--port", "$Port",
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
if ($AutoRematch) {
    $clientArgs += "--auto-rematch"
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

$server = $null
$clients = @()
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

    $ok = $true
    for ($i = 0; $i -lt $clients.Count; ++$i) {
        if (-not $clients[$i].WaitForExit($ClientTimeoutMs)) {
            Write-Error "client $i timed out"
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
        } elseif ($clientOutput -match "corrections=[1-9]" -or
                  $clientOutput -match "desyncs=[1-9]") {
            Write-Error "client $i reported corrections/desync"
            $ok = $false
        } elseif ($RequireTrace -and
                  $clientOutput -notmatch " trace=[1-9]") {
            Write-Error "client $i did not exercise WAN trace"
            $ok = $false
        } elseif ($RequireSpike -and
                  $clientOutput -notmatch " spike=[1-9]") {
            Write-Error "client $i did not exercise latency spike simulation"
            $ok = $false
        } elseif ($RequireWanSimulation -and
                  ($clientOutput -notmatch " lost=[1-9]" -or
                   $clientOutput -notmatch " dup=[1-9]")) {
            Write-Error "client $i did not exercise loss/jitter/duplicate simulation"
            $ok = $false
        } elseif ($FixedStepScheduler -and
                  $clientOutput -notmatch "steps=[1-9]") {
            Write-Error "client $i did not advance through fixed-step scheduler"
            $ok = $false
        } elseif ($MinMatches -gt 1) {
            $match = [regex]::Match($clientOutput, "match=([0-9]+)")
            if (-not $match.Success -or
                [int]$match.Groups[1].Value -lt $MinMatches) {
                Write-Error "client $i did not reach match $MinMatches"
                $ok = $false
            }
        }
    }

    if ($server.HasExited -and $null -ne $server.ExitCode -and
        $server.ExitCode -ne 0) {
        Write-Error "server failed with exit code $($server.ExitCode)"
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
