#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

namespace drum
{
// Values 0 to 9 are stored in projects and never change. The sub-buses (sum of the mics of one instrument, document 08) come after.
enum class DrumInstrument { kick, snare, tom1, tom2, tom3, floorTom, hiHat, overheads, room, master, kickBus, snareBus, tomsBus };
inline constexpr int instrumentCount = 13;
inline constexpr std::array<const char*, instrumentCount> instrumentNames {
    "Kick", "Snare", "Tom 1", "Tom 2", "Tom 3", "Floor Tom", "Hi-hat", "Overheads", "Room", "Drum Master Bus",
    "Kick Bus", "Snare Bus", "Toms Bus"
};
inline DrumInstrument instrumentFromValue(float value) noexcept
{
    if (!std::isfinite(value)) return DrumInstrument::kick;
    const auto index = static_cast<int>(std::round(value));
    // Below the first role: Kick. Beyond the last one: the drum master bus, as before the sub-buses existed (values above 9 clamped to 9).
    return index < 0 ? DrumInstrument::kick : index >= instrumentCount ? DrumInstrument::master : static_cast<DrumInstrument>(index);
}
// A sub-bus sits on the group track of one instrument and receives the sum of its mics: no kit alignment, no gate.
inline bool isSubBus(DrumInstrument value) noexcept
{
    return value == DrumInstrument::kickBus || value == DrumInstrument::snareBus || value == DrumInstrument::tomsBus;
}
inline const char* instrumentName(DrumInstrument value) noexcept
{
    return instrumentNames[static_cast<size_t>(value)];
}
}