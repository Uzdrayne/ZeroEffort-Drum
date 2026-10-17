#include "ZeroEffortDrumProcessor.h"
#include "../UI/ZeroEffortDrumEditor.h"
#include "../Drum/DrumFilterResponse.h"
#include "RegionCache.h"
#include "CacheValidity.h"
#include "../Drum/DrumArchive.h"
#include "../Drum/DrumAutoPreset.h"
#include "../Drum/DrumAnalysisSnapshot.h"
#include "../Drum/DrumPresetPreview.h"
#include "../Drum/DrumAutoRadar.h"
#include <cmath>

namespace
{
class DrumModification final : public juce::ARAAudioModification
{
public:
    DrumModification(juce::ARAAudioSource* source, ARA::ARAAudioModificationHostRef reference,
                     const DrumModification* clone)
        : ARAAudioModification(source, reference, clone)
    {
        if (clone != nullptr)
            settings.store(clone->settings.load());
    }
    drum::AtomicSettings settings;
    std::array<std::atomic<float>, 4> meters {};
    std::array<std::atomic<float>, 3> bandMeters {};
    std::array<std::atomic<float>, drum::finishMeterCount> finishMeters {};
};

struct PrototypeStatus
{
    static_assert(std::atomic<unsigned int>::is_always_lock_free
                  && std::atomic<juce::int64>::is_always_lock_free
                  && std::atomic<float>::is_always_lock_free
                  && std::atomic<drum::PlaybackState>::is_always_lock_free);
    std::atomic<unsigned int> revision { 0 };
    std::atomic<unsigned int> cachedRevision { 0 };
    drum::CacheValidity cacheValidity;
    std::atomic<int> loadedRegions { 0 };
    std::atomic<int> failedRegions { 0 };
    std::atomic<int> monoRegions { 0 }, stereoRegions { 0 };
    std::atomic<drum::CacheFailure> lastFailure { drum::CacheFailure::none };
    std::atomic<double> sourceRate { 0.0 }, renderRate { 0.0 }, duration { 0.0 };
    std::atomic<int> sourceChannels { 0 }, renderChannels { 0 };
    std::atomic<bool> restored { false };
    std::atomic<unsigned int> playbackCalls { 0 }, playingCalls { 0 };
    std::atomic<drum::PlaybackState> playbackState { drum::PlaybackState::waiting };
    std::atomic<juce::int64> hostSample { 0 }, firstSample { 0 }, endSample { 0 };
    std::atomic<float> cachedPeak { 0.0f }, renderedPeak { 0.0f };
    std::atomic<float> hostOutputPeak { 0.0f };
    std::atomic<unsigned int> hostOutputCalls { 0 };
    std::atomic<int> hostOutputChannels { 0 };
    std::atomic<bool> hostOutputActive { false }, hostOutputFallback { false }, hostOutputSilent { false };
    std::array<std::atomic<float>, 4> meters {};
    std::array<std::atomic<float>, 3> bandMeters {};
    std::array<std::atomic<float>, drum::finishMeterCount> finishMeters {};
    drum::SpectrumMailbox spectrum;

    void reject(drum::CacheFailure failure, const drum::RegionRequirements& region) noexcept
    {
        sourceRate.store(region.sourceRate);
        renderRate.store(region.renderRate);
        sourceChannels.store(region.sourceChannels);
        renderChannels.store(region.renderChannels);
        duration.store(region.duration);
        lastFailure.store(failure);
        failedRegions.fetch_add(1);
    }
};

class PrototypeRenderer final : public juce::ARAPlaybackRenderer
{
    struct RegionPlayback
    {
        std::vector<drum::CachedRegion> cache;
        DrumModification* modification = nullptr;
        drum::CacheValidity::Token regionValidity, sourceValidity;
        drum::DrumEngine engine;
        juce::AudioBuffer<float> scratch;
    };
public:
    PrototypeRenderer(ARA::PlugIn::DocumentController* controller,
                      std::shared_ptr<PrototypeStatus> statusIn)
        : ARAPlaybackRenderer(controller), status(std::move(statusIn)) {}

    // Unprocessed sum of the rendered regions of the last block, used by the kit analysis.
    const juce::AudioBuffer<float>& rawMix() const noexcept { return raw; }
    int rawSamples() const noexcept { return rawLength; }

    void prepareToPlay(double sampleRate, int blockSize, int channels,
                       juce::AudioProcessor::ProcessingPrecision,
                       AlwaysNonRealtime) override
    {
        regions.clear();
        status->loadedRegions.store(0);
        status->failedRegions.store(0);
        status->lastFailure.store(drum::CacheFailure::none);
        status->monoRegions.store(0);
        status->stereoRegions.store(0);
        status->renderRate.store(sampleRate);
        status->renderChannels.store(channels);
        status->playbackCalls.store(0);
        status->playingCalls.store(0);
        status->playbackState.store(drum::PlaybackState::waiting);
        status->cachedPeak.store(0.0f);
        status->renderedPeak.store(0.0f);
        status->hostOutputPeak.store(0.0f);
        status->hostOutputCalls.store(0);
        status->hostSample.store(0);
        status->firstSample.store(0);
        status->endSample.store(0);
        revision = status->revision.load();
        rate = sampleRate;
        raw.setSize(juce::jmax(1, channels), juce::jmax(1, blockSize));
        rawLength = 0;
        expectedSample = std::numeric_limits<juce::int64>::min();
        juce::int64 cachedBytes = 0;

        for (auto* region : getPlaybackRegions())
        {
            auto* source = region->getAudioModification()->getAudioSource();
            const auto duration = region->getDurationInPlaybackTime();
            const auto sourceStart = region->getStartInAudioModificationTime();
            const auto playbackStart = region->getStartInPlaybackTime();
            const auto modificationDuration = region->getDurationInAudioModificationTime();
            const drum::RegionRequirements requirements {
                source->isSampleAccessEnabled(), source->getSampleRate(), sampleRate,
                source->getChannelCount(), channels, duration, sourceStart, playbackStart, modificationDuration
            };
            const auto failure = drum::checkRegion(requirements);
            if (failure != drum::CacheFailure::none)
            {
                status->reject(failure, requirements);
                continue;
            }

            const auto length = static_cast<int>(std::llround(duration * sampleRate));
            const auto regionBytes = static_cast<juce::int64>(length) * source->getChannelCount()
                * static_cast<juce::int64>(sizeof(float));
            if (!drum::cacheHasCapacity(length, source->getChannelCount(), cachedBytes, regions.size()))
            {
                status->reject(drum::CacheFailure::memoryLimit, requirements);
                continue;
            }

            juce::ARAAudioSourceReader reader(source);
            const auto sourceOffset = static_cast<juce::int64>(std::llround(sourceStart * sampleRate));
            if (sourceOffset > reader.lengthInSamples || length > reader.lengthInSamples - sourceOffset)
            {
                status->reject(drum::CacheFailure::sourceRange, requirements);
                continue;
            }
            drum::CachedRegion cached;
            cached.startSample = static_cast<juce::int64>(std::llround(playbackStart * sampleRate));
            cached.audio.setSize(source->getChannelCount(), length);
            if (!reader.read(&cached.audio, 0, length, sourceOffset, true, true))
            {
                status->reject(drum::CacheFailure::readFailure, requirements);
                continue;
            }
            cachedBytes += regionBytes;
            status->cachedPeak.store(juce::jmax(status->cachedPeak.load(), cached.audio.getMagnitude(0, length)));
            status->firstSample.store(regions.empty() ? cached.startSample
                : juce::jmin(status->firstSample.load(), cached.startSample));
            status->endSample.store(juce::jmax(status->endSample.load(), cached.startSample + length));
            auto playback = std::make_unique<RegionPlayback>();
            playback->modification = static_cast<DrumModification*>(region->getAudioModification());
            playback->regionValidity = status->cacheValidity.observe(region);
            playback->sourceValidity = status->cacheValidity.observe(source);
            playback->cache.push_back(std::move(cached));
            playback->scratch.setSize(channels, juce::jmax(1, blockSize));
            playback->engine.prepare(sampleRate, blockSize, channels);
            regions.push_back(std::move(playback));
            if (source->getChannelCount() == 1)
                status->monoRegions.fetch_add(1);
            else
                status->stereoRegions.fetch_add(1);
            status->loadedRegions.fetch_add(1);
        }
        status->cachedRevision.store(revision);
    }

