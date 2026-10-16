param(
    [ValidateSet('Configure', 'Build', 'Test', 'Verify')]
    [string]$Action = 'Verify',
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [ValidateSet('Auto', 'VS2019', 'VS2022')]
    [string]$Toolchain = 'Auto'
)

$ErrorActionPreference = 'Stop'
$cmake = Join-Path $env:LOCALAPPDATA 'CubaseVST\tools\cmake-3.31.8-windows-x86_64\bin\cmake.exe'
if (-not (Test-Path $cmake)) { & (Join-Path $PSScriptRoot 'Bootstrap.ps1') }

function Test-VisualStudio([string]$range) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $false }
    $path = & $vswhere -products * -version $range -latest `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    return [bool]$path
}

# Visual Studio 2019 stays the default; 2022 is used when 2019 is not installed.
if ($Toolchain -eq 'Auto') {
    $Toolchain = if (Test-VisualStudio '[16.0,17.0)') { 'VS2019' }
        elseif (Test-VisualStudio '[17.0,18.0)') { 'VS2022' }
        else { throw 'No Visual Studio 2019 or 2022 with the C++ x64 tools was found.' }
}
$configurePreset = if ($Toolchain -eq 'VS2022') { 'windows-vs2022' } else { 'windows-vs2019' }
$suffix = if ($Toolchain -eq 'VS2022') { '-vs2022' } else { '' }
$preset = $Configuration.ToLowerInvariant() + $suffix
Write-Host "Toolchain: $Toolchain (configure preset $configurePreset, preset $preset)"

Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    if ($Action -in 'Configure', 'Build', 'Verify') {
        & $cmake --preset $configurePreset
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
