<#
.SYNOPSIS
  Runs the whole analysis chain on a corpus described by a manifest (corpus.json), outside the repository.

.DESCRIPTION
  For every track: ZeroEffortDrumTests --export-wav (snapshot). Then --replay of all the snapshots, and for every bus
  (--bus) or raw / mix pair (--pair) the comparison reports. Nothing is played, nothing is written inside the repository.
  Layout of the output folder (default <corpus>\out): snapshots\<kit>, reports\replay.*, buses\*, pairs\*, summary.md

  Manifest: see docs\corpus.example.json and docs\Corpus.md.
#>
param(
    [Parameter(Mandatory)][string]$Corpus,                       # folder that holds corpus.json
    [string]$Exe,
    [string]$Out,
    [switch]$SkipSnapshots, [switch]$SkipBuses, [switch]$SkipPairs
)
$ErrorActionPreference = 'Stop'
if (-not $Exe) { $Exe = Join-Path (Split-Path $PSScriptRoot -Parent) 'build\ZeroEffortDrumTests_artefacts\Release\ZeroEffortDrumTests.exe' }
if (-not (Test-Path $Exe)) { throw "ZeroEffortDrumTests.exe not found: $Exe (build the Release configuration first)" }
$manifestPath = Join-Path $Corpus 'corpus.json'
if (-not (Test-Path $manifestPath)) { throw "No corpus.json in $Corpus" }
$manifest = Get-Content $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $Out) { $Out = Join-Path $Corpus 'out' }
$style = if ($manifest.style) { $manifest.style } else { 'rock' }
$intensity = if ($null -ne $manifest.intensity) { $manifest.intensity } else { 0.8 }
$summary = New-Object System.Collections.Generic.List[string]
$summary.Add("# Corpus $(Split-Path $Corpus -Leaf) : $(Get-Date -Format 'yyyy-MM-dd HH:mm')")
$summary.Add('')

function Resolve-Track([string]$relative) { Join-Path $Corpus ($relative -replace '/', '\') }

foreach ($kit in $manifest.kits) {
    $summary.Add("## $($kit.id)")
    if (-not $SkipSnapshots) {
        foreach ($track in $kit.tracks) {
            $file = Resolve-Track $track.file
            if (-not (Test-Path $file)) { $summary.Add("- MISSING $($track.file)"); continue }
            $position = if ($null -ne $track.position) { $track.position } else { 0 }
            $line = & $Exe --export-wav $file (Join-Path $Out "snapshots\$($kit.id)") $track.role $style $intensity --position $position 2>&1 | Select-Object -First 1
            $summary.Add("- $($track.file) (role $($track.role), position $position) : $line")
        }
    }
    if (-not $SkipBuses) {
        foreach ($bus in $kit.buses) {
            $arguments = @('--bus', (Resolve-Track $bus.mix), (Join-Path $Out "buses\$($kit.id)_$($bus.name -replace '\W','_')"))
            foreach ($input in $bus.inputs) { $arguments += ("{0}={1}" -f [IO.Path]::GetFileNameWithoutExtension($input), (Resolve-Track $input)) }
            New-Item -ItemType Directory -Force (Split-Path $arguments[2]) | Out-Null
            $verdict = & $Exe @arguments 2>&1 | Select-String -Pattern '^\*\*' | Select-Object -First 1
            $summary.Add("- bus $($bus.name) : $($verdict.Line)")
        }
    }
    if (-not $SkipPairs) {
        foreach ($pair in $kit.pairs) {
            $name = "$($kit.id)_$([IO.Path]::GetFileNameWithoutExtension($pair.raw))"
            New-Item -ItemType Directory -Force (Join-Path $Out 'pairs') | Out-Null
            $verdict = & $Exe --pair (Resolve-Track $pair.raw) (Resolve-Track $pair.mix) (Join-Path $Out "pairs\$name") 2>&1 | Select-String -Pattern '^\*\*' | Select-Object -First 1
            $summary.Add("- pair $($pair.raw) / $($pair.mix) : $($verdict.Line)")
        }
    }
    $summary.Add('')
}
if (-not $SkipSnapshots -and (Test-Path (Join-Path $Out 'snapshots'))) {
    New-Item -ItemType Directory -Force (Join-Path $Out 'reports') | Out-Null
    & $Exe --replay (Join-Path $Out 'snapshots') (Join-Path $Out 'reports\replay') | Select-String -Pattern 'Analyses :|Determinisme' | ForEach-Object { $summary.Add("- replay : $($_.Line)") }
}
New-Item -ItemType Directory -Force $Out | Out-Null
$summary | Set-Content (Join-Path $Out 'summary.md') -Encoding UTF8
$summary