    void releaseResources() override
    {
        regions.clear();
        status->loadedRegions.store(0);
        status->monoRegions.store(0);
        status->stereoRegions.store(0);
    }

    bool processBlock(juce::AudioBuffer<float>& buffer, juce::AudioProcessor::Realtime,
                      const juce::AudioPlayHead::PositionInfo& position) noexcept override
    {
        const auto sample = position.getTimeInSamples();
        const auto seconds = position.getTimeInSeconds();
        const auto validSeconds = seconds && std::isfinite(*seconds) && std::abs(*seconds) <= 7.0 * 24.0 * 3600.0;
        const auto startSample = sample ? *sample : validSeconds
            ? static_cast<juce::int64>(std::llround(*seconds * rate)) : juce::int64 { 0 };
        buffer.clear();
        rawLength = buffer.getNumSamples() <= raw.getNumSamples() && buffer.getNumChannels() <= raw.getNumChannels()
            ? buffer.getNumSamples() : 0;
        if (rawLength > 0) raw.clear(0, rawLength);
        auto state = !position.getIsPlaying() ? drum::PlaybackState::stopped
            : !(sample.hasValue() || validSeconds) ? drum::PlaybackState::missingPosition
            : drum::PlaybackState::outsideRegions;
        std::array<float, 4> meters {};
        std::array<float, 3> bandMeters {};
        if (state == drum::PlaybackState::outsideRegions)
        {
            for (auto& playback : regions)
            {
                if (!playback->regionValidity->load(std::memory_order_relaxed)
                    || !playback->sourceValidity->load(std::memory_order_relaxed))
                {
                    if (state != drum::PlaybackState::rendered)
                        state = drum::PlaybackState::stale;
                    continue;
                }
                if (startSample != expectedSample)
                    playback->engine.reset();
                const auto settings = playback->modification->settings.load();
                for (int offset = 0; offset < buffer.getNumSamples();)
                {
                    const auto length = juce::jmin(playback->scratch.getNumSamples(), buffer.getNumSamples() - offset);
                    juce::AudioBuffer<float> block(playback->scratch.getArrayOfWritePointers(), buffer.getNumChannels(), length);
                    if (drum::renderRegions(playback->cache, startSample + offset, block))
                        state = drum::PlaybackState::rendered;
                    if (rawLength > 0)
                        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                            raw.addFrom(channel, offset, block, channel, 0, length);
                    playback->engine.process(block, settings);
                    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                        buffer.addFrom(channel, offset, block, channel, 0, length);
                    meters[0] = juce::jmax(meters[0], playback->engine.inputMeter.load());
                    meters[1] = juce::jmax(meters[1], playback->engine.outputMeter.load());
                    meters[2] = juce::jmax(meters[2], playback->engine.gateDisplayMeter.load());
                    meters[3] = juce::jmax(meters[3], playback->engine.compressionDisplayMeter.load());
                    for (size_t band = 0; band < 3; ++band)
                        bandMeters[band] = juce::jmax(bandMeters[band], playback->engine.bandReductionMeters[band].load());
                    playback->modification->meters[0].store(playback->engine.inputMeter.load(), std::memory_order_relaxed);
                    playback->modification->meters[1].store(playback->engine.outputMeter.load(), std::memory_order_relaxed);
                    playback->modification->meters[2].store(playback->engine.gateDisplayMeter.load(), std::memory_order_relaxed);
                    playback->modification->meters[3].store(playback->engine.compressionDisplayMeter.load(), std::memory_order_relaxed);
                    for (size_t band = 0; band < 3; ++band)
                        playback->modification->bandMeters[band].store(playback->engine.bandReductionMeters[band].load(), std::memory_order_relaxed);
                    for (size_t meter = 0; meter < drum::finishMeterCount; ++meter)
                    {
                        const auto value = playback->engine.finishMeters[meter].load(std::memory_order_relaxed);
                        status->finishMeters[meter].store(value, std::memory_order_relaxed);
                        playback->modification->finishMeters[meter].store(value, std::memory_order_relaxed);
                    }
                    offset += length;
                }
            }
            expectedSample = startSample + buffer.getNumSamples();
        }
        else
            expectedSample = std::numeric_limits<juce::int64>::min();
        for (size_t index = 0; index < meters.size(); ++index)
            status->meters[index].store(meters[index], std::memory_order_relaxed);
        for (size_t band = 0; band < 3; ++band)
            status->bandMeters[band].store(bandMeters[band], std::memory_order_relaxed);
        status->playbackCalls.fetch_add(1, std::memory_order_relaxed);
        if (position.getIsPlaying())
            status->playingCalls.fetch_add(1, std::memory_order_relaxed);
        status->hostSample.store(startSample, std::memory_order_relaxed);
        status->playbackState.store(state, std::memory_order_relaxed);
        if (buffer.getNumSamples() > 0 && buffer.getNumChannels() > 0)
            status->renderedPeak.store(juce::jmax(status->renderedPeak.load(std::memory_order_relaxed),
                buffer.getMagnitude(0, buffer.getNumSamples())), std::memory_order_relaxed);
        return true;
    }

    using juce::ARAPlaybackRenderer::processBlock;

private:
    std::shared_ptr<PrototypeStatus> status;
    juce::AudioBuffer<float> raw;
    int rawLength = 0;
    unsigned int revision = 0;
    double rate = 48000.0;
    juce::int64 expectedSample = std::numeric_limits<juce::int64>::min();
    std::vector<std::unique_ptr<RegionPlayback>> regions;
};

class PrototypeDocument final : public juce::ARADocumentControllerSpecialisation
{
public:
    using juce::ARADocumentControllerSpecialisation::ARADocumentControllerSpecialisation;
    std::shared_ptr<PrototypeStatus> status = std::make_shared<PrototypeStatus>();

protected:
    juce::ARAAudioModification* doCreateAudioModification(juce::ARAAudioSource* source,
        ARA::ARAAudioModificationHostRef reference, const juce::ARAAudioModification* clone) noexcept override
    {
        return new DrumModification(source, reference, static_cast<const DrumModification*>(clone));
    }

