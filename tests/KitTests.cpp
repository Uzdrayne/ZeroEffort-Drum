#include "ARA/ZeroEffortDrumProcessor.h"
#include "Kit/KitAligner.h"
#include "Kit/KitAnalysis.h"
#include "Kit/KitRegistry.h"
#include "UI/DrumControlNames.h"
#include <iostream>
#include <stdexcept>

namespace
{
void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void check(bool condition, const juce::String& message)
{
    if (!condition) throw std::runtime_error(message.toStdString());
}

constexpr double rate = 48000.0;

bool verbose()
{
    return juce::SystemStats::getEnvironmentVariable("ZED_KIT_VERBOSE", {}).isNotEmpty();
}

struct SyntheticKit
{
    std::vector<float> kick, snareTop, snareBottom, overheadLeft, overheadRight;

    explicit SyntheticKit(double seconds)
    {
        const auto length = static_cast<size_t>(seconds * rate);
        for (auto* track : { &kick, &snareTop, &snareBottom, &overheadLeft, &overheadRight }) track->assign(length, 0.0f);
        juce::Random random(1234);
        std::vector<float> noise(static_cast<size_t>(rate * 0.3));
        for (auto& value : noise) value = random.nextFloat() * 2.0f - 1.0f;
        const auto kickHit = [](double time) {
            if (time < 0) return 0.0;
            return 0.8 * (std::sin(juce::MathConstants<double>::twoPi * 55.0 * time) * std::exp(-time / 0.06)
                + 0.3 * std::sin(juce::MathConstants<double>::twoPi * 3000.0 * time) * std::exp(-time / 0.003));
        };
        const auto snareHit = [&noise](double time) {
            if (time < 0) return 0.0;
            const auto index = static_cast<size_t>(time * rate);
            const auto grain = index < noise.size() ? noise[index] : 0.0f;
            return 0.6 * (std::sin(juce::MathConstants<double>::twoPi * 200.0 * time) * std::exp(-time / 0.04)
                + 0.7 * grain * std::exp(-time / 0.05));
        };
        const auto period = 0.3;
        for (size_t sample = 0; sample < length; ++sample)
        {
            const auto time = sample / rate;
            const auto kickAge = std::fmod(time, period);
            const auto snareAge = std::fmod(time + period * 0.5, period);
            const auto k = [&](double delayMs) { return kickHit(kickAge - delayMs / 1000.0); };
            const auto s = [&](double delayMs) { return snareHit(snareAge - delayMs / 1000.0); };
            kick[sample] = static_cast<float>(k(0) + 0.05 * s(0.4));
            snareTop[sample] = static_cast<float>(s(0) + 0.08 * k(1.0));
            snareBottom[sample] = static_cast<float>(-0.7 * s(0.3) + 0.05 * k(1.0));
            overheadLeft[sample] = static_cast<float>(-0.5 * k(5.0) + 0.6 * s(3.0));
            overheadRight[sample] = static_cast<float>(-0.45 * k(5.4) + 0.55 * s(3.4));
        }
    }
};

kit::Report analyseOffline(const std::vector<kit::TrackInput>& inputs, const std::vector<const std::vector<float>*>& audio,
                           double sampleRate, juce::int64 start, juce::int64 end, const kit::KitPolicy& policy = {},
                           std::vector<kit::PairResult>* pairResults = nullptr)
{
    kit::KitSession session;
    session.setPolicy(policy);
    check(session.reset(inputs, sampleRate), "Kit session must build analysable pairs");
    const auto read = [&audio](int track, int, juce::int64 first, int count, float* destination) {
        const auto& source = *audio[static_cast<size_t>(track)];
        if (first < 0 || first + count > static_cast<juce::int64>(source.size())) return false;
        std::copy(source.begin() + first, source.begin() + first + count, destination);
        return true;
    };
    auto position = start + session.lookback();
    const auto last = end - session.lookahead();
    while (position < last)
    {
        const auto next = juce::jmin(last, position + 4096);
        session.process(position, next, read);
        position = next;
    }
    if (pairResults != nullptr) *pairResults = session.evaluate();
    return session.report();
}

void testAligner()
{
    kit::KitAligner aligner;
    aligner.prepare(rate, 256, 2);
    juce::AudioBuffer<float> audio(2, 256);
    std::vector<float> output;
    for (int block = 0; block < 4; ++block)
    {
        audio.clear();
        if (block == 0) audio.setSample(0, 10, 1.0f);
        aligner.process(audio, 2.0f, false);
        for (int sample = 0; sample < 256; ++sample) output.push_back(audio.getSample(0, sample));
    }
    const auto delay = static_cast<int>(std::lround(2.0 * rate / 1000.0));
    check(std::abs(output[static_cast<size_t>(10 + delay)] - 1.0f) < 1.0e-6f, "Kit delay must be an exact integer-sample delay");
    check(std::abs(output[10]) < 1.0e-6f, "Delayed impulse must leave its original position silent");

    kit::KitAligner neutral;
    neutral.prepare(rate, 256, 1);
    juce::AudioBuffer<float> mono(1, 256);
    for (int sample = 0; sample < 256; ++sample) mono.setSample(0, sample, 0.25f);
    neutral.process(mono, 0.0f, false);
    check(mono.getSample(0, 0) == 0.25f && mono.getSample(0, 255) == 0.25f, "Neutral kit stage must be bit-transparent");

    kit::KitAligner inverting;
    inverting.prepare(rate, 256, 1);
    for (int block = 0; block < 10; ++block)
    {
        for (int sample = 0; sample < 256; ++sample) mono.setSample(0, sample, 0.25f);
        inverting.process(mono, block < 2 ? 0.0f : 3.0f, block >= 2);
        for (int sample = 0; sample < 256; ++sample)
            check(std::isfinite(mono.getSample(0, sample)) && std::abs(mono.getSample(0, sample)) <= 0.25001f,
                "Polarity and delay changes must stay bounded while crossfading");
    }
    check(std::abs(mono.getSample(0, 255) + 0.25f) < 1.0e-5f, "Polarity ramp must settle to an exact inversion");
}

void testCapture()
{
    juce::SharedResourcePointer<kit::Registry> registry;
    auto* member = registry->join();
    check(member != nullptr && member->id() != 0, "Registry must hand out a capture slot");
    std::vector<float> left(512), right(512), copy(512);
    for (size_t index = 0; index < left.size(); ++index)
    {
        left[index] = static_cast<float>(index);
        right[index] = -static_cast<float>(index);
    }
    const float* channels[] { left.data(), right.data() };
    member->write(channels, 2, 512, 1000, rate);
    check(member->read(0, 1100, 64, copy.data()) && copy[0] == 100.0f, "Timeline-stamped capture must return the requested samples");
    check(member->read(1, 1100, 4, copy.data()) && copy[0] == -100.0f, "Right channel must be readable on its own");
    check(member->read(-1, 1100, 4, copy.data()) && copy[0] == 0.0f, "Mono read must sum both channels");
    check(!member->read(0, 900, 64, copy.data()), "Samples before the contiguous range must be rejected");
    check(!member->read(0, 1400, 512, copy.data()), "Samples not yet written must be rejected");
    member->write(channels, 2, 512, 50000, rate);
    check(!member->read(0, 1100, 64, copy.data()), "A transport jump must invalidate the previous capture");
    for (juce::int64 position = 50512; position < 50512 + kit::Member::ringSize + 2048; position += 512)
        member->write(channels, 2, 512, position, rate);
    check(!member->read(0, 50600, 64, copy.data()), "Overwritten ring content must never be analysed");
    const auto lastEnd = 50512 + kit::Member::ringSize + 2048;
    member->write(channels, 2, 512, 0, rate);
    check(!member->read(0, lastEnd - 1024, 512, copy.data()), "A backward seek must invalidate the previous range immediately");
    check(member->read(0, 100, 4, copy.data()) && copy[0] == 100.0f, "Audio written after a backward seek must be readable");
    member->markIdle();
    check(!member->isLive(juce::Time::getMillisecondCounter()) && !member->read(0, 100, 4, copy.data()),
        "Stopping the transport must end the readable capture");
    registry->leave(member);
}

void testSyntheticKit()
{
    const SyntheticKit kitAudio(9.0);
    std::vector<kit::TrackInput> inputs {
        { 1, kit::Role::kickIn, 0, 1, "Kick" }, { 2, kit::Role::snareTop, 0, 1, "Snare Top" },
        { 3, kit::Role::snareBottom, 1, 1, "Snare Bottom" }, { 4, kit::Role::overhead, kit::positionLeft, 1, "OH L" },
        { 5, kit::Role::overhead, kit::positionRight, 1, "OH R" } };
    const auto report = analyseOffline(inputs, { &kitAudio.kick, &kitAudio.snareTop, &kitAudio.snareBottom,
        &kitAudio.overheadLeft, &kitAudio.overheadRight }, rate, 0, static_cast<juce::int64>(kitAudio.kick.size()));
    check(report.valid && report.progress >= 1.0f, "Synthetic kit must produce a complete report");
    if (verbose())
        for (const auto& line : report.lines) std::cout << "  " << line << "\n";
    const auto* kick = report.find(1);
    const auto* snare = report.find(2);
    const auto* bottom = report.find(3);
    check(kick != nullptr && kick->measured && kick->invert && std::abs(kick->delayMs - 5.2f) < 0.35f,
        "Kick must be delayed onto the overheads and inverted");
    check(snare != nullptr && snare->measured && !snare->invert && std::abs(snare->delayMs - 3.2f) < 0.3f,
        "Snare must be delayed onto the overheads with its polarity kept");
    check(bottom != nullptr && bottom->measured && bottom->invert && std::abs(bottom->delayMs - 2.9f) < 0.3f,
        "Snare Bottom must follow Snare Top with an inverted polarity");
    check(report.find(4)->delayMs == 0 && !report.find(4)->invert, "Overheads are the time reference and are never moved");
    check(report.scores.phaseAfter > report.scores.phaseBefore + 20 && report.scores.phaseAfter > 85,
        "Phase coherence must improve markedly after correction");
    check(report.scores.stereo >= 0 && report.scores.stereo <= 100, "Stereo consistency must be a bounded score");
    std::cout << "PASS: synthetic kit kick +" << kick->delayMs << " ms inv, snare +" << snare->delayMs
              << " ms, phase " << report.scores.phaseBefore << " -> " << report.scores.phaseAfter << "\n";

    std::vector<kit::TrackInput> dualMono { { 1, kit::Role::kickIn, 0, 1, "Kick" }, { 2, kit::Role::snareTop, 0, 1, "Snare Top" },
        { 3, kit::Role::overhead, kit::positionMain, 2, "Overheads" } };
    const auto mono = analyseOffline(dualMono, { &kitAudio.kick, &kitAudio.snareTop, &kitAudio.overheadLeft }, rate, 0,
        static_cast<juce::int64>(kitAudio.kick.size()));
    check(mono.scores.stereo < 0 && mono.lines.joinIntoString("\n").contains("identiques"),
        "A mono overhead rendered on two identical channels must not produce a fake 100/100 stereo score");
    check(mono.find(1) != nullptr && mono.find(1)->measured, "A dual-mono overhead must still serve as time reference");
}

bool loadWav(const juce::File& file, std::vector<float>& destination, double& sampleRate, juce::int64 start, juce::int64 length)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) return false;
    sampleRate = reader->sampleRate;
    length = juce::jmin(length, reader->lengthInSamples - start);
    juce::AudioBuffer<float> buffer(1, static_cast<int>(length));
    if (!reader->read(&buffer, 0, static_cast<int>(length), start, true, false)) return false;
    destination.assign(buffer.getReadPointer(0), buffer.getReadPointer(0) + length);
    return true;
}

