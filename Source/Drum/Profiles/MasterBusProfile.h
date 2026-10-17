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
        return result;
    }
};
}