    juce::ARAPlaybackRenderer* doCreatePlaybackRenderer() override
    {
        return new PrototypeRenderer(getDocumentController(), status);
    }

    void willUpdatePlaybackRegionProperties(juce::ARAPlaybackRegion* region,
        ARA::PlugIn::PropertiesPtr<ARA::ARAPlaybackRegionProperties>) override
    {
        status->cacheValidity.invalidate(region);
        status->revision.fetch_add(1);
    }

    void didUpdatePlaybackRegionContent(juce::ARAPlaybackRegion* region, juce::ARAContentUpdateScopes) override
    {
        status->cacheValidity.invalidate(region);
        status->revision.fetch_add(1);
    }

    void willDestroyPlaybackRegion(juce::ARAPlaybackRegion* region) override
    {
        status->cacheValidity.invalidate(region);
        status->revision.fetch_add(1);
    }

    void willUpdateAudioSourceProperties(juce::ARAAudioSource* source,
        ARA::PlugIn::PropertiesPtr<ARA::ARAAudioSourceProperties>) override
    {
        status->cacheValidity.invalidate(source);
        status->revision.fetch_add(1);
    }

    void doUpdateAudioSourceContent(juce::ARAAudioSource* source, juce::ARAContentUpdateScopes) override
    {
        status->cacheValidity.invalidate(source);
        status->revision.fetch_add(1);
    }

    void willEnableAudioSourceSamplesAccess(juce::ARAAudioSource* source, bool) override
    {
        status->cacheValidity.invalidate(source);
        status->revision.fetch_add(1);
    }

    void willDestroyAudioSource(juce::ARAAudioSource* source) override
    {
        status->cacheValidity.invalidate(source);
        status->revision.fetch_add(1);
    }

    bool doStoreObjectsToStream(juce::ARAOutputStream& stream,
                               const juce::ARAStoreObjectsFilter* filter) override
    {
        const auto& modifications = filter->getAudioModificationsToStore<DrumModification>();
        if (!stream.writeInt(0x44524133) || !stream.writeInt(static_cast<int>(modifications.size()))
            || !stream.writeInt(drum::controlCount))
            return false;
        for (const auto* modification : modifications)
            if (!drum::writeDrumEntry(stream, juce::String(modification->getPersistentID()), modification->settings.load()))
                return false;
        return true;
    }

    bool doRestoreObjectsFromStream(juce::ARAInputStream& stream,
                                   const juce::ARARestoreObjectsFilter* filter) override
    {
        std::vector<drum::ArchivedDrum> entries;
        if (!drum::readDrumArchive(stream, entries) || stream.failed())
            return false;
        for (const auto& entry : entries)
            if (auto* modification = filter->getAudioModificationToRestoreStateWithID<DrumModification>(entry.id.toRawUTF8()))
                modification->settings.store(entry.settings);
        status->restored.store(!stream.failed());
        return !stream.failed();
    }
};

}

struct ZeroEffortDrumProcessor::AutoSession
{
    drum::DrumAnalyser analyser;
    drum::DrumProposal proposal;
    drum::DrumPresetPreview preview;
    drum::DrumStyle style = drum::DrumStyle::natural;
    float intensity = 1;
    int phase = 0;
    drum::Settings before {}, applied {};
    juce::AudioBuffer<float> scratch { 2, 32768 };
    juce::String key, text { "Auto / Ready" };
    double rate = 0;
    juce::int64 offset = 0, total = 0;
    unsigned int generation = 0;
    bool busy = false, ready = false, changed = false, applyWhenReady = true, capturePrepared = false;
    drum::AutoRadarView radar;
};

ZeroEffortDrumProcessor::~ZeroEffortDrumProcessor()
{
    stopTimer();
    kitRegistry->leave(kitMember);
}

ZeroEffortDrumProcessor::ZeroEffortDrumProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::mono(), true)
                                     .withOutput("Output", juce::AudioChannelSet::mono(), true)),
      parameters(*this, nullptr, "DrumPrototypeState", createParameters())
{
    autoSession = std::make_unique<AutoSession>();
    for (size_t index = 0; index < controls.size(); ++index)
        controls[index] = parameters.getRawParameterValue(drum::controlSpecs[index].id);
    kitMember = kitRegistry->join();
    kitTick();
    startTimerHz(5);
}

juce::AudioProcessorValueTreeState::ParameterLayout ZeroEffortDrumProcessor::createParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    juce::StringArray groups;
    for (int group = 1; group <= kit::groupCount; ++group) groups.add("Kit " + juce::String(group));
    const auto fixed = juce::AudioParameterChoiceAttributes().withAutomatable(false);
    for (const auto& spec : drum::controlSpecs)
    {
        const juce::String id(spec.id);
        if (id == "drumInstrument")
        {
            juce::StringArray names;
            for (const auto* name : drum::instrumentNames) names.add(name);
            layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { spec.id, 1 }, spec.name, names, 0));
        }
        else if (id == "kitGroup")
            layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { spec.id, 1 }, spec.name, groups, 0, fixed));
        else if (id == "kitMic")
            layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { spec.id, 1 }, spec.name,
                juce::StringArray { "Main", "Secondary", "Left", "Right" }, 0, fixed));
        else if (id == "kitMode")
            layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { spec.id, 1 }, spec.name,
                juce::StringArray { "Analyze only", "Suggest", "Auto" }, 1, fixed));
        else if (spec.toggle)
            layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { spec.id, 1 }, spec.name, spec.initial > 0));
        else
            layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { spec.id, 1 }, spec.name,
                juce::NormalisableRange<float> { spec.minimum, spec.maximum, 0.01f, spec.skew }, spec.initial,
                juce::AudioParameterFloatAttributes().withLabel(spec.unit).withAutomatable(id != "kitStrength")));
    }
    return layout;
}

void ZeroEffortDrumProcessor::prepareToPlay(double sampleRate, int maximumBlockSize)
{
    spectrumCapture.reset();
    engine.prepare(sampleRate, maximumBlockSize, getTotalNumOutputChannels());
    preparedRate = sampleRate;
    prepareToPlayForARA(sampleRate, maximumBlockSize, getTotalNumOutputChannels(), getProcessingPrecision());
}

void ZeroEffortDrumProcessor::releaseResources()
{
    releaseResourcesForARA();
}

bool ZeroEffortDrumProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo())
        && output == layouts.getMainInputChannelSet();
}

void ZeroEffortDrumProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    std::optional<juce::int64> timeline;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
            if (position->getIsPlaying())
                if (const auto samples = position->getTimeInSamples())
                    timeline = *samples;
    const auto capture = [this, &timeline](const juce::AudioBuffer<float>& source, int samples) {
        if (kitMember == nullptr) return;
        if (!timeline.has_value() || samples <= 0) kitMember->markIdle();
        else kitMember->write(source.getArrayOfReadPointers(), source.getNumChannels(), samples, *timeline,
            getSampleRate() > 0 ? getSampleRate() : preparedRate);
    };
    // Kit alignment is the first stage of DrumEngine: per instance in insert, per ARA region.
    if (!processBlockForARA(buffer, isNonRealtime() ? Realtime::no : Realtime::yes, getPlayHead()))
    {
        capture(buffer, buffer.getNumSamples());
        autoCapture.push(buffer, getSampleRate());
        drum::Settings settings {};
        for (size_t index = 0; index < settings.size(); ++index)
            settings[index] = controls[index]->load(std::memory_order_relaxed);
        engine.process(buffer, settings);
    }
    else if (isPlaybackRenderer())
    {
        if (const auto* renderer = getPlaybackRenderer<PrototypeRenderer>())
            capture(renderer->rawMix(), renderer->rawSamples());
    }
    if (!isBoundToARA() || isPlaybackRenderer())
        spectrumCapture.push(buffer, getSpectrumMailbox(), getSampleRate());
}


