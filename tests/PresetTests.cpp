#include "ARA/ZeroEffortDrumProcessor.h"
#include "Drum/DrumPresetLibrary.h"
#include <iostream>
#include <stdexcept>

namespace
{
void check(bool condition, const juce::String& message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

struct TemporaryFolder
{
    TemporaryFolder() : folder(juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("zed_presets_" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64())))
    {
        folder.createDirectory();
    }
    ~TemporaryFolder() { folder.deleteRecursively(); }
    juce::File folder;
};

void testNames()
{
    using drum::DrumPresetLibrary;
    check(DrumPresetLibrary::cleanName("  Kick: tight?  ") == "Kick tight", "Characters forbidden in file names must be dropped");
    check(DrumPresetLibrary::cleanName("..\\..\\evil") == "evil", "A name can never climb out of its folder");
    check(DrumPresetLibrary::cleanName("...") .isEmpty() && DrumPresetLibrary::cleanName("   ").isEmpty(), "Nothing usable means no name");
    check(DrumPresetLibrary::cleanName("con").isEmpty() && DrumPresetLibrary::cleanName("NUL.txt").isEmpty()
        && DrumPresetLibrary::cleanName("com1").isEmpty() && DrumPresetLibrary::cleanName("LPT9").isEmpty(),
        "Windows device names must be refused");
    check(DrumPresetLibrary::cleanName("com10") == "com10" && DrumPresetLibrary::cleanName("Console") == "Console",
        "Names that only look like device names are fine");
    check(DrumPresetLibrary::cleanName(juce::String::repeatedString("a", 200)).length() == DrumPresetLibrary::maximumNameLength,
        "Names are limited in length");
    check(DrumPresetLibrary::cleanName("Snare\tcrack") == "Snarecrack", "Control characters must be dropped");

    using drum::DrumInstrument;
    const auto toms = drum::PresetFamily::toms;
    check(drum::presetFamilyOf(DrumInstrument::tom1) == toms && drum::presetFamilyOf(DrumInstrument::tom2) == toms
        && drum::presetFamilyOf(DrumInstrument::tom3) == toms && drum::presetFamilyOf(DrumInstrument::floorTom) == toms,
        "The four toms share their presets");
    check(drum::presetFamilyOf(DrumInstrument::kick) != drum::presetFamilyOf(DrumInstrument::snare)
        && drum::presetFamilyOf(DrumInstrument::hiHat) != drum::presetFamilyOf(DrumInstrument::overheads)
        && drum::presetFamilyOf(DrumInstrument::room) != drum::presetFamilyOf(DrumInstrument::master),
        "Every other instrument has its own presets");
}

void testLibrary()
{
    TemporaryFolder temporary;
    const drum::DrumPresetLibrary library(temporary.folder);
    const auto kick = drum::PresetFamily::kick;

    auto sound = drum::defaultSettings();
    sound[drum::bodyGain] = 3.25f;
    sound[drum::ratio] = 4.5f;
    sound[drum::compressorOn] = 1;
    sound[drum::instrument] = 0;
    sound[drum::bypass] = 1;
    sound[drum::autoStyleOption] = 2;
    sound[drum::kitDelay] = 7;
    check(library.save(kick, "Tight kick", sound, drum::DrumInstrument::kick, "0.11.1", "Rock", 75).wasOk(), "A preset must be saved");
    check(library.hint(kick, "Tight kick") == "Rock 75 %", "A preset remembers the Auto style it was saved with");
    check(library.hint(kick, "Missing").isEmpty(), "A missing preset has no hint");
    check(library.exists(kick, "Tight kick") && !library.exists(drum::PresetFamily::snare, "Tight kick"),
        "A preset exists in its own family only");

    auto current = drum::defaultSettings();
    current[drum::instrument] = 0;
    current[drum::kitDelay] = 3;
    current[drum::kitMic] = 1;
    current[drum::bypass] = 0;
    current[drum::autoStyleOption] = 1;
    current[drum::autoIntensityOption] = 40;
    current[drum::bodyGain] = -2;
    drum::Settings loaded {};
    check(library.load(kick, "Tight kick", current, loaded).wasOk(), "A saved preset must load");
    check(loaded[drum::kitMic] == sound[drum::kitMic], "Loading Kick In must restore its microphone position");
    for (size_t index = 0; index < loaded.size(); ++index)
    {
        if (drum::DrumPresetLibrary::isStored(index))
            check(std::abs(loaded[index] - sound[index]) < 1e-6f, "Stored controls must round-trip: " + juce::String(drum::controlSpecs[index].id));
        else
            check(loaded[index] == current[index], "Track-level controls must stay as they are: " + juce::String(drum::controlSpecs[index].id));
    }

    check(library.load(drum::PresetFamily::snare, "Tight kick", current, loaded).failed(), "A preset cannot be loaded as another family");
    check(library.load(kick, "Missing", current, loaded).failed(), "A missing preset must fail cleanly");

    const auto write = [&](const juce::String& name, const juce::String& text) {
        library.folderFor(kick).createDirectory();
        check(library.fileFor(kick, name).replaceWithText(text), "Test file must be written");
    };
    write("Hand made", R"({"format":1,"family":"Kick","controls":{"inputDb":99,"bodyGainDb":-1.5,"noSuchControl":5,"highPassHz":"text"}})");
    check(library.load(kick, "Hand made", current, loaded).wasOk(), "A hand-written preset must load");
    check(loaded[drum::input] == drum::controlSpecs[drum::input].maximum, "Out-of-range values must be clamped");
    check(loaded[drum::highPass] == drum::controlSpecs[drum::highPass].initial, "Unusable values fall back to the default");
    check(loaded[drum::lowPass] == drum::controlSpecs[drum::lowPass].initial, "Missing controls fall back to the default");
    check(loaded[drum::instrument] == current[drum::instrument] && loaded[drum::kitMic] == current[drum::kitMic],
        "Legacy presets without identity must preserve the current instrument and microphone");

    check(library.hint(kick, "Hand made").isEmpty(), "A preset saved without a style has no hint");
    write("Odd hint", R"({"format":1,"family":"Kick","style":"<b>Rock!</b>","intensity":250,"controls":{}})");
    check(library.hint(kick, "Odd hint") == "bRockb 100 %", "The hint is sanitised and bounded");
    write("Newer", R"({"format":3,"family":"Kick","controls":{}})");
    check(library.load(kick, "Newer", current, loaded).failed(), "A preset from a newer format must be refused");
    write("Broken", "{ not json");
    check(library.load(kick, "Broken", current, loaded).failed(), "A broken file must be refused");
    write("No controls", R"({"format":1,"family":"Kick"})");
    check(library.load(kick, "No controls", current, loaded).failed(), "A file without settings must be refused");
    write("Huge", juce::String::repeatedString("x", static_cast<int>(drum::DrumPresetLibrary::maximumFileBytes) + 10));
    check(library.load(kick, "Huge", current, loaded).failed(), "An oversized file must be refused");

    check(library.save(kick, "..\\..\\evil", sound, drum::DrumInstrument::kick, "0.11.1").wasOk()
        && library.fileFor(kick, "evil").existsAsFile() && !temporary.folder.getParentDirectory().getChildFile("evil.zedpreset").exists(),
        "A hostile name must stay inside the family folder");

    const auto names = library.list(kick);
    check(names.size() == 8, "Every preset file must be listed");
    check(names.front() == "Broken" && names.back() == "Tight kick", "Presets are listed in name order");
    check(library.list(drum::PresetFamily::room).empty(), "A family without presets lists nothing");

    check(library.save(kick, "Tight kick", current, drum::DrumInstrument::kick, "0.11.1").wasOk(), "Saving again must overwrite");
    check(library.list(kick).size() == 8, "Overwriting must not add a file");
    check(library.remove(kick, "Tight kick").wasOk() && !library.exists(kick, "Tight kick"), "A preset can be deleted");
    check(library.remove(kick, "Tight kick").failed(), "Deleting twice must fail cleanly");
}

void testIdentityFormat()
{
    TemporaryFolder temporary;
    const drum::DrumPresetLibrary library(temporary.folder);
    for (int role = 0; role < drum::instrumentCount; ++role)
    {
        const auto instrument = static_cast<drum::DrumInstrument>(role);
        const auto family = drum::presetFamilyOf(instrument);
        for (int position = 0; position <= 3; ++position)
        {
            auto sound = drum::defaultSettings();
            sound[drum::instrument] = static_cast<float>(role);
            sound[drum::kitMic] = static_cast<float>(position);
            auto current = drum::defaultSettings();
            current[drum::instrument] = static_cast<float>(drum::DrumInstrument::floorTom);
            current[drum::kitMic] = static_cast<float>((position + 1) % 4);
            current[drum::kitGroup] = 6;
            current[drum::kitDelay] = 4;
            current[drum::kitPolarity] = 1;
            check(library.save(family, "Identity", sound, instrument, "test").wasOk(), "Identity preset must save");
            drum::Settings loaded {};
            check(library.load(family, "Identity", current, loaded).wasOk(), "Identity preset must load");
            check(loaded[drum::instrument] == sound[drum::instrument] && loaded[drum::kitMic] == sound[drum::kitMic],
                "Every instrument and microphone position must round-trip");
            for (auto control : { drum::kitGroup, drum::kitMode, drum::kitStrength, drum::kitDelay, drum::kitPolarity })
                check(loaded[control] == current[control], "Presets must not import Kit membership or corrections");
        }
    }

    const auto kick = drum::PresetFamily::kick;
    const auto current = drum::defaultSettings();
    auto invalid = current;
    invalid[drum::kitMic] = 0.5f;
    check(library.save(kick, "Bad save", invalid, drum::DrumInstrument::kick, "test").failed()
        && !library.exists(kick, "Bad save"), "Invalid identity must not be written");
    check(library.save(kick, "Bad family", current, drum::DrumInstrument::snare, "test").failed(),
        "Saved identity and family must agree");
    const auto toms = drum::PresetFamily::toms;
    check(library.fileFor(toms, "Legacy").replaceWithText(
        R"({"format":1,"family":"Toms","instrument":"Tom 2","controls":{"bodyDb":2}})"), "Legacy tom fixture must be written");
    auto oldTrack = current;
    oldTrack[drum::instrument] = static_cast<float>(drum::DrumInstrument::floorTom);
    oldTrack[drum::kitMic] = 3;
    drum::Settings oldLoaded {};
    check(library.load(toms, "Legacy", oldTrack, oldLoaded).wasOk()
        && oldLoaded[drum::instrument] == oldTrack[drum::instrument] && oldLoaded[drum::kitMic] == oldTrack[drum::kitMic],
        "Legacy descriptive metadata must not invent a restorable identity");
    for (const auto* json : {
        R"({"format":2,"family":"Kick","controls":{"instrument":0}})",
        R"({"format":2,"family":"Kick","controls":{"instrument":0,"kitMic":"In"}})",
        R"({"format":2,"family":"Kick","controls":{"instrument":0,"kitMic":1.5}})",
        R"({"format":2,"family":"Kick","controls":{"instrument":0,"kitMic":4}})",
        R"({"format":2,"family":"Kick","controls":{"instrument":13,"kitMic":0}})",
        R"({"format":2,"family":"Kick","controls":{"instrument":1,"kitMic":0}})",
        R"({"format":0,"family":"Kick","controls":{}})" })
    {
        check(library.fileFor(kick, "Invalid").replaceWithText(json), "Malformed preset fixture must be written");
        auto loaded = current;
        loaded[drum::bodyGain] = -3;
        const auto untouched = loaded;
        const auto result = library.load(kick, "Invalid", current, loaded);
        check(result.failed() && result.getErrorMessage().isNotEmpty(), "Malformed identity must be refused explicitly");
        check(loaded == untouched, "A refused preset must not return partial settings");
    }
}

void testProcessor()
{
    TemporaryFolder temporary;
    ZeroEffortDrumProcessor processor;
    processor.setRateAndBufferSizeDetails(48000, 256);
    processor.prepareToPlay(48000, 256);
    processor.setUserPresetRoot(temporary.folder);

    processor.setControl(drum::instrument, static_cast<float>(drum::DrumInstrument::snare));
    processor.setControl(drum::bodyGain, 3.5f);
    processor.setControl(drum::ratio, 5.0f);
    processor.setKitControl(ZeroEffortDrumProcessor::kitMic, 1);
    processor.setAutoStyle(drum::DrumStyle::metal, 0.6f);
    check(processor.saveUserPreset("Crack").wasOk() && processor.listUserPresets().size() == 1, "The current settings must be saved");
    check(processor.getUserPresetHint("Crack") == "Metal 60 %", "The hint shows the Auto style selected when saving");
    check(processor.getAutoPresetText().contains("Crack"), "The status must name the saved preset");
    check(processor.saveUserPreset("???").failed(), "An unusable name must be refused");

    processor.setControl(drum::instrument, static_cast<float>(drum::DrumInstrument::kick));
    check(processor.listUserPresets().empty(), "A Kick must not be offered Snare presets");
    check(processor.loadUserPreset("Crack").failed(), "A Kick cannot load a Snare preset");

    processor.setControl(drum::instrument, static_cast<float>(drum::DrumInstrument::tom2));
    check(processor.saveUserPreset("Boomy").wasOk(), "A tom preset must be saved");
    processor.setControl(drum::instrument, static_cast<float>(drum::DrumInstrument::floorTom));
    check(processor.listUserPresets().size() == 1 && processor.listUserPresets().front() == "Boomy",
        "The toms share their presets");
    check(processor.loadUserPreset("Boomy").wasOk()
        && processor.getSettings()[drum::instrument] == static_cast<float>(drum::DrumInstrument::tom2),
        "A shared tom preset must restore its original tom");
    check(processor.revertAutoPreset()
        && processor.getSettings()[drum::instrument] == static_cast<float>(drum::DrumInstrument::floorTom),
        "Revert must restore the previous tom");

    processor.setControl(drum::instrument, static_cast<float>(drum::DrumInstrument::snare));
    processor.setKitControl(ZeroEffortDrumProcessor::kitMic, 0);
    processor.setKitControl(ZeroEffortDrumProcessor::kitDelay, 3);
    processor.setKitControl(ZeroEffortDrumProcessor::kitPolarity, 1);
    processor.setControl(drum::bodyGain, -4.0f);
    processor.setControl(drum::ratio, 2.0f);
    processor.setControl(drum::bypass, 1.0f);
    processor.setAutoStyle(drum::DrumStyle::natural, 0.3f);
    const auto before = processor.getSettings();
    check(processor.loadUserPreset("Crack").wasOk(), "A preset must load");
    const auto after = processor.getSettings();
    check(std::abs(after[drum::bodyGain] - 3.5f) < 0.01f && std::abs(after[drum::ratio] - 5.0f) < 0.05f,
        "Loading must apply the stored sound");
    check(processor.getAutoStyle() == drum::DrumStyle::natural && std::abs(processor.getAutoIntensity() - 0.3f) < 0.001f,
        "Loading must not apply the remembered Auto style");
    check(after[drum::kitMic] == 1 && after[drum::bypass] == before[drum::bypass]
        && after[drum::kitDelay] == before[drum::kitDelay] && after[drum::kitPolarity] == before[drum::kitPolarity],
        "Loading must restore Snare Bottom without importing bypass or phase corrections");
    const auto view = processor.getKitView();
    const auto member = std::find_if(view.members.begin(), view.members.end(), [&](const auto& entry) {
        return entry.id == processor.getKitMemberId();
    });
    check(member != view.members.end() && member->identity.role == kit::Role::snareBottom,
        "Loading must immediately publish the restored identity to Kit");
    check(processor.canRevertAutoPreset() && !processor.canApplyAutoPreset(), "A loaded preset can be reverted");
    check(processor.revertAutoPreset(), "Revert must be possible after a load");
    const auto reverted = processor.getSettings();
    for (size_t index = 0; index < before.size(); ++index)
        check(std::abs(reverted[index] - before[index]) < 0.01f, "Revert must restore every control: " + juce::String(drum::controlSpecs[index].id));

    check(processor.loadUserPreset("Nothing").failed() && processor.getAutoPresetText().contains("not found"),
        "A failed load must say why");
    check(processor.deleteUserPreset("Crack").wasOk() && processor.listUserPresets().empty(), "A preset can be deleted");
    processor.releaseResources();
}

void testPresetEditorIdentity()
{
    TemporaryFolder temporary;
    ZeroEffortDrumProcessor processor;
    processor.setUserPresetRoot(temporary.folder);
    check(processor.saveUserPreset("Kick In").wasOk(), "Kick In fixture must save");
    processor.setKitControl(ZeroEffortDrumProcessor::kitMic, 1);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto* position = dynamic_cast<juce::ComboBox*>(editor->findChildWithID("kitMic"));
    auto* instrument = dynamic_cast<juce::ComboBox*>(editor->findChildWithID("drumInstrument"));
    check(position != nullptr && instrument != nullptr, "Identity menus must exist");
    const auto waitFor = [](const auto& ready) {
        for (int attempt = 0; attempt < 20 && !ready(); ++attempt)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    };
    check(processor.loadUserPreset("Kick In").wasOk(), "Kick In must load from Kick Out");
    waitFor([&] { return position->getText() == "Kick In"; });
    check(position->isVisible() && position->getText() == "Kick In" && instrument->getSelectedId() == 1,
        "The editor header must display the loaded Kick In identity");
    check(processor.revertAutoPreset(), "Identity load must be undoable in the editor");
    waitFor([&] { return position->getText() == "Kick Out"; });
    check(position->getText() == "Kick Out", "Revert must refresh the previous microphone label");
    processor.setControl(drum::instrument, static_cast<float>(drum::DrumInstrument::tom2));
    check(processor.saveUserPreset("Tom 2").wasOk(), "Tom identity fixture must save");
    processor.setControl(drum::instrument, static_cast<float>(drum::DrumInstrument::floorTom));
    check(processor.loadUserPreset("Tom 2").wasOk(), "Shared tom preset must load in the editor");
    waitFor([&] { return instrument->getText() == "Tom 2"; });
    check(instrument->getText() == "Tom 2" && !position->isVisible(), "Loading must refresh the instrument menu and position visibility");
    check(processor.revertAutoPreset(), "Tom identity must be undoable");
    waitFor([&] { return instrument->getText() == "Floor Tom"; });
    check(instrument->getText() == "Floor Tom", "Revert must refresh the original instrument label");
}
}

void testPresets()
{
    testNames();
    testLibrary();
    testIdentityFormat();
    testProcessor();
    testPresetEditorIdentity();
    std::cout << "PASS: user presets (names, files, 52 identities, legacy format, invalid identity, Kit preservation, load / revert, editor labels)\n";
}