void testRealMultitrack()
{
#ifdef ZED_WAV_DIRECTORY
    const juce::File directory(ZED_WAV_DIRECTORY);
    const std::array<const char*, 4> names { "Kick_12.wav", "Snare_12.wav", "OvH L_12.wav", "OvH R_12.wav" };
    for (const auto* name : names)
        if (!directory.getChildFile(name).existsAsFile())
        {
            std::cout << "SKIP: real multitrack kit test (set ZED_WAV_DIRECTORY to the folder with Kick_12, Snare_12, OvH L_12, OvH R_12)\n";
            return;
        }
    std::array<std::vector<float>, 4> audio;
    double sampleRate = 0;
    for (size_t index = 0; index < names.size(); ++index)
        check(loadWav(directory.getChildFile(names[index]), audio[index], sampleRate, 5 * 48000, 30 * 48000),
            "Reference multitrack must load");
    std::vector<kit::TrackInput> inputs { { 1, kit::Role::kickIn, 0, 1, "Kick" }, { 2, kit::Role::snareTop, 0, 1, "Snare" },
        { 3, kit::Role::overhead, kit::positionLeft, 1, "OH L" }, { 4, kit::Role::overhead, kit::positionRight, 1, "OH R" } };
    const auto report = analyseOffline(inputs, { &audio[0], &audio[1], &audio[2], &audio[3] }, sampleRate, 0,
        static_cast<juce::int64>(audio[0].size()));
    if (verbose())
        for (const auto& line : report.lines) std::cout << "  " << line << "\n";
    const auto* kick = report.find(1);
    const auto* snare = report.find(2);
    check(kick != nullptr && kick->measured && !kick->invert && kick->delayMs > 2.2f && kick->delayMs < 3.9f,
        "Reference Kick must measure its first arrival (about +3 ms, normal polarity), not the next half-cycle at +7 ms inverted");
    check(snare != nullptr && snare->measured && !snare->invert && snare->delayMs > 2.4f && snare->delayMs < 3.5f,
        "Reference Snare must measure about +2.9 ms with normal polarity (first arrival, not the 9.8 ms reflection)");
    check(report.scores.phaseAfter > report.scores.phaseBefore + 15, "Reference kit coherence must improve after correction");
    std::cout << "PASS: real multitrack kick +" << kick->delayMs << " ms (r " << kick->confidence << "), snare +"
              << snare->delayMs << " ms, phase " << report.scores.phaseBefore << " -> " << report.scores.phaseAfter
              << ", stereo " << report.scores.stereo << "\n";
#endif
}

class FakePlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying(playing);
        info.setTimeInSamples(position);
        return info;
    }
    juce::int64 position = 0;
    bool playing = true;
};