namespace
{
std::vector<DrumModification*> selectedModifications(const ZeroEffortDrumProcessor& processor)
{
    std::vector<DrumModification*> result;
    if (auto* view = processor.getEditorView())
        for (auto* region : view->getViewSelection().getEffectivePlaybackRegions())
        {
            auto* modification = static_cast<DrumModification*>(region->getAudioModification());
            if (std::find(result.begin(), result.end(), modification) == result.end())
                result.push_back(modification);
        }
    return result;
}

std::vector<DrumModification*> recalledModifications(const ZeroEffortDrumProcessor& processor,
                                                   const std::vector<juce::String>& identifiers)
{
    std::vector<DrumModification*> result;
    if (auto* controller = processor.getDocumentController())
        for (auto* source : controller->getDocument()->getAudioSources())
            for (auto* modification : source->getAudioModifications())
                if (!modification->isDeactivatedForUndoHistory() && !modification->getPlaybackRegions().empty()
                    && std::find(identifiers.begin(), identifiers.end(), juce::String(modification->getPersistentID())) != identifiers.end())
                    result.push_back(static_cast<DrumModification*>(modification));
    return result;
}

juce::ARAPlaybackRegion* singleAutoRegion(const ZeroEffortDrumProcessor& processor)
{
    if (auto* view = processor.getEditorView())
    {
        const auto& regions = view->getViewSelection().getEffectivePlaybackRegions();
        if (regions.size() == 1) return static_cast<juce::ARAPlaybackRegion*>(regions.front());
    }
    return nullptr;
}
}

juce::String ZeroEffortDrumProcessor::autoTargetKey() const
{
    if (!isBoundToARA()) return "insert";
    if (auto* region = singleAutoRegion(*this))
    {
        const auto* document = juce::ARADocumentControllerSpecialisation::getSpecialisedDocumentController<PrototypeDocument>(getDocumentController());
        return juce::String(region->getAudioModification()->getPersistentID()) + ":"
            + juce::String::toHexString(reinterpret_cast<juce::pointer_sized_int>(region)) + ":"
            + juce::String(document->status->revision.load());
    }
    return {};
}

void ZeroEffortDrumProcessor::startAutoPreset()
{
    cancelAutoPreset();
    auto& session = *autoSession;
    session.ready = session.changed = false;
    session.key = autoTargetKey();
    session.before = getSettings();
    session.style = getAutoStyle();
    session.intensity = getAutoIntensity();
    session.offset = 0;
    session.phase = 0;
    session.radar = drum::AutoRadarView();
    session.radar.role = static_cast<int>(drum::instrumentFromValue(session.before[drum::instrument]));
    session.radar.title = juce::String(drum::instrumentName(drum::instrumentFromValue(session.before[drum::instrument]))) + " / "
        + drum::drumStyleName(session.style) + " " + juce::String(juce::roundToInt(session.intensity * 100)) + " %";
    if (session.key.isEmpty())
    {
        session.text = "Auto / Select a single audio event";
        return;
    }
    if (isBoundToARA())
    {
        auto* region = singleAutoRegion(*this);
        auto* source = region->getAudioModification()->getAudioSource();
        const auto duration = region->getDurationInAudioModificationTime();
        const auto start = region->getStartInAudioModificationTime();
        session.rate = source->getSampleRate();
        if (!source->isSampleAccessEnabled() || source->getChannelCount() < 1 || source->getChannelCount() > 2
            || !std::isfinite(session.rate) || session.rate < 8000 || session.rate > 192000
            || !std::isfinite(duration) || duration < 2 || duration > 3600
            || !std::isfinite(start) || start < 0 || start > 86400
            || std::abs(region->getDurationInPlaybackTime() - duration) > 0.000001)
        {
            session.text = "Auto / Source unavailable, stretched or duration outside 2 s - 1 h";
            return;
        }
        session.total = static_cast<juce::int64>(std::llround(duration * session.rate));
        session.analyser.prepare(session.rate);
        session.text = "Auto / ARA analysis 0 %";
    }
    else
    {
        session.generation = autoCapture.begin();
        session.total = 0;
        session.capturePrepared = false;
        session.text = "Auto / Playback : capture 8 s";
    }
    session.busy = true;
}

void ZeroEffortDrumProcessor::cancelAutoPreset()
{
    autoCapture.cancel();
    if (autoSession->busy) autoSession->text = "Auto / Cancelled";
    autoSession->busy = false;
    autoSession->radar.refused = false;
}

