param(
    [ValidateSet('Configure', 'Build', 'Test', 'Verify')]
    [string]$Action = 'Verify',
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$cmake = Join-Path $env:LOCALAPPDATA 'CubaseVST\tools\cmake-3.31.8-windows-x86_64\bin\cmake.exe'
if (-not (Test-Path $cmake)) { & (Join-Path $PSScriptRoot 'Bootstrap.ps1') }
$preset = $Configuration.ToLowerInvariant()
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    if ($Action -in 'Configure', 'Build', 'Verify') {
        & $cmake --preset windows-vs2019
        if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    }
    if ($Action -in 'Build', 'Verify') {
        & $cmake --build --preset $preset --parallel 2
        if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
    }
    if ($Action -in 'Test', 'Verify') {
        & (Join-Path (Split-Path $cmake -Parent) 'ctest.exe') --preset $preset
        if ($LASTEXITCODE -ne 0) { throw 'Processor tests failed.' }
    }
}
finally { Pop-Location }