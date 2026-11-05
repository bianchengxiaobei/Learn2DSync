[CmdletBinding()]
param(
    [string]$Configuration = "Release",
    [string]$BuildDir = "",
    [string]$OutDir = "",
    [string]$Version = "",
    [switch]$NoBuild,
    [switch]$IncludeOriginalHeroArt,
    [switch]$IncludeOriginalMapArt
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $root "build\vs2022"
}
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $root "dist"
}
if ([string]::IsNullOrWhiteSpace($Version)) {
    $cmakeText = [IO.File]::ReadAllText((Join-Path $root "CMakeLists.txt"))
    if ($cmakeText -match 'project\(nsync VERSION ([0-9]+\.[0-9]+\.[0-9]+)') {
        $Version = $Matches[1]
    } else {
        $Version = "0.1.0"
    }
}

$configDir = Join-Path $BuildDir $Configuration
if (-not $NoBuild) {
    $preset = $Configuration.ToLowerInvariant()
    & cmake --build --preset $preset --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "build failed"
    }
}

$clientExe = Join-Path $configDir "nsync_client.exe"
$serverExe = Join-Path $configDir "nsync_server.exe"
if (-not (Test-Path -LiteralPath $clientExe)) {
    throw "missing $clientExe"
}
if (-not (Test-Path -LiteralPath $serverExe)) {
    throw "missing $serverExe"
}

$stageName = "nsync-$Version-win64"
$stage = Join-Path $OutDir $stageName
$zip = Join-Path $OutDir ($stageName + ".zip")
if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Copy-Item -LiteralPath $clientExe -Destination $stage
Copy-Item -LiteralPath $serverExe -Destination $stage
Get-ChildItem -LiteralPath $configDir -Filter "*.dll" -File |
    ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $stage
    }

$assetSource = Join-Path $configDir "assets"
if (-not (Test-Path -LiteralPath (Join-Path $assetSource "data\content.json"))) {
    $assetSource = Join-Path $root "assets"
}
Copy-Item -LiteralPath $assetSource -Destination (Join-Path $stage "assets") -Recurse

$configSource = Join-Path $configDir "config"
if (-not (Test-Path -LiteralPath (Join-Path $configSource "client.ini"))) {
    $configSource = Join-Path $root "config"
}
Copy-Item -LiteralPath $configSource -Destination (Join-Path $stage "config") -Recurse

$shaderSource = Join-Path $configDir "shaders"
if (-not (Test-Path -LiteralPath (Join-Path $shaderSource "vs_dx11.bin"))) {
    $shaderSource = Join-Path $root "shaders"
}
Copy-Item -LiteralPath $shaderSource -Destination (Join-Path $stage "shaders") -Recurse
Copy-Item -LiteralPath (Join-Path $root "README.md") -Destination $stage
$packageScripts = Join-Path $stage "scripts"
New-Item -ItemType Directory -Path $packageScripts -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root "scripts\deploy_server.ps1") `
    -Destination $packageScripts

$originalDir = Join-Path $stage "assets\generated\original_heroes"
if (-not $IncludeOriginalHeroArt.IsPresent) {
    if (Test-Path -LiteralPath $originalDir) {
        Remove-Item -LiteralPath $originalDir -Recurse -Force
    }
    $clientIni = Join-Path $stage "config\client.ini"
    $clientText = [IO.File]::ReadAllText($clientIni)
    if ($clientText -match '(?m)^client\.hero_art=') {
        $clientText = [Regex]::Replace(
            $clientText,
            '(?m)^client\.hero_art=.*$',
            'client.hero_art=placeholder')
    } else {
        $clientText += "`r`nclient.hero_art=placeholder`r`n"
    }
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($clientIni, $clientText, $utf8)
}

$originalMapDir = Join-Path $stage "assets\generated\original_map"
if (-not $IncludeOriginalMapArt.IsPresent) {
    if (Test-Path -LiteralPath $originalMapDir) {
        Remove-Item -LiteralPath $originalMapDir -Recurse -Force
    }
    Get-ChildItem -LiteralPath (Join-Path $stage "assets\maps") `
        -Filter "*_art.json" -File -ErrorAction SilentlyContinue |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
}

$required = @(
    "nsync_client.exe",
    "nsync_server.exe",
    "assets\data\content.json",
    "assets\maps\arena_01.json",
    "config\client.ini",
    "config\server.ini",
    "shaders\vs_dx11.bin",
    "shaders\fs_dx11.bin",
    "scripts\deploy_server.ps1"
)
foreach ($relative in $required) {
    $path = Join-Path $stage $relative
    if (-not (Test-Path -LiteralPath $path)) {
        throw "package missing $relative"
    }
}

$manifest = New-Object System.Collections.Generic.List[string]
$manifest.Add("nsync $Version windows x64")
$manifest.Add("configuration=$Configuration")
$manifest.Add("packaged_utc=$([DateTime]::UtcNow.ToString('o'))")
$manifest.Add("original_hero_art=$($IncludeOriginalHeroArt.IsPresent)")
$manifest.Add("original_map_art=$($IncludeOriginalMapArt.IsPresent)")
$manifest.Add("files:")
Get-ChildItem -LiteralPath $stage -Recurse -File |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($stage.Length + 1).Replace('\', '/')
        $manifest.Add(("{0} {1}" -f $relative, $_.Length))
    }
[IO.File]::WriteAllLines(
    (Join-Path $stage "PACKAGE_MANIFEST.txt"),
    $manifest,
    (New-Object System.Text.UTF8Encoding($false)))

if (Test-Path -LiteralPath $zip) {
    Remove-Item -LiteralPath $zip -Force
}
Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
[IO.File]::WriteAllText(
    ($zip + ".sha256"),
    ("{0}  {1}`r`n" -f $hash, [IO.Path]::GetFileName($zip)),
    (New-Object System.Text.UTF8Encoding($false)))

$sizeMb = [math]::Round((Get-Item -LiteralPath $zip).Length / 1MB, 2)
Write-Host ("package: {0}" -f $zip)
Write-Host ("size: {0} MiB" -f $sizeMb)
Write-Host ("sha256: {0}" -f $hash)