void ZeroEffortDrumProcessor::updateAutoPreset()
{
    auto& session = *autoSession;
    if ((!session.busy && !session.ready && !session.changed)) return;
    if (session.key != autoTargetKey() || getSettings() != (session.changed ? session.applied : session.before))
    {
        cancelAutoPreset();
        session.ready = session.changed = false;
        session.text = "Auto / Selection or settings changed : analyze again";
        return;
    }
    if (!session.busy) return;
    if (isBoundToARA())
    {
        auto* region = singleAutoRegion(*this);
        auto* source = region->getAudioModification()->getAudioSource();
        if (!source->isSampleAccessEnabled())
        {
            cancelAutoPreset();
            session.text = "Auto / Audio access interrupted";
            return;
        }
        juce::ARAAudioSourceReader reader(source);
        const auto start = static_cast<juce::int64>(std::llround(region->getStartInAudioModificationTime() * session.rate));
        const auto count = static_cast<int>(juce::jmin(juce::int64 { 32768 }, session.total - session.offset));
        const auto position = start + session.offset;
        if (position > reader.lengthInSamples || count > reader.lengthInSamples - position
            || !reader.read(&session.scratch, 0, count, position, true, true))
        {
            cancelAutoPreset();
            session.text = "Auto / Source read failed";
            return;
        }
        if (session.phase != 0)
            session.preview.push(session.scratch, count);
        else
        for (int sample = 0; sample < count; ++sample)
        {
            auto value = session.scratch.getSample(0, sample);
            if (source->getChannelCount() == 2)
            {
                const auto other = session.scratch.getSample(1, sample);
                if (!std::isfinite(other) || std::abs(other) > std::abs(value)) value = other;
            }
            session.scratch.setSample(0, sample, value);
        }
        if (session.phase == 0) session.analyser.push(session.scratch.getReadPointer(0), count);
        session.offset += count;
    }
    else
    {
        if (!autoCapture.ready(session.generation))
        {
            // Analyse the samples as they arrive so the radar builds up while the track plays.
            const auto available = autoCapture.available(session.generation);
            if (available > 0 && !session.capturePrepared)
            {
                session.analyser.prepare(autoCapture.sampleRate());
                session.capturePrepared = true;
            }
            if (session.capturePrepared && available > session.offset)
            {
                const auto count = static_cast<int>(juce::jmin(juce::int64 { 32768 }, available - session.offset));
                session.analyser.push(autoCapture.data() + session.offset, count);
                session.offset += count;
                session.radar.source = drum::DrumAutoRadar::measure(session.analyser.analysis());
            }
            session.text = "Auto / Playback : capture " + juce::String(juce::roundToInt(autoCapture.progress() * 100)) + " %";
            session.radar.progress = autoCapture.progress();
            return;
        }
        if (session.total == 0)
        {
            session.total = autoCapture.size();
            if (!session.capturePrepared)
            {
                session.analyser.prepare(autoCapture.sampleRate());
                session.capturePrepared = true;
            }
        }
        const auto count = static_cast<int>(juce::jmin(juce::int64 { 32768 }, session.total - session.offset));
        if (session.phase == 0)
            session.analyser.push(autoCapture.data() + session.offset, count);
        else
        {
            session.scratch.copyFrom(0, 0, autoCapture.data() + session.offset, count);
            juce::AudioBuffer<float> mono(session.scratch.getArrayOfWritePointers(), 1, count);
            session.preview.push(mono, count);
        }
        session.offset += count;
    }
    session.text = juce::String("Auto / ") + (session.phase == 0 ? "Analysis " : session.phase == 1 ? "Calibration " : "Verification ")
        + juce::String(session.total > 0 ? 100.0 * session.offset / session.total : 0.0, 0) + " %";
    session.radar.progress = session.total > 0 ? static_cast<float>(session.offset) / static_cast<float>(session.total) : 0.0f;
    if (session.phase == 0)
        session.radar.source = drum::DrumAutoRadar::measure(session.analyser.analysis());
    else if (session.phase == 2)
        if (const auto* output = session.preview.outputAnalysis()) session.radar.result = drum::DrumAutoRadar::measure(*output);
    if (session.offset == session.total)
    {
        if (session.phase == 0)
        {
            session.proposal = session.analyser.propose(session.before, session.style, session.intensity);
#if ZED_ANALYSIS_EXPORT
            // Development export, never in a production build: runs on the message thread, not in processBlock.
            drum::SnapshotContext context;
            context.original = session.before;
            context.style = session.style;
            context.intensity = session.intensity;
            context.source = isBoundToARA() ? "ara-region" : "live-capture";
            context.licence = juce::SystemStats::getEnvironmentVariable("ZED_ANALYSIS_EXPORT_LICENCE", "private-dev");
            context.channels = isBoundToARA() ? singleAutoRegion(*this)->getAudioModification()->getAudioSource()->getChannelCount() : 1;
            if (const auto exported = drum::DrumAnalysisSnapshot::exportSnapshot(session.analyser.analysis(), context, session.proposal);
                exported.failed())
                juce::Logger::writeToLog("Analysis export failed: " + exported.getErrorMessage());
#endif
        }
        else if (session.phase == 1)
            session.preview.refineThreshold(session.proposal);
        else
            session.preview.finish(session.proposal);
        if (session.proposal.valid && session.phase < 2)
        {
            ++session.phase;
            session.offset = 0;
            const auto sampleRate = isBoundToARA() ? session.rate : autoCapture.sampleRate();
            const auto channels = isBoundToARA() ? singleAutoRegion(*this)->getAudioModification()->getAudioSource()->getChannelCount() : 1;
            auto settings = session.proposal;
            settings.settings[drum::bypass] = 0;
            session.preview.prepare(sampleRate, channels, settings);
            session.radar.phase = static_cast<drum::AutoRadarView::Phase>(session.phase);
            session.radar.progress = 0;
            if (session.phase == 1)
            {
                session.radar.target = drum::DrumAutoRadar::intention(session.analyser.analysis(), session.proposal);
                session.radar.hasTarget = true;
            }
            else
            {
                session.preview.enableOutputAnalysis(sampleRate);
                session.radar.hasResult = true;
            }
            return;
        }
        session.ready = session.proposal.valid;
        session.busy = false;
        session.text = "Auto / " + session.proposal.summary;
        session.radar.phase = drum::AutoRadarView::finished;
        session.radar.progress = 1;
        session.radar.refused = !session.ready;
        if (!session.ready) session.radar.message = session.proposal.summary;
        if (session.ready && session.applyWhenReady) applyAutoPreset();
    }
}

drum::AutoRadarView ZeroEffortDrumProcessor::getAutoRadar() const
{
    auto view = autoSession->radar;
    view.busy = autoSession->busy;
    view.ready = autoSession->ready;
    view.active = view.busy || view.ready || view.refused;
    return view;
}

std::vector<drum::Change> ZeroEffortDrumProcessor::getAutoChanges() const
{
    const auto& session = *autoSession;
    if (!(session.ready || session.busy) || session.radar.phase == drum::AutoRadarView::analysis || !session.proposal.valid) return {};
    return drum::DrumAutoRadar::significantChanges(session.before, session.proposal.settings);
}

void ZeroEffortDrumProcessor::setAutoApplyWhenReady(bool apply) noexcept { autoSession->applyWhenReady = apply; }

bool ZeroEffortDrumProcessor::discardAutoPreset()
{
    auto& session = *autoSession;
    if (!session.ready && !session.radar.refused) return false;
    const bool wasReady = session.ready;
    session.ready = false;
    session.radar.refused = false;
    if (wasReady) session.text = "Auto / Proposal ignored : settings unchanged";
    return true;
}

bool ZeroEffortDrumProcessor::autoPresetBusy() const { return autoSession->busy; }
bool ZeroEffortDrumProcessor::canApplyAutoPreset() const
{
    return autoSession->ready && autoSession->key == autoTargetKey() && getSettings() == autoSession->before;
}
bool ZeroEffortDrumProcessor::canRevertAutoPreset() const
{
    return autoSession->changed && autoSession->key == autoTargetKey() && getSettings() == autoSession->applied;
}
juce::String ZeroEffortDrumProcessor::getAutoPresetText() const { return autoSession->text; }

void ZeroEffortDrumProcessor::setAutoStyle(drum::DrumStyle style, float intensity)
{
    if (style != drum::DrumStyle::natural && style != drum::DrumStyle::rock && style != drum::DrumStyle::metal) return;
    intensity = std::isfinite(intensity) ? juce::jlimit(0.0f, 1.0f, intensity) : 0;
    if (style == getAutoStyle() && intensity == getAutoIntensity()) return;
    cancelAutoPreset();
    autoSession->ready = false;
    autoSession->style = style;
    autoSession->intensity = intensity;
    setControl(drum::autoStyleOption, static_cast<float>(style));
    setControl(drum::autoIntensityOption, intensity * 100);
    autoSession->text = "Auto / " + juce::String(drum::drumStyleName(style)) + " / Analyze";
}
drum::DrumStyle ZeroEffortDrumProcessor::getAutoStyle() const
{
    if (isBoundToARA() && canEditSettings())
        return static_cast<drum::DrumStyle>(juce::jlimit(0, 2, static_cast<int>(getSettings()[drum::autoStyleOption])));
    return autoSession->style;
}
float ZeroEffortDrumProcessor::getAutoIntensity() const
{
    return isBoundToARA() && canEditSettings() ? getSettings()[drum::autoIntensityOption] * 0.01f : autoSession->intensity;
}

