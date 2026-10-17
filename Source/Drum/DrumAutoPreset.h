#pragma once
#include "DrumRole.h"

namespace drum
{
class DrumAnalyser
{
public:
    void prepare(double sampleRate) { measurements.prepare(sampleRate); }
    void push(const float* audio, int length) { measurements.push(audio, length); }
    const DrumAnalysis& analysis() const { return measurements; }
    DrumAnalysis& editableAnalysis() { return measurements; }  // snapshot restore only
    void setExcludeLeadFrames(bool exclude) noexcept { measurements.setExcludeLeadFrames(exclude); }
    void setDoubleHitDetection(bool enable) noexcept { measurements.setDoubleHitDetection(enable); }
    void setPolicy(const ProposalPolicy& newPolicy) noexcept { policy = newPolicy; }
    const ProposalPolicy& getPolicy() const noexcept { return policy; }

    DrumProposal propose(const Settings& original, DrumStyle style = DrumStyle::natural, float intensity = 1.0f) const
    {
        if (style != DrumStyle::natural && style != DrumStyle::rock && style != DrumStyle::metal) style = DrumStyle::natural;
        intensity = std::isfinite(intensity) ? juce::jlimit(0.0f, 1.0f, intensity) : 0.0f;
        const auto strength = intensity <= 0.5f ? intensity : 0.5f + 2.5f * (intensity - 0.5f);
        auto result = proposeAtStrength(original, style, strength);
        if (result.valid)
        {
            result.settings[autoMeasuredOn] = original[autoMeasuredOn];
            result.settings[autoStyleOption] = original[autoStyleOption];
            result.settings[autoIntensityOption] = original[autoIntensityOption];
            const auto role = instrumentFromValue(original[instrument]);
            configureFinishing(result, role, style, intensity);
            if (result.settings[autoMeasuredOn] > 0.5f && roleDescriptor(role).measuredBody)
            {
                const auto lower = roleDescriptor(role).measuredBodyLowHz;
                const auto total = measurements.bandEnergy(20, 16000);
                if (total > 0 && measurements.bandEnergy(lower, 200) / total >= 0.12)
                {
                    result.settings[bodyFrequency] = measurements.spectralPeak(lower, 200);
                    result.summary += " / Measured body " + juce::String(result.settings[bodyFrequency], 1)
                        + " Hz (spectral peak, not a certified fundamental)";
                }
                else result.summary += " / Measured body uncertain: profile frequency kept";
            }
            result.summary = result.summary.replace(juce::String(strength * 100, 0) + "%",
                juce::String(intensity * 100, 0) + "%", false);
            for (size_t control = 0; control < result.settings.size(); ++control)
                result.settings[control] = juce::jlimit(controlSpecs[control].minimum,
                    controlSpecs[control].maximum, result.settings[control]);
        }
        return result;
    }

private:
    void configureFinishing(DrumProposal& proposal, DrumInstrument role, DrumStyle style, float intensity) const
    {
        auto& settings = proposal.settings;
        for (size_t control = levelMatchOn; control < autoStyleOption; ++control)
            settings[control] = controlSpecs[control].initial;
        settings[monitorMonoOn] = 0;
        if (intensity <= 0) return;
        const auto dose = intensity <= 0.5f ? intensity : 0.5f + 2.5f * (intensity - 0.5f);
        const auto character = style == DrumStyle::natural ? 0.55f : style == DrumStyle::rock ? 0.85f : 1.0f;
        const auto& descriptor = roleDescriptor(role);
        const bool close = descriptor.closeMic;
        const bool tom = descriptor.tom;
        const bool cymbals = descriptor.cymbals;
        const bool bus = descriptor.bus;
        const auto crest = measurements.crestDb();
        const auto total = measurements.bandEnergy(20, 16000);
        const auto fraction = [&](double lower, double upper) {
            return total > 0 ? measurements.bandEnergy(lower, upper) / total : 0.0;
        };
        settings[levelMatchOn] = 1;
        settings[autoMeasuredOn] = descriptor.measuredBody ? 1.0f : 0.0f;
        const auto transientNeed = juce::jlimit(0.25f, 1.0f, (crest - 3) / 9);
        settings[crushOn] = !cymbals && crest > descriptor.crushCrestMinimum ? 1.0f : 0.0f;
        settings[crushAmount] = descriptor.crushAmount * character * dose * transientNeed;
        settings[crushThreshold] = juce::jlimit(-60.0f, -8.0f,
            juce::jmax(measurements.rmsDb() + 3, measurements.percentile(0.85f)) - 6 - 8 * intensity);
        settings[crushRatio] = descriptor.crushRatio;
        settings[crushAttack] = descriptor.crushAttackMs;
        settings[crushRelease] = descriptor.crushReleaseMs;
        settings[crushHP] = descriptor.crushHighPassHz;
        settings[crushLP] = descriptor.crushLowPassHz;
        const auto inputDrive = juce::jlimit(0.0f, 10.0f, -12 - measurements.peakDb());
        settings[colourOn] = !cymbals && fraction(40, 3500) > 0.12 ? 1.0f : 0.0f;
        settings[colourDrive] = juce::jmin(24.0f, (descriptor.colourDrive + inputDrive) * character * dose);
        settings[colourMix] = descriptor.colourMix * character * dose;
        settings[shapeOn] = close && crest > 5 ? 1.0f : 0.0f;
        settings[shapeAttack] = descriptor.shapeAttackDb * character * dose * transientNeed;
        settings[shapeSustain] = descriptor.shapeSustainDb * character * dose;
        const auto lowPeak = measurements.spectralPeak(220, 1600);
        const auto highPeak = measurements.spectralPeak(3500, 12000);
        const auto prominent = [&](float frequency) {
            const auto centre = measurements.bandEnergy(frequency / 1.12, frequency * 1.12);
            const auto neighbours = measurements.bandEnergy(frequency / 1.5, frequency / 1.2)
                + measurements.bandEnergy(frequency * 1.2, frequency * 1.5);
            return total > 0 && centre / total > 0.025 && centre > neighbours * 1.6;
        };
        const bool lowResonance = prominent(lowPeak);
        const bool highResonance = prominent(highPeak);
        const bool lowRelevant = fraction(180, 1800) > descriptor.lowRelevantFraction;
        const bool highRelevant = fraction(3000, 16000) > descriptor.highRelevantFraction;
        settings[dynamicLowHz] = lowResonance ? lowPeak : descriptor.dynamicLowHz;
        settings[dynamicHighHz] = highResonance ? highPeak : descriptor.dynamicHighHz;
        settings[dynamicLowQ] = lowResonance ? 2.5f : 1.2f;
        settings[dynamicHighQ] = highResonance ? 2.0f : 1.0f;
        settings[dynamicLowRange] = lowResonance || lowRelevant
            ? juce::jmin(6.0f, (lowResonance ? 3.5f : 1.8f) * dose) : 0.0f;
        settings[dynamicHighRange] = highResonance || highRelevant
            ? juce::jmin(6.0f, (highResonance ? 3.0f : descriptor.dynamicHighRange) * dose) : 0.0f;
        settings[dynamicOn] = settings[dynamicLowRange] + settings[dynamicHighRange] > 0 ? 1.0f : 0.0f;
        const auto bandThreshold = [&](float frequency) {
            const auto bandFraction = juce::jlimit(0.002, 1.0, fraction(frequency / 1.5, frequency * 1.5));
            return juce::jlimit(-60.0f, -10.0f, measurements.rmsDb()
                + static_cast<float>(10 * std::log10(bandFraction)) - 3 - 5 * intensity);
        };
        settings[dynamicLowThreshold] = bandThreshold(settings[dynamicLowHz]);
        settings[dynamicHighThreshold] = bandThreshold(settings[dynamicHighHz]);
        const auto clipDose = juce::jlimit(0.0f, 1.0f, (intensity - (style == DrumStyle::natural ? 0.65f : 0.35f))
            / (style == DrumStyle::natural ? 0.35f : 0.65f));
        settings[clipOn] = !cymbals && clipDose > 0 && crest > descriptor.clipCrestMinimum ? 1.0f : 0.0f;
        settings[clipCeiling] = juce::jlimit(-18.0f, -1.0f, measurements.peakDb() - 3 - 5 * clipDose);
        settings[clipMix] = descriptor.clipMix * character * clipDose;
        if (close)
        {
            const auto contrast = measurements.percentile(0.9f) - measurements.percentile(0.2f);
            const bool bleed = fraction(1600, 16000) > 0.015 && contrast > 8;
            settings[cleanupOn] = bleed ? 1.0f : 0.0f;
            settings[cleanupDepth] = juce::jmin(descriptor.cleanupMaximum, descriptor.cleanupBase * character * dose);
            if (bleed && descriptor.family != RoleFamily::kick)
            {
                settings[focusLow] = descriptor.focusLowHz;
                settings[focusHigh] = descriptor.focusHighHz;
                settings[gateHold] = descriptor.gateHoldMs;
                settings[gateHysteresis] = 6;
                settings[gateThreshold] = juce::jmin(settings[gateThreshold], measurements.percentile(0.9f) - 15);
                settings[gateRange] = juce::jmax(6.0f, settings[gateRange]);
                settings[gateRatio] = juce::jmax(1.6f, settings[gateRatio]);
            }
        }
        const bool ambience = descriptor.ambience
            && style != DrumStyle::metal && crest > 10 && fraction(5000, 16000) < 0.35;
        settings[ambienceOn] = ambience ? 1.0f : 0.0f;
        settings[ambienceAmount] = juce::jmin(descriptor.ambienceMaximum, descriptor.ambienceBase * character * dose);
        settings[ambienceSize] = descriptor.ambienceSize;
        settings[ambiencePredelay] = descriptor.ambiencePredelayMs;
        settings[ambienceHighPass] = 180;
        settings[ambienceLowPass] = 6500;
        settings[ambienceCharacter] = 25 * character * dose;
        settings[ambienceCompression] = 20 * character * dose;
        proposal.summary +=         " / Auto Finishing: Crush " + juce::String(settings[crushOn] > 0 ? settings[crushAmount] : 0, 1)
                    + "%, Color " + juce::String(settings[colourOn] > 0 ? settings[colourMix] : 0, 1)
            + "%, Impact " + juce::String(settings[shapeOn] > 0 ? settings[shapeAttack] : 0, 1)
            + " dB, Dyn " + juce::String(settings[dynamicLowRange], 1) + "/" + juce::String(settings[dynamicHighRange], 1)
            + " dB, Room " + juce::String(ambience ? settings[ambienceAmount] : 0, 1)
            + "%, Clip " + juce::String(settings[clipOn] > 0 ? settings[clipMix] : 0, 1)
            + "%, Bleed Clean " + juce::String(settings[cleanupOn] > 0 ? settings[cleanupDepth] : 0, 1) + " dB";
    }

    DrumProposal proposeAtStrength(const Settings& original, DrumStyle style, float intensity) const
    {
        return roleDescriptor(instrumentFromValue(original[instrument])).propose(measurements, original, style, intensity, policy);
    }

    DrumAnalysis measurements;
    ProposalPolicy policy;
};
}
