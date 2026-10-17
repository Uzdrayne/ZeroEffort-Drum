#pragma once
#include "DrumEngine.h"
#include <cmath>

namespace drum
{
enum class DrumStyle { natural, rock, metal };

// Bump when a rule that shapes the proposals changes (profiles, style tables).
// Version 2: the Kick compressor reads the active crest by default (J3 activated).
inline constexpr int profileVersion = 2;

// J3, problem 2. Activated on 17 October 2026 (validated by the user): it changes when the Kick compressor switches on.
struct ProposalDefaults
{
    static constexpr bool compressorUsesActiveCrest = true;
};

// Choices that change a proposal without changing the analysis. Recorded in the snapshots.
struct ProposalPolicy
{
    bool compressorUsesActiveCrest = ProposalDefaults::compressorUsesActiveCrest;
};

inline const char* drumStyleName(DrumStyle style)
{
    return style == DrumStyle::rock ? "Rock" : style == DrumStyle::metal ? "Metal" : "Naturel";
}

struct DrumProposal
{
    bool valid = false;
    Settings settings = defaultSettings();
    juce::String summary;
    int hits = 0;
    float targetReduction = 0;
    float maximumReduction = 0;
    std::array<float, 3> bandTargets {}, bandCeilings {};
};
}
