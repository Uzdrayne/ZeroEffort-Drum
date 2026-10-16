#pragma once
#include "DrumEngine.h"
#include <cmath>

namespace drum
{
enum class DrumStyle { natural, rock, metal };

// Bump when a rule that shapes the proposals changes (profiles, style tables).
inline constexpr int profileVersion = 1;

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
