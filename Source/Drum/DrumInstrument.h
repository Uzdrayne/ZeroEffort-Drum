#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

namespace drum
{
enum class DrumInstrument { kick, snare, tom1, tom2, tom3, floorTom, hiHat, overheads, room, master };
inline constexpr std::array<const char*, 10> instrumentNames {
    "Kick", "Snare", "Tom 1", "Tom 2", "Tom 3", "Floor Tom", "Hi-hat", "Overheads", "Room", "Drum Master Bus"
};
inline DrumInstrument instrumentFromValue(float value) noexcept
{
    return static_cast<DrumInstrument>(std::isfinite(value) ? juce::jlimit(0, 9, static_cast<int>(std::round(value))) : 0);
}
inline const char* instrumentName(DrumInstrument value) noexcept
{
    return instrumentNames[static_cast<size_t>(value)];
}
}