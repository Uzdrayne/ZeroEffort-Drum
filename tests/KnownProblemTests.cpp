#include "Drum/DrumReplay.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
constexpr double twoPi = juce::MathConstants<double>::twoPi;

void check(bool condition, const juce::String& message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

std::unique_ptr<drum::DrumAnalyser> analyse(const std::vector<float>& audio, double rate)
{
    auto analyser = std::make_unique<drum::DrumAnalyser>();
    analyser->prepare(rate);
    for (size_t offset = 0; offset < audio.size(); offset += 4096)
        analyser->push(audio.data() + offset, static_cast<int>(juce::jmin(size_t { 4096 }, audio.size() - offset)));
    return analyser;
}

// 60 Hz kicks with a 3 ms broadband click on the attack, over a very quiet noise floor.
std::vector<float> clickedKicks(double rate, double seconds, double period, double floorAmplitude)
{
    juce::Random random(21);
    std::vector<float> audio(static_cast<size_t>(rate * seconds));
    for (size_t index = 0; index < audio.size(); ++index)
    {
        const auto time = static_cast<double>(index) / rate;
        const auto age = std::fmod(time, period);
        const auto click = age < 0.003 ? 0.5 * (random.nextDouble() * 2 - 1) * (1 - age / 0.003) : 0.0;
        audio[index] = static_cast<float>(0.6 * std::exp(-age * 28) * std::sin(twoPi * 60 * time) + click
            + floorAmplitude * (random.nextDouble() * 2 - 1));
    }
    return audio;
}

// Problem 1 of document 01, section 8: the ~4 frames per hit whose centre precedes the hit but whose window already
// holds the attack must not land in the bleed spectrum. The band is 6-12 kHz, where only the click has energy.
void testLeadFramesAreNotBleed(bool excludeLeadFrames)
{
    constexpr double rate = 48000;
    const auto audio = clickedKicks(rate, 6, 0.5, 0.0001);
    auto analyser = std::make_unique<drum::DrumAnalyser>();
    analyser->setExcludeLeadFrames(excludeLeadFrames);
    analyser->prepare(rate);
    analyser->push(audio.data(), static_cast<int>(audio.size()));
    const auto bleed = analyser->analysis().phasePower(drum::DrumAnalysis::Phase::bleed, 6000, 12000);
    const auto onset = analyser->analysis().phasePower(drum::DrumAnalysis::Phase::onset, 6000, 12000);
    // The true bleed is the noise floor: the click is thousands of times stronger than it.
    check(std::isfinite(bleed) && std::isfinite(onset), "Problem 1: the bleed and onset phases need frames");
    const auto contamination = bleed / onset;
    std::cout << "  lead frames " << (excludeLeadFrames ? "excluded" : "kept    ") << ": bleed / onset power at 6-12 kHz = "
              << contamination << std::endl;
    if (excludeLeadFrames)
    {
        check(contamination < 1.0e-3, "Problem 1: attack frames contaminate the bleed spectrum, bleed / onset = " + juce::String(contamination));
        check(analyser->analysis().leadFrameCount() >= 3 * 11, "About 4 lead frames per hit must be set aside, got " + juce::String(analyser->analysis().leadFrameCount()));
    }
    else
    {
        // Policy off: the former behaviour, still reachable.
        check(contamination > 0.1 && analyser->analysis().leadFrameCount() == 0, "With the policy off the lead frames stay in the bleed spectrum");
    }
    // The overall spectrum and its frame count are not touched by the policy.
    check(analyser->analysis().spectrumFrames() == static_cast<int>((audio.size() - 4096) / 480 + 1),
        "Every analysed frame must be counted once, lead frames included");
}

// A08: double kicks at 180 bpm (83 ms) are under-counted because of the fixed 120 ms refractory period (problem 3).
// Default (J4 correction off): the test passes while the count is wrong and fails when somebody fixes the detector by
// default, as a reminder to turn it into a plain test. With the correction on, the count must be right.
std::vector<float> doubleKickTake(double rate, double seconds, double period)
{
    std::vector<float> audio(static_cast<size_t>(rate * seconds));
    juce::Random random(3);
    for (size_t index = 0; index < audio.size(); ++index)
    {
        const auto time = static_cast<double>(index) / rate;
        const auto age = std::fmod(time, period);
        audio[index] = static_cast<float>(0.6 * std::exp(-age * 45) * std::sin(twoPi * 60 * time) + 0.0001 * (random.nextDouble() * 2 - 1));
    }
    return audio;
}

void testDoubleKicksExpectedFailure()
{
    constexpr double rate = 48000;
    const auto audio = doubleKickTake(rate, 4, 60.0 / 180.0 / 4.0);    // sixteenth notes at 180 bpm: 83.3 ms
    const auto realHits = static_cast<int>(4.0 * 12.0);   // 12 hits per second for 4 s
    check(drum::MeasureConstants::doubleHitsByDefault, "J4a double hit detection is active by default");
    const auto counted = analyse(audio, rate)->analysis().hitCount();
    std::cout << "  A08 double kicks at 180 bpm: " << counted << " hits counted for " << realHits << " played" << std::endl;
    check(std::abs(counted - realHits) <= realHits / 10, "A08 the double kicks must be counted, got " + juce::String(counted) + " of " + juce::String(realHits));

    // The former detector stays reachable and still under-counts
    auto legacy = std::make_unique<drum::DrumAnalyser>();
    legacy->setDoubleHitDetection(false);
    legacy->prepare(rate);
    for (size_t offset = 0; offset < audio.size(); offset += 4096)
        legacy->push(audio.data() + offset, static_cast<int>(juce::jmin(size_t { 4096 }, audio.size() - offset)));
    const auto old = legacy->analysis().hitCount();
    std::cout << "  A08 without the J4a correction: " << old << " hits counted for " << realHits << " played" << std::endl;
    check(std::abs(old - realHits) > realHits / 10, "A08 the former detector under-counts double kicks, got " + juce::String(old));

    // Ordinary playing is not disturbed: the same count with and without the correction
    for (const auto period : { 0.5, 0.35, 0.25 })
    {
        const auto take = doubleKickTake(rate, 6, period);
        auto changed = analyse(take, rate);
        auto plain = std::make_unique<drum::DrumAnalyser>();
        plain->setDoubleHitDetection(false);
        plain->prepare(rate);
        for (size_t offset = 0; offset < take.size(); offset += 4096)
            plain->push(take.data() + offset, static_cast<int>(juce::jmin(size_t { 4096 }, take.size() - offset)));
        check(plain->analysis().hitCount() == changed->analysis().hitCount(), "The correction must not change the count at a period of "
            + juce::String(period) + " s: " + juce::String(plain->analysis().hitCount()) + " against " + juce::String(changed->analysis().hitCount()));
    }
}

// 60 Hz bursts of constant level: 0.15 s of sound every second. The global crest is high only because of the silence.
std::vector<float> burstKicks(double rate, double seconds)
{
    juce::Random random(8);
    std::vector<float> audio(static_cast<size_t>(rate * seconds));
    for (size_t index = 0; index < audio.size(); ++index)
    {
        const auto time = static_cast<double>(index) / rate;
        const auto age = std::fmod(time, 1.0);
        const auto fade = juce::jmin(1.0, age / 0.002, (0.15 - age) / 0.002);
        audio[index] = static_cast<float>((age < 0.15 ? 0.6 * std::sin(twoPi * 60 * time) * juce::jmax(0.0, fade) : 0.0)
            + 0.0001 * (random.nextDouble() * 2 - 1));
    }
    return audio;
}

// Problem 2: the Kick compressor switches on from the global crest, which grows with the silence between hits.
void testCompressorUsesActiveCrest()
{
    constexpr double rate = 48000;
    const auto audio = burstKicks(rate, 8);
    const auto analyser = analyse(audio, rate);
    const auto& analysis = analyser->analysis();
    std::cout << "  burst kick: global crest " << analysis.crestDb() << " dB, active crest " << analysis.activeCrestDb() << " dB" << std::endl;
    check(analysis.crestDb() > 10.0f && analysis.activeCrestDb() < 10.0f, "The burst signal must separate the two crests");

    // J3 is active by default (validated by the user on 17 October 2026); the legacy behaviour stays reachable through the policy.
    check(drum::ProposalDefaults::compressorUsesActiveCrest && drum::MeasureConstants::excludeLeadFramesByDefault,
        "Both J3 choices must be active by default");

    const auto original = drum::defaultSettings();
    auto legacyPolicy = analyser->getPolicy();
    legacyPolicy.compressorUsesActiveCrest = false;
    auto legacy = analyse(audio, rate);
    legacy->setPolicy(legacyPolicy);
    const auto current = legacy->propose(original, drum::DrumStyle::rock, 1.0f);
    check(current.valid && current.settings[drum::compressorOn] == 1.0f, "With the policy off the compressor follows the global crest");

    const auto active = analyser->propose(original, drum::DrumStyle::rock, 1.0f);
    check(active.valid && active.settings[drum::compressorOn] == 0.0f && active.settings[drum::multibandOn] == 0.0f,
        "By default a sparse but steady kick does not get a compressor");
    for (size_t control = 0; control < current.settings.size(); ++control)
        if (control != static_cast<size_t>(drum::compressorOn) && control != static_cast<size_t>(drum::multibandOn))
            check(current.settings[control] == active.settings[control], juce::String("Only the compressor decision may change, not ") + drum::controlSpecs[control].id);

    // Other roles keep reading the global crest
    auto snare = original;
    snare[drum::instrument] = 1.0f;
    check(analyser->propose(snare, drum::DrumStyle::rock, 1.0f).settings == legacy->propose(snare, drum::DrumStyle::rock, 1.0f).settings,
        "The policy only concerns the Kick profile");
}

// Both J3 choices must survive a snapshot, and a replay must be able to override the recorded policy.
void testPolicyInSnapshots()
{
    constexpr double rate = 48000;
    const auto audio = burstKicks(rate, 8);
    auto analyser = std::make_unique<drum::DrumAnalyser>();
    analyser->setExcludeLeadFrames(true);
    analyser->prepare(rate);
    analyser->push(audio.data(), static_cast<int>(audio.size()));
    drum::SnapshotContext context;
    context.policy.compressorUsesActiveCrest = true;
    analyser->setPolicy(context.policy);
    const auto proposal = analyser->propose(context.original, context.style, context.intensity);
    const auto snapshot = drum::DrumAnalysisSnapshot::toSnapshot(analyser->analysis(), context, proposal);

    juce::var parsed;
    check(juce::JSON::parse(juce::JSON::toString(snapshot, false), parsed).wasOk(), "Snapshot parses");
    auto restoredAnalyser = std::make_unique<drum::DrumAnalyser>();
    drum::RestoredSnapshot restored;
    check(drum::DrumAnalysisSnapshot::restoreSnapshot(restoredAnalyser->editableAnalysis(), parsed, restored).wasOk(), "Snapshot restores");
    check(restored.context.policy.compressorUsesActiveCrest && restoredAnalyser->analysis().excludesLeadFrames()
        && restoredAnalyser->analysis().leadFrameCount() == analyser->analysis().leadFrameCount()
        && restoredAnalyser->analysis().leadFrameCount() > 0, "The policies and the lead frame count must be recorded");
    restoredAnalyser->setPolicy(restored.context.policy);
    check(restoredAnalyser->propose(context.original, context.style, context.intensity).settings == proposal.settings, "Replay with the recorded policy");

    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("zed_policy", "");
    folder.createDirectory();
    check(drum::DrumAnalysisSnapshot::writeFile(folder.getChildFile("kick.json"), snapshot).wasOk(), "Snapshot written");
    const auto recorded = drum::DrumReplay::replayFolder(folder);
    const auto same = drum::DrumReplay::compare(recorded.recorded, recorded.current);
    check(same.modified.empty() && same.flips.empty(), "Replay with the recorded policy changes nothing");
    const auto overridden = drum::DrumReplay::replayFolder(folder, false);
    const auto diff = drum::DrumReplay::compare(overridden.recorded, overridden.current);
    check(overridden.current.entries.size() == 1 && std::isfinite(overridden.current.entries[0].activeCrest), "Replay reports the active crest");
    bool compressorChanged = false;
    for (const auto& item : diff.modified) compressorChanged = compressorChanged || item.control == "compressorOn";
    check(proposal.settings[drum::compressorOn] == 0.0f && compressorChanged && diff.listen.size() == 1,
        "Turning the recorded policy off in the replay must switch the compressor on and ask for a listen");
    std::cout << "  replay override: " << diff.modified.size() << " setting(s) change when the recorded policy is turned off" << std::endl;
    folder.deleteRecursively();
}
}
void testKnownProblems()
{
    testLeadFramesAreNotBleed(false);
    testLeadFramesAreNotBleed(true);
    testDoubleKicksExpectedFailure();
    testCompressorUsesActiveCrest();
    testPolicyInSnapshots();
    std::cout << "PASS: known problems" << std::endl;
}
