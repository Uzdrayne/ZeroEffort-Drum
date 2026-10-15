$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$path = Join-Path $root 'Source/Drum/DrumAutoPreset.h'
$source = [IO.File]::ReadAllText($path)
$start = $source.IndexOf('    DrumProposal propose(')
$end = $source.IndexOf('    float spectralPeak(')
$methods = $source.Substring($start, $end - $start)
$methods = $methods.Replace('    friend class LegacyProposalReference;', '')
$members = 'rate|samples|energy|peak|hits|invalid|clipped|windows|onsetFrames|bleedFrames|envelopeHistograms|onsetSpectrum|bleedSpectrum|bandEnergy|dominantBody|findResonance|phaseEnergy|percentileOf|spectralPeak'
$methods = [regex]::Replace($methods, "(?<![\w.])($members)\b", 'analysis.$1')
$header = @'
#pragma once
#include "Drum/DrumAutoPreset.h"

namespace drum
{
// Frozen pre-extraction proposal arithmetic; uses production measurements, not production profiles.
class LegacyProposalReference
{
public:
    explicit LegacyProposalReference(const KickAnalyser& source) : analysis(source) {}

'@
$footer = @'
    const KickAnalyser& analysis;
};
}
'@
[IO.File]::WriteAllText((Join-Path $root 'tests/LegacyDrumProposalReference.h'), $header + "`n" + $methods + $footer)