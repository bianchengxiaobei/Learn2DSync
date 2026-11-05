[CmdletBinding()]
param(
    [string]$InstallDir = (Join-Path $env:ProgramData "NSYNC\server"),
    [int]$Port = 40100,
    [int]$Players = 10,
    [string]$Map = "maps/arena_01.json",
    [int]$MatchDurationTicks = 18000,
    [int]$ScoreLimit = 30,
    [int]$LobbyCountdownMs = 3000,
    [int]$MatchEndDelayMs = 3000,
    [int]$LobbyMinHumans = 1,
    [string]$LogLevel = "info",
    [string]$PackageZip = "",
    [string]$BuildDir = "",
    [switch]$Install,
    [switch]$Start,
    [switch]$Stop,
    [switch]$Status,
    [switch]$Uninstall,
    [switch]$Force,
    [switch]$OpenFirewall
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $root "build\vs2022"
}
$InstallDir = [IO.Path]::GetFullPath($InstallDir)
$exePath = Join-Path $InstallDir "nsync_server.exe"
$configPath = Join-Path $InstallDir "config\server.ini"
$pidPath = Join-Path $InstallDir "server.pid"
$logDir = Join-Path $InstallDir "logs"
$firewallRule = "NSYNC Server UDP $Port"

function Get-ServerProcess {
    if (-not (Test-Path -LiteralPath $pidPath)) {
        return $null
    }
    $text = [IO.File]::ReadAllText($pidPath).Trim()
    $serverPid = 0
    if (-not [int]::TryParse($text, [ref]$serverPid)) {
        return $null
    }
    $process = Get-Process -Id $serverPid -ErrorAction SilentlyContinue
    if ($null -eq $process) {
        return $null
    }
    if ($process.ProcessName -ne "nsync_server") {
        return $null
    }
    return $process
}

function Get-ConfiguredPort {
    if (Test-Path -LiteralPath $configPath) {
        $text = [IO.File]::ReadAllText($configPath)
        if ($text -match '(?m)^server\.port=(\d+)') {
            return [int]$Matches[1]
        }
    }
    return $Port
}

function Test-ServerPort {
    param([int]$LocalPort)
    try {
        $endpoint = Get-NetUDPEndpoint -LocalPort $LocalPort -ErrorAction SilentlyContinue
        if ($null -ne $endpoint) {
            return $true
        }
    } catch {
    }
    $lines = & netstat -ano -p UDP 2>$null
    foreach ($line in $lines) {
        if ($line -match ":$LocalPort\s+") {
            return $true
        }
    }
    return $false
}

function Get-LatestLog {
    if (-not (Test-Path -LiteralPath $logDir)) {
        return $null
    }
    return Get-ChildItem -LiteralPath $logDir -Filter "*.out.log" -File |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
}

