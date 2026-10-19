#pragma once
#include "ProfileAssembly.h"

namespace drum
{
struct HiHatProfile
{
    static DrumProposal propose(const DrumAnalysis& analysis, const Settings& original, DrumStyle style, float intensity,
        const ProposalPolicy& policy = {})
    {
        ProfileParameters profile {};
        profile.highPassHz = 250;
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
            // The high-pass is already at its maximum (250 Hz): the engineer's steeper cut is reached with the Body band.
            profile.referenceBodyHz = 200;
            profile.referenceBodyDb = -7.1f;
        }
        return ProfileAssembly::propose(analysis, original, style, intensity, profile);
    }
};
}