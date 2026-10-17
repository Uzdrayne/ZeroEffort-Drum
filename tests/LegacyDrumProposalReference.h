#pragma once
#include "Drum/DrumAutoPreset.h"

namespace drum
{
// Frozen 0.8 proposal arithmetic, captured and tested before profile extraction.
// Only measurement access and shared symbol names may follow structural renames.
// Keep numerical decisions and summaries independent of production profiles.
class LegacyProposalReference
{
public:
    explicit LegacyProposalReference(const DrumAnalysis& source) : analysis(source) {}

    DrumProposal propose(const Settings& original, DrumStyle style = DrumStyle::natural, float intensity = 1.0f) const
    {
        DrumProposal result;
        result.settings = original;
        result.hits = analysis.hits;
        if (style != DrumStyle::natural && style != DrumStyle::rock && style != DrumStyle::metal) style = DrumStyle::natural;
        intensity = std::isfinite(intensity) ? juce::jlimit(0.0f, 1.0f, intensity) : 0.0f;
        if (instrumentFromValue(original[instrument]) != DrumInstrument::kick)
            return proposeInstrument(original, style, intensity);
        const auto rms = analysis.samples > 0 ? std::sqrt(analysis.energy / static_cast<double>(analysis.samples)) : 0.0;
        const auto crest = rms > 0 ? juce::Decibels::gainToDecibels(analysis.peak / rms) : 0.0;
        const auto bass = analysis.bandEnergy(35, 150);
        const auto total = analysis.bandEnergy(20, juce::jmin(16000.0, analysis.rate * 0.45));
        if (analysis.invalid || analysis.rate < 8000 || analysis.samples < analysis.rate * 2 || analysis.peak < 0.001f || analysis.hits < 3
            || crest < 6 || total <= 0 || bass / total < 0.15)
        {
            result.summary = "Insufficient analysis: at least 3 clear kick hits within 2 s.";
            return result;
        }
        if (analysis.clipped > analysis.samples / 100)
        {
            result.summary = "Signal clipped: lower the source level, then analyze.";
            return result;
        }
        auto& settings = result.settings;
        settings = defaultSettings();
        settings[bypass] = original[bypass];
        settings[output] = juce::jmin(original[output], -3.0f);
        settings[filtersOn] = 1;
        settings[highPass] = juce::jlimit(20.0f, 35.0f, analysis.kickDominantBody() * 0.4f);
        settings[lowPass] = 18000;
        settings[bodyFrequency] = analysis.kickDominantBody();
        settings[eqQ] = 0.85f;
        const auto resonance = analysis.kickTemporalResonance();
        settings[boxFrequency] = resonance.first;
        settings[boxGain] = resonance.second;
        const auto onsetTreble = analysis.phaseEnergy(analysis.onsetSpectrum, 6000, 12000) / juce::jmax(1, analysis.onsetFrames);
        const auto bleedTreble = analysis.phaseEnergy(analysis.bleedSpectrum, 6000, 12000) / juce::jmax(1, analysis.bleedFrames);
        const auto onsetBass = analysis.phaseEnergy(analysis.onsetSpectrum, 35, 150) / juce::jmax(1, analysis.onsetFrames);
        if (analysis.bleedFrames >= 10 && analysis.onsetFrames >= 3 && onsetBass > 0
            && bleedTreble > onsetBass * 0.02 && bleedTreble > onsetTreble * 0.5)
            settings[airGain] = -juce::jlimit(0.0f, 3.0f,
                static_cast<float>(3 * std::log2(bleedTreble / (onsetBass * 0.02))));
        if (style != DrumStyle::natural)
        {
            const auto metal = style == DrumStyle::metal;
            settings[bodyFrequency] = juce::jlimit(45.0f, 90.0f, settings[bodyFrequency] * 0.75f);
            settings[bodyGain] = metal ? 4.5f : 3.5f;
            if (settings[boxGain] == 0) settings[boxFrequency] = metal ? 400.0f : 320.0f;
            settings[boxGain] = juce::jmax(-8.0f, settings[boxGain] - (metal ? 6.0f : 4.0f));
            settings[attackFrequency] = metal ? 4500.0f : 3000.0f;
            settings[attackGain] = metal ? 4.5f : 3.0f;
            settings[eqQ] = metal ? 1.2f : 1.0f;
            settings[bodyQ] = metal ? 2.5f : 2.0f;
            settings[boxQ] = metal ? 1.8f : 1.4f;
            settings[attackQ] = metal ? 2.0f : 1.5f;
            settings[airQ] = settings[eqQ];
        }
        for (int control : { bodyGain, boxGain, attackGain, airGain }) settings[control] *= intensity;
        settings[eqOn] = std::abs(settings[bodyGain]) + std::abs(settings[boxGain])
            + std::abs(settings[attackGain]) + std::abs(settings[airGain]) > 0.1f ? 1.0f : 0.0f;
        const auto styleIndex = static_cast<size_t>(style);
        const auto& gateHistogram = analysis.envelopeHistograms[styleIndex + 3];
        const auto quiet = analysis.percentileOf(gateHistogram, 0.2f), loud = analysis.percentileOf(gateHistogram, 0.9f);
        settings[gateOn] = loud - quiet >= 12 ? 1.0f : 0.0f;
        settings[gateThreshold] = juce::jlimit(-80.0f, -12.0f,
            juce::jmin(analysis.percentileOf(gateHistogram, 0.35f) + 6, loud - 12));
        settings[gateRatio] = 1 + (style == DrumStyle::metal ? 1.5f : style == DrumStyle::rock ? 1.0f : 0.5f) * intensity;
        settings[gateRange] = (style == DrumStyle::metal ? 12.0f : style == DrumStyle::rock ? 8.0f : 6.0f) * intensity;
        settings[gateRelease] = kickGateReleases[styleIndex];
        settings[compressorOn] = crest > 10 && intensity > 0 ? 1.0f : 0.0f;
        settings[ratio] = style == DrumStyle::metal ? 4.0f : style == DrumStyle::rock ? 3.0f : 2.0f;
        settings[attack] = style == DrumStyle::metal ? 4.0f : style == DrumStyle::rock ? 8.0f : 25.0f;
        settings[release] = style == DrumStyle::metal ? 80.0f : style == DrumStyle::rock ? 100.0f : 140.0f;
        result.targetReduction = (style == DrumStyle::metal ? 6.0f : style == DrumStyle::rock ? 4.0f : 1.5f) * intensity;
        result.maximumReduction = (style == DrumStyle::metal ? 10.0f : style == DrumStyle::rock ? 8.0f : 3.0f) * intensity;
        settings[threshold] = juce::jlimit(-60.0f, -6.0f, analysis.percentileOf(analysis.envelopeHistograms[styleIndex], 0.95f)
            - result.targetReduction / (1 - 1 / settings[ratio]));
        settings[mix] = style == DrumStyle::natural ? 70.0f : 100.0f;
        settings[multibandOn] = settings[compressorOn];
        settings[crossoverLow] = juce::jlimit(100.0f, 180.0f, analysis.kickDominantBody() * 1.4f);
        settings[crossoverHigh] = style == DrumStyle::metal ? 2500.0f : 2000.0f;
        result.bandTargets = { result.targetReduction, result.targetReduction * 0.5f, result.targetReduction * 0.25f };
        result.bandCeilings = { result.maximumReduction, result.maximumReduction * 0.6f, result.maximumReduction * 0.4f };
        const std::array<float, 3> bandRatios { settings[ratio], style == DrumStyle::metal ? 2.5f : 2.0f, 1.5f };
        const std::array<float, 3> bandAttacks { style == DrumStyle::natural ? 25.0f : 12.0f, 12.0f, 15.0f };
        const std::array<float, 3> bandReleases { settings[release], 110.0f, 80.0f };
        for (size_t band = 0; band < 3; ++band)
        {
            const auto base = static_cast<size_t>(lowThreshold) + band * 7;
            settings[base] = -35;
            settings[base + 1] = bandRatios[band];
            settings[base + 2] = bandAttacks[band];
            settings[base + 3] = bandReleases[band];
            settings[base + 4] = settings[base + 6] = 0;
            settings[base + 5] = result.bandCeilings[band];
        }
        if (intensity == 0) settings[gateOn] = 0;
        const auto focusedBody = analysis.kickDominantBody();
        settings[focusOn] = intensity > 0 ? 1.0f : 0.0f;
        settings[focusLow] = juce::jlimit(25.0f, 55.0f, focusedBody * 0.3f);
        settings[focusHigh] = juce::jlimit(140.0f, 300.0f, focusedBody * 2);
        settings[gateHold] = style == DrumStyle::metal ? 15.0f : 25.0f;
        settings[gateHysteresis] = 4;
        settings[cleanupOn] = settings[gateOn];
        settings[cleanupDepth] = (style == DrumStyle::metal ? 18.0f : style == DrumStyle::rock ? 12.0f : 6.0f) * intensity;
        settings[presenceOn] = intensity > 0 ? 1.0f : 0.0f;
        settings[reinforceFrequency] = juce::jlimit(40.0f, 80.0f, focusedBody * 0.75f);
        settings[reinforceAmount] = (style == DrumStyle::metal ? 45.0f : style == DrumStyle::rock ? 35.0f : 15.0f) * intensity;
        settings[reinforceDecay] = style == DrumStyle::metal ? 140.0f : style == DrumStyle::rock ? 180.0f : 220.0f;
        settings[transientAmount] = (style == DrumStyle::metal ? 3.0f : style == DrumStyle::rock ? 2.0f : 0.5f) * intensity;
        settings[saturationDrive] = (style == DrumStyle::metal ? 6.0f : style == DrumStyle::rock ? 4.0f : 1.0f) * intensity;
        result.valid = true;
        result.summary = juce::String(drumStyleName(style)) + " " + juce::String(intensity * 100, 0) + "% / "
            + juce::String(analysis.hits) + " candidate hits / Body EQ " + juce::String(settings[bodyFrequency], 0) + " Hz / ";
        if (settings[eqOn] == 0)
                result.summary += "Neutral EQ: no reliable correction / ";
        else
        {
            if (settings[bodyGain] > 0.1f)
                result.summary += "Body +" + juce::String(settings[bodyGain], 1) + " dB / ";
            if (settings[boxGain] < -0.1f)
                result.summary += (style == DrumStyle::natural ? "Resonance " : "Box ")
                    + juce::String(settings[boxFrequency], 0) + " Hz " + juce::String(settings[boxGain], 1) + " dB / ";
            if (settings[attackGain] > 0.1f)
                result.summary += "Attack +" + juce::String(settings[attackGain], 1) + " dB / ";
            if (settings[airGain] < -0.1f)
                result.summary += "Persistent high-frequency bleed " + juce::String(settings[airGain], 1) + " dB";
        }
        result.summary += " COMP active P75 target " + juce::String(settings[compressorOn] > 0 ? result.targetReduction : 0, 1)
            + " dB / ceiling " + juce::String(result.maximumReduction, 1) + " dB";
        result.summary += " / EQ Q " + juce::String(settings[eqQ], 2)
            + " / Q Body " + juce::String(bandQ(settings, 0), 2)
            + " / Box " + juce::String(bandQ(settings, 1), 2)
            + " / Attack " + juce::String(bandQ(settings, 2), 2)
            + " / EXP " + juce::String(settings[gateOn] > 0 ? "ON" : "OFF")
            + " threshold " + juce::String(settings[gateThreshold], 1)
            + " dB / detector P20 " + juce::String(analysis.percentileOf(analysis.envelopeHistograms[styleIndex + 3], 0.2f), 1)
            + " / P90 " + juce::String(analysis.percentileOf(analysis.envelopeHistograms[styleIndex + 3], 0.9f), 1) + " dB";
        result.summary += " / COMP " + juce::String(settings[multibandOn] > 0 ? "Multiband" : "Broadband");
        if (settings[gateOn] == 0)
            result.summary += intensity == 0 ? " (zero intensity)" : " (insufficient contrast)";
        result.summary += "\n\nFOCUS / PRESENCE\nLow detector " + juce::String(settings[focusLow], 0)
            + "-" + juce::String(settings[focusHigh], 0) + " Hz / " + (settings[focusOn] > 0 ? "ON" : "OFF")
            + "\nEXP hold " + juce::String(settings[gateHold], 0) + " ms / hysteresis "
            + juce::String(settings[gateHysteresis], 1) + " dB\nTreble cleanup >1.6 kHz: "
            + (settings[cleanupOn] > 0 ? "ON" : "OFF") + " / range " + juce::String(settings[cleanupDepth], 1)
            + " dB\nTuned reinforcement: " + (settings[presenceOn] > 0 ? "ON" : "OFF") + " / "
            + juce::String(settings[reinforceFrequency], 1) + " Hz / " + juce::String(settings[reinforceAmount], 0)
            + "% / decay -60 dB in " + juce::String(settings[reinforceDecay], 0)
            + " ms\nPunch +" + juce::String(settings[transientAmount], 1) + " dB / Low drive +"
            + juce::String(settings[saturationDrive], 1)
            + " dB\nSynthetic reinforcement, not a correction of the original pitch. Manual Polarity available.";
        return result;
    }

private:


