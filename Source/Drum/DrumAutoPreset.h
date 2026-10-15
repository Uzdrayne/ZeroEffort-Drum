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
        const auto character = style == DrumStyle::natural ? 0.35f : style == DrumStyle::rock ? 0.75f : 1.0f;
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
        settings[crushOn] = !cymbals && crest > (bus ? 12.0f : 9.0f) ? 1.0f : 0.0f;
        settings[crushAmount] = (bus ? 7.0f : room ? 18.0f : 12.0f) * character * dose;
        settings[crushThreshold] = juce::jlimit(-48.0f, -12.0f, measurements.percentile(0.75f) - 10);
        settings[crushRatio] = room ? 10.0f : bus ? 4.0f : 8.0f;
        settings[crushAttack] = bus ? 12.0f : role == DrumInstrument::kick ? 8.0f : 4.0f;
        settings[crushRelease] = tom ? 220.0f : room ? 180.0f : 110.0f;
        settings[crushHP] = role == DrumInstrument::kick ? 35.0f : bus ? 60.0f : 100.0f;
        settings[crushLP] = room ? 6000.0f : 8000.0f;
        settings[colourOn] = !cymbals && style != DrumStyle::natural && fraction(40, 2000) > 0.2 ? 1.0f : 0.0f;
        settings[colourDrive] = (bus ? 3.0f : 6.0f) * character * dose;
        settings[colourMix] = (bus ? 10.0f : 18.0f) * dose;
        settings[shapeOn] = close && crest > 8 ? 1.0f : 0.0f;
        settings[shapeAttack] = (role == DrumInstrument::kick ? 1.5f : 2.0f) * character * dose;
        settings[shapeSustain] = (tom ? 1.0f : -0.75f) * character * dose;
        const auto lowPeak = measurements.spectralPeak(220, 1600);
        const auto highPeak = measurements.spectralPeak(3500, 12000);
        const auto prominent = [&](float frequency) {
            const auto centre = measurements.bandEnergy(frequency / 1.12, frequency * 1.12);
            const auto neighbours = measurements.bandEnergy(frequency / 1.5, frequency / 1.2)
                + measurements.bandEnergy(frequency * 1.2, frequency * 1.5);
            return total > 0 && centre / total > 0.025 && centre > neighbours * 1.6;
        };
        settings[dynamicLowHz] = lowPeak;
        settings[dynamicHighHz] = highPeak;
        settings[dynamicLowQ] = 2.5f;
        settings[dynamicHighQ] = cymbals ? 1.2f : 2.0f;
        settings[dynamicLowRange] = prominent(lowPeak) ? juce::jmin(6.0f, 3.0f * dose) : 0.0f;
        settings[dynamicHighRange] = prominent(highPeak) || (cymbals && fraction(5000, 16000) > 0.55)
            ? juce::jmin(6.0f, 2.5f * dose) : 0.0f;
        settings[dynamicOn] = settings[dynamicLowRange] + settings[dynamicHighRange] > 0 ? 1.0f : 0.0f;
        settings[dynamicLowThreshold] = juce::jlimit(-54.0f, -12.0f, measurements.rmsDb() - 6);
        settings[dynamicHighThreshold] = juce::jlimit(-54.0f, -12.0f, measurements.rmsDb() - 8);
        settings[clipOn] = !cymbals && style != DrumStyle::natural && crest > 14
            && measurements.peakDb() > -6 ? 1.0f : 0.0f;
        settings[clipCeiling] = juce::jlimit(-12.0f, -1.0f, measurements.peakDb() - 2);
        settings[clipMix] = 15.0f * character * dose;
        const bool ambience = (role == DrumInstrument::snare || tom || bus)
            && style != DrumStyle::metal && crest > 10 && fraction(5000, 16000) < 0.35;
        settings[ambienceOn] = ambience ? 1.0f : 0.0f;
        settings[ambienceAmount] = (bus ? 3.0f : tom ? 5.0f : 7.0f) * character * dose;
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
            + "%, Clip " + juce::String(settings[clipOn] > 0 ? settings[clipMix] : 0, 1) + "%";
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
            case DrumInstrument::kick: return KickProfile::propose(measurements, original, style, intensity);
        }
        return KickProfile::propose(measurements, original, style, intensity);
    }

    DrumAnalysis measurements;
};
}