void ZeroEffortDrumProcessor::writeSettings(const drum::Settings& settings)
{
    if (isBoundToARA())
    {
        for (auto* modification : selectedModifications(*this))
        {
            modification->settings.store(settings);
            modification->notifyContentChanged(juce::ARAContentUpdateScopes::samplesAreAffected(), true);
        }
    }
    else
    {
        const auto current = getSettings();
        for (size_t index = 0; index < settings.size(); ++index)
            if (settings[index] != current[index])
                setControl(static_cast<int>(index), settings[index]);
    }
}

bool ZeroEffortDrumProcessor::applyAutoPreset()
{
    if (!canApplyAutoPreset()) return false;
    auto settings = autoSession->proposal.settings;
    drum::keepKitSettings(settings, getSettings());
    writeSettings(settings);
    autoSession->applied = getSettings();
    autoSession->ready = false;
    autoSession->changed = true;
    autoSession->text = "Auto / Applied / " + autoSession->proposal.summary;
    return true;
}

bool ZeroEffortDrumProcessor::revertAutoPreset()
{
    if (!canRevertAutoPreset()) return false;
    auto settings = autoSession->before;
    drum::keepKitSettings(settings, getSettings());
    writeSettings(settings);
    autoSession->changed = false;
    autoSession->text = "Auto / Previous settings restored";
    return true;
}

drum::Settings ZeroEffortDrumProcessor::getSettings() const
{
    if (isBoundToARA())
    {
        const auto selected = selectedModifications(*this);
        if (!selected.empty())
        {
            lastEditedIds.clear();
            for (const auto* modification : selected) lastEditedIds.emplace_back(modification->getPersistentID());
            return selected.front()->settings.load();
        }
        auto settings = drum::defaultSettings();
        const auto recalled = recalledModifications(*this, lastEditedIds);
        if (!recalled.empty()) settings[drum::bypass] = recalled.front()->settings.load()[drum::bypass];
        return settings;
    }
    drum::Settings settings {};
    for (size_t index = 0; index < settings.size(); ++index)
        settings[index] = controls[index]->load();
    return settings;
}

bool ZeroEffortDrumProcessor::canEditSettings() const
{
    return !isBoundToARA() || !selectedModifications(*this).empty();
}

bool ZeroEffortDrumProcessor::canBypass() const
{
    return canEditSettings() || !recalledModifications(*this, lastEditedIds).empty();
}

void ZeroEffortDrumProcessor::setControl(int index, float value)
{
    if (index < 0 || index >= drum::controlCount || !std::isfinite(value)) return;
    const auto& spec = drum::controlSpecs[static_cast<size_t>(index)];
    value = juce::jlimit(spec.minimum, spec.maximum, value);
    if (index == drum::instrument) value = static_cast<float>(drum::instrumentFromValue(value));
    if (isBoundToARA())
    {
        auto targets = selectedModifications(*this);
        if (targets.empty() && index == drum::bypass) targets = recalledModifications(*this, lastEditedIds);
        for (auto* modification : targets)
        {
            modification->settings.values[static_cast<size_t>(index)].store(value);
            modification->notifyContentChanged(juce::ARAContentUpdateScopes::samplesAreAffected(), true);
        }
    }
    else if (auto* parameter = parameters.getParameter(spec.id))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        parameter->endChangeGesture();
    }
}

std::array<float, 4> ZeroEffortDrumProcessor::getMeters() const noexcept
{
    if (isBoundToARA())
    {
        const auto selected = selectedModifications(*this);
        if (selected.empty()) return {};
        const auto& meters = selected.front()->meters;
        return { meters[0].load(), meters[1].load(), meters[2].load(), meters[3].load() };
    }
    if (auto* controller = getDocumentController())
    {
        const auto* document = juce::ARADocumentControllerSpecialisation::getSpecialisedDocumentController<PrototypeDocument>(controller);
        return { document->status->meters[0].load(), document->status->meters[1].load(),
                 document->status->meters[2].load(), document->status->meters[3].load() };
    }
    return { engine.inputMeter.load(), engine.outputMeter.load(), engine.gateDisplayMeter.load(), engine.compressionDisplayMeter.load() };
}

drum::SpectrumMailbox& ZeroEffortDrumProcessor::getSpectrumMailbox() noexcept
{
    if (auto* controller = getDocumentController())
        return juce::ARADocumentControllerSpecialisation::getSpecialisedDocumentController<PrototypeDocument>(controller)->status->spectrum;
    return spectrumMailbox;
}

juce::AudioProcessorEditor* ZeroEffortDrumProcessor::createEditor()
{
    return drum::ui::createEditor(*this);
}

void ZeroEffortDrumProcessor::recordHostOutput(float peak, int channels, bool active, bool fallback,
                                             bool previouslySilent) noexcept
{
    if (!isPlaybackRenderer())
        return;
    if (auto* controller = getDocumentController())
    {
        auto* document = juce::ARADocumentControllerSpecialisation::getSpecialisedDocumentController<PrototypeDocument>(controller);
        auto& status = *document->status;
        status.hostOutputCalls.fetch_add(1, std::memory_order_relaxed);
        status.hostOutputPeak.store(juce::jmax(status.hostOutputPeak.load(std::memory_order_relaxed), peak), std::memory_order_relaxed);
        status.hostOutputChannels.store(channels, std::memory_order_relaxed);
        status.hostOutputActive.store(active, std::memory_order_relaxed);
        status.hostOutputFallback.store(fallback, std::memory_order_relaxed);
        status.hostOutputSilent.store(previouslySilent, std::memory_order_relaxed);
    }
}

