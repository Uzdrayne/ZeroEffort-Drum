<#
.SYNOPSIS
  Runs the whole analysis chain on a corpus described by a manifest (corpus.json), outside the repository.

.DESCRIPTION
  For every track: ZeroEffortDrumTests --export-wav (snapshot). Then --replay of all the snapshots, and for every bus
  (--bus) or raw / mix pair (--pair) the comparison reports. Nothing is played, nothing is written inside the repository.
  Layout of the output folder (default <corpus>\out): snapshots\<kit>, reports\replay.*, buses\*, pairs\*, versus\* (pairs with a role), summary.md

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
    # Optional labels per kit (one mix by one engineer): they override the global style and are written in the summary.
    $kitStyle = if ($kit.style) { $kit.style } else { $style }
    $labels = @(@("style", $kitStyle), @("mixer", $kit.mixer), @("genre", $kit.genre), @("samples", $kit.samples)) | Where-Object { $_[1] -ne $null -and "$($_[1])" -ne "" } | ForEach-Object { "$($_[0]) $($_[1])" }
    $summary.Add("## $($kit.id)" + $(if ($labels) { " (" + ($labels -join ", ") + ")" } else { "" }))
    if ($kit.notes) { $summary.Add("> $($kit.notes)") }
    if (-not $SkipSnapshots) {
        foreach ($track in $kit.tracks) {
            $file = Resolve-Track $track.file
            if (-not (Test-Path $file)) { $summary.Add("- MISSING $($track.file)"); continue }
            $position = if ($null -ne $track.position) { $track.position } else { 0 }
            $line = & $Exe --export-wav $file (Join-Path $Out "snapshots\$($kit.id)") $track.role $kitStyle $intensity --position $position 2>&1 | Out-String
            $summary.Add("- $($track.file) (role $($track.role), position $position) : " + ($line -split "\r?\n" | Select-Object -First 1))
        }
    }
    if (-not $SkipBuses) {
        foreach ($bus in $kit.buses) {
            $arguments = @('--bus', (Resolve-Track $bus.mix), (Join-Path $Out "buses\$($kit.id)_$($bus.name -replace '\W','_')"))
            foreach ($input in $bus.inputs) { $arguments += ("{0}={1}" -f [IO.Path]::GetFileNameWithoutExtension($input), (Resolve-Track $input)) }
            New-Item -ItemType Directory -Force (Split-Path $arguments[2]) | Out-Null
            $verdict = (& $Exe @arguments 2>&1 | Out-String -Stream) | Select-String -Pattern '^\*\*' | Select-Object -First 1
            $summary.Add("- bus $($bus.name) : $($verdict.Line)")
        }
    }
    if (-not $SkipPairs) {
        foreach ($pair in $kit.pairs) {
            $name = "$($kit.id)_$([IO.Path]::GetFileNameWithoutExtension($pair.raw))"
            New-Item -ItemType Directory -Force (Join-Path $Out 'pairs') | Out-Null
            $verdict = (& $Exe --pair (Resolve-Track $pair.raw) (Resolve-Track $pair.mix) (Join-Path $Out "pairs\$name") 2>&1 | Out-String -Stream) | Select-String -Pattern '^\*\*' | Select-Object -First 1
            $summary.Add("- pair $($pair.raw) / $($pair.mix) : $($verdict.Line)")
            # With a role, the same raw take is also run through the plug-in Auto proposal and compared with the mixer (--vs-plugin).
            if ($null -ne $pair.role) {
                New-Item -ItemType Directory -Force (Join-Path $Out 'versus') | Out-Null
                $versus = (& $Exe --vs-plugin (Resolve-Track $pair.raw) $pair.role $kitStyle $intensity (Resolve-Track $pair.mix) (Join-Path $Out "versus\$name") 2>&1 | Out-String -Stream) | Select-String -Pattern '^\*\*Accord|^ERROR' | Select-Object -First 1
                $summary.Add("  - plug-in : $($versus.Line)")
            }
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