function Stop-DeployedServer {
    $process = Get-ServerProcess
    if ($null -eq $process) {
        Remove-Item -LiteralPath $pidPath -Force -ErrorAction SilentlyContinue
        Write-Host "server is not running"
        return
    }
    Stop-Process -Id $process.Id -Force
    for ($index = 0; $index -lt 25; ++$index) {
        if ($null -eq (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) {
            break
        }
        Start-Sleep -Milliseconds 200
    }
    if ($null -ne (Get-Process -Id $process.Id -ErrorAction SilentlyContinue)) {
        & taskkill /PID $process.Id /T /F | Out-Null
    }
    Remove-Item -LiteralPath $pidPath -Force -ErrorAction SilentlyContinue
    Write-Host ("server stopped (pid {0})" -f $process.Id)
}

function Write-ServerConfig {
    $configText = @"
# NSYNC server deployment
server.tick_rate=60
server.max_catchup_steps=5
server.max_ticks=0
log.level=$LogLevel
server.net=true
server.port=$Port
server.players=$Players
server.map=$Map
server.seed=1
server.snapshot_interval=3
server.input_sync_model=original
server.server_lag_ticks=8
server.client_event_lag_ticks=3
server.sync_period_ticks=30
server.match_duration_ticks=$MatchDurationTicks
server.score_limit=$ScoreLimit
server.mode=tdm
server.lobby=true
server.lobby_countdown_ms=$LobbyCountdownMs
server.match_end_delay_ms=$MatchEndDelayMs
server.lobby_min_humans=$LobbyMinHumans
update.url=
"@
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($configPath, $configText, $utf8)
}

function Install-DeployedServer {
    if ($Port -lt 1 -or $Port -gt 65535) {
        throw "invalid port"
    }
    if ($Players -lt 1 -or $Players -gt 10) {
        throw "invalid player count"
    }
    if ($LobbyMinHumans -lt 1 -or $LobbyMinHumans -gt $Players) {
        throw "invalid lobby minimum"
    }

    $temporary = $null
    try {
        if (-not [string]::IsNullOrWhiteSpace($PackageZip)) {
            $temporary = Join-Path $env:TEMP (
                "nsync-deploy-" + [Guid]::NewGuid().ToString("N"))
            Expand-Archive -LiteralPath $PackageZip -DestinationPath $temporary
            $found = Get-ChildItem -LiteralPath $temporary -Recurse -Filter "nsync_server.exe" |
                Select-Object -First 1
            if ($null -eq $found) {
                throw "nsync_server.exe not found in $PackageZip"
            }
            $sourceDir = $found.Directory.FullName
        } else {
            $sourceDir = Join-Path $BuildDir "Release"
            if (-not (Test-Path -LiteralPath (Join-Path $sourceDir "nsync_server.exe"))) {
                if (Test-Path -LiteralPath (Join-Path $root "nsync_server.exe")) {
                    $sourceDir = $root
                }
            }
        }

        $sourceExe = Join-Path $sourceDir "nsync_server.exe"
        if (-not (Test-Path -LiteralPath $sourceExe)) {
            throw "missing $sourceExe; build Release or pass -PackageZip"
        }
        $assetsSource = Join-Path $sourceDir "assets"
        if (-not (Test-Path -LiteralPath (Join-Path $assetsSource "data\content.json"))) {
            $assetsSource = Join-Path $root "assets"
        }
        if (-not (Test-Path -LiteralPath (Join-Path $assetsSource "data\content.json"))) {
            throw "assets/data/content.json not found"
        }
        $mapPath = Join-Path $assetsSource $Map
        if (-not (Test-Path -LiteralPath $mapPath)) {
            throw "map not found: $Map"
        }

        $running = Get-ServerProcess
        if ($null -ne $running) {
            if (-not $Force) {
                throw "server is running; stop it or pass -Force"
            }
            Stop-DeployedServer
        }

        New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
        New-Item -ItemType Directory -Path (Join-Path $InstallDir "config") -Force | Out-Null
        New-Item -ItemType Directory -Path $logDir -Force | Out-Null

        Copy-Item -LiteralPath $sourceExe -Destination $InstallDir -Force
        Get-ChildItem -LiteralPath $sourceDir -Filter "*.dll" -File |
            ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination $InstallDir -Force
            }
        $readme = Join-Path $sourceDir "README.md"
        if (Test-Path -LiteralPath $readme) {
            Copy-Item -LiteralPath $readme -Destination $InstallDir -Force
        }
        Copy-Item -LiteralPath $PSCommandPath -Destination $InstallDir -Force

        $assetsTarget = Join-Path $InstallDir "assets"
        if (Test-Path -LiteralPath $assetsTarget) {
            Remove-Item -LiteralPath $assetsTarget -Recurse -Force
        }
        Copy-Item -LiteralPath $assetsSource -Destination $assetsTarget -Recurse -Force
        $originalHeroes = Join-Path $assetsTarget "generated\original_heroes"
        if (Test-Path -LiteralPath $originalHeroes) {
            Remove-Item -LiteralPath $originalHeroes -Recurse -Force
        }

        Write-ServerConfig

        if ($OpenFirewall) {
            try {
                $existing = Get-NetFirewallRule -DisplayName $firewallRule -ErrorAction SilentlyContinue
                if ($null -eq $existing) {
                    New-NetFirewallRule `
                        -DisplayName $firewallRule `
                        -Direction Inbound `
                        -Protocol UDP `
                        -LocalPort $Port `
                        -Action Allow `
                        -Profile Any | Out-Null
                } elseif ($existing.Enabled -ne "True") {
                    Enable-NetFirewallRule -DisplayName $firewallRule | Out-Null
                }
            } catch {
                Write-Warning ("firewall rule was not applied: {0}" -f $_.Exception.Message)
            }
        }

        Write-Host ("server installed: {0}" -f $InstallDir)
        Write-Host ("config: {0}" -f $configPath)
        Write-Host ("start: powershell -File `"{0}\deploy_server.ps1`" -Start -InstallDir `"{0}`"" -f $InstallDir)
    } finally {
        if ($null -ne $temporary -and (Test-Path -LiteralPath $temporary)) {
            Remove-Item -LiteralPath $temporary -Recurse -Force
        }
    }
}

function Start-DeployedServer {
    if (-not (Test-Path -LiteralPath $exePath)) {
        throw "server is not installed; run -Install first"
    }
    $running = Get-ServerProcess
    if ($null -ne $running) {
        Write-Host ("server already running (pid {0})" -f $running.Id)
        return
    }
    $listenPort = Get-ConfiguredPort
    if (Test-ServerPort $listenPort) {
        throw "UDP port $listenPort is already in use"
    }
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $stdout = Join-Path $logDir ("server-$stamp.out.log")
    $stderr = Join-Path $logDir ("server-$stamp.err.log")
    $arguments = "--net --config `"$configPath`""
    $process = Start-Process `
        -FilePath $exePath `
        -ArgumentList $arguments `
        -WorkingDirectory $InstallDir `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr `
        -WindowStyle Hidden `
        -PassThru
    [IO.File]::WriteAllText(
        $pidPath,
        $process.Id.ToString(),
        (New-Object System.Text.UTF8Encoding($false)))
    $listening = $false
    for ($index = 0; $index -lt 40; ++$index) {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
        if ($process.HasExited) {
            break
        }
        if (Test-ServerPort $listenPort) {
            $listening = $true
            break
        }
    }
    if ($process.HasExited -or -not $listening) {
        $tail = ""
        if (Test-Path -LiteralPath $stdout) {
            $tail = (Get-Content -LiteralPath $stdout -Tail 20 -ErrorAction SilentlyContinue) -join "`n"
        }
        if (Test-Path -LiteralPath $stderr) {
            $tail += (Get-Content -LiteralPath $stderr -Tail 20 -ErrorAction SilentlyContinue) -join "`n"
        }
        Remove-Item -LiteralPath $pidPath -Force -ErrorAction SilentlyContinue
        throw ("server failed to start`n" + $tail)
    }
    Write-Host ("server started (pid {0}, udp {1})" -f $process.Id, $listenPort)
    Write-Host ("log: {0}" -f $stdout)
}

