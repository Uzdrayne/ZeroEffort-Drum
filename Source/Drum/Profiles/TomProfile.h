#pragma once
#include "ProfileAssembly.h"

namespace drum
{
struct TomProfile
{
    static DrumProposal propose(const DrumAnalysis& analysis, const Settings& original, DrumStyle style, float intensity)
    {
        ProfileParameters profile {};
        profile.highPassHz = 35;
        profile.bodyFrequency = [](const DrumAnalysis& measures) { return measures.spectralPeak(45, 200); };
        profile.boxHz = 450;
        profile.attackHz = 3500;
        profile.gateReleaseMs = 350;
        profile.crestMinimum = 6;
        profile.compressorRatio = style == DrumStyle::metal ? 4.0f : 3.0f;
        profile.attackMs = 20;
        profile.releaseMs = 250;
        profile.targetReduction = 3;
        profile.maximumReduction = 6;
        profile.closeMic = true;
        return ProfileAssembly::propose(analysis, original, style, intensity, profile);
    }
};
}