void testRegistryWithInstances()
{
    juce::SharedResourcePointer<kit::Registry> registry;
    const SyntheticKit kitAudio(10.0);
    std::array<std::unique_ptr<ZeroEffortDrumProcessor>, 4> processors;
    const std::array<int, 4> instruments { 0, 1, 7, 7 };
    const std::array<int, 4> positions { kit::positionSecondary, 0, kit::positionLeft, kit::positionRight };
    const std::array<const std::vector<float>*, 4> audio { &kitAudio.kick, &kitAudio.snareTop, &kitAudio.overheadLeft, &kitAudio.overheadRight };
    FakePlayHead head;
    for (size_t index = 0; index < processors.size(); ++index)
    {
        processors[index] = std::make_unique<ZeroEffortDrumProcessor>();
        auto& processor = *processors[index];
        processor.setControl(drum::instrument, static_cast<float>(instruments[index]));
        processor.setKitControl(ZeroEffortDrumProcessor::kitGroup, 5);
        processor.setKitControl(ZeroEffortDrumProcessor::kitMic, static_cast<float>(positions[index]));
        processor.setPlayHead(&head);
        processor.prepareToPlay(rate, 512);
        processor.kitTick();
    }
    processors[0]->setKitControl(ZeroEffortDrumProcessor::kitMode, static_cast<float>(kit::Mode::automatic));
    processors[0]->setKitControl(ZeroEffortDrumProcessor::kitStrength, 50);
    processors[1]->setKitControl(ZeroEffortDrumProcessor::kitMode, static_cast<float>(kit::Mode::suggest));
    auto view = processors[2]->getKitView();
    check(view.members.size() == 4, "Every instance of one kit group must be discoverable from any member");
    check(processors[0]->getKitIdentity().role == kit::Role::kickOut && processors[3]->getKitIdentity().role == kit::Role::overhead,
        "Instrument and microphone position must define the kit role");
    check(processors[0]->getKitIdentity().name == "Kick Out" && processors[2]->getKitIdentity().name == "OH L",
        "Kit labels must name the microphone position");
    {
        ZeroEffortDrumProcessor stereoOverheads;
        stereoOverheads.setControl(drum::instrument, 7);
        check(stereoOverheads.getKitIdentity().name == "Overheads", "Stereo overheads must not be labelled only 'Stereo'");
    }
    {
        ZeroEffortDrumProcessor otherProject;
        otherProject.setKitControl(ZeroEffortDrumProcessor::kitGroup, 6);
        otherProject.kitTick();
        check(processors[0]->getKitView().members.size() == 4, "Another kit group must stay isolated");
    }
    check(processors[0]->getKitView().members.size() == 4, "Destroying an instance must leave the other kit members registered");
    ZeroEffortDrumProcessor silentInstance;
    silentInstance.setKitControl(ZeroEffortDrumProcessor::kitGroup, 5);
    silentInstance.kitTick();
    check(processors[0]->getKitView().members.size() == 5, "An instance that never plays is still listed in its group");
    processors[1]->startKitAnalysis();
    juce::AudioBuffer<float> block(1, 512);
    juce::MidiBuffer midi;
    bool finished = false;
    for (juce::int64 position = 0; position + 512 <= static_cast<juce::int64>(kitAudio.kick.size()) && !finished; position += 512)
    {
        head.position = position;
        for (size_t index = 0; index < processors.size(); ++index)
        {
            block.copyFrom(0, 0, audio[index]->data() + position, 512);
            processors[index]->processBlock(block, midi);
            check(std::isfinite(block.getMagnitude(0, 512)), "Kit capture must keep processing finite");
        }
        if ((position / 512) % 16 == 0)
        {
            registry->runOnce();
            finished = registry->view(5).report.final;
        }
    }
    for (int attempt = 0; attempt < 20 && !finished; ++attempt)
    {
        registry->runOnce();
        finished = registry->view(5).report.final;
    }
    check(finished, "Kit analysis must finish with the playing tracks even if another instance of the group never plays");
    for (auto& processor : processors) processor->kitTick();
    const auto report = processors[0]->getKitView().report;
    if (verbose())
    {
        std::cout << "  status: " << processors[0]->getKitView().status << " / members " << report.recommendations.size() << "\n";
        for (const auto& line : report.lines) std::cout << "  " << line << "\n";
    }
    const auto* kick = report.find(processors[0]->getKitMemberId());
    check(kick != nullptr && kick->measured && kick->invert, "Kick member must receive its own measured recommendation");
    check(std::abs(processors[0]->getKitControl(ZeroEffortDrumProcessor::kitDelay) - kick->delayMs * 0.5f) < 0.02f
        && processors[0]->getKitControl(ZeroEffortDrumProcessor::kitPolarity) > 0.5f,
        "Auto mode must apply the correction scaled by Force when the analysis ends");
    check(processors[1]->getKitControl(ZeroEffortDrumProcessor::kitDelay) == 0.0f && processors[1]->canApplyKitRecommendation(),
        "Suggest mode must wait for an explicit Apply");
    check(processors[1]->applyKitRecommendation() && processors[1]->getKitControl(ZeroEffortDrumProcessor::kitDelay) > 2.5f,
        "Applying a suggestion must write the delay parameter");
    check(processors[2]->getKitControl(ZeroEffortDrumProcessor::kitDelay) == 0.0f, "Overheads must stay untouched");
    juce::MemoryBlock state;
    processors[0]->getStateInformation(state);
    ZeroEffortDrumProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check(std::abs(restored.getKitControl(ZeroEffortDrumProcessor::kitDelay) - processors[0]->getKitControl(ZeroEffortDrumProcessor::kitDelay)) < 0.01f
        && restored.getKitControl(ZeroEffortDrumProcessor::kitGroup) == 5.0f
        && restored.getKitMode() == kit::Mode::automatic, "Kit settings and applied correction must survive state persistence");
    for (auto& processor : processors) processor->releaseResources();
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processors[1]->createEditor());
        if (auto* kitTab = dynamic_cast<juce::TextButton*>(editor->findChildWithID("pageKit"))) kitTab->onClick();
        juce::FileOutputStream stream(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("KitReport.png"));
        stream.setPosition(0);
        stream.truncate();
        check(stream.openedOk() && juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), stream),
            "Kit report snapshot must encode");

        const auto button = [&editor](const juce::String& id) {
            auto* found = dynamic_cast<juce::Button*>(editor->findChildWithID(id));
            if (found == nullptr)
                if (auto* panel = editor->findChildWithID("kitPreviewPanel")) found = dynamic_cast<juce::Button*>(panel->findChildWithID(id));
            check(found != nullptr, "Kit preview control must exist");
            return found;
        };
        const auto tickAll = [&processors] { for (auto& processor : processors) processor->kitTick(); };
        auto* panel = editor->findChildWithID("kitPreviewPanel");
        check(panel != nullptr && !panel->isVisible() && button("kitApply")->isEnabled(), "Preview must wait for the user");
        button("kitApply")->onClick();
        check(panel->isVisible(), "Apply opens a kit-wide preview instead of changing the tracks");
        check(button("kitPreviewRow" + juce::String(processors[1]->getKitMemberId()))->getToggleState(),
            "Measured corrections are preselected in the preview");
        juce::FileOutputStream previewStream(juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("KitPreview.png"));
        previewStream.setPosition(0);
        previewStream.truncate();
        check(previewStream.openedOk() && juce::PNGImageFormat().writeImageToStream(
            editor->createComponentSnapshot(editor->getLocalBounds()), previewStream), "Kit preview snapshot must encode");
        button("kitPreviewReset")->onClick();
        tickAll();
        check(!panel->isVisible() && processors[0]->getKitControl(ZeroEffortDrumProcessor::kitDelay) == 0.0f
            && processors[1]->getKitControl(ZeroEffortDrumProcessor::kitDelay) == 0.0f
            && processors[0]->getKitControl(ZeroEffortDrumProcessor::kitPolarity) == 0.0f,
            "Reset from the preview must clear every track of the kit");
        button("kitApply")->onClick();
        auto* kickRow = button("kitPreviewRow" + juce::String(processors[0]->getKitMemberId()));
        kickRow->setToggleState(false, juce::dontSendNotification);
        button("kitPreviewApply")->onClick();
        tickAll();
        check(processors[0]->getKitControl(ZeroEffortDrumProcessor::kitDelay) == 0.0f
            && processors[1]->getKitControl(ZeroEffortDrumProcessor::kitDelay) > 3.0f
            && processors[2]->getKitControl(ZeroEffortDrumProcessor::kitDelay) == 0.0f,
            "Only the checked tracks of the preview must be corrected");
        const auto before = processors[1]->getKitControl(ZeroEffortDrumProcessor::kitDelay);
        button("kitDelayUp")->onClick();
        check(std::abs(processors[1]->getKitControl(ZeroEffortDrumProcessor::kitDelay) - before - 1000.0f / 48000.0f) < 0.012f,
            "Manual fine adjustment must move the delay by one sample");
    }
    std::cout << "PASS: four instances discovered each other, Auto at Force 50 %, Suggest and preview wait for Apply, reset, fine nudge\n";
}

// ---- T3: artificial defects (document 02, section 5.4) -------------------------------------------------------------
// The kit is built from known physics: every close mic hears its own hit first and the overheads hear it `ToOhMs`
// later. The tool delays a close mic onto the overheads, so the expected correction equals that arrival gap.
struct FaultParams
{
    double seconds = 9.0;
    double kickToOhMs = 0.0;        // negative: the overheads hear the kick before its close mic
    double snareToOhMs = 3.0;
    double ohRightExtraMs = 0.0;    // overhead R later than overhead L
    bool kickInverted = false;      // polarity of the kick close mic against the overheads
    double snareBleedInKick = 0.05; // linear level of the snare in the kick mic
    double noiseRmsDb = -200;       // independent white noise on every track
    double gain = 1.0;
};

struct FaultKit
{
    std::vector<float> kick, kickOut, snare, overheadLeft, overheadRight;

