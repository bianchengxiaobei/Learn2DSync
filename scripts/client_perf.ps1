param(
    [Parameter(Mandatory = $true)][string]$BinDir,
    [int]$Port = 41400,
    [int]$Frames = 18000,
    [int]$Width = 1280,
    [int]$Height = 720,
    [int]$TimeoutSeconds = 120,
    [string]$GpuVendor = "",
    [int]$MatchTicks = 0
)

$ErrorActionPreference = "Stop"
$mapPath = Join-Path (Split-Path -Parent $PSScriptRoot) "assets\maps\arena_01.json"
$serverLog = Join-Path $env:TEMP "nsync_perf_server_$Port.log"
$botLog = Join-Path $env:TEMP "nsync_perf_bot_$Port.log"
$clientLog = Join-Path $env:TEMP "nsync_perf_client_$Port.log"
Remove-Item $serverLog, "$serverLog.err", $botLog, "$botLog.err", `
    $clientLog, "$clientLog.err" -ErrorAction SilentlyContinue

$serverTicks = [math]::Max(3600, [int]($Frames / 6 + 1200))
$matchTicks = [math]::Max(3000, [int]($Frames / 6 + 600))
$server = $null
$bot = $null
$client = $null
try {
    $server = Start-Process `
        -FilePath (Join-Path $BinDir "nsync_server.exe") `
        -ArgumentList @(
            "--net",
            "--port", "$Port",
            "--players", "2",
            "--lobby-min-humans", "2",
            "--ticks", "$serverTicks",
            "--match-duration-ticks", "$matchTicks",
            "--match-end-delay-ms", "500",
            "--log-level", "warn",
            "--map", $mapPath
        ) `
        -WorkingDirectory $BinDir `
        -RedirectStandardOutput $serverLog `
        -RedirectStandardError "$serverLog.err" `
        -PassThru

    Start-Sleep -Milliseconds 800

    $bot = Start-Process `
        -FilePath (Join-Path $BinDir "nsync_net_real.exe") `
        -ArgumentList @(
            "--client",
            "--host", "127.0.0.1",
            "--port", "$Port",
            "--ticks", "$serverTicks",
            "--input-delay", "16",
            "--client-lag",
            "--fixed-step-scheduler",
            "--log-level", "warn",
            "--map", $mapPath
        ) `
        -WorkingDirectory $BinDir `
        -RedirectStandardOutput $botLog `
        -RedirectStandardError "$botLog.err" `
        -PassThru

    $clientArgs = @(
        "--connect",
        "--server-host", "127.0.0.1",
        "--server-port", "$Port",
        "--frames", "$Frames",
        "--no-vsync",
        "--no-audio",
        "--no-menu",
        "--width", "$Width",
        "--height", "$Height",
        "--name", "perf",
        "--log-level", "info"
    )
    if ($GpuVendor -ne "") {
        $clientArgs += @("--gpu-vendor", $GpuVendor)
    }
    $client = Start-Process `
        -FilePath (Join-Path $BinDir "nsync_client.exe") `
        -ArgumentList $clientArgs `
        -WorkingDirectory $BinDir `
        -RedirectStandardOutput $clientLog `
        -RedirectStandardError "$clientLog.err" `
        -PassThru

    if (-not $client.WaitForExit($TimeoutSeconds * 1000)) {
        throw "client did not finish within $TimeoutSeconds s"
    }
    Start-Sleep -Milliseconds 300
    $output = Get-Content $clientLog -Raw
    if ($output -match "login failed" -or $output -match "connect failed") {
        throw "client connection failed"
    }
    if ($output -notmatch "match started id=([0-9]+)") {
        throw "client did not enter a match"
    }
    $render = [regex]::Match(
        $output,
        "render stats: frames=([0-9]+) elapsed_ms=([0-9]+) avg_fps_x10=([0-9]+)")
    $online = [regex]::Match(
        $output,
        "online stats:.*corrections=([0-9]+).*desyncs=([0-9]+).*" +
        "bytes_sent=([0-9]+) bytes_received=([0-9]+)")
    if (-not $render.Success -or -not $online.Success) {
        throw "missing render/online stats in client log"
    }

    $frames = [int]$render.Groups[1].Value
    $elapsedMs = [int]$render.Groups[2].Value
    $avgFps = [double]$render.Groups[3].Value / 10.0
    $upKbps = if ($elapsedMs -gt 0) {
        [math]::Round(
            [double]$online.Groups[3].Value * 8.0 / 1024.0 / ($elapsedMs / 1000.0),
            2)
    } else { 0 }
    $downKbps = if ($elapsedMs -gt 0) {
        [math]::Round(
            [double]$online.Groups[4].Value * 8.0 / 1024.0 / ($elapsedMs / 1000.0),
            2)
    } else { 0 }

    $result = (
        "perf result: ok={0} width={1} height={2} frames={3} elapsed_ms={4} " +
        "avg_fps={5} corrections={6} desyncs={7} avg_up_kbps={8} avg_down_kbps={9} " +
        "gpu_vendor={10}") -f `
        ($online.Groups[1].Value -eq "0" -and $online.Groups[2].Value -eq "0"),
        $Width, $Height, $frames, $elapsedMs, $avgFps,
        $online.Groups[1].Value, $online.Groups[2].Value, $upKbps, $downKbps, $GpuVendor
    Write-Output $result

    Get-Content $clientLog | Where-Object { $_ -match "perf:|render stats:|online stats:" }
    if ($online.Groups[1].Value -ne "0" -or $online.Groups[2].Value -ne "0") {
        exit 1
    }
} finally {
    if ($null -ne $client -and -not $client.HasExited) {
        Stop-Process -Id $client.Id -Force -ErrorAction SilentlyContinue
    }
    if ($null -ne $bot -and -not $bot.HasExited) {
        Stop-Process -Id $bot.Id -Force -ErrorAction SilentlyContinue
    }
    if ($null -ne $server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
    }
}
