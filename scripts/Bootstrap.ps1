param()

$ErrorActionPreference = 'Stop'
$version = '3.31.8'
$archiveName = "cmake-$version-windows-x86_64.zip"
$toolsRoot = Join-Path $env:LOCALAPPDATA 'CubaseVST\tools'
$cmakePath = Join-Path $toolsRoot "cmake-$version-windows-x86_64\bin\cmake.exe"

if (-not (Test-Path $cmakePath)) {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    New-Item -ItemType Directory -Path $toolsRoot -Force | Out-Null
    $releaseUrl = "https://github.com/Kitware/CMake/releases/download/v$version"
    $archivePath = Join-Path $toolsRoot $archiveName
    $hashes = (Invoke-WebRequest "$releaseUrl/cmake-$version-SHA-256.txt" -UseBasicParsing).Content
    if ($hashes -is [byte[]]) { $hashes = [Text.Encoding]::UTF8.GetString($hashes) }
    $hashLine = $hashes -split '\r?\n' | Where-Object { $_.Trim().EndsWith($archiveName) } | Select-Object -First 1
    if (-not $hashLine) { throw 'CMake archive checksum not found.' }
    $expectedHash = ($hashLine.Trim() -split '\s+')[0]
    Invoke-WebRequest "$releaseUrl/$archiveName" -OutFile $archivePath -UseBasicParsing
    if ((Get-FileHash $archivePath -Algorithm SHA256).Hash -ne $expectedHash) {
        Remove-Item $archivePath
        throw 'CMake archive checksum mismatch.'
    }
    Expand-Archive -Path $archivePath -DestinationPath $toolsRoot -Force
    Remove-Item $archivePath
}

& $cmakePath --version
if ($LASTEXITCODE -ne 0) { throw 'CMake verification failed.' }