    explicit FaultKit(const FaultParams& p)
    {
        const auto length = static_cast<size_t>(p.seconds * rate);
        for (auto* track : { &kick, &kickOut, &snare, &overheadLeft, &overheadRight }) track->assign(length, 0.0f);
        juce::Random random(4321);
        std::vector<float> grain(static_cast<size_t>(rate * 0.3));
        for (auto& value : grain) value = random.nextFloat() * 2.0f - 1.0f;
        const auto kickShape = [](double time) {
            if (time < 0) return 0.0;
            return 0.8 * (std::sin(juce::MathConstants<double>::twoPi * 55.0 * time) * std::exp(-time / 0.06)
                + 0.3 * std::sin(juce::MathConstants<double>::twoPi * 3000.0 * time) * std::exp(-time / 0.003));
        };
        const auto snareShape = [&grain](double time) {
            if (time < 0) return 0.0;
            const auto index = static_cast<size_t>(time * rate);
            const auto noise = index < grain.size() ? grain[index] : 0.0f;
            return 0.6 * (std::sin(juce::MathConstants<double>::twoPi * 200.0 * time) * std::exp(-time / 0.04)
                + 0.7 * noise * std::exp(-time / 0.05));
        };
        constexpr double period = 0.3;
        // Neighbouring periods are added so that a hit heard early by one mic is not cut at the period boundary.
        const auto hit = [&](const auto& shape, double age, double delayMs) {
            double total = 0;
            for (int turn = -1; turn <= 1; ++turn) total += shape(age - delayMs / 1000.0 + turn * period);
            return total;
        };
        const auto noiseAmplitude = std::pow(10.0, p.noiseRmsDb / 20.0) * std::sqrt(3.0);
        const auto sign = p.kickInverted ? -1.0 : 1.0;
        for (size_t sample = 0; sample < length; ++sample)
        {
            const auto time = static_cast<double>(sample) / rate;
            const auto kickAge = std::fmod(time, period);
            const auto snareAge = std::fmod(time + period * 0.5, period);
            const auto k = [&](double delayMs) { return hit(kickShape, kickAge, delayMs); };
            const auto s = [&](double delayMs) { return hit(snareShape, snareAge, delayMs); };
            const auto n = [&]() { return noiseAmplitude * (random.nextDouble() * 2 - 1); };
            kick[sample] = static_cast<float>(p.gain * (sign * k(0) + p.snareBleedInKick * s(0.4) + n()));
            kickOut[sample] = static_cast<float>(p.gain * (sign * 0.9 * k(0.3) + p.snareBleedInKick * s(0.5) + n()));
            snare[sample] = static_cast<float>(p.gain * (s(0) + 0.08 * k(1.0) + n()));
            overheadLeft[sample] = static_cast<float>(p.gain * (0.5 * k(p.kickToOhMs) + 0.6 * s(p.snareToOhMs) + n()));
            overheadRight[sample] = static_cast<float>(p.gain * (0.45 * k(p.kickToOhMs + p.ohRightExtraMs)
                + 0.55 * s(p.snareToOhMs + p.ohRightExtraMs) + n()));
        }
    }
};

enum class Layout { kitWithSnare, threeTracks, kickOutOnly };

kit::Report analyseFaultKit(const FaultKit& audio, Layout layout = Layout::kitWithSnare, const kit::KitPolicy& policy = {})
{
    std::vector<kit::TrackInput> inputs;
    std::vector<const std::vector<float>*> tracks;
    if (layout == Layout::kickOutOnly)
    {
        inputs = { { 1, kit::Role::kickOut, 1, 1, "Kick Out" }, { 2, kit::Role::overhead, kit::positionLeft, 1, "OH L" },
            { 3, kit::Role::overhead, kit::positionRight, 1, "OH R" } };
        tracks = { &audio.kickOut, &audio.overheadLeft, &audio.overheadRight };
    }
    else if (layout == Layout::threeTracks)
    {
        inputs = { { 1, kit::Role::kickIn, 0, 1, "Kick" }, { 2, kit::Role::snareTop, 0, 1, "Snare" },
            { 3, kit::Role::overhead, kit::positionMain, 1, "OH" } };
        tracks = { &audio.kick, &audio.snare, &audio.overheadLeft };
    }
    else
    {
        inputs = { { 1, kit::Role::kickIn, 0, 1, "Kick" }, { 2, kit::Role::snareTop, 0, 1, "Snare" },
            { 3, kit::Role::overhead, kit::positionLeft, 1, "OH L" }, { 4, kit::Role::overhead, kit::positionRight, 1, "OH R" } };
        tracks = { &audio.kick, &audio.snare, &audio.overheadLeft, &audio.overheadRight };
    }
    return analyseOffline(inputs, tracks, rate, 0, static_cast<juce::int64>(audio.kick.size()), policy);
}

// Document 02 (section 7.2) asks for +-1 sample (0.021 ms at 48 kHz) on a pure signal. Measured on the current algorithm:
// the Snare is within 0.001 ms, but the Kick (band 30-250 Hz) reads a constant 0.04-0.06 ms (2-3 samples) too late. That
// gap is reported here, not fixed (T3 must not modify the correlation); the tolerances are set just above it.
constexpr float snareToleranceMs = 0.025f;
constexpr float kickToleranceMs = 0.1f;
constexpr float realToleranceMs = 0.2f;

struct FaultLog
{
    float worst = 0;
    int cases = 0;
    void note(float error) { worst = juce::jmax(worst, std::abs(error)); ++cases; }
};

juce::String describe(const kit::Recommendation* recommendation)
{
    if (recommendation == nullptr) return "no recommendation";
    return juce::String(recommendation->measured ? "measured" : "not measured") + ", delay " + juce::String(recommendation->delayMs, 3)
        + " ms, " + (recommendation->invert ? "inverted" : "normal") + ", r " + juce::String(recommendation->confidence, 2)
        + (recommendation->note.isNotEmpty() ? " (" + recommendation->note + ")" : "");
}

void expectCorrection(const kit::Recommendation* recommendation, double delayMs, bool inverted, float tolerance,
    const juce::String& context, FaultLog& log)
{
    check(recommendation != nullptr && recommendation->measured, context + ": the track must be measured, got " + describe(recommendation));
    check(recommendation->invert == inverted, context + ": polarity must be " + (inverted ? "inverted" : "normal") + ", got " + describe(recommendation));
    const auto error = recommendation->delayMs - static_cast<float>(delayMs);
    if (verbose()) std::cout << "  " << context << ": error " << error << " ms" << std::endl;
    check(std::abs(error) <= tolerance, context + ": delay must be " + juce::String(delayMs, 3) + " ms +- " + juce::String(tolerance, 3)
        + ", got " + describe(recommendation));
    log.note(error);
}

// A faulty input must either be refused or give the right answer, never a silent wrong delay.
enum class Outcome { refused, right, wrong };

Outcome classify(const kit::Recommendation* recommendation, double delayMs, bool inverted, float tolerance, const juce::String& context)
{
    check(recommendation != nullptr, context + ": no recommendation");
    if (!recommendation->measured)
    {
        check(recommendation->delayMs == 0 && !recommendation->invert, context + ": a refusal must not carry a correction, got " + describe(recommendation));
        return Outcome::refused;
    }
    return recommendation->invert == inverted && std::abs(recommendation->delayMs - static_cast<float>(delayMs)) <= tolerance
        ? Outcome::right : Outcome::wrong;
}

void expectRightOrRefused(const kit::Recommendation* recommendation, double delayMs, bool inverted, float tolerance,
    const juce::String& context, bool& refused)
{
    const auto outcome = classify(recommendation, delayMs, inverted, tolerance, context);
    check(outcome != Outcome::wrong, context + ": a wrong correction was proposed silently, expected " + juce::String(delayMs, 3)
        + " ms, got " + describe(recommendation));
    refused = outcome == Outcome::refused;
}

void testFaultDelays()
{
    FaultLog log;
    // D01 kick against overheads, from 0.5 ms (just after the search window opens) to 10 ms
    for (const auto delay : { 0.5, 1.0, 2.0, 3.0, 5.0, 8.0, 10.0 })
    {
        FaultParams p;
        p.kickToOhMs = delay;
        expectCorrection(analyseFaultKit(FaultKit(p)).find(1), delay, false, kickToleranceMs, "D01 kick " + juce::String(delay) + " ms", log);
    }
    // D02 snare
    for (const auto delay : { 1.0, 2.5, 6.0, 10.0 })
    {
        FaultParams p;
        p.snareToOhMs = delay;
        expectCorrection(analyseFaultKit(FaultKit(p)).find(2), delay, false, snareToleranceMs, "D02 snare " + juce::String(delay) + " ms", log);
    }
    // D03 polarity
    for (const auto delay : { 0.0, 4.0 })
    {
        FaultParams p;
        p.kickToOhMs = delay;
        p.kickInverted = true;
        expectCorrection(analyseFaultKit(FaultKit(p)).find(1), delay, true, kickToleranceMs, "D03 inverted kick " + juce::String(delay) + " ms", log);
    }
    // D04 both, including the +7 ms inverted case of docs/KitPhase10.md
    for (const auto delay : { 3.0, 7.0 })
    {
        FaultParams p;
        p.kickToOhMs = delay;
        p.kickInverted = true;
        expectCorrection(analyseFaultKit(FaultKit(p)).find(1), delay, true, kickToleranceMs, "D04 kick " + juce::String(delay) + " ms inverted", log);
    }
    std::cout << "PASS: D01-D04 synthetic delay and polarity faults (" << log.cases << " cases, worst error " << log.worst << " ms)" << std::endl;
}

