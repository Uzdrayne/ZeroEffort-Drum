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
                           double sampleRate, juce::int64 start, juce::int64 end)
{
    kit::KitSession session;
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
            std::cout << "SKIP: real multitrack kit test (wav_test_file missing)\n";
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
    testRegistryWithInstances();
    std::cout << "PASS: kit alignment, polarity, capture and multi-instance registry\n";
}
