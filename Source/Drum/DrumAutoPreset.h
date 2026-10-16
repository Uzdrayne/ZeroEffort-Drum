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
class DrumAnalyser
{
public:
    void prepare(double sampleRate) { measurements.prepare(sampleRate); }
    void push(const float* audio, int length) { measurements.push(audio, length); }
    const DrumAnalysis& analysis() const { return measurements; }
    DrumAnalysis& editableAnalysis() { return measurements; }  // snapshot restore only
    void setExcludeLeadFrames(bool exclude) noexcept { measurements.setExcludeLeadFrames(exclude); }
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
            if (result.settings[autoMeasuredOn] > 0.5f && role >= DrumInstrument::snare && role <= DrumInstrument::floorTom)
            {
                const auto lower = role == DrumInstrument::snare ? 100.0f : 45.0f;
                const auto total = measurements.bandEnergy(20, 16000);
                if (total > 0 && measurements.bandEnergy(lower, 200) / total >= 0.12)
                {
                    result.settings[bodyFrequency] = measurements.spectralPeak(lower, 200);
                    result.summary += " / Corps mesure " + juce::String(result.settings[bodyFrequency], 1)
                        + " Hz (pic spectral, pas une fondamentale certifiee)";
                }
                else result.summary += " / Corps mesure incertain : frequence de profil conservee";
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
        const bool close = role >= DrumInstrument::kick && role <= DrumInstrument::floorTom;
        const bool tom = role >= DrumInstrument::tom1 && role <= DrumInstrument::floorTom;
        const bool cymbals = role == DrumInstrument::hiHat || role == DrumInstrument::overheads;
        const bool room = role == DrumInstrument::room;
        const bool bus = role == DrumInstrument::master;
        const auto crest = measurements.crestDb();
        const auto total = measurements.bandEnergy(20, 16000);
        const auto fraction = [&](double lower, double upper) {
            return total > 0 ? measurements.bandEnergy(lower, upper) / total : 0.0;
        };
        settings[levelMatchOn] = 1;
        settings[autoMeasuredOn] = role >= DrumInstrument::snare && role <= DrumInstrument::floorTom ? 1.0f : 0.0f;
        const auto transientNeed = juce::jlimit(0.25f, 1.0f, (crest - 3) / 9);
        settings[crushOn] = !cymbals && crest > (bus ? 7.0f : 5.0f) ? 1.0f : 0.0f;
        settings[crushAmount] = (bus ? 10.0f : room ? 26.0f : tom ? 22.0f : 24.0f) * character * dose * transientNeed;
        settings[crushThreshold] = juce::jlimit(-60.0f, -8.0f,
            juce::jmax(measurements.rmsDb() + 3, measurements.percentile(0.85f)) - 6 - 8 * intensity);
        settings[crushRatio] = room ? 10.0f : bus ? 4.0f : 8.0f;
        settings[crushAttack] = bus ? 12.0f : role == DrumInstrument::kick ? 8.0f : 4.0f;
        settings[crushRelease] = tom ? 220.0f : room ? 180.0f : 110.0f;
        settings[crushHP] = role == DrumInstrument::kick ? 35.0f : bus ? 60.0f : 100.0f;
        settings[crushLP] = room ? 6000.0f : 8000.0f;
        const auto inputDrive = juce::jlimit(0.0f, 10.0f, -12 - measurements.peakDb());
        settings[colourOn] = !cymbals && fraction(40, 3500) > 0.12 ? 1.0f : 0.0f;
        settings[colourDrive] = juce::jmin(24.0f, ((bus ? 5.0f : 9.0f) + inputDrive) * character * dose);
        settings[colourMix] = (bus ? 14.0f : room ? 26.0f : 28.0f) * character * dose;
        settings[shapeOn] = close && crest > 5 ? 1.0f : 0.0f;
        settings[shapeAttack] = (role == DrumInstrument::kick ? 2.4f : 3.0f) * character * dose * transientNeed;
        settings[shapeSustain] = (tom ? 1.5f : role == DrumInstrument::snare ? 0.5f : -1.0f) * character * dose;
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
        const bool lowRelevant = fraction(180, 1800) > (cymbals ? 0.15 : 0.035);
        const bool highRelevant = fraction(3000, 16000) > (close ? 0.02 : 0.06);
        settings[dynamicLowHz] = lowResonance ? lowPeak : role == DrumInstrument::kick ? 300.0f : tom ? 450.0f : 650.0f;
        settings[dynamicHighHz] = highResonance ? highPeak : cymbals ? 8000.0f : 6500.0f;
        settings[dynamicLowQ] = lowResonance ? 2.5f : 1.2f;
        settings[dynamicHighQ] = highResonance ? 2.0f : 1.0f;
        settings[dynamicLowRange] = lowResonance || lowRelevant
            ? juce::jmin(6.0f, (lowResonance ? 3.5f : 1.8f) * dose) : 0.0f;
        settings[dynamicHighRange] = highResonance || highRelevant
            ? juce::jmin(6.0f, (highResonance ? 3.0f : cymbals ? 2.8f : 1.8f) * dose) : 0.0f;
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
        settings[clipOn] = !cymbals && clipDose > 0 && crest > (bus ? 9.0f : 6.0f) ? 1.0f : 0.0f;
        settings[clipCeiling] = juce::jlimit(-18.0f, -1.0f, measurements.peakDb() - 3 - 5 * clipDose);
        settings[clipMix] = (bus ? 18.0f : 32.0f) * character * clipDose;
        if (close)
        {
            const auto contrast = measurements.percentile(0.9f) - measurements.percentile(0.2f);
            const bool bleed = fraction(1600, 16000) > 0.015 && contrast > 8;
            settings[cleanupOn] = bleed ? 1.0f : 0.0f;
            settings[cleanupDepth] = juce::jmin(tom ? 12.0f : 18.0f, (tom ? 7.0f : 10.0f) * character * dose);
            if (bleed && role != DrumInstrument::kick)
            {
                settings[focusLow] = tom ? 40.0f : 70.0f;
                settings[focusHigh] = tom ? 260.0f : 400.0f;
                settings[gateHold] = tom ? 60.0f : 35.0f;
                settings[gateHysteresis] = 6;
                settings[gateThreshold] = juce::jmin(settings[gateThreshold], measurements.percentile(0.9f) - 15);
                settings[gateRange] = juce::jmax(6.0f, settings[gateRange]);
                settings[gateRatio] = juce::jmax(1.6f, settings[gateRatio]);
            }
        }
        const bool ambience = (role == DrumInstrument::snare || tom || bus)
            && style != DrumStyle::metal && crest > 10 && fraction(5000, 16000) < 0.35;
        settings[ambienceOn] = ambience ? 1.0f : 0.0f;
        settings[ambienceAmount] = juce::jmin(bus ? 4.0f : 100.0f,
            (bus ? 3.0f : tom ? 5.0f : 7.0f) * character * dose);
        settings[ambienceSize] = tom ? 35.0f : 22.0f;
        settings[ambiencePredelay] = bus ? 8.0f : 16.0f;
        settings[ambienceHighPass] = 180;
        settings[ambienceLowPass] = 6500;
        settings[ambienceCharacter] = 25 * character * dose;
        settings[ambienceCompression] = 20 * character * dose;
        proposal.summary += " / Finitions Auto : Crush " + juce::String(settings[crushOn] > 0 ? settings[crushAmount] : 0, 1)
            + "%, Couleur " + juce::String(settings[colourOn] > 0 ? settings[colourMix] : 0, 1)
            + "%, Impact " + juce::String(settings[shapeOn] > 0 ? settings[shapeAttack] : 0, 1)
            + " dB, Dyn " + juce::String(settings[dynamicLowRange], 1) + "/" + juce::String(settings[dynamicHighRange], 1)
            + " dB, Room " + juce::String(ambience ? settings[ambienceAmount] : 0, 1)
            + "%, Clip " + juce::String(settings[clipOn] > 0 ? settings[clipMix] : 0, 1)
            + "%, Bleed Clean " + juce::String(settings[cleanupOn] > 0 ? settings[cleanupDepth] : 0, 1) + " dB";
    }

    DrumProposal proposeAtStrength(const Settings& original, DrumStyle style, float intensity) const
    {
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
            case DrumInstrument::kick: return KickProfile::propose(measurements, original, style, intensity, policy);
        }
        return KickProfile::propose(measurements, original, style, intensity, policy);
    }

    DrumAnalysis measurements;
    ProposalPolicy policy;
};
}