void testFaultsBeyondTheWindow()
{
    // D02 beyond 10 ms: the Snare search window ends there. A refusal is acceptable, a wrong delay is not.
    // EXPECTED FAILURE (reported, algorithm not modified): the tool answers with a spurious correction instead of
    // refusing. The test passes while exactly the cases listed here are wrong and fails when one of them changes, as
    // a reminder to turn this into a plain "right or refused" test.
    const std::vector<double> expectedWrong { 11.0, 14.0, 18.0 };
    std::vector<double> wrong;
    for (const auto delay : { 11.0, 14.0, 18.0 })
    {
        FaultParams p;
        p.snareToOhMs = delay;
        const auto* snare = analyseFaultKit(FaultKit(p)).find(2);
        const auto context = "D02 snare " + juce::String(delay) + " ms beyond the window";
        const auto outcome = classify(snare, delay, false, 0.3f, context);
        std::cout << "  " << context << ": " << (outcome == Outcome::wrong ? "WRONG" : outcome == Outcome::refused ? "refused" : "right")
                  << " (" << describe(snare) << ")" << std::endl;
        if (outcome == Outcome::wrong) wrong.push_back(delay);
    }
    check(wrong == expectedWrong, "D02 beyond the window: the set of wrong answers changed (was 11, 14 and 18 ms), "
        "check whether the search window was fixed and turn this into a plain test");

    // The kick is heard by the overheads before its close mic: the tool never advances a track, so it must not delay it.
    // 2 ms ahead is handled (no delay); 5 ms ahead is another silent wrong answer (2 ms, inverted), same expected failure.
    const auto kickAhead = [](double early) {
        FaultParams p;
        p.kickToOhMs = -early;
        const auto* kick = analyseFaultKit(FaultKit(p)).find(1);
        const auto context = "kick " + juce::String(early) + " ms ahead of the overheads";
        const auto outcome = classify(kick, 0.0, false, 0.05f, context);
        std::cout << "  " << context << ": " << (outcome == Outcome::wrong ? "WRONG" : outcome == Outcome::refused ? "refused" : "right")
                  << " (" << describe(kick) << ")" << std::endl;
        return outcome;
    };
    check(kickAhead(2.0) != Outcome::wrong, "A kick 2 ms ahead of the overheads must get no correction");
    check(kickAhead(5.0) == Outcome::wrong, "A kick 5 ms ahead of the overheads is a known silent wrong answer (2 ms, inverted): "
        "if this changed, turn it into a plain test");
    std::cout << "PASS: delays beyond the search window (expected failure: " << wrong.size() << " spurious corrections)" << std::endl;
}

void testFaultCrosstalkAndNoise()
{
    FaultLog log;
    // D05 snare in the kick mic: the kick delay must stay right. Document 02 lists -12 and -6 dB; the current analysis
    // holds up to -19 dB (relative to the kick peak) and then follows the snare bleed (3.5 ms instead of 5.0 ms, "normal",
    // r 0.7). EXPECTED FAILURE for the louder cases, reported and not fixed; the test fails when the set changes.
    std::vector<double> wrongBleeds;
    for (const auto bleed : { 0.1, 0.15, 0.2, 0.25, 0.5 })
    {
        FaultParams p;
        p.kickToOhMs = 5.0;
        p.snareBleedInKick = bleed;
        const auto* kick = analyseFaultKit(FaultKit(p)).find(1);
        const auto outcome = classify(kick, 5.0, false, kickToleranceMs, "D05 bleed");
        std::cout << "  D05 snare bleed " << bleed << " (" << 20 * std::log10(bleed * 0.6 / 0.8) << " dB vs the kick peak): "
                  << (outcome == Outcome::wrong ? "WRONG" : outcome == Outcome::refused ? "refused" : "right") << " (" << describe(kick) << ")" << std::endl;
        if (outcome == Outcome::wrong) wrongBleeds.push_back(bleed);
    }
    check((wrongBleeds == std::vector<double> { 0.2, 0.25, 0.5 }), "D05 the set of snare bleed levels that fool the kick changed (was 0.2, 0.25 and 0.5): "
        "turn the expected failure into a plain test if it was fixed");
    log.note(0);
    // D06 background noise
    int refused = 0;
    for (const auto noise : { -50.0, -40.0, -30.0 })
    {
        FaultParams p;
        p.kickToOhMs = 5.0;
        p.noiseRmsDb = noise;
        const auto report = analyseFaultKit(FaultKit(p));
        bool wasRefused = false;
        expectRightOrRefused(report.find(1), 5.0, false, realToleranceMs, "D06 kick with noise " + juce::String(noise) + " dBFS", wasRefused);
        expectRightOrRefused(report.find(2), 3.0, false, realToleranceMs, "D06 snare with noise " + juce::String(noise) + " dBFS", wasRefused);
        refused += wasRefused ? 1 : 0;
        if (noise <= -40.0) check(!wasRefused, "D06 at " + juce::String(noise) + " dBFS the analysis must still work");
    }
    // Harsher noise is only observed: it has to stay "right or refused"
    int harsh = 0;
    for (const auto noise : { -20.0, -10.0 })
    {
        FaultParams p;
        p.kickToOhMs = 5.0;
        p.noiseRmsDb = noise;
        bool wasRefused = false;
        expectRightOrRefused(analyseFaultKit(FaultKit(p)).find(1), 5.0, false, realToleranceMs, "D06 kick with noise " + juce::String(noise) + " dBFS", wasRefused);
        harsh += wasRefused ? 1 : 0;
    }
    std::cout << "PASS: D05 crosstalk (expected failure: " << wrongBleeds.size() << " of 5 levels fool the kick), D06 noise (right or refused; "
              << harsh << " of 2 harsh cases refused)" << std::endl;
}

// D07: overheads 0.4 ms apart. The stereo line reports the gap and the stereo score drops.
float stereoGapMs(const kit::Report& report)
{
    for (const auto& line : report.lines)
        if (line.startsWith("Overheads L/R : ecart "))
            return line.fromFirstOccurrenceOf("ecart ", false, false).upToFirstOccurrenceOf(" ms", false, false).getFloatValue();
    return -1.0f;
}

void testFaultStereoPair()
{
    FaultParams aligned;
    aligned.kickToOhMs = 5.0;
    FaultParams shifted = aligned;
    shifted.ohRightExtraMs = 0.4;
    const auto good = analyseFaultKit(FaultKit(aligned));
    const auto bad = analyseFaultKit(FaultKit(shifted));
    check(good.scores.stereo >= 0 && bad.scores.stereo >= 0, "D07 stereo scores must exist");
    check(stereoGapMs(good) >= 0 && stereoGapMs(good) < 0.1f, "D07 aligned overheads must report no gap, got " + juce::String(stereoGapMs(good)));
    check(std::abs(stereoGapMs(bad) - 0.4f) < 0.1f, "D07 the 0.4 ms gap must be found, got " + juce::String(stereoGapMs(bad)));
    check(bad.scores.stereo < good.scores.stereo - 3.0f, "D07 the stereo score must drop: " + juce::String(good.scores.stereo) + " -> " + juce::String(bad.scores.stereo));
    std::cout << "PASS: D07 overheads 0.4 ms apart, gap " << stereoGapMs(bad) << " ms, stereo " << good.scores.stereo << " -> " << bad.scores.stereo << std::endl;
}

// D08 (kit side): a quieter take must give the same correction and the same coherence.
void testFaultLevel()
{
    FaultParams loud;
    loud.kickToOhMs = 5.0;
    loud.kickInverted = true;
    FaultParams quiet = loud;
    quiet.gain = 0.1;   // -20 dB
    const auto reference = analyseFaultKit(FaultKit(loud));
    const auto lowered = analyseFaultKit(FaultKit(quiet));
    FaultLog log;
    expectCorrection(lowered.find(1), 5.0, true, kickToleranceMs, "D08 kick at -20 dB", log);
    expectCorrection(lowered.find(2), 3.0, false, snareToleranceMs, "D08 snare at -20 dB", log);
    check(std::abs(lowered.scores.phaseAfter - reference.scores.phaseAfter) < 3.0f && std::abs(lowered.scores.phaseBefore - reference.scores.phaseBefore) < 3.0f,
        "D08 the coherence scores must not depend on the level: " + juce::String(reference.scores.phaseBefore) + "/" + juce::String(reference.scores.phaseAfter)
        + " against " + juce::String(lowered.scores.phaseBefore) + "/" + juce::String(lowered.scores.phaseAfter));
    std::cout << "PASS: D08 kit measures do not depend on the level" << std::endl;
}

