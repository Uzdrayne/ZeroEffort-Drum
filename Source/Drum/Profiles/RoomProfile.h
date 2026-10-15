#pragma once
#include "ProfileAssembly.h"

namespace drum
{
struct RoomProfile
{
    static DrumProposal propose(const DrumAnalysis& analysis, const Settings& original, DrumStyle style, float intensity)
    {
        ProfileParameters profile {};
        profile.highPassHz = 40;
        profile.bodyHz = 180;
        profile.gateReleaseMs = 180;
        profile.crestMinimum = 6;
        profile.compressorRatio = style == DrumStyle::metal ? 4.0f : 3.0f;
        profile.attackMs = 30;
        profile.releaseMs = 180;
        profile.targetReduction = 3;
        profile.maximumReduction = 6;
        profile.stereoCaution = true;
        return ProfileAssembly::propose(analysis, original, style, intensity, profile);
    }
};
}