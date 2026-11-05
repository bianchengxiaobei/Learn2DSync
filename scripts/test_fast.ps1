[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build\vs2022"

if (-not (Test-Path -LiteralPath $buildDir)) {
    throw "build directory not found: $buildDir"
}

if (-not $NoBuild) {
    $targets = @(
        "nsync_tests",
        "nsync_replay",
        "nsync_datac",
        "nsync_atlas",
        "nsync_mapc",
        "nsync_client",
        "nsync_server",
        "nsync_net"
    )
    & cmake --build $buildDir --config $Config --target $targets
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

$start = Get-Date
& ctest --test-dir $buildDir -C $Config -L FAST --output-on-failure
$exitCode = $LASTEXITCODE
$elapsed = (Get-Date) - $start

if ($exitCode -eq 0) {
    Write-Host ("FAST tests passed in {0:N1}s ({1})" -f $elapsed.TotalSeconds, $Config)
}
exit $exitCode