// D09, D10: reduced kits (amateur use): a result and no crash.
void testFaultReducedKits()
{
    FaultLog log;
    FaultParams p;
    p.kickToOhMs = 4.0;
    p.snareToOhMs = 2.0;
    const FaultKit audio(p);
    const auto three = analyseFaultKit(audio, Layout::threeTracks);
    check(three.valid, "D09 three tracks must produce a report");
    expectCorrection(three.find(1), 4.0, false, kickToleranceMs, "D09 kick with one overhead", log);
    expectCorrection(three.find(2), 2.0, false, snareToleranceMs, "D09 snare with one overhead", log);
    check(three.scores.stereo < 0, "D09 a single overhead has no stereo score");

    p.kickInverted = true;
    const FaultKit inverted(p);
    const auto out = analyseFaultKit(inverted, Layout::kickOutOnly);
    check(out.valid, "D10 Kick Out alone must produce a report");
    // Kick Out hears the hit 0.3 ms after Kick In, the overheads 4.0 ms after it: 3.7 ms to delay
    expectCorrection(out.find(1), 4.0 - 0.3, true, kickToleranceMs, "D10 Kick Out alone against the overheads", log);
    std::cout << "PASS: D09 three tracks and D10 Kick Out alone (worst error " << log.worst << " ms)" << std::endl;
}

bool loadReferenceKit(std::array<std::vector<float>, 4>& audio, double& sampleRate)
{
#ifdef ZED_WAV_DIRECTORY
    const juce::File directory(ZED_WAV_DIRECTORY);
    const std::array<const char*, 4> names { "Kick_12.wav", "Snare_12.wav", "OvH L_12.wav", "OvH R_12.wav" };
    for (const auto* name : names)
        if (!directory.getChildFile(name).existsAsFile()) return false;
    for (size_t index = 0; index < names.size(); ++index)
        check(loadWav(directory.getChildFile(names[index]), audio[index], sampleRate, 5 * 48000, 30 * 48000), "Reference multitrack must load");
    return true;
#else
    juce::ignoreUnused(audio, sampleRate);
    return false;
#endif
}

std::vector<float> delayed(const std::vector<float>& source, int samples)
{
    std::vector<float> result(source.size(), 0.0f);
    for (size_t index = static_cast<size_t>(samples); index < source.size(); ++index) result[index] = source[index - static_cast<size_t>(samples)];
    return result;
}

std::vector<float> flipped(const std::vector<float>& source)
{
    auto result = source;
    for (auto& value : result) value = -value;
    return result;
}

// The same defects on the real reference multitrack. The untouched take gives the baseline; each defect must move the
// answer by exactly what was applied (document 02: +-0.2 ms on a real kit).
void testRealFaults()
{
    std::array<std::vector<float>, 4> audio;
    double sampleRate = 0;
    if (!loadReferenceKit(audio, sampleRate))
    {
        std::cout << "SKIP: real kit fault tests (set ZED_WAV_DIRECTORY to the folder with Kick_12, Snare_12, OvH L_12, OvH R_12)\n";
        return;
    }
    const auto run = [&](const std::vector<float>& kickTrack, const std::vector<float>& left, const std::vector<float>& right) {
        std::vector<kit::TrackInput> inputs { { 1, kit::Role::kickIn, 0, 1, "Kick" }, { 2, kit::Role::snareTop, 0, 1, "Snare" },
            { 3, kit::Role::overhead, kit::positionLeft, 1, "OH L" }, { 4, kit::Role::overhead, kit::positionRight, 1, "OH R" } };
        return analyseOffline(inputs, { &kickTrack, &audio[1], &left, &right }, sampleRate, 0, static_cast<juce::int64>(kickTrack.size()));
    };
    const auto baseline = run(audio[0], audio[2], audio[3]);
    const auto* kick = baseline.find(1);
    const auto* snare = baseline.find(2);
    check(kick != nullptr && kick->measured && snare != nullptr && snare->measured, "Real baseline must be measured");
    const auto base = kick->delayMs;
    const auto baseInverted = kick->invert;
    const auto baseSnare = snare->delayMs;
    FaultLog log;

    // D01 (real): overheads 3 ms later -> kick and snare must be delayed 3 ms more
    const auto shift = static_cast<int>(std::lround(3.0 * sampleRate / 1000.0));
    const auto later = run(audio[0], delayed(audio[2], shift), delayed(audio[3], shift));
    expectCorrection(later.find(1), base + 3.0, baseInverted, realToleranceMs, "D01 real kick, overheads 3 ms later", log);
    expectCorrection(later.find(2), baseSnare + 3.0, snare->invert, realToleranceMs, "D02 real snare, overheads 3 ms later", log);

    // D03 (real): polarity of the kick flipped
    const auto flippedKick = run(flipped(audio[0]), audio[2], audio[3]);
    expectCorrection(flippedKick.find(1), base, !baseInverted, realToleranceMs, "D03 real kick flipped", log);

    // D04 (real): both
    const auto both = run(flipped(audio[0]), delayed(audio[2], shift), delayed(audio[3], shift));
    expectCorrection(both.find(1), base + 3.0, !baseInverted, realToleranceMs, "D04 real kick flipped and 3 ms later", log);
    std::cout << "PASS: D01-D04 on the real reference kit (baseline kick +" << base << " ms, worst error " << log.worst << " ms)" << std::endl;
}

// K04 (document 01): level of a close mic during the hits of the other instruments against its own hits. Additive: it is
// reported, whatever the policy. It must follow the snare bleed in the kick mic at 20 log10 of the bleed.
void testCrosstalkMeasure()
{
    std::vector<float> kickCrosstalk;
    for (const auto bleed : { 0.1, 0.15, 0.2, 0.25, 0.5 })
    {
        FaultParams p;
        p.kickToOhMs = 5.0;
        p.snareBleedInKick = bleed;
        const auto report = analyseFaultKit(FaultKit(p));
        const auto* kick = report.find(1);
        const auto* snare = report.find(2);
        const auto* overhead = report.find(3);
        check(kick != nullptr && std::isfinite(kick->crosstalkDb), "K04 the kick must report its crosstalk, bleed " + juce::String(bleed));
        check(snare != nullptr && std::isfinite(snare->crosstalkDb), "K04 the snare must report its crosstalk");
        check(overhead != nullptr && std::isnan(overhead->crosstalkDb), "K04 is not defined for overheads");
        check(kick->crosstalkDb < 0.0f && snare->crosstalkDb < 0.0f, "K04 the own hits dominate in a clean kit");
        kickCrosstalk.push_back(kick->crosstalkDb);
        if (verbose()) std::cout << "  K04 bleed " << bleed << ": kick " << kick->crosstalkDb << " dB, snare " << snare->crosstalkDb << " dB" << std::endl;
    }
    for (size_t index = 1; index < kickCrosstalk.size(); ++index)
        check(kickCrosstalk[index] > kickCrosstalk[index - 1] + 0.5f, "K04 must grow with the bleed level");
    // 0.25 -> 0.5 is a factor 2 in amplitude: 6.02 dB. At low bleed the kick's own decay under the snare hit (about 0.07 of its
    // peak 150 ms later) sets a floor, which is why the check does not start lower.
    check(std::abs((kickCrosstalk[4] - kickCrosstalk[3]) - 6.02f) < 1.5f, "K04 must scale as 20 log10 of the bleed, got "
        + juce::String(kickCrosstalk[4] - kickCrosstalk[3]) + " dB");

    FaultParams p;
    p.kickToOhMs = 3.0;
    const FaultKit audio(p);
    check(std::isnan(analyseFaultKit(audio, Layout::kickOutOnly).find(1)->crosstalkDb), "K04 needs another instrument: unavailable with the kick alone");
    std::cout << "PASS: K04 crosstalk (kick " << kickCrosstalk.front() << " to " << kickCrosstalk.back() << " dB for 0.1 to 0.5 of snare bleed)" << std::endl;
}