    DrumProposal proposeInstrument(const Settings& original, DrumStyle style, float intensity) const
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
        const auto tom = selected >= DrumInstrument::tom1 && selected <= DrumInstrument::floorTom;
        const auto close = tom || selected == DrumInstrument::snare;
        const auto master = selected == DrumInstrument::master;
        const auto highOnly = selected == DrumInstrument::hiHat || selected == DrumInstrument::overheads;
        const auto crest = juce::Decibels::gainToDecibels(static_cast<float>(analysis.peak / rms));
        settings[filtersOn] = 1;
        settings[highPass] = selected == DrumInstrument::hiHat ? 250.0f
            : selected == DrumInstrument::overheads ? 100.0f : selected == DrumInstrument::snare ? 70.0f
            : selected == DrumInstrument::room ? 40.0f : master ? 20.0f : 35.0f;
        settings[lowPass] = 18000;
        settings[bodyFrequency] = tom ? analysis.spectralPeak(45, 200) : 180;
        settings[eqQ] = 0.85f;
        if (style != DrumStyle::natural && close)
        {
            const auto strength = style == DrumStyle::metal ? 1.3f : 1.0f;
            settings[bodyGain] = 1.5f * intensity;
            settings[bodyQ] = 1.2f;
            settings[boxFrequency] = tom ? 450.0f : 650.0f;
            settings[boxGain] = -2.0f * strength * intensity;
            settings[boxQ] = 1.2f;
            settings[attackFrequency] = tom ? 3500.0f : 4500.0f;
            settings[attackGain] = 2.0f * strength * intensity;
            settings[attackQ] = 1.0f;
            settings[eqOn] = intensity > 0 ? 1.0f : 0.0f;
        }
        const auto quiet = analysis.percentileOf(analysis.envelopeHistograms[3], 0.2f);
        const auto loud = analysis.percentileOf(analysis.envelopeHistograms[3], 0.9f);
        settings[gateOn] = close && loud - quiet >= 18 && intensity > 0 ? 1.0f : 0.0f;
        settings[gateThreshold] = juce::jlimit(-80.0f, -12.0f, juce::jmin(quiet + 3, loud - 18));
        settings[gateRange] = 4 * intensity;
        settings[gateRatio] = 1.5f;
        settings[gateRelease] = tom ? 350.0f : 180.0f;
        settings[compressorOn] = intensity > 0 && crest > (highOnly ? 12.0f : 6.0f) ? 1.0f : 0.0f;
        settings[ratio] = master || highOnly ? 2.0f : style == DrumStyle::metal ? 4.0f : 3.0f;
        settings[attack] = master ? 30.0f : close ? 20.0f : 30.0f;
        settings[release] = master ? 100.0f : tom ? 250.0f : 180.0f;
        settings[mix] = 100;
        result.targetReduction = (master || highOnly ? 1.5f : selected == DrumInstrument::room ? 3.0f : 3.0f) * intensity;
        result.maximumReduction = (master || highOnly ? 3.0f : 6.0f) * intensity;
        settings[threshold] = juce::jlimit(-60.0f, 0.0f, analysis.percentileOf(analysis.envelopeHistograms[0], 0.75f)
            - result.targetReduction / (1 - 1 / settings[ratio]));
        if (master)
        {
            settings[glueOn] = settings[compressorOn];
            settings[compressorOn] = 0;
            settings[glueThreshold] = settings[threshold];
            settings[glueRange] = result.maximumReduction;
            settings[glueRatio] = style == DrumStyle::metal ? 4.0f : 2.0f;
            settings[glueAttack] = 30;
            settings[glueRelease] = style == DrumStyle::natural ? 200.0f : 100.0f;
        }
        result.valid = true;
        result.summary = juce::String(instrumentName(selected)) + " / " + drumStyleName(style) + " "
            + juce::String(intensity * 100, 0) + "% / explicit profile, no instrument recognition"
            + " / source RMS " + juce::String(juce::Decibels::gainToDecibels(rms), 1) + " dBFS"
            + " / peak " + juce::String(juce::Decibels::gainToDecibels(analysis.peak), 1) + " dBFS"
            + " / crest " + juce::String(crest, 1) + " dB"
            + " / EQ coloration " + (settings[eqOn] > 0 ? "deliberate" : "neutral")
            + (master ? " / Bus Glue target P75 " : " / COMP broadband target P75 ") + juce::String(result.targetReduction, 1)
            + " dB / calibration ceiling " + juce::String(result.maximumReduction, 1)
            + " dB / Kick Focus and reinforcement OFF / EXP " + (settings[gateOn] > 0 ? "ON cautious" : "OFF");
        if (highOnly || selected == DrumInstrument::room)
            result.summary += " / check stereo image and decays by ear";
        if (master)
        {
            result.summary += " / Bus Glue: soft knee, sidechain HP 80 Hz, attack 30 ms, not an SSL emulation";
            result.summary += "\nBUS: analysis of the sum only. No individual Kick/Snare/Tom level can be measured."
                "\nCheck track levels and phase before compression. Ambience proposed from the role and measurements.";
            if (analysis.peak > juce::Decibels::decibelsToGain(-1.0f))
                result.summary += "\nSource peaks close to 0 dBFS: lower the group gain before the following processing.";
            const auto total = analysis.bandEnergy(20, juce::jmin(16000.0, analysis.rate * 0.45));
            if (total > 0 && analysis.bandEnergy(35, 150) / total < 0.08)
                result.summary += "\nSubdued low end: check Kick level, tone and phase; this does not measure its individual level.";
            if (crest < 8)
                result.summary += "\nBus already dense: watch for pumping and avoid chasing strong reduction.";
        }
        return result;
    }

    const DrumAnalysis& analysis;
};
}