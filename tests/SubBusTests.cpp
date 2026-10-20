#include "ARA/ZeroEffortDrumProcessor.h"
#include "Drum/DrumPresetLibrary.h"
#include "Drum/DrumRole.h"
#include "Kit/KitTypes.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void check(bool condition, const juce::String& message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

// Hits with a low body and a bright click over a quiet floor: a take on which the drum bus proposes its ambience.
std::vector<float> take(double rate, int seconds)
{
    juce::Random random(5);
    std::vector<float> audio(static_cast<size_t>(rate * seconds));
    for (size_t index = 0; index < audio.size(); ++index)
    {
        const auto age = std::fmod(static_cast<double>(index) / rate, 0.45);
        const auto noise = random.nextFloat() * 2.0f - 1.0f;
        audio[index] = static_cast<float>(0.55 * std::exp(-age * 22) * std::sin(juce::MathConstants<double>::twoPi * 70 * age)
            + 0.04 * std::exp(-age * 90) * noise + 0.0004 * noise);
    }
    return audio;
}

void testRoles()
{
    using drum::DrumInstrument;
    for (const auto* name : drum::instrumentNames) check(name != nullptr && *name != 0, "Every role must have a name");
    check(drum::instrumentCount == 13 && drum::instrumentNames.size() == 13 && drum::controlSpecs[drum::instrument].maximum == 12,
        "There are ten roles and three sub-buses");
    // Projects store the role by its number: 0 to 9 can never change.
    const char* stored[] = { "Kick", "Snare", "Tom 1", "Tom 2", "Tom 3", "Floor Tom", "Hi-hat", "Overheads", "Room", "Drum Master Bus" };
    for (int role = 0; role < 10; ++role)
        check(juce::String(drum::instrumentNames[static_cast<size_t>(role)]) == stored[role], "The stored role numbers must not move");
    check(drum::instrumentFromValue(9) == DrumInstrument::master && drum::instrumentFromValue(10) == DrumInstrument::kickBus
        && drum::instrumentFromValue(11) == DrumInstrument::snareBus && drum::instrumentFromValue(12) == DrumInstrument::tomsBus
        && drum::instrumentFromValue(40) == DrumInstrument::master && drum::instrumentFromValue(-3) == DrumInstrument::kick,
        "Role values are clamped to the known roles");
    check(drum::isSubBus(DrumInstrument::kickBus) && drum::isSubBus(DrumInstrument::tomsBus) && !drum::isSubBus(DrumInstrument::master)
        && !drum::isSubBus(DrumInstrument::kick), "Only the three group roles are sub-buses");
    for (size_t index = 0; index < drum::roleDescriptors.size(); ++index)
        check(static_cast<size_t>(drum::roleDescriptors[index].id) == index, "The role table must follow the role numbers");
    for (const auto role : { DrumInstrument::kickBus, DrumInstrument::snareBus, DrumInstrument::tomsBus })
        check(kit::roleFor(static_cast<int>(role), 0) == kit::Role::none && kit::positionNames(static_cast<int>(role)).isEmpty(),
            "A sub-bus is outside the kit alignment and has no microphone position");
}

