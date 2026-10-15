#pragma once
#include "ProfileAssembly.h"

namespace drum
{
struct OverheadsProfile
{
    static DrumProposal propose(const DrumAnalysis& analysis, const Settings& original, DrumStyle style, float intensity)
    {
        ProfileParameters profile {};
        profile.highPassHz = 100;
        profile.bodyHz = 180;
        profile.gateReleaseMs = 180;
        profile.crestMinimum = 12;
        profile.compressorRatio = 2;
        profile.attackMs = 30;
        profile.releaseMs = 180;
        profile.targetReduction = 1.5f;
        profile.maximumReduction = 3;
        profile.stereoCaution = true;
        return ProfileAssembly::propose(analysis, original, style, intensity, profile);
    }
};
}