juce::String ZeroEffortDrumProcessor::getDiagnosticText() const
{
    if (auto* controller = getDocumentController())
    {
        const auto* document = juce::ARADocumentControllerSpecialisation::getSpecialisedDocumentController<PrototypeDocument>(controller);
        const auto& status = *document->status;
        const auto stale = status.playbackState.load() == drum::PlaybackState::stale;
        const auto loaded = status.loadedRegions.load();
        const auto failed = status.failedRegions.load();
        juce::String text = juce::String(isPlaybackRenderer() ? "ARA playback" : "ARA editor")
            + "\nRegions cached: " + juce::String(loaded)
            + "   Unavailable: " + juce::String(failed)
            + "\n" + drum::describeCacheState(loaded, failed, stale)
            + (status.restored.load() ? " / Archive restored" : "");
        text += "\nRender: " + juce::String(status.renderRate.load(), 0) + " Hz / "
            + juce::String(status.renderChannels.load()) + " ch";
        text += "\nCached sources: " + juce::String(status.monoRegions.load()) + " mono / "
            + juce::String(status.stereoRegions.load()) + " stereo";
        text += "\nPlayback: " + juce::String(drum::describePlaybackState(status.playbackState.load()));
        text += "\nCalls: " + juce::String(status.playbackCalls.load())
            + " / playing: " + juce::String(status.playingCalls.load());
        text += "\nHost sample: " + juce::String(status.hostSample.load());
        text += "\nCache samples: " + juce::String(status.firstSample.load())
            + " to " + juce::String(status.endSample.load());
        text += "\nCached peak: " + juce::String(juce::Decibels::gainToDecibels(status.cachedPeak.load(), -160.0f), 1)
            + " dBFS; rendered peak: "
            + juce::String(juce::Decibels::gainToDecibels(status.renderedPeak.load(), -160.0f), 1) + " dBFS";
        text += "\nHost output: " + juce::String(juce::Decibels::gainToDecibels(status.hostOutputPeak.load(), -160.0f), 1)
            + " dBFS / " + juce::String(status.hostOutputChannels.load()) + " ch"
            + " / calls: " + juce::String(status.hostOutputCalls.load());
        text += "\nOutput bus: " + juce::String(status.hostOutputActive.load() ? "active" : "inactive")
            + " / fallback: " + juce::String(status.hostOutputFallback.load() ? "yes" : "no")
            + "\nHost silence flags before check: " + juce::String(status.hostOutputSilent.load() ? "set" : "clear");
        if (failed > 0)
        {
            text += "\nLast rejected: " + juce::String(drum::describeFailure(status.lastFailure.load()));
            text += "\nSource: " + juce::String(status.sourceRate.load(), 0) + " Hz / "
                + juce::String(status.sourceChannels.load()) + " ch";
            text += "\nRegion duration: " + juce::String(status.duration.load(), 3) + " s";
        }
        return text;
    }
    return "VST3 insert / ARA inactive";
}

void ZeroEffortDrumProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    state.setProperty("autoStyle", static_cast<int>(getAutoStyle()), nullptr);
    state.setProperty("autoIntensity", getAutoIntensity(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destination);
}

void ZeroEffortDrumProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(parameters.state.getType()))
        {
            auto state = juce::ValueTree::fromXml(*xml);
            for (size_t index = drum::focusOn; index < drum::controlSpecs.size(); ++index)
            {
                const auto& spec = drum::controlSpecs[index];
                if (!state.getChildWithProperty("id", spec.id).isValid())
                {
                    juce::ValueTree parameter("PARAM");
                    parameter.setProperty("id", spec.id, nullptr);
                    parameter.setProperty("value", spec.initial, nullptr);
                    state.appendChild(parameter, nullptr);
                }
            }
            parameters.replaceState(state);
            const auto style = juce::jlimit(0, 2, static_cast<int>(state.getProperty("autoStyle", 0)));
            setAutoStyle(static_cast<drum::DrumStyle>(style), static_cast<float>(state.getProperty("autoIntensity", 1.0f)));
            kitTick();
        }
}

void ZeroEffortDrumProcessor::updateTrackProperties(const TrackProperties& properties)
{
    trackName = properties.name.value_or(juce::String()).trim();
    kitTick();
}

namespace
{
juce::ARAPlaybackRegion* firstRenderedRegion(const ZeroEffortDrumProcessor& processor)
{
    if (const auto* renderer = processor.getPlaybackRenderer())
    {
        const auto& regions = renderer->getPlaybackRegions();
        if (!regions.empty()) return regions.front();
    }
    return nullptr;
}

juce::ARAPlaybackRegion* firstSelectedRegion(const ZeroEffortDrumProcessor& processor)
{
    if (auto* view = processor.getEditorView())
    {
        const auto& regions = view->getViewSelection().getEffectivePlaybackRegions();
        if (!regions.empty()) return static_cast<juce::ARAPlaybackRegion*>(regions.front());
    }
    return nullptr;
}

// The ARA region sequence is the Cubase track: shared by the editor and playback instances.
juce::String trackKey(juce::ARAPlaybackRegion* region)
{
    return region == nullptr ? juce::String()
        : "ara:" + juce::String::toHexString(reinterpret_cast<juce::pointer_sized_int>(region->getRegionSequence()));
}
}

drum::Settings ZeroEffortDrumProcessor::memberSettings() const
{
    if (isBoundToARA())
        if (auto* region = firstRenderedRegion(*this))
            return static_cast<DrumModification*>(region->getAudioModification())->settings.load();
    return getSettings();
}

juce::String ZeroEffortDrumProcessor::memberKey() const
{
    if (!isBoundToARA()) return "insert:" + juce::String(getKitMemberId());
    if (auto* region = firstRenderedRegion(*this)) return trackKey(region);
    return trackKey(firstSelectedRegion(*this));
}

juce::String ZeroEffortDrumProcessor::selectionKey() const
{
    if (!isBoundToARA()) return memberKey();
    if (auto* region = firstSelectedRegion(*this)) return trackKey(region);
    return memberKey();
}

int ZeroEffortDrumProcessor::currentInstrument() const
{
    return static_cast<int>(drum::instrumentFromValue(memberSettings()[drum::instrument]));
}

float ZeroEffortDrumProcessor::getKitControl(KitControl control) const noexcept
{
    return control >= 0 && control < kitControlCount ? getSettings()[static_cast<size_t>(kitSetting(control))] : 0.0f;
}

void ZeroEffortDrumProcessor::setKitControl(KitControl control, float value)
{
    if (control < 0 || control >= kitControlCount || !std::isfinite(value)) return;
    if (getSettings()[static_cast<size_t>(kitSetting(control))] == value) return;
    setControl(kitSetting(control), value);
    if (control == kitGroup || control == kitMic) kitTick();
}

kit::Mode ZeroEffortDrumProcessor::getKitMode() const noexcept
{
    return static_cast<kit::Mode>(juce::jlimit(0, 2, static_cast<int>(std::lround(getKitControl(kitMode)))));
}

kit::Identity ZeroEffortDrumProcessor::getKitIdentity() const
{
    const auto settings = memberSettings();
    kit::Identity identity;
    identity.group = juce::jlimit(0, kit::groupCount - 1, static_cast<int>(std::lround(settings[drum::kitGroup])));
    identity.instrument = static_cast<int>(drum::instrumentFromValue(settings[drum::instrument]));
    identity.position = juce::jlimit(0, 3, static_cast<int>(std::lround(settings[drum::kitMic])));
    identity.role = kit::roleFor(identity.instrument, identity.position);
    juce::String label = drum::instrumentNames[static_cast<size_t>(identity.instrument)];
    const auto secondary = identity.position == kit::positionSecondary;
    if (identity.instrument == 0) label = secondary ? "Kick Out" : "Kick In";
    if (identity.instrument == 1) label = secondary ? "Snare Bottom" : "Snare Top";
    if (identity.instrument == 7 || identity.instrument == 8)
    {
        const juce::String base = identity.instrument == 7 ? "OH" : "Room";
        label = identity.position == kit::positionLeft ? base + " L" : identity.position == kit::positionRight ? base + " R"
            : identity.instrument == 7 ? juce::String("Overheads") : juce::String("Room");
    }
    identity.name = trackName.isNotEmpty() && trackName != label ? trackName + " (" + label + ")" : label;
    identity.key = memberKey();
    return identity;
}

kit::GroupView ZeroEffortDrumProcessor::getKitView() const
{
    return kitRegistry->view(juce::jlimit(0, kit::groupCount - 1, static_cast<int>(std::lround(getKitControl(kitGroup)))));
}

