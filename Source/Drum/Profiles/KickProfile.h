#pragma once
#include "../DrumAnalysis.h"

namespace drum
{
struct KickProfile
{
    static DrumProposal propose(const DrumAnalysis& analysis, const Settings& original, DrumStyle style, float intensity)
    {
        DrumProposal result;
        result.settings = original;
        result.hits = analysis.hits;
        const auto rms = analysis.samples > 0 ? std::sqrt(analysis.energy / static_cast<double>(analysis.samples)) : 0.0;
        const auto crest = rms > 0 ? juce::Decibels::gainToDecibels(analysis.peak / rms) : 0.0;
        const auto bass = analysis.bandEnergy(35, 150);
        const auto total = analysis.bandEnergy(20, juce::jmin(16000.0, analysis.rate * 0.45));
        if (analysis.invalid || analysis.rate < 8000 || analysis.samples < analysis.rate * 2 || analysis.peak < 0.001f || analysis.hits < 3
            || crest < 6 || total <= 0 || bass / total < 0.15)
        {
            result.summary = "Analyse insuffisante : au moins 3 frappes de kick nettes sur 2 s.";
            return result;
        }
        if (analysis.clipped > analysis.samples / 100)
        {
            result.summary = "Signal sature : reduire le niveau source puis analyser.";
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
            + juce::String(analysis.hits) + " attaques candidates / Body EQ " + juce::String(settings[bodyFrequency], 0) + " Hz / ";
        if (settings[eqOn] == 0)
                result.summary += "EQ neutre : pas de correction fiable / ";
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
                result.summary += "Aigu persistant " + juce::String(settings[airGain], 1) + " dB";
        }
        result.summary += " COMP actif P75 cible " + juce::String(settings[compressorOn] > 0 ? result.targetReduction : 0, 1)
            + " dB / plafond " + juce::String(result.maximumReduction, 1) + " dB";
        result.summary += " / EQ Q " + juce::String(settings[eqQ], 2)
            + " / Q Body " + juce::String(bandQ(settings, 0), 2)
            + " / Box " + juce::String(bandQ(settings, 1), 2)
            + " / Attack " + juce::String(bandQ(settings, 2), 2)
            + " / EXP " + juce::String(settings[gateOn] > 0 ? "ON" : "OFF")
            + " seuil " + juce::String(settings[gateThreshold], 1)
            + " dB / detecteur P20 " + juce::String(analysis.percentileOf(analysis.envelopeHistograms[styleIndex + 3], 0.2f), 1)
            + " / P90 " + juce::String(analysis.percentileOf(analysis.envelopeHistograms[styleIndex + 3], 0.9f), 1) + " dB";
        result.summary += " / COMP " + juce::String(settings[multibandOn] > 0 ? "Multibande" : "Large bande");
        if (settings[gateOn] == 0)
            result.summary += intensity == 0 ? " (intensite nulle)" : " (contraste insuffisant)";
        result.summary += "\n\nFOCUS / PRESENCE\nDetecteur grave " + juce::String(settings[focusLow], 0)
            + "-" + juce::String(settings[focusHigh], 0) + " Hz / " + (settings[focusOn] > 0 ? "ON" : "OFF")
            + "\nEXP hold " + juce::String(settings[gateHold], 0) + " ms / hysteresis "
            + juce::String(settings[gateHysteresis], 1) + " dB\nNettoyage aigu >1.6 kHz : "
            + (settings[cleanupOn] > 0 ? "ON" : "OFF") + " / range " + juce::String(settings[cleanupDepth], 1)
            + " dB\nRenfort accorde : " + (settings[presenceOn] > 0 ? "ON" : "OFF") + " / "
            + juce::String(settings[reinforceFrequency], 1) + " Hz / " + juce::String(settings[reinforceAmount], 0)
            + "% / extinction -60 dB en " + juce::String(settings[reinforceDecay], 0)
            + " ms\nPunch +" + juce::String(settings[transientAmount], 1) + " dB / Drive grave +"
            + juce::String(settings[saturationDrive], 1)
            + " dB\nRenfort synthetique, pas une correction du pitch original. Polarity manuel disponible.";
        return result;
    }
};
}
