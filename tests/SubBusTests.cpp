#include "ARA/ZeroEffortDrumProcessor.h"
#include "Drum/DrumPresetLibrary.h"
#include "Drum/DrumRole.h"
#include "Kit/KitTypes.h"
#include "Kit/KitRegistry.h"
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

// Low kick-like hits with a click, over a faint floor.
std::vector<float> lowHits(double rate, int seconds, float gain = 1.0f)
{
    juce::Random random(21);
    std::vector<float> audio(static_cast<size_t>(rate * seconds));
    for (size_t index = 0; index < audio.size(); ++index)
    {
        const auto age = std::fmod(static_cast<double>(index) / rate, 0.4);
        const auto noise = random.nextFloat() * 2.0f - 1.0f;
        audio[index] = gain * static_cast<float>(0.5 * std::exp(-age * 20) * std::sin(juce::MathConstants<double>::twoPi * 60 * age)
            + 0.05 * std::exp(-age * 80) * noise + 0.0002 * noise);
    }
    return audio;
}

std::vector<float> shifted(const std::vector<float>& audio, size_t samples, float gain)
{
    std::vector<float> result(audio.size(), 0.0f);
    for (size_t index = samples; index < audio.size(); ++index) result[index] = gain * audio[index - samples];
    return result;
}

void testGroupMeasures()
{
    constexpr double rate = 48000;
    const auto a = lowHits(rate, 6);
    const auto louder = [](const kit::GroupReport& report, size_t mic) { return report.mics[mic].levelDb; };

    // Two in-phase microphones, the second 6 dB lower: the sum adds, no alert.
    kit::GroupMic first { "Kick In", a, 0, false }, second { "Kick Out", shifted(a, 0, 0.5f), 0, false };
    const auto plain = kit::GroupMeasures::analyse({ first, second }, rate);
    check(plain.valid && plain.mics.size() == 2, "Two microphones give a group report");
    check(std::abs((louder(plain, 0) - louder(plain, 1)) - 6.02f) < 0.3f, "The active level of each mic is read against the sum (6 dB apart)");
    check(plain.sumGainDb > 0.5f && plain.sumGainLowDb > 0.5f && plain.coherence > 0.95f && plain.mics[1].lowCorrelation > 0.9f,
        "In-phase microphones add");
    check(std::abs(plain.mics[1].residualDelayMs) < 0.4f && plain.clipMarginDb > 0, "No residual delay, and some headroom");
    check(!kit::GroupMeasures::toText(plain, "Kick Bus").contains("<-"), "No alert on a healthy group");

    // The same microphone with inverted polarity: the low end cancels and the report says so.
    kit::GroupMic flipped { "Kick Out", shifted(a, 0, 0.8f), 0, true };
    const auto cancelled = kit::GroupMeasures::analyse({ first, flipped }, rate);
    check(cancelled.valid && cancelled.sumGainLowDb < -6.0f && cancelled.mics[1].lowCorrelation < -0.9f && cancelled.coherence < -0.9f,
        "An inverted microphone cancels the low end");
    check(kit::GroupMeasures::toText(cancelled, "Kick Bus").contains("polarity suspect"), "The report flags the polarity");
    // ...but a microphone whose instance already inverts it is correct: its raw audio is flipped by the correction.
    kit::GroupMic opposed { "Kick Out", shifted(a, 0, -0.8f), 0, true };
    const auto repaired = kit::GroupMeasures::analyse({ first, opposed }, rate);
    check(repaired.valid && repaired.sumGainLowDb > 0 && repaired.mics[1].lowCorrelation > 0.9f, "The polarity correction of the instance is taken into account");

    // A microphone 3 ms late: its residual delay is seen, and gone once the other one is delayed by the same amount.
    const auto lateAudio = shifted(a, 144, 0.7f);
    const auto late = kit::GroupMeasures::analyse({ first, { "Kick Out", lateAudio, 0, false } }, rate);
    check(late.valid && std::abs(late.mics[1].residualDelayMs - 3.0f) < 0.5f, "A 3 ms residual delay is measured");
    check(kit::GroupMeasures::toText(late, "Kick Bus").contains("alignment insufficient"), "The report flags the alignment");
    const auto fixedByDelay = kit::GroupMeasures::analyse({ { "Kick In", a, 3.0f, false }, { "Kick Out", lateAudio, 0, false } }, rate);
    check(fixedByDelay.valid && std::abs(fixedByDelay.mics[1].residualDelayMs) < 0.4f, "The delay applied by an instance removes the residual delay");

    // Limits: one microphone, silence, too short, bad rate, determinism.
    check(!kit::GroupMeasures::analyse({ first }, rate).valid && kit::GroupMeasures::toText(kit::GroupMeasures::analyse({ first }, rate), "x").contains("at least two"),
        "A group of one microphone gives no invented numbers");
    check(std::isnan(kit::GroupMeasures::analyse({ first }, rate).sumGainDb), "...they stay unavailable");
    kit::GroupMic silent { "Silent", std::vector<float>(a.size(), 0.0f), 0, false };
    check(!kit::GroupMeasures::analyse({ silent, silent }, rate).valid, "Silence is refused");
    kit::GroupMic brief { "Brief", std::vector<float>(1000, 0.1f), 0, false };
    check(!kit::GroupMeasures::analyse({ brief, brief }, rate).valid, "Less than a second is refused");
    check(!kit::GroupMeasures::analyse({ first, second }, 100.0).valid, "A bad sample rate is refused");
    const auto again = kit::GroupMeasures::analyse({ first, second }, rate);
    check(again.sumGainDb == plain.sumGainDb && again.mics[1].lowCorrelation == plain.mics[1].lowCorrelation, "The measures are deterministic");
}