juce::uint32 ZeroEffortDrumProcessor::getKitMemberId() const noexcept
{
    return kitMember != nullptr ? kitMember->id() : 0;
}

void ZeroEffortDrumProcessor::startKitAnalysis()
{
    kitTick();
    kitRegistry->startAnalysis(juce::jlimit(0, kit::groupCount - 1, static_cast<int>(std::lround(getKitControl(kitGroup)))));
}

void ZeroEffortDrumProcessor::stopKitAnalysis()
{
    kitRegistry->stopAnalysis(juce::jlimit(0, kit::groupCount - 1, static_cast<int>(std::lround(getKitControl(kitGroup)))));
}

const kit::Recommendation* ZeroEffortDrumProcessor::findOwnRecommendation(const kit::Report& report) const
{
    if (const auto* recommendation = report.findTrack(selectionKey())) return recommendation;
    return report.find(getKitMemberId());
}

bool ZeroEffortDrumProcessor::canApplyKitRecommendation() const
{
    if (getKitMode() == kit::Mode::off || !canEditSettings()) return false;
    const auto view = getKitView();
    const auto* recommendation = findOwnRecommendation(view.report);
    return view.report.final && recommendation != nullptr && recommendation->measured;
}

bool ZeroEffortDrumProcessor::applyKitRecommendation()
{
    if (!canApplyKitRecommendation()) return false;
    const auto view = getKitView();
    const auto& recommendation = *findOwnRecommendation(view.report);
    const auto strength = juce::jlimit(0.0f, 1.0f, getKitControl(kitStrength) * 0.01f);
    setControl(drum::kitDelay, recommendation.delayMs * strength);
    setControl(drum::kitPolarity, recommendation.invert && strength > 0 ? 1.0f : 0.0f);
    return true;
}

// Auto mode writes to what this instance actually plays: its own ARA regions, or its parameters.
void ZeroEffortDrumProcessor::writeOwnKit(float delayMs, bool invert)
{
    if (!isBoundToARA())
    {
        setControl(drum::kitDelay, delayMs);
        setControl(drum::kitPolarity, invert ? 1.0f : 0.0f);
        return;
    }
    std::vector<DrumModification*> written;
    if (const auto* renderer = getPlaybackRenderer())
        for (auto* region : renderer->getPlaybackRegions())
        {
            auto* modification = static_cast<DrumModification*>(region->getAudioModification());
            if (std::find(written.begin(), written.end(), modification) != written.end()) continue;
            written.push_back(modification);
            modification->settings.values[drum::kitDelay].store(juce::jlimit(0.0f, kit::maxDelayMs, delayMs));
            modification->settings.values[drum::kitPolarity].store(invert ? 1.0f : 0.0f);
            modification->notifyContentChanged(juce::ARAContentUpdateScopes::samplesAreAffected(), true);
        }
}

void ZeroEffortDrumProcessor::requestKitApply(const std::vector<juce::uint32>& members)
{
    kitRegistry->requestApply(juce::jlimit(0, kit::groupCount - 1, static_cast<int>(std::lround(getKitControl(kitGroup)))), members, false);
    kitTick();
}

void ZeroEffortDrumProcessor::requestKitReset()
{
    const auto view = getKitView();
    std::vector<juce::uint32> members;
    for (const auto& member : view.members) members.push_back(member.id);
    kitRegistry->requestApply(view.group, members, true);
    kitTick();
}

void ZeroEffortDrumProcessor::kitTick()
{
    if (kitMember == nullptr) return;
    const auto identity = getKitIdentity();
    if (!identityPublished || identity != publishedIdentity)
    {
        kitRegistry->setIdentity(*kitMember, identity);
        publishedIdentity = identity;
        identityPublished = true;
    }
    const auto settings = memberSettings();
    const auto amount = juce::jlimit(0.0f, 1.0f, settings[drum::kitStrength] * 0.01f);
    const auto mode = static_cast<int>(std::lround(settings[drum::kitMode]));
    const auto serial = kitRegistry->applySerial(identity.group);
    if (identity.group != kitApplyGroup)
    {
        kitApplyGroup = identity.group;
        kitApplySerial = serial;
    }
    else if (serial != kitApplySerial)
    {
        kitApplySerial = serial;
        const auto request = kitRegistry->applyRequest(identity.group);
        if (std::find(request.members.begin(), request.members.end(), kitMember->id()) != request.members.end())
        {
            if (request.reset)
                writeOwnKit(0.0f, false);
            else if (mode != static_cast<int>(kit::Mode::off))
                if (const auto* recommendation = request.report.find(kitMember->id()))
                    if (recommendation->measured)
                        writeOwnKit(recommendation->delayMs * amount, recommendation->invert && amount > 0);
        }
    }
    if (mode != static_cast<int>(kit::Mode::automatic)) return;
    const auto revision = kitRegistry->revision(identity.group);
    const auto strength = settings[drum::kitStrength];
    if (revision == kitAppliedRevision && strength == kitAppliedStrength) return;
    kitAppliedRevision = revision;
    kitAppliedStrength = strength;
    const auto view = kitRegistry->view(identity.group);
    if (view.report.final && view.report.valid)
        if (const auto* recommendation = view.report.find(kitMember->id()))
            if (recommendation->measured)
                writeOwnKit(recommendation->delayMs * amount, recommendation->invert && amount > 0);
}


juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ZeroEffortDrumProcessor();
}

const ARA::ARAFactory* JUCE_CALLTYPE createARAFactory()
{
    return juce::ARADocumentControllerSpecialisation::createARAFactory<PrototypeDocument>();
}

std::array<float, 3> ZeroEffortDrumProcessor::getBandMeters() const noexcept
{
    if (isBoundToARA())
    {
        const auto selected = selectedModifications(*this);
        if (selected.empty()) return {};
        const auto& meters = selected.front()->bandMeters;
        return { meters[0].load(), meters[1].load(), meters[2].load() };
    }
    if (auto* controller = getDocumentController())
    {
        const auto* document = juce::ARADocumentControllerSpecialisation::getSpecialisedDocumentController<PrototypeDocument>(controller);
        return { document->status->bandMeters[0].load(), document->status->bandMeters[1].load(), document->status->bandMeters[2].load() };
    }
    return { engine.bandReductionMeters[0].load(), engine.bandReductionMeters[1].load(), engine.bandReductionMeters[2].load() };
}

std::array<float, drum::finishMeterCount> ZeroEffortDrumProcessor::getFinishMeters() const noexcept
{
    std::array<float, drum::finishMeterCount> result {};
    if (isBoundToARA())
    {
        const auto selected = selectedModifications(*this);
        if (!selected.empty())
            for (size_t index = 0; index < result.size(); ++index)
                result[index] = selected.front()->finishMeters[index].load(std::memory_order_relaxed);
        return result;
    }
    if (auto* controller = getDocumentController())
    {
        const auto* document = juce::ARADocumentControllerSpecialisation::getSpecialisedDocumentController<PrototypeDocument>(controller);
        for (size_t index = 0; index < result.size(); ++index)
            result[index] = document->status->finishMeters[index].load(std::memory_order_relaxed);
    }
    else
        for (size_t index = 0; index < result.size(); ++index)
            result[index] = engine.finishMeters[index].load(std::memory_order_relaxed);
    return result;
}