void testProposals()
{
    constexpr double rate = 48000;
    const auto audio = take(rate, 20);
    drum::DrumAnalyser analyser, grouped;
    analyser.prepare(rate);
    grouped.prepare(rate);
    for (size_t offset = 0; offset < audio.size(); offset += 4096)
    {
        const auto count = static_cast<int>(juce::jmin<size_t>(4096, audio.size() - offset));
        analyser.push(audio.data() + offset, count);
        grouped.push(audio.data() + offset, count);
    }
    check(grouped.getPolicy().subBusReference && drum::ProposalDefaults::subBusReference, "The group EQ is on by default (profileVersion 4)");
    auto neutral = analyser.getPolicy();
    neutral.subBusReference = false;
    analyser.setPolicy(neutral);
    const auto proposeGrouped = [&](drum::DrumInstrument role, drum::DrumStyle style, float intensity) {
        auto original = drum::defaultSettings();
        original[drum::instrument] = static_cast<float>(role);
        return grouped.propose(original, style, intensity);
    };
    const auto propose = [&](drum::DrumInstrument role, drum::DrumStyle style) {
        auto original = drum::defaultSettings();
        original[drum::instrument] = static_cast<float>(role);
        return analyser.propose(original, style, 0.75f);
    };
    for (const auto style : { drum::DrumStyle::natural, drum::DrumStyle::rock, drum::DrumStyle::metal })
    {
        const auto master = propose(drum::DrumInstrument::master, style);
        check(master.valid, "The drum bus must propose on this take");
        for (const auto role : { drum::DrumInstrument::kickBus, drum::DrumInstrument::snareBus, drum::DrumInstrument::tomsBus })
        {
            const auto sub = propose(role, style);
            check(sub.valid && sub.summary.contains("SUB-BUS"), "A sub-bus must propose and say that it is a sub-bus");
            check(sub.settings[drum::ambienceOn] == 0 && sub.settings[drum::gateOn] == 0 && sub.settings[drum::cleanupOn] == 0
                && sub.settings[drum::focusOn] == 0 && sub.settings[drum::presenceOn] == 0,
                "A sub-bus carries no ambience, gate, cleanup or Kick modules");
            check(sub.settings[drum::instrument] == static_cast<float>(role), "The proposal keeps the chosen role");
            check(sub.settings[drum::glueOn] == master.settings[drum::glueOn] && sub.settings[drum::eqOn] == master.settings[drum::eqOn]
                && sub.settings[drum::glueThreshold] == master.settings[drum::glueThreshold]
                && sub.settings[drum::highPass] == master.settings[drum::highPass],
                "The first sub-bus profile is the drum bus tuning");
        }
    }
    using drum::DrumInstrument;
    using drum::DrumStyle;
    for (const auto role : { DrumInstrument::kickBus, DrumInstrument::snareBus })
    {
        const auto neutralRock = propose(role, DrumStyle::rock);
        const auto rock = proposeGrouped(role, DrumStyle::rock, 0.75f);
        check(neutralRock.settings[drum::eqOn] == 0 && neutralRock.summary.contains("Neutral group profile"), "Without the option the group EQ stays off");
        check(rock.valid && rock.settings[drum::eqOn] == 1 && rock.settings[drum::boxFrequency] == 280 && rock.settings[drum::boxGain] < -1.0f
            && rock.settings[drum::attackFrequency] == 3000 && rock.settings[drum::attackGain] > 1.0f && rock.summary.contains("Group EQ"),
            "The Kick Bus and Snare Bus get a dip around 280 Hz and a presence lift");
        check(proposeGrouped(role, DrumStyle::metal, 1.0f).settings[drum::attackGain] > rock.settings[drum::attackGain], "The lift grows with the intensity");
        check(proposeGrouped(role, DrumStyle::natural, 0.75f).settings[drum::eqOn] == 0, "Natural keeps its careful correction");
        check(proposeGrouped(role, DrumStyle::rock, 0.0f).settings[drum::eqOn] == 0, "Zero intensity changes nothing");
    }
    check(proposeGrouped(DrumInstrument::tomsBus, DrumStyle::rock, 0.75f).settings[drum::eqOn] == 0, "The Toms Bus has no data yet and stays neutral");
    check(propose(drum::DrumInstrument::master, drum::DrumStyle::rock).summary.contains("BUS: analysis of the sum only")
        && !propose(drum::DrumInstrument::master, drum::DrumStyle::rock).summary.contains("SUB-BUS"),
        "The drum master bus keeps its own text");
}

void testProcessor()
{
    ZeroEffortDrumProcessor processor;
    processor.setRateAndBufferSizeDetails(48000, 256);
    processor.prepareToPlay(48000, 256);
    juce::MemoryBlock saved;
    for (const auto role : { drum::DrumInstrument::kickBus, drum::DrumInstrument::snareBus, drum::DrumInstrument::tomsBus })
    {
        processor.setControl(drum::instrument, static_cast<float>(role));
        check(processor.currentInstrument() == static_cast<int>(role), "The processor must accept a sub-bus role");
        check(processor.getKitIdentity().role == kit::Role::none && processor.getKitIdentity().name.contains(drum::instrumentName(role)),
            "A sub-bus is named in the kit view and takes no part in the alignment");
        processor.getStateInformation(saved);
        ZeroEffortDrumProcessor restored;
        restored.setRateAndBufferSizeDetails(48000, 256);
        restored.prepareToPlay(48000, 256);
        restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        check(restored.currentInstrument() == static_cast<int>(role), "A sub-bus role must survive a project reload");
        restored.releaseResources();
    }

    // Presets of a sub-bus are kept apart from those of the microphones and of the drum bus.
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("zed_subbus_" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()));
    struct Cleanup { juce::File folder; ~Cleanup() { folder.deleteRecursively(); } } cleanup { folder };
    processor.setUserPresetRoot(folder);
    processor.setControl(drum::instrument, static_cast<float>(drum::DrumInstrument::kickBus));
    check(processor.saveUserPreset("Tight sum").wasOk() && processor.listUserPresets().size() == 1, "A sub-bus preset must be saved");
    for (const auto other : { drum::DrumInstrument::kick, drum::DrumInstrument::snareBus, drum::DrumInstrument::master })
    {
        processor.setControl(drum::instrument, static_cast<float>(other));
        check(processor.listUserPresets().empty(), "A sub-bus preset must not be offered to another role");
    }
    processor.releaseResources();
}
}

void testSubBuses()
{
    testRoles();
    testProposals();
    testProcessor();
    std::cout << "PASS: sub-buses (role numbers kept, kit-free, no ambience or gate, reload, presets kept apart)" << std::endl;
}
