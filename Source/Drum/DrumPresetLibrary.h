#pragma once
#include "DrumEngine.h"
#include "DrumInstrument.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace drum
{
// A user preset belongs to a family: the four toms share theirs, every other instrument has its own.
enum class PresetFamily { kick, snare, toms, hiHat, overheads, room, bus };

inline PresetFamily presetFamilyOf(DrumInstrument instrument) noexcept
{
    switch (instrument)
    {
        case DrumInstrument::kick: return PresetFamily::kick;
        case DrumInstrument::snare: return PresetFamily::snare;
        case DrumInstrument::tom1: case DrumInstrument::tom2: case DrumInstrument::tom3: case DrumInstrument::floorTom:
            return PresetFamily::toms;
        case DrumInstrument::hiHat: return PresetFamily::hiHat;
        case DrumInstrument::overheads: return PresetFamily::overheads;
        case DrumInstrument::room: return PresetFamily::room;
        case DrumInstrument::master: return PresetFamily::bus;
    }
    return PresetFamily::kick;
}

inline const char* presetFamilyName(PresetFamily family) noexcept
{
    constexpr std::array<const char*, 7> names { "Kick", "Snare", "Toms", "Hi-hat", "Overheads", "Room", "Bus" };
    return names[static_cast<size_t>(family)];
}

// One .zedpreset file (JSON) per preset, in a folder per family. Controls are stored by their stable id, so a preset
// survives reordering of the control list; unknown ids are ignored and missing ones fall back to the default value.
class DrumPresetLibrary
{
public:
    static constexpr int formatVersion = 1;
    static constexpr int maximumNameLength = 48;
    static constexpr juce::int64 maximumFileBytes = 256 * 1024;

    explicit DrumPresetLibrary(juce::File rootFolder) : root(std::move(rootFolder)) {}

    static juce::File defaultRoot()
    {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("ZeroEffort Drum").getChildFile("Presets");
    }
    const juce::File& getRoot() const noexcept { return root; }
    void setRoot(const juce::File& folder) { root = folder; }

    // What a preset holds: the sound of the instrument. The role, the bypass, the kit alignment, the Auto options and the
    // mono monitoring belong to the track or the session, not to the sound.
    static bool isStored(size_t control) noexcept
    {
        switch (control)
        {
            case bypass: case instrument: case autoStyleOption: case autoIntensityOption: case monitorMonoOn:
            case kitGroup: case kitMic: case kitMode: case kitStrength: case kitPolarity: case kitDelay:
                return false;
            default:
                return true;
        }
    }

    // A name that is safe as a Windows file name, or an empty string when nothing usable is left.
    static juce::String cleanName(const juce::String& name)
    {
        juce::String cleaned;
        for (const auto character : name.trim())
            if (character >= 32 && juce::String("<>:\"/\\|?*").indexOfChar(character) < 0)
                cleaned += character;
        cleaned = cleaned.substring(0, maximumNameLength).trim();
        while (cleaned.startsWithChar('.')) cleaned = cleaned.substring(1).trimStart();
        while (cleaned.endsWithChar('.')) cleaned = cleaned.dropLastCharacters(1).trimEnd();
        const auto base = cleaned.upToFirstOccurrenceOf(".", false, false).trimEnd().toUpperCase();
        for (const char* reserved : { "CON", "PRN", "AUX", "NUL" })
            if (base == reserved) return {};
        for (const char* prefix : { "COM", "LPT" })
            if (base.length() == 4 && base.startsWith(prefix) && base[3] >= '1' && base[3] <= '9') return {};
        return cleaned;
    }

    juce::File folderFor(PresetFamily family) const { return root.getChildFile(presetFamilyName(family)); }
    juce::File fileFor(PresetFamily family, const juce::String& name) const
    {
        return folderFor(family).getChildFile(cleanName(name) + ".zedpreset");
    }

    bool exists(PresetFamily family, const juce::String& name) const
    {
        return cleanName(name).isNotEmpty() && fileFor(family, name).existsAsFile();
    }

    std::vector<juce::String> list(PresetFamily family) const
    {
        std::vector<juce::String> names;
        for (const auto& entry : juce::RangedDirectoryIterator(folderFor(family), false, "*.zedpreset", juce::File::findFiles))
            names.push_back(entry.getFile().getFileNameWithoutExtension());
        std::sort(names.begin(), names.end(), [](const juce::String& a, const juce::String& b) {
            return a.compareNatural(b, false) < 0;
        });
        return names;
    }

    juce::Result save(PresetFamily family, const juce::String& name, const Settings& settings,
                      DrumInstrument origin, const juce::String& pluginVersion) const
    {
        const auto clean = cleanName(name);
        if (clean.isEmpty()) return juce::Result::fail("This name cannot be used for a preset");
        const auto folder = folderFor(family);
        if (!folder.createDirectory()) return juce::Result::fail("Cannot create the preset folder: " + folder.getFullPathName());
        auto controls = std::make_unique<juce::DynamicObject>();
        for (size_t index = 0; index < controlSpecs.size(); ++index)
            if (isStored(index))
                controls->setProperty(controlSpecs[index].id, static_cast<double>(settings[index]));
        auto object = std::make_unique<juce::DynamicObject>();
        object->setProperty("format", formatVersion);
        object->setProperty("plugin", pluginVersion);
        object->setProperty("name", clean);
        object->setProperty("family", presetFamilyName(family));
        object->setProperty("instrument", instrumentName(origin));
        object->setProperty("controls", juce::var(controls.release()));
        const auto target = fileFor(family, clean);
        juce::TemporaryFile temporary(target);
        if (!temporary.getFile().replaceWithText(juce::JSON::toString(juce::var(object.release())))
            || !temporary.overwriteTargetFileWithTemporary())
            return juce::Result::fail("Cannot write " + target.getFullPathName());
        return juce::Result::ok();
    }

    // `result` starts from the defaults, takes the stored controls, then the track-level controls of `current`.
    juce::Result load(PresetFamily family, const juce::String& name, const Settings& current, Settings& result) const
    {
        if (cleanName(name).isEmpty()) return juce::Result::fail("This name cannot be used for a preset");
        const auto file = fileFor(family, name);
        if (!file.existsAsFile()) return juce::Result::fail("Preset not found: " + name);
        if (file.getSize() > maximumFileBytes) return juce::Result::fail("Preset file too large: " + name);
        const auto parsed = juce::JSON::parse(file.loadFileAsString());
        const auto* object = parsed.getDynamicObject();
        if (object == nullptr) return juce::Result::fail("Not a preset file: " + name);
        if (!object->getProperty("format").isInt() || static_cast<int>(object->getProperty("format")) > formatVersion)
            return juce::Result::fail("This preset was saved by a newer version: " + name);
        if (object->getProperty("family").toString() != presetFamilyName(family))
            return juce::Result::fail("This preset belongs to another instrument: " + name);
        const auto* controls = object->getProperty("controls").getDynamicObject();
        if (controls == nullptr) return juce::Result::fail("Preset without settings: " + name);
        result = defaultSettings();
        for (size_t index = 0; index < controlSpecs.size(); ++index)
        {
            if (!isStored(index))
            {
                result[index] = current[index];
                continue;
            }
            const auto stored = controls->getProperty(controlSpecs[index].id);
            if (!stored.isDouble() && !stored.isInt() && !stored.isInt64()) continue;
            const auto value = static_cast<double>(stored);
            if (std::isfinite(value))
                result[index] = juce::jlimit(controlSpecs[index].minimum, controlSpecs[index].maximum, static_cast<float>(value));
        }
        return juce::Result::ok();
    }

    // To the recycle bin, so that a deletion can be undone.
    juce::Result remove(PresetFamily family, const juce::String& name) const
    {
        if (cleanName(name).isEmpty()) return juce::Result::fail("This name cannot be used for a preset");
        const auto file = fileFor(family, name);
        if (!file.existsAsFile()) return juce::Result::fail("Preset not found: " + name);
        return file.moveToTrash() ? juce::Result::ok() : juce::Result::fail("Cannot delete " + file.getFullPathName());
    }

private:
    juce::File root;
};
}
