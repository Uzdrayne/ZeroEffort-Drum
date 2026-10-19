#pragma once
#include "ProfileAssembly.h"

namespace drum
{
struct OverheadsProfile
{
    static DrumProposal propose(const DrumAnalysis& analysis, const Settings& original, DrumStyle style, float intensity,
        const ProposalPolicy& policy = {})
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
        if (policy.cymbalReference)
        {
            profile.referenceHighPassHz = 180;
            profile.referenceAirHz = 6000;
            profile.referenceAirDb = 3.6f;
        }
        return ProfileAssembly::propose(analysis, original, style, intensity, profile);
    }
};
}