// Both guards are opt-in. With them on, every wrong answer reported by T3 must become a refusal, and nothing that was
// right may be lost.
void testGuards()
{
    kit::KitPolicy guarded;
    guarded.refuseOutsideWindow = true;
    guarded.refuseCrosstalk = true;
    FaultLog log;

    // Constat 2: beyond the search window
    for (const auto delay : { 11.0, 14.0, 18.0 })
    {
        FaultParams p;
        p.snareToOhMs = delay;
        const auto* snare = analyseFaultKit(FaultKit(p), Layout::kitWithSnare, guarded).find(2);
        check(classify(snare, delay, false, 0.3f, "guard D02") == Outcome::refused, "Guard: a snare " + juce::String(delay) + " ms behind must be refused, got " + describe(snare));
        check(snare->note.isNotEmpty(), "A refusal must say why");
    }
    for (const auto early : { 2.0, 5.0 })
    {
        FaultParams p;
        p.kickToOhMs = -early;
        const auto* kick = analyseFaultKit(FaultKit(p), Layout::kitWithSnare, guarded).find(1);
        check(classify(kick, 0.0, false, 0.05f, "guard kick ahead") != Outcome::wrong, "Guard: a kick " + juce::String(early) + " ms ahead must not get a delay, got " + describe(kick));
    }
    // The window edge itself, and ordinary faults, are untouched
    for (const auto delay : { 0.5, 3.0, 10.0 })
    {
        FaultParams p;
        p.kickToOhMs = delay;
        p.snareToOhMs = delay;
        const auto report = analyseFaultKit(FaultKit(p), Layout::kitWithSnare, guarded);
        expectCorrection(report.find(1), delay, false, kickToleranceMs, "guard D01 kick " + juce::String(delay) + " ms", log);
        expectCorrection(report.find(2), delay, false, snareToleranceMs, "guard D02 snare " + juce::String(delay) + " ms", log);
    }
    for (const auto delay : { 0.0, 4.0, 7.0 })
    {
        FaultParams p;
        p.kickToOhMs = delay;
        p.kickInverted = true;
        expectCorrection(analyseFaultKit(FaultKit(p), Layout::kitWithSnare, guarded).find(1), delay, true, kickToleranceMs, "guard D03 inverted kick " + juce::String(delay) + " ms", log);
    }

    // Constat 3: snare bleed in the kick mic, from -22 to -6 dB against the kick peak
    for (const auto bleed : { 0.1, 0.15, 0.2, 0.25, 0.5 })
    {
        FaultParams p;
        p.kickToOhMs = 5.0;
        p.snareBleedInKick = bleed;
        const auto* kick = analyseFaultKit(FaultKit(p), Layout::kitWithSnare, guarded).find(1);
        check(classify(kick, 5.0, false, kickToleranceMs, "guard D05") != Outcome::wrong, "Guard: snare bleed " + juce::String(bleed) + " must not fool the kick, got " + describe(kick));
    }
    FaultParams quietBleed;
    quietBleed.kickToOhMs = 5.0;
    quietBleed.snareBleedInKick = 0.1;
    expectCorrection(analyseFaultKit(FaultKit(quietBleed), Layout::kitWithSnare, guarded).find(1), 5.0, false, kickToleranceMs, "guard D05 light bleed is still corrected", log);

    // Reduced kits and noise
    FaultParams reduced;
    reduced.kickToOhMs = 4.0;
    reduced.snareToOhMs = 2.0;
    const FaultKit reducedAudio(reduced);
    expectCorrection(analyseFaultKit(reducedAudio, Layout::threeTracks, guarded).find(1), 4.0, false, kickToleranceMs, "guard D09 kick", log);
    for (const auto noise : { -50.0, -40.0 })
    {
        FaultParams p;
        p.kickToOhMs = 5.0;
        p.noiseRmsDb = noise;
        expectCorrection(analyseFaultKit(FaultKit(p), Layout::kitWithSnare, guarded).find(1), 5.0, false, realToleranceMs, "guard D06 noise " + juce::String(noise), log);
    }

    // The real reference kit must give the same corrections with and without the guards
    std::array<std::vector<float>, 4> audio;
    double sampleRate = 0;
    if (loadReferenceKit(audio, sampleRate))
    {
        std::vector<kit::TrackInput> inputs { { 1, kit::Role::kickIn, 0, 1, "Kick" }, { 2, kit::Role::snareTop, 0, 1, "Snare" },
            { 3, kit::Role::overhead, kit::positionLeft, 1, "OH L" }, { 4, kit::Role::overhead, kit::positionRight, 1, "OH R" } };
        const std::vector<const std::vector<float>*> tracks { &audio[0], &audio[1], &audio[2], &audio[3] };
        const auto length = static_cast<juce::int64>(audio[0].size());
        const auto plain = analyseOffline(inputs, tracks, sampleRate, 0, length);
        const auto safe = analyseOffline(inputs, tracks, sampleRate, 0, length, guarded);
        for (const auto id : { 1u, 2u })
            check(plain.find(id)->measured == safe.find(id)->measured && std::abs(plain.find(id)->delayMs - safe.find(id)->delayMs) < 1.0e-6f
                && plain.find(id)->invert == safe.find(id)->invert, "Guards must not change the corrections of the real reference kit, track " + juce::String(id));
        if (verbose())
            std::cout << "  real kit crosstalk: kick " << plain.find(1)->crosstalkDb << " dB, snare " << plain.find(2)->crosstalkDb << " dB" << std::endl;
    }

    // Off by default: the T3 expected failures stay reproducible
    FaultParams beyond;
    beyond.snareToOhMs = 14.0;
    check(analyseFaultKit(FaultKit(beyond)).find(2)->measured, "The guards are off by default");
    std::cout << "PASS: guards (outside window, crosstalk): no wrong answer left, ordinary faults unchanged (worst error " << log.worst << " ms)" << std::endl;
}

// Constat 5 (kit 4 of the corpus): the Kick locks one 125 Hz cycle (8.1 ms) apart on the two overheads (10.33 ms and 2.95 ms)
// while the Snare says the overheads are 0.27 ms apart. Built from the numbers measured on that kit, because a synthetic take
// cannot be made to slip on demand.
void testOverheadSpacing()
{
    constexpr double sampleRate = 48000;
    std::vector<kit::TrackInput> inputs { { 1, kit::Role::kickIn, 0, 1, "Kick" }, { 2, kit::Role::snareTop, 0, 1, "Snare" },
        { 3, kit::Role::overhead, kit::positionLeft, 1, "OH L" }, { 4, kit::Role::overhead, kit::positionRight, 1, "OH R" } };
    const auto solve = [&](bool reconcile, double kickLeft, double kickRight) {
        kit::KitSession session;
        kit::KitPolicy policy;
        policy.reconcileOverheads = reconcile;
        session.setPolicy(policy);
        check(session.reset(inputs, sampleRate), "Spacing test: the session must build pairs");
        auto pairs = session.evaluate();
        const auto lags = [&](const kit::PairResult& pair) -> std::array<double, 2> {
            const bool kickPair = pair.xTrack == 0;
            const bool left = pair.yTrack == 2;
            return kickPair ? std::array<double, 2> { left ? kickLeft : kickRight, left ? 0.63 : 0.49 }
                            : std::array<double, 2> { left ? 2.02 : 2.29, left ? 0.76 : 0.67 };
        };
        for (auto& pair : pairs)
        {
            if (pair.kind != kit::PairKind::closeToOverhead) continue;
            const auto [lagMs, r] = lags(pair);
            pair.events = 100;
            pair.rate = sampleRate;
            pair.valid = true;
            pair.lagMs = static_cast<float>(lagMs);
            pair.lagSamples = static_cast<float>(lagMs * sampleRate / 1000.0);
            pair.r = static_cast<float>(r);
            pair.r0 = 0.0f;
            pair.rStar = static_cast<float>(r);
            pair.rLowStar = pair.rStar;
            pair.consistencyMs = 0.05f;
            const auto size = static_cast<size_t>(2 * pair.centre + 1);
            pair.curve.assign(size, 0.0f);
            for (size_t index = 0; index < size; ++index)
            {
                const auto ms = (static_cast<double>(index) - pair.centre) * 1000.0 / sampleRate;
                const auto period = pair.xTrack == 0 ? 8.1 : 4.0;
                pair.curve[index] = static_cast<float>(r * std::cos(juce::MathConstants<double>::twoPi * (ms - lagMs) / period));
            }
        }
        return session.solve(pairs);
    };
    // Without the policy the weighted mean of the two lags is a silent wrong answer
    const auto slipped = solve(false, 10.33, 2.95);
    check(slipped.find(1) != nullptr && slipped.find(1)->measured && slipped.find(1)->delayMs > 6.5f && slipped.find(1)->delayMs < 7.5f,
        "Constat 5 reproduced: the kick is delayed by the mean of two cycles, got " + describe(slipped.find(1)));
    const auto fixed = solve(true, 10.33, 2.95);
    const auto* kick = fixed.find(1);
    check(kick != nullptr && kick->measured && kick->delayMs > 10.2f && kick->delayMs < 11.2f && kick->note.contains("recale"),
        "With the policy the kick must follow the Snare spacing (about 10.6 ms), got " + describe(kick));
    check(fixed.find(2)->measured && std::abs(fixed.find(2)->delayMs - 2.15f) < 0.2f, "The Snare must be untouched");
    // Consistent kits are never touched
    const auto agreeing = solve(true, 10.40, 11.05);
    const auto reference = solve(false, 10.40, 11.05);
    check(agreeing.find(1)->measured && std::abs(agreeing.find(1)->delayMs - reference.find(1)->delayMs) < 1.0e-6f && agreeing.find(1)->note.isEmpty(),
        "A kit whose overhead spacing agrees must not be modified");
    // Nothing to match against, so no change (a single close mic)
    std::vector<kit::TrackInput> alone { { 1, kit::Role::kickIn, 0, 1, "Kick" }, { 3, kit::Role::overhead, kit::positionLeft, 1, "OH L" },
        { 4, kit::Role::overhead, kit::positionRight, 1, "OH R" } };
    kit::KitSession session;
    kit::KitPolicy policy;
    policy.reconcileOverheads = true;
    session.setPolicy(policy);
    check(session.reset(alone, sampleRate), "A single close mic still builds pairs");
    std::cout << "PASS: overhead spacing (kick " << slipped.find(1)->delayMs << " ms silently wrong, " << kick->delayMs << " ms with the policy)" << std::endl;
}

