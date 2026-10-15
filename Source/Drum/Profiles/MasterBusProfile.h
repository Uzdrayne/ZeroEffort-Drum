#pragma once
#include "ProfileAssembly.h"

namespace drum
{
struct MasterBusProfile
{
    static DrumProposal propose(const DrumAnalysis& analysis, const Settings& original, DrumStyle style, float intensity)
    {
        ProfileParameters profile {};
        profile.highPassHz = 20;
        profile.bodyHz = 180;
        profile.gateReleaseMs = 180;
        profile.crestMinimum = 6;
        profile.compressorRatio = 2;
        profile.attackMs = 30;
        profile.releaseMs = 100;
        profile.targetReduction = 1.5f;
        profile.maximumReduction = 3;
        profile.busGlue = true;
        float crest = 0;
        auto result = ProfileAssembly::propose(analysis, original, style, intensity, profile, &crest);
        if (!result.valid) return result;
        auto& settings = result.settings;
        settings[glueOn] = settings[compressorOn];
        settings[compressorOn] = 0;
        settings[glueThreshold] = settings[threshold];
        settings[glueRange] = result.maximumReduction;
        settings[glueRatio] = style == DrumStyle::metal ? 4.0f : 2.0f;
        settings[glueAttack] = 30;
        settings[glueRelease] = style == DrumStyle::natural ? 200.0f : 100.0f;
        result.summary += " / Bus Glue : knee doux, sidechain HP 80 Hz, attaque 30 ms, pas une emulation SSL";
        result.summary += "\nBUS : analyse de la somme seulement. Aucun niveau individuel Kick/Snare/Tom mesurable."
            "\nVerifier niveaux et phase des pistes avant compression. Ambiance proposee selon le role et les mesures.";
        if (analysis.peak > juce::Decibels::decibelsToGain(-1.0f))
            result.summary += "\nCretes source proches de 0 dBFS : reduire le gain du groupe avant les traitements suivants.";
        const auto total = analysis.bandEnergy(20, juce::jmin(16000.0, analysis.rate * 0.45));
        if (total > 0 && analysis.bandEnergy(35, 150) / total < 0.08)
            result.summary += "\nGrave discret : verifier niveau, timbre et phase du Kick ; pas une mesure de son niveau individuel.";
        if (crest < 8)
            result.summary += "\nBus deja dense : surveiller le pompage, eviter de rechercher une forte reduction.";
        return result;
    }
};
}
