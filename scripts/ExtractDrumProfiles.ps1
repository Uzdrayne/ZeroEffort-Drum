$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$directory = Join-Path $root 'Source/Drum'
$path = Join-Path $directory 'DrumAutoPreset.h'
$source = [IO.File]::ReadAllText($path).Replace("`r`n", "`n")
function Write-Header($name, $text) {
    [IO.File]::WriteAllText((Join-Path $directory $name), $text)
}
function Qualify-Measurements($text) {
    $members = 'rate|samples|energy|peak|hits|invalid|clipped|windows|onsetFrames|bleedFrames|envelopeHistograms|onsetSpectrum|bleedSpectrum|bandEnergy|dominantBody|findResonance|phaseEnergy|percentileOf|spectralPeak'
    return [regex]::Replace($text, "(?<![\w.])($members)\b", 'analysis.$1')
}
$classStart = $source.IndexOf('class KickAnalyser')
$proposalStart = $source.IndexOf('    DrumProposal propose(')
$otherStart = $source.IndexOf('    DrumProposal proposeInstrument(')
$helperStart = $source.IndexOf('    float spectralPeak(')
$dataStart = $source.IndexOf('    int fftSize =')
$shared = $source.Substring(0, $classStart)
Write-Header 'DrumProposal.h' ($shared + "}`n")
$measurement = $source.Substring($classStart, $proposalStart - $classStart).Replace('class KickAnalyser', 'class DrumAnalysis')
$measurement += $source.Substring($helperStart, $dataStart - $helperStart)
$measurement += "private:`n    friend class KickProfile;`n    friend class ProfileAssembly;`n    friend class MasterBusProfile;`n    friend class LegacyProposalReference;`n`n"
$measurement += $source.Substring($dataStart)
Write-Header 'DrumAnalysis.h' ("#pragma once`n#include `"DrumProposal.h`"`n`nnamespace drum`n{`n" + $measurement)
$kick = $source.Substring($proposalStart, $source.IndexOf("`nprivate:", $proposalStart) - $proposalStart)
$kick = $kick.Replace('    DrumProposal propose(const Settings& original, KickStyle style = KickStyle::natural, float intensity = 1.0f) const',
    '    static DrumProposal propose(const DrumAnalysis& analysis, const Settings& original, KickStyle style, float intensity)')
$kick = $kick.Replace("        if (style != KickStyle::natural && style != KickStyle::rock && style != KickStyle::metal) style = KickStyle::natural;`n        intensity = std::isfinite(intensity) ? juce::jlimit(0.0f, 1.0f, intensity) : 0.0f;`n        if (instrumentFromValue(original[instrument]) != DrumInstrument::kick)`n            return proposeInstrument(original, style, intensity);`n", '')
Write-Header 'Profiles/KickProfile.h' ("#pragma once`n#include `"../DrumAnalysis.h`"`n`nnamespace drum`n{`nstruct KickProfile`n{`n" + (Qualify-Measurements $kick) + "};`n}`n")
$other = $source.Substring($otherStart, $helperStart - $otherStart)
$other = $other.Replace('    DrumProposal proposeInstrument(const Settings& original, KickStyle style, float intensity) const',
    '    static DrumProposal propose(const DrumAnalysis& analysis, const Settings& original, KickStyle style, float intensity, const ProfileParameters& profile)')
$classificationStart = $other.IndexOf('        const auto tom =')
$classificationEnd = $other.IndexOf('        const auto crest =')
$other = $other.Remove($classificationStart, $classificationEnd - $classificationStart)
$hpStart = $other.IndexOf('        settings[highPass] =')
$hpEnd = $other.IndexOf('        settings[lowPass] =')
$other = $other.Remove($hpStart, $hpEnd - $hpStart).Insert($hpStart, "        settings[highPass] = profile.highPassHz;`n")
$replacements = @{
    'tom ? spectralPeak(45, 200) : 180' = 'profile.bodyHz'
    'style != KickStyle::natural && close' = 'style != KickStyle::natural && profile.closeMic'
    'tom ? 450.0f : 650.0f' = 'profile.boxHz'
    'tom ? 3500.0f : 4500.0f' = 'profile.attackHz'
    'close && loud - quiet' = 'profile.closeMic && loud - quiet'
    'tom ? 350.0f : 180.0f' = 'profile.gateReleaseMs'
    '(highOnly ? 12.0f : 6.0f)' = 'profile.crestMinimum'
    'master || highOnly ? 2.0f : style == KickStyle::metal ? 4.0f : 3.0f' = 'profile.compressorRatio'
    'master ? 30.0f : close ? 20.0f : 30.0f' = 'profile.attackMs'
    'master ? 100.0f : tom ? 250.0f : 180.0f' = 'profile.releaseMs'
    '(master || highOnly ? 1.5f : selected == DrumInstrument::room ? 3.0f : 3.0f)' = 'profile.targetReduction'
    '(master || highOnly ? 3.0f : 6.0f)' = 'profile.maximumReduction'
    '(master ? " / Bus Glue cible P75 " : " / COMP large bande cible P75 ")' = '(profile.busGlue ? " / Bus Glue cible P75 " : " / COMP large bande cible P75 ")'
    'if (highOnly || selected == DrumInstrument::room)' = 'if (profile.stereoCaution)'
}
foreach ($entry in $replacements.GetEnumerator()) { $other = $other.Replace($entry.Key, $entry.Value) }
$glueStart = $other.IndexOf('        if (master)')
$glueEnd = $other.IndexOf('        result.valid = true;', $glueStart)
$glue = $other.Substring($glueStart, $glueEnd - $glueStart)
$other = $other.Remove($glueStart, $glueEnd - $glueStart)
$warningStart = $other.IndexOf('        if (master)')
$warningEnd = $other.IndexOf('        return result;', $warningStart)
$warnings = $other.Substring($warningStart, $warningEnd - $warningStart)
$other = $other.Remove($warningStart, $warningEnd - $warningStart)
$parameters = @'
#pragma once
#include "../DrumAnalysis.h"

namespace drum
{
struct ProfileParameters
{
    float highPassHz, bodyHz, boxHz, attackHz;
    float gateReleaseMs, crestMinimum, compressorRatio, attackMs, releaseMs;
    float targetReduction, maximumReduction;
    bool closeMic, stereoCaution, busGlue;
};

class ProfileAssembly
{
public:
'@
Write-Header 'Profiles/ProfileAssembly.h' ($parameters + "`n" + (Qualify-Measurements $other) + "};`n}`n")
$master = @'
#pragma once
#include "ProfileAssembly.h"

namespace drum
{
struct MasterBusProfile
{
    static DrumProposal propose(const DrumAnalysis& analysis, const Settings& original, KickStyle style, float intensity)
    {
        const ProfileParameters profile { 20, 180, 0, 0, 180, 6, 2, 30, 100, 1.5f, 3, false, false, true };
        auto result = ProfileAssembly::propose(analysis, original, style, intensity, profile);
        if (!result.valid) return result;
        auto& settings = result.settings;
        const auto crest = juce::Decibels::gainToDecibels(static_cast<float>(analysis.peak
            / std::sqrt(analysis.energy / static_cast<double>(analysis.samples))));
'@
$glue = $glue.Replace("        if (master)`n        {`n", '').Replace("        }`n", '')
$warnings = $warnings.Replace("        if (master)`n        {`n", '').Replace("        }`n", '')
Write-Header 'Profiles/MasterBusProfile.h' ($master + "`n" + $glue + (Qualify-Measurements $warnings) + "        return result;`n    }`n};`n}`n")
$facade = @'
#pragma once
#include "Profiles/KickProfile.h"
#include "Profiles/SnareProfile.h"
#include "Profiles/TomProfile.h"
#include "Profiles/HiHatProfile.h"
#include "Profiles/OverheadsProfile.h"
#include "Profiles/RoomProfile.h"
#include "Profiles/MasterBusProfile.h"

namespace drum
{
class KickAnalyser
{
public:
    void prepare(double sampleRate) { measurements.prepare(sampleRate); }
    void push(const float* audio, int length) { measurements.push(audio, length); }
    const DrumAnalysis& analysis() const { return measurements; }

    DrumProposal propose(const Settings& original, KickStyle style = KickStyle::natural, float intensity = 1.0f) const
    {
        if (style != KickStyle::natural && style != KickStyle::rock && style != KickStyle::metal) style = KickStyle::natural;
        intensity = std::isfinite(intensity) ? juce::jlimit(0.0f, 1.0f, intensity) : 0.0f;
        switch (instrumentFromValue(original[instrument]))
        {
            case DrumInstrument::snare: return SnareProfile::propose(measurements, original, style, intensity);
            case DrumInstrument::tom1:
            case DrumInstrument::tom2:
            case DrumInstrument::tom3:
            case DrumInstrument::floorTom: return TomProfile::propose(measurements, original, style, intensity);
            case DrumInstrument::hiHat: return HiHatProfile::propose(measurements, original, style, intensity);
            case DrumInstrument::overheads: return OverheadsProfile::propose(measurements, original, style, intensity);
            case DrumInstrument::room: return RoomProfile::propose(measurements, original, style, intensity);
            case DrumInstrument::master: return MasterBusProfile::propose(measurements, original, style, intensity);
            case DrumInstrument::kick: return KickProfile::propose(measurements, original, style, intensity);
        }
        return KickProfile::propose(measurements, original, style, intensity);
    }

private:
    DrumAnalysis measurements;
};
}
'@
Write-Header 'DrumAutoPreset.h' ($facade + "`n")
$referencePath = Join-Path $root 'tests/LegacyDrumProposalReference.h'
$reference = [IO.File]::ReadAllText($referencePath).Replace('const KickAnalyser&', 'const DrumAnalysis&')
[IO.File]::WriteAllText($referencePath, $reference)