void testGroupAudio()
{
    constexpr double rate = 48000;
    juce::SharedResourcePointer<kit::Registry> registry;
    const auto audio = lowHits(rate, 4);
    const auto join = [&](kit::Role role, int group, const juce::String& name, float delay, bool inverted) {
        auto* member = registry->join();
        check(member != nullptr, "A member must join");
        kit::Identity identity;
        identity.group = group;
        identity.role = role;
        identity.name = name;
        identity.delayMs = delay;
        identity.inverted = inverted;
        registry->setIdentity(*member, identity);
        return member;
    };
    auto* kickIn = join(kit::Role::kickIn, 0, "Kick In", 2.0f, false);
    auto* kickOut = join(kit::Role::kickOut, 0, "Kick Out", 0.0f, true);
    auto* snare = join(kit::Role::snareTop, 0, "Snare Top", 0.0f, false);
    auto* otherKit = join(kit::Role::kickIn, 1, "Other kick", 0.0f, false);
    for (auto* member : { kickIn, kickOut, snare, otherKit })
        for (size_t offset = 0; offset + 4096 <= audio.size(); offset += 4096)
        {
            const float* channel = audio.data() + offset;
            member->write(&channel, 1, 4096, static_cast<juce::int64>(offset), rate);
        }
    double readRate = 0;
    const auto mics = registry->recentAudio(0, { kit::Role::kickIn, kit::Role::kickOut }, 131072, readRate);
    check(mics.size() == 2 && readRate == rate, "Only the live microphones of the requested roles and group are read");
    check(mics[0].samples.size() > static_cast<size_t>(rate) && mics[0].samples.size() == mics[1].samples.size(), "Both read the same span");
    bool inCorrect = false, outCorrect = false;
    for (const auto& mic : mics)
    {
        if (mic.name == "Kick In") inCorrect = mic.delayMs == 2.0f && !mic.inverted;
        if (mic.name == "Kick Out") outCorrect = mic.delayMs == 0.0f && mic.inverted;
    }
    check(inCorrect && outCorrect, "The correction of each instance travels with its audio");
    check(registry->recentAudio(0, { kit::Role::snareTop }, 131072, readRate).empty(), "One microphone is not a group");
    check(registry->recentAudio(5, { kit::Role::kickIn }, 131072, readRate).empty(), "An empty Kit group gives nothing");

    ZeroEffortDrumProcessor bus;
    bus.setRateAndBufferSizeDetails(48000, 256);
    bus.prepareToPlay(48000, 256);
    check(bus.getGroupReportText().isEmpty(), "Only a sub-bus builds a group report");
    bus.setControl(drum::instrument, static_cast<float>(drum::DrumInstrument::kickBus));
    const auto report = bus.getGroupReportText();
    check(report.contains("GROUP REPORT / Kick Bus / Kit 1") && report.contains("Kick In") && report.contains("Kick Out") && !report.contains("Snare Top"),
        "The Kick Bus reports its own microphones");
    bus.setControl(drum::instrument, static_cast<float>(drum::DrumInstrument::tomsBus));
    check(bus.getGroupReportText().contains("No group report yet"), "A group without microphones says what is missing");
    bus.releaseResources();
    for (auto* member : { kickIn, kickOut, snare, otherKit }) registry->leave(member);
}
}
void testSubBuses()
{
    testRoles();
    testProposals();
    testProcessor();
    testGroupMeasures();
    testGroupAudio();
    std::cout << "PASS: sub-buses (role numbers kept, kit-free, group EQ, reload, presets apart, group report: level, polarity, delay, registry read)" << std::endl;
}