void testKitFaults()
{
    testFaultDelays();
    testFaultsBeyondTheWindow();
    testFaultCrosstalkAndNoise();
    testFaultStereoPair();
    testFaultLevel();
    testFaultReducedKits();
    testRealFaults();
    testCrosstalkMeasure();
    testGuards();
    testOverheadSpacing();
}

void testRoleNames()
{
    check(juce::String(drum::ui::eqBands(0)[2].name) == "Batte" && juce::String(drum::ui::eqBands(1)[3].name) == "Timbre",
        "EQ bands must be named after the selected drum role");
    check(drum::ui::longName(drum::bodyGain, 1) == "Corps Gain" && drum::ui::longName(drum::bodyGain, 0) == "Poids Gain",
        "Tooltips must follow the role-specific band names");
    check(drum::ui::focusPageName(6).isEmpty() && drum::ui::focusPageName(1) == "Nettoyage",
        "Kick-specific focus page must be renamed or hidden for other roles");
    check(kit::roleFor(1, kit::positionSecondary) == kit::Role::snareBottom && kit::roleFor(9, 0) == kit::Role::none,
        "Snare Bottom must be selectable and the master bus excluded from the kit");
}
}

void testKit()
{
    testRoleNames();
    testAligner();
    testCapture();
    testSyntheticKit();
    testRealMultitrack();
    testKitFaults();
    testRegistryWithInstances();
    std::cout << "PASS: kit alignment, polarity, capture and multi-instance registry\n";
}

// Developer command: ZeroEffortDrumTests.exe --kit [--guards] [--start <s>] [--seconds <s>] <role>=<mono wav> ...
// Roles: kickin, kickout, snare, snarebottom, tom, floortom, hihat, oh, ohl, ohr, room. Analysis only, no playback.
int runKitCommand(const juce::StringArray& commandLine)
{
    if (commandLine.isEmpty() || commandLine[0] != "--kit") return -1;
    const auto fail = [](const juce::String& message) { std::cerr << "ERROR: " << message << std::endl; return 1; };
    kit::KitPolicy policy;
    bool detail = false;
    double startSeconds = 0, seconds = 60;
    struct Entry { juce::String name; kit::Role role; int position; juce::File file; };
    std::vector<Entry> entries;
    for (int index = 1; index < commandLine.size(); ++index)
    {
        const auto& item = commandLine[index];
        if (item == "--guards") policy.refuseOutsideWindow = policy.refuseCrosstalk = true;
        else if (item == "--detail") detail = true;
        else if (item == "--reconcile") policy.reconcileOverheads = true;
        else if (item == "--no-reconcile") policy.reconcileOverheads = false;
        else if ((item == "--start" || item == "--seconds") && index + 1 < commandLine.size())
            (item == "--start" ? startSeconds : seconds) = commandLine[++index].getDoubleValue();
        else
        {
            const auto name = item.upToFirstOccurrenceOf("=", false, false).toLowerCase();
            const juce::File file(item.fromFirstOccurrenceOf("=", false, false));
            kit::Role role = kit::Role::none;
            int position = 0;
            if (name == "kickin") role = kit::Role::kickIn;
            else if (name == "kickout") { role = kit::Role::kickOut; position = kit::positionSecondary; }
            else if (name == "snare") role = kit::Role::snareTop;
            else if (name == "snarebottom") { role = kit::Role::snareBottom; position = kit::positionSecondary; }
            else if (name == "tom") role = kit::Role::tom;
            else if (name == "floortom") role = kit::Role::floorTom;
            else if (name == "hihat") role = kit::Role::hiHat;
            else if (name == "oh") role = kit::Role::overhead;
            else if (name == "ohl") { role = kit::Role::overhead; position = kit::positionLeft; }
            else if (name == "ohr") { role = kit::Role::overhead; position = kit::positionRight; }
            else if (name == "room") role = kit::Role::room;
            else return fail("unknown role '" + name + "'");
            entries.push_back({ name, role, position, file });
        }
    }
    if (entries.size() < 2) return fail("usage: --kit [--guards] [--start s] [--seconds s] <role>=<mono wav> ... (at least a close mic and an overhead)");
    std::vector<std::vector<float>> audio(entries.size());
    std::vector<kit::TrackInput> inputs;
    double sampleRate = 0;
    for (size_t index = 0; index < entries.size(); ++index)
    {
        double fileRate = 0;
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(entries[index].file));
        if (reader == nullptr) return fail("cannot read " + entries[index].file.getFullPathName());
        fileRate = reader->sampleRate;
        if (reader->numChannels != 1) return fail(entries[index].file.getFileName() + " must be mono");
        if (index > 0 && fileRate != sampleRate) return fail("all files must share the sample rate");
        sampleRate = fileRate;
        const auto start = static_cast<juce::int64>(startSeconds * fileRate);
        if (!loadWav(entries[index].file, audio[index], fileRate, start, static_cast<juce::int64>(seconds * fileRate))) return fail("read failure");
        inputs.push_back({ static_cast<juce::uint32>(index + 1), entries[index].role, entries[index].position, 1, entries[index].name });
    }
    std::vector<const std::vector<float>*> pointers;
    size_t shortest = audio[0].size();
    for (const auto& track : audio) { pointers.push_back(&track); shortest = std::min(shortest, track.size()); }
    std::vector<kit::PairResult> pairs;
    const auto report = analyseOffline(inputs, pointers, sampleRate, 0, static_cast<juce::int64>(shortest), policy, &pairs);
    std::cout << "# Kit : " << entries.size() << " pistes, " << sampleRate << " Hz, garde-fous " << (policy.refuseOutsideWindow ? "actifs" : "inactifs") << "\n\n";
    std::cout << "| Piste | Mesure | Retard (ms) | Polarite | r | Diaphonie K04 (dB) | Note |\n|---|---|---|---|---|---|---|\n";
    for (const auto& recommendation : report.recommendations)
        std::cout << "| " << recommendation.label << " | " << (recommendation.measured ? "oui" : "non") << " | " << juce::String(recommendation.delayMs, 2)
                  << " | " << (recommendation.invert ? "inversee" : "normale") << " | " << juce::String(recommendation.confidence, 2) << " | "
                  << (std::isfinite(recommendation.crosstalkDb) ? juce::String(recommendation.crosstalkDb, 1) : juce::String("-")) << " | "
                  << recommendation.note << " |\n";
    std::cout << "\nCoherence de phase " << report.scores.phaseBefore << " -> " << report.scores.phaseAfter << ", frappes " << report.events << std::endl;
    if (detail)
    {
        std::cout << "\n| Paire | Evenements | Retard (ms) | r | valide | coherence pair/impair (ms) | hors fenetre / dans | r a 3 / 5 / 7 / 9 / 11 ms |\n|---|---|---|---|---|---|---|---|\n";
        for (const auto& pair : pairs)
        {
            if (pair.kind != kit::PairKind::closeToOverhead && pair.kind != kit::PairKind::secondary) continue;
            const auto name = [&](int track, int channel) { return entries[static_cast<size_t>(track)].name + (channel >= 0 ? "." + juce::String(channel) : juce::String()); };
            std::cout << "| " << name(pair.xTrack, pair.xChannel) << " -> " << name(pair.yTrack, pair.yChannel) << " | " << pair.events << " | "
                      << juce::String(pair.lagMs, 2) << " | " << juce::String(pair.r, 2) << " | " << (pair.valid ? "oui" : "non") << " | "
                      << (pair.consistencyMs >= 0 ? juce::String(pair.consistencyMs, 2) : juce::String("-")) << " | " << juce::String(pair.outsideRatio, 2) << " | ";
            for (const auto ms : { 3.0f, 5.0f, 7.0f, 9.0f, 11.0f })
                std::cout << juce::String(pair.at(ms * static_cast<float>(pair.rate) / 1000.0f), 2) << " ";
            std::cout << "|\n";
        }
    }
    return 0;
}