function Show-DeployedServer {
    $process = Get-ServerProcess
    $listenPort = Get-ConfiguredPort
    $listening = Test-ServerPort $listenPort
    if ($null -eq $process) {
        Write-Host ("server: stopped (udp {0}: {1})" -f $listenPort, $(if ($listening) { "in use" } else { "free" }))
        exit 1
    }
    Write-Host ("server: running pid={0} udp={1}:{2}" -f $process.Id, $listenPort, $(if ($listening) { "listening" } else { "not listening" }))
    $log = Get-LatestLog
    if ($null -ne $log) {
        Write-Host ("log: {0}" -f $log.FullName)
    }
    exit 0
}

if ($Stop -or $Uninstall) {
    Stop-DeployedServer
    if ($Uninstall) {
        $uninstallPort = Get-ConfiguredPort
        $uninstallRule = "NSYNC Server UDP $uninstallPort"
        try {
            Remove-NetFirewallRule -DisplayName $uninstallRule -ErrorAction SilentlyContinue | Out-Null
        } catch {
        }
        if ($Force) {
            if (Test-Path -LiteralPath $InstallDir) {
                Remove-Item -LiteralPath $InstallDir -Recurse -Force
                Write-Host ("removed {0}" -f $InstallDir)
            }
        } else {
            Write-Host "pass -Force to remove the install directory"
        }
    }
    exit 0
}

if ($Status -and -not $Install -and -not $Start) {
    Show-DeployedServer
}

$needsInstall = $Install -or -not (Test-Path -LiteralPath $exePath)
if ($needsInstall) {
    Install-DeployedServer
}
if ($Start) {
    Start-DeployedServer
} elseif (-not $Install -and -not $Status) {
    Write-Host "server installed; pass -Start to launch it"
}
if ($Status) {
    Show-DeployedServer
}
exit 0
