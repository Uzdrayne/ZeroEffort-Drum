#pragma once
#include "ProfileAssembly.h"

namespace drum
{
struct SnareProfile
{
    static DrumProposal propose(const DrumAnalysis& analysis, const Settings& original, DrumStyle style, float intensity)
    {
        ProfileParameters profile {};
        profile.highPassHz = 70;
        profile.bodyHz = 180;
        profile.boxHz = 650;
        profile.attackHz = 4500;
        profile.gateReleaseMs = 180;
        profile.crestMinimum = 6;
        profile.compressorRatio = style == DrumStyle::metal ? 4.0f : 3.0f;
        profile.attackMs = 20;
        profile.releaseMs = 180;
        profile.targetReduction = 3;
        profile.maximumReduction = 6;
        profile.closeMic = true;
        return ProfileAssembly::propose(analysis, original, style, intensity, profile);
    }
};
}