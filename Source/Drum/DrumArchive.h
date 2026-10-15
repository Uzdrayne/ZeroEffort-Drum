#pragma once

#include "DrumEngine.h"

namespace drum
{
struct ArchivedDrum
{
    juce::String id;
    Settings settings;
};

inline bool writeDrumEntry(juce::OutputStream& stream, const juce::String& id, const Settings& settings)
{
    if (id.isEmpty() || id.getNumBytesAsUTF8() > 4096 || !stream.writeString(id))
        return false;
    for (const auto value : settings)
        if (!stream.writeFloat(value))
            return false;
    return true;
}

inline bool readDrumArchive(juce::InputStream& stream, std::vector<ArchivedDrum>& entries)
{
    entries.clear();
    if (stream.getNumBytesRemaining() < 4)
        return false;
    const auto magic = stream.readInt();
    if (magic == 0x44524131)
        return true;
    if ((magic != 0x44524132 && magic != 0x44524133) || stream.getNumBytesRemaining() < 4)
        return false;
    const auto count = stream.readInt();
    if (count < 0 || count > 10000)
        return false;
    if (magic == 0x44524133 && stream.getNumBytesRemaining() < 4) return false;
    const auto storedControls = magic == 0x44524132 ? 29 : stream.readInt();
    if (storedControls < 29 || storedControls > 256) return false;
    for (int index = 0; index < count; ++index)
    {
        juce::MemoryOutputStream identifier;
        bool terminated = false;
        for (int byte = 0; byte <= 4096 && stream.getNumBytesRemaining() > 0; ++byte)
        {
            const auto value = stream.readByte();
            if (value == 0)
            {
                terminated = true;
                break;
            }
            identifier.writeByte(value);
        }
        if (!terminated || identifier.getDataSize() == 0
            || stream.getNumBytesRemaining() < static_cast<juce::int64>(storedControls * sizeof(float)))
            return false;
        ArchivedDrum entry { identifier.toUTF8(), defaultSettings() };
        for (int control = 0; control < storedControls; ++control)
        {
            const auto value = stream.readFloat();
            if (!std::isfinite(value))
                return false;
            if (control < controlCount)
            {
                const auto position = static_cast<size_t>(control);
                entry.settings[position] = juce::jlimit(controlSpecs[position].minimum, controlSpecs[position].maximum, value);
            }
        }
        entries.push_back(std::move(entry));
    }
    return true;
}
}