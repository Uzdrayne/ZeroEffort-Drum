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
    float (*bodyFrequency)(const DrumAnalysis&) = nullptr;
    // Reference EQ (ProposalPolicy::cymbalReference), applied to Rock and Metal only; gains scale with the strength.
    float referenceHighPassHz = 0, referenceBodyHz = 0, referenceBodyDb = 0, referenceAirHz = 0, referenceAirDb = 0;
};

class ProfileAssembly
{
public:
    static DrumProposal propose(const DrumAnalysis& analysis, const Settings& original, DrumStyle style,
        float intensity, const ProfileParameters& profile, float* measuredCrest = nullptr)
    {
        DrumProposal result;
        result.settings = original;
        const auto selected = instrumentFromValue(original[instrument]);
        const auto rms = analysis.samples > 0 ? std::sqrt(analysis.energy / static_cast<double>(analysis.samples)) : 0.0;
        if (analysis.invalid || analysis.rate < 8000 || analysis.samples < analysis.rate * 2 || analysis.peak < 0.001f || rms < 0.0001 || analysis.windows < 1)
        {
            result.summary = "Insufficient analysis: need a finite, audible signal of at least 2 s for " + juce::String(instrumentName(selected));
            return result;
        }
        if (analysis.clipped > analysis.samples / 100)
        {
            result.summary = "Signal clipped: lower the source level, then analyze.";
            return result;
        }
        auto& settings = result.settings;
        settings = defaultSettings();
        settings[instrument] = static_cast<float>(selected);
        settings[bypass] = original[bypass];
        settings[output] = juce::jmin(original[output], -3.0f);
        const auto crest = juce::Decibels::gainToDecibels(static_cast<float>(analysis.peak / rms));
        if (measuredCrest != nullptr) *measuredCrest = crest;
        settings[filtersOn] = 1;
        settings[highPass] = profile.highPassHz;
        settings[lowPass] = 18000;
        settings[bodyFrequency] = profile.bodyFrequency != nullptr ? profile.bodyFrequency(analysis) : profile.bodyHz;
        settings[eqQ] = 0.85f;
        if (style != DrumStyle::natural && profile.closeMic)
        {
            const auto strength = style == DrumStyle::metal ? 1.3f : 1.0f;
            settings[bodyGain] = 1.5f * intensity;
            settings[bodyQ] = 1.2f;
            settings[boxFrequency] = profile.boxHz;
            settings[boxGain] = -2.0f * strength * intensity;
            settings[boxQ] = 1.2f;
            settings[attackFrequency] = profile.attackHz;
            settings[attackGain] = 2.0f * strength * intensity;
            settings[attackQ] = 1.0f;
            settings[eqOn] = intensity > 0 ? 1.0f : 0.0f;
        }
        const bool reference = style != DrumStyle::natural && intensity > 0
            && (profile.referenceBodyDb != 0 || profile.referenceAirDb != 0 || profile.referenceHighPassHz > 0);
        if (reference)
        {
            if (profile.referenceHighPassHz > 0) settings[highPass] = profile.referenceHighPassHz;
            if (profile.referenceBodyDb != 0)
            {
                settings[bodyFrequency] = profile.referenceBodyHz;
                settings[bodyGain] = profile.referenceBodyDb * intensity;
            }
            if (profile.referenceAirDb != 0)
            {
                settings[airFrequency] = profile.referenceAirHz;
                settings[airGain] = profile.referenceAirDb * intensity;
            }
            settings[eqOn] = 1;
        }
        const auto quiet = analysis.percentileOf(analysis.envelopeHistograms[3], 0.2f);
        const auto loud = analysis.percentileOf(analysis.envelopeHistograms[3], 0.9f);
        settings[gateOn] = profile.closeMic && loud - quiet >= 18 && intensity > 0 ? 1.0f : 0.0f;
        settings[gateThreshold] = juce::jlimit(-80.0f, -12.0f, juce::jmin(quiet + 3, loud - 18));
        settings[gateRange] = 4 * intensity;
        settings[gateRatio] = 1.5f;
        settings[gateRelease] = profile.gateReleaseMs;
        settings[compressorOn] = intensity > 0 && crest > profile.crestMinimum ? 1.0f : 0.0f;
        settings[ratio] = profile.compressorRatio;
        settings[attack] = profile.attackMs;
        settings[release] = profile.releaseMs;
        settings[mix] = 100;
        result.targetReduction = profile.targetReduction * intensity;
        result.maximumReduction = profile.maximumReduction * intensity;
        settings[threshold] = juce::jlimit(-60.0f, 0.0f, analysis.percentileOf(analysis.envelopeHistograms[0], 0.75f)
            - result.targetReduction / (1 - 1 / settings[ratio]));
        result.valid = true;
        result.summary = juce::String(instrumentName(selected)) + " / " + drumStyleName(style) + " "
            + juce::String(intensity * 100, 0) + "% / explicit profile, no instrument recognition"
            + " / source RMS " + juce::String(juce::Decibels::gainToDecibels(rms), 1) + " dBFS"
            + " / peak " + juce::String(juce::Decibels::gainToDecibels(analysis.peak), 1) + " dBFS"
            + " / crest " + juce::String(crest, 1) + " dB"
            + " / EQ coloration " + (settings[eqOn] > 0 ? "deliberate" : "neutral")
            + (profile.busGlue ? " / Bus Glue target P75 " : " / COMP broadband target P75 ") + juce::String(result.targetReduction, 1)
            + " dB / calibration ceiling " + juce::String(result.maximumReduction, 1)
            + " dB / Kick Focus and reinforcement OFF / EXP " + (settings[gateOn] > 0 ? "ON cautious" : "OFF");
        if (reference) result.summary += " / reference EQ (beta, one engineer)";
        if (profile.stereoCaution)
            result.summary += " / check stereo image and decays by ear";
        return result;
    }
};
}
