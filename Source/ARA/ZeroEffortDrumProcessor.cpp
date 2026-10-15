#include "ZeroEffortDrumProcessor.h"
#include "../Drum/DrumFilterResponse.h"
#include "RegionCache.h"
#include "CacheValidity.h"
#include "../Drum/DrumArchive.h"
#include "../Drum/DrumAutoPreset.h"
#include "../Drum/DrumPresetPreview.h"
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

class PrototypeEditor final : public juce::AudioProcessorEditor,
                              public juce::AudioProcessorEditorARAExtension,
                              private juce::Timer
{
public:
    explicit PrototypeEditor(ZeroEffortDrumProcessor& processor)
        : AudioProcessorEditor(processor), AudioProcessorEditorARAExtension(&processor), owner(processor)
    {
        look.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff58d1ad));
        look.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff3c4548));
        look.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff657075));
        look.setColour(juce::Slider::trackColourId, juce::Colour(0xff58d1ad));
        look.setColour(juce::Slider::thumbColourId, juce::Colour(0xffe8edeb));
        look.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffe8edeb));
        look.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        look.setColour(juce::ToggleButton::textColourId, juce::Colour(0xffe8edeb));
        look.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff58d1ad));
        setLookAndFeel(&look);
        title.setText("ZeroEffort Drum", juce::dontSendNotification);
        title.setFont(juce::Font(juce::FontOptions("Bahnschrift", 23.0f, juce::Font::bold)));
        addAndMakeVisible(title);
        for (size_t index = 0; index < drum::instrumentNames.size(); ++index)
            instrumentMenu.addItem(drum::instrumentNames[index], static_cast<int>(index) + 1);
        instrumentMenu.setTooltip("Element traite par cette instance. Choisir puis Analyser ; aucun changement DSP avant Appliquer.");
        instrumentMenu.onChange = [this] {
            owner.setControl(drum::instrument, static_cast<float>(instrumentMenu.getSelectedId() - 1));
            owner.cancelAutoPreset();
            timerCallback();
        };
        addAndMakeVisible(instrumentMenu);
        addAndMakeVisible(status);
        addAndMakeVisible(context);
        addAndMakeVisible(diagnostic);
        diagnostic.setButtonText("Diagnostic");
        diagnostic.setClickingTogglesState(true);
        diagnostic.onClick = [this] { resized(); repaint(); };
        diagnostic.setTooltip("Afficher les mesures techniques ARA et la sortie hote");
        addAndMakeVisible(presenceView);
        busView.setButtonText("Bus");
        busView.setClickingTogglesState(true);
        busView.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff58d1ad));
        busView.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff131819));
        busView.onClick = [this] { selectPage(5); };
        busView.setTooltip("Compression de cohesion et petite ambiance. Pour le master : utiliser un insert sur le groupe Drum.");
        addAndMakeVisible(busView);
        for (size_t index = 0; index < finishPages.size(); ++index)
        {
            auto& button = finishPages[index];
            button.setButtonText(index == 0 ? "Impact" : "Precision");
            button.setClickingTogglesState(true);
            button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff58d1ad));
            button.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff131819));
            button.onClick = [this, index] { selectPage(6 + static_cast<int>(index)); };
            addAndMakeVisible(button);
        }
        const std::array<const char*, 16> meterTips {
            "Crush : reduction de la voie parallele, en dB.",
            "EQ dynamique grave : frequence et attenuation mesuree, en dB.",
            "EQ dynamique aigu : frequence et attenuation mesuree, en dB.",
            "Attack/Sustain : plus grand changement de gain du bloc, en dB.",
            "Couleur : crete du residu harmonique a 4x, en dBFS ; pas une reduction de gain.",
            "Clip : crete du residu d'arrondi a 4x, en dBFS ; pas un plafond de sortie.",
            "Room : crete du retour ajoute au direct, avant Output, en dBFS.",
            "Niveau A/B : compensation RMS avant Output, en dB ; pas LUFS.",
            "Glue : reduction mesuree du compresseur bus, en dB.",
            "Multibande grave : reduction mesuree, en dB.",
            "Multibande medium : reduction mesuree, en dB.",
            "Multibande aigu : reduction mesuree, en dB.",
            "Nettoyage Kick : attenuation selective de l'aigu, en dB.",
            "Renfort Kick : crete du corps synthetique, en dBFS.",
            "Room : reduction du compresseur de retour, en dB.",
            "Correlation L/R avant ecoute mono : -1 oppose, +1 similaire. Pas une analyse inter-micros."
        };
        for (size_t index = 0; index < headerIndicators.size(); ++index)
        {
            auto& indicator = headerIndicators[index];
            indicator.setFont(juce::Font(juce::FontOptions("Bahnschrift", 10.0f, juce::Font::plain)));
            indicator.setJustificationType(juce::Justification::centredLeft);
            indicator.setTooltip(meterTips[index]);
            addAndMakeVisible(indicator);
        }
        presenceView.setButtonText("Focus / Presence");
        presenceView.setClickingTogglesState(true);
        presenceView.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff58d1ad));
        presenceView.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff131819));
        presenceView.setTooltip("Reglages du detecteur kick, nettoyage selectif et renfort accorde");
        presenceView.onClick = [this] { selectPage(4); };
        const std::array<const char*, 4> pageNames { "Filtres", "Expander", "EQ", "Compresseur" };
        for (size_t index = 0; index < pages.size(); ++index)
        {
            pages[index].setButtonText(pageNames[index]);
            pages[index].setClickingTogglesState(true);
            pages[index].setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff58d1ad));
            pages[index].setColour(juce::TextButton::textColourOnId, juce::Colour(0xff131819));
            pages[index].setTooltip(juce::String("Reglages / ") + pageNames[index]);
            pages[index].onClick = [this, index] { selectPage(static_cast<int>(index)); };
            addAndMakeVisible(pages[index]);
        }
        spectrumSwitch.setButtonText("Spectre OUT");
        spectrumSwitch.setTooltip("FFT de la sortie : echelle droite en dBFS, canal de plus forte amplitude. Aucun effet audio.");
        spectrumSwitch.onClick = [this] {
            owner.getSpectrumMailbox().subscribers.fetch_add(spectrumSwitch.getToggleState() ? 1 : -1);
            repaint();
        };
        addAndMakeVisible(spectrumSwitch);
        spectrumSwitch.setToggleState(true, juce::dontSendNotification);
        owner.getSpectrumMailbox().subscribers.fetch_add(1);
        for (auto* button : { &analyse, &apply, &revert }) addAndMakeVisible(*button);
        analyse.setButtonText("Analyser");
        apply.setButtonText("Appliquer");
        revert.setButtonText("Revenir");
        analyse.setTooltip("ARA : un evenement. Insert : capturer 8 secondes d'audio entrant.");
        apply.setTooltip("Remplacer les reglages par la proposition prudente");
        revert.setTooltip("Retablir les reglages precedant la derniere application Auto");
        analyse.onClick = [this] {
            if (owner.autoPresetBusy()) owner.cancelAutoPreset(); else owner.startAutoPreset();
            timerCallback();
        };
        apply.onClick = [this] { owner.applyAutoPreset(); timerCallback(); };
        revert.onClick = [this] { owner.revertAutoPreset(); timerCallback(); };
        addAndMakeVisible(autoReport);
        for (size_t index = 0; index < styles.size(); ++index)
        {
            auto& button = styles[index];
            button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff58d1ad));
            button.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff131819));
            button.setButtonText(drum::drumStyleName(static_cast<drum::DrumStyle>(index)));
            button.setTooltip(index == 0 ? "Correction prudente sans coloration imposee" : "Direction sonore volontaire, pas un defaut detecte");
            button.onClick = [this, index] { owner.setAutoStyle(static_cast<drum::DrumStyle>(index), owner.getAutoIntensity()); timerCallback(); };
            addAndMakeVisible(button);
        }
        addAndMakeVisible(intensity);
        intensity.setSliderStyle(juce::Slider::LinearHorizontal);
        intensity.setTextBoxStyle(juce::Slider::TextBoxRight, false, 54, 22);
        intensity.setRange(0, 100, 1);
        intensity.setTextValueSuffix(" %");
        intensity.setTooltip("Intensite du prochain preset : douce jusqu'a 50 %, accentuee vers 100 %. EQ, nettoyage et compression. Aucun effet avant Appliquer.");
        intensity.onValueChange = [this] { owner.setAutoStyle(owner.getAutoStyle(), static_cast<float>(intensity.getValue()) / 100); timerCallback(); };
        addAndMakeVisible(intensityLabel);
        intensityLabel.setText("Intensite", juce::dontSendNotification);
        addAndMakeVisible(details);
        details.setButtonText("Bilan");
        details.setTooltip("Afficher la proposition et les mesures du rendu complet");
        details.onClick = [this] {
            auto report = std::make_unique<juce::TextEditor>();
            report->setMultiLine(true);
            report->setReadOnly(true);
            report->setScrollbarsShown(true);
            report->setText(owner.getAutoPresetText());
            report->setSize(780, 570);
            juce::DialogWindow::LaunchOptions options;
            options.dialogTitle = "Auto / Bilan";
            options.dialogBackgroundColour = juce::Colour(0xff202729);
            options.content.setOwned(report.release());
            options.componentToCentreAround = this;
            options.useNativeTitleBar = true;
            options.resizable = true;
            options.launchAsync();
        };
        autoReport.setFont(juce::Font(juce::FontOptions(11.0f)));
        autoReport.setColour(juce::Label::textColourId, juce::Colour(0xffa4b3b5));
        status.setFont(juce::Font(juce::FontOptions(13.0f)));
        status.setColour(juce::Label::backgroundColourId, juce::Colour(0xff202729));
        for (size_t index = 0; index < drum::controlSpecs.size(); ++index)
        {
            const auto& spec = drum::controlSpecs[index];
            if (spec.toggle)
            {
                toggles[index].setButtonText(spec.name);
                toggles[index].onClick = [this, index] {
                    owner.setControl(static_cast<int>(index), toggles[index].getToggleState() ? 1.0f : 0.0f);
                    if (index == drum::multibandOn) { resized(); repaint(); }
                };
                toggles[index].setTooltip(juce::String(spec.name) + " : activer / desactiver");
                addAndMakeVisible(toggles[index]);
            }
            else
            {
                sliders[index].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                sliders[index].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 86, 22);
                sliders[index].setRange(spec.minimum, spec.maximum, 0.01);
                sliders[index].setSkewFactor(spec.skew);
                sliders[index].setTextValueSuffix(juce::String(" ") + spec.unit);
                sliders[index].setDoubleClickReturnValue(true, spec.initial);
                sliders[index].onValueChange = [this, index] { owner.setControl(static_cast<int>(index), static_cast<float>(sliders[index].getValue())); };
                sliders[index].setTooltip(juce::String(spec.name) + " / " + spec.unit);
                if (index >= drum::bodyQ && index <= drum::airQ)
                {
                    sliders[index].textFromValueFunction = [](double value) {
                        return value <= 0 ? juce::String("Global") : juce::String(value, 2);
                    };
                    sliders[index].valueFromTextFunction = [](const juce::String& text) {
                        return text.equalsIgnoreCase("Global") ? 0.0 : text.getDoubleValue();
                    };
                    sliders[index].setTooltip(juce::String(spec.name) + " / 0 = Q global historique, double-clic pour heriter");
                    sliders[index].updateText();
                }
                labels[index].setText(spec.name, juce::dontSendNotification);
                labels[index].setJustificationType(juce::Justification::centred);
                labels[index].setFont(juce::Font(juce::FontOptions(12.0f)));
                addAndMakeVisible(sliders[index]);
                addAndMakeVisible(labels[index]);
            }
        }
        setSize(1020, 760);
        selectPage(2);
        timerCallback();
        startTimerHz(20);
    }

    ~PrototypeEditor() override
    {
        if (spectrumSwitch.getToggleState()) owner.getSpectrumMailbox().subscribers.fetch_sub(1);
        owner.cancelAutoPreset();
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(juce::Colour(0xff181e20));
        graphics.setColour(juce::Colour(0xff283235));
        graphics.fillRect(0, 76, getWidth(), 270);
        graphics.setColour(juce::Colour(0xff3c474a));
        for (int row : { 76, 346, 390, 722 })
            graphics.drawHorizontalLine(row, 20.0f, static_cast<float>(getWidth() - 20));
        graphics.saveState();
        graphics.addTransform(juce::AffineTransform::translation(0, -44));
        const juce::Rectangle<float> plot(64, 156, 664, 156);
        graphics.setFont(11.0f);
        const auto gainY = [&](double decibels) {
            return plot.getCentreY() - static_cast<float>(juce::jlimit(-18.0, 18.0, decibels)) * plot.getHeight() / 36;
        };
        for (int decibels : { -18, -12, -6, 0, 6, 12, 18 })
        {
            graphics.setColour(juce::Colour(0xff3c474a));
            graphics.drawHorizontalLine(static_cast<int>(gainY(decibels)), plot.getX(), plot.getRight());
            graphics.setColour(juce::Colour(0xffb8c6c8));
            graphics.drawText(juce::String(decibels > 0 ? "+" : "") + juce::String(decibels),
                22, static_cast<int>(gainY(decibels)) - 8, 34, 16, juce::Justification::centredRight);
        }
        graphics.drawText("EQ dB", 22, 132, 44, 18, juce::Justification::centredLeft);
        graphics.drawText("OUT dBFS", 700, 132, 76, 18, juce::Justification::centredRight);
        for (int level : { 0, -20, -40, -60, -80, -100 })
            graphics.drawText(juce::String(level), 734,
                static_cast<int>(plot.getY() - level * plot.getHeight() / 100) - 8,
                40, 16, juce::Justification::centredLeft);
        for (float frequency : { 30.0f, 60.0f, 100.0f, 300.0f, 1000.0f, 3000.0f, 10000.0f })
        {
            const auto horizontal = plot.getX() + plot.getWidth() * std::log(frequency / 20.0f) / std::log(1000.0f);
            graphics.setColour(juce::Colour(0xff3c474a));
            graphics.drawVerticalLine(static_cast<int>(horizontal), plot.getY(), plot.getBottom());
            graphics.setColour(juce::Colour(0xffa4b3b5));
            graphics.drawText(frequency < 1000 ? juce::String(frequency, 0) : juce::String(frequency / 1000, 0) + "k",
                static_cast<int>(horizontal) - 20, 316, 40, 16, juce::Justification::centred);
        }
        graphics.setColour(juce::Colour(0xff748084));
        graphics.drawHorizontalLine(static_cast<int>(plot.getCentreY()), plot.getX(), plot.getRight());
        const auto settings = owner.getSettings();
        std::array<juce::dsp::IIR::Coefficients<float>::Ptr, 4> response;
        const std::array<std::pair<int, int>, 4> responseBands {{ { drum::bodyFrequency, drum::bodyGain },
            { drum::boxFrequency, drum::boxGain }, { drum::attackFrequency, drum::attackGain }, { drum::airFrequency, drum::airGain } }};
        const auto sampleRate = owner.getSampleRate() > 0 ? owner.getSampleRate() : 48000.0;
        const drum::DrumFilterResponse filterResponse(settings, sampleRate);
        const drum::DrumDynamicResponse dynamicResponse(settings, sampleRate, { liveMeters[1], liveMeters[2] });
        const auto filterColour = juce::Colour(0xffe7c76b);
        graphics.setColour(filterResponse.enabled ? filterColour : juce::Colour(0xff748084));
        const auto frequencyText = [](double frequency)
        {
            return frequency < 1000 ? juce::String(frequency, 0) + " Hz"
                : juce::String(frequency / 1000, 1) + " kHz";
        };
        const auto stateText = filterResponse.enabled ? " / 12 dB/oct" : " / 12 dB/oct / OFF";
        graphics.drawText("HP " + frequencyText(filterResponse.highCutoff) + stateText,
            80, 132, 200, 18, juce::Justification::centredLeft);
        graphics.setColour(filterResponse.enabled ? juce::Colour(0xffe8909a) : juce::Colour(0xff748084));
        graphics.drawText("LP " + frequencyText(filterResponse.lowCutoff) + stateText,
            296, 132, 200, 18, juce::Justification::centredLeft);
        if (filterResponse.enabled)
            for (double cutoff : { filterResponse.highCutoff, filterResponse.lowCutoff })
            {
                const auto horizontal = plot.getX() + plot.getWidth()
                    * static_cast<float>(std::log(cutoff / 20.0) / std::log(1000.0));
                if (horizontal >= plot.getX() && horizontal <= plot.getRight())
                {
                    graphics.setColour(filterColour.withAlpha(0.45f));
                    graphics.drawLine(horizontal, plot.getY(), horizontal, plot.getBottom(), 1.0f);
                }
            }
        for (size_t index = 0; index < response.size(); ++index)
            response[index] = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate,
                juce::jmin(settings[static_cast<size_t>(responseBands[index].first)], static_cast<float>(sampleRate * 0.45)),
                drum::bandQ(settings, index), juce::Decibels::decibelsToGain(settings[static_cast<size_t>(responseBands[index].second)]));
        juce::Path curve, highCurve, lowCurve, spectrumCurve, dynamicCurve, dynamicArea;
        std::array<float, 665> staticY {};
        for (int pixel = 0; pixel <= 664; ++pixel)
        {
            const auto frequency = 20.0 * std::pow(1000.0, static_cast<double>(pixel) / 664.0);
            const auto filteredMagnitude = filterResponse.magnitude(frequency);
            double magnitude = filteredMagnitude;
            if (settings[drum::eqOn] > 0.5f)
                for (const auto& coefficients : response)
                    magnitude *= coefficients->getMagnitudeForFrequency(juce::jmin(frequency, sampleRate * 0.49), sampleRate);
            const auto decibels = juce::Decibels::gainToDecibels(magnitude, -24.0);
            const auto vertical = gainY(decibels);
            staticY[static_cast<size_t>(pixel)] = vertical;
            const auto liveY = gainY(juce::Decibels::gainToDecibels(magnitude * dynamicResponse.magnitude(frequency), -24.0));
            if (pixel == 0)
            {
                dynamicCurve.startNewSubPath(plot.getX(), liveY);
                dynamicArea.startNewSubPath(plot.getX(), liveY);
            }
            else
            {
                dynamicCurve.lineTo(plot.getX() + pixel, liveY);
                dynamicArea.lineTo(plot.getX() + pixel, liveY);
            }
            if (pixel == 0) curve.startNewSubPath(plot.getX(), vertical);
            else curve.lineTo(plot.getX() + static_cast<float>(pixel), vertical);
            const auto highY = gainY(juce::Decibels::gainToDecibels(filterResponse.highMagnitude(frequency), -100.0));
            const auto lowY = gainY(juce::Decibels::gainToDecibels(filterResponse.lowMagnitude(frequency), -100.0));
            const auto spectrumY = plot.getY() - juce::jlimit(-100.0f, 0.0f, spectrumDisplay.magnitudeDb(frequency))
                * plot.getHeight() / 100;
            if (pixel == 0)
            {
                highCurve.startNewSubPath(plot.getX(), highY);
                lowCurve.startNewSubPath(plot.getX(), lowY);
                spectrumCurve.startNewSubPath(plot.getX(), spectrumY);
            }
            else
            {
                highCurve.lineTo(plot.getX() + pixel, highY);
                lowCurve.lineTo(plot.getX() + pixel, lowY);
                spectrumCurve.lineTo(plot.getX() + pixel, spectrumY);
            }
        }
        if (spectrumSwitch.getToggleState())
        {
            graphics.setColour(juce::Colour(0xff829dc1).withAlpha(0.6f));
            graphics.strokePath(spectrumCurve, juce::PathStrokeType(1.0f));
            auto spectrumFill = spectrumCurve;
            spectrumFill.lineTo(plot.getRight(), plot.getBottom());
            spectrumFill.lineTo(plot.getX(), plot.getBottom());
            spectrumFill.closeSubPath();
            graphics.setColour(juce::Colour(0xff829dc1).withAlpha(0.13f));
            graphics.fillPath(spectrumFill);
        }
        if (filterResponse.enabled)
        {
            graphics.setColour(filterColour.withAlpha(0.6f));
            graphics.strokePath(highCurve, juce::PathStrokeType(1.4f));
            graphics.setColour(juce::Colour(0xffe8909a).withAlpha(0.6f));
            graphics.strokePath(lowCurve, juce::PathStrokeType(1.4f));
            for (const auto& filterCurve : { highCurve, lowCurve })
            {
                auto fill = filterCurve;
                fill.lineTo(plot.getRight(), plot.getCentreY());
                fill.lineTo(plot.getX(), plot.getCentreY());
                fill.closeSubPath();
                graphics.setColour(filterColour.withAlpha(0.07f));
                graphics.fillPath(fill);
            }
        }
        graphics.setColour(juce::Colour(0xff58d1ad));
        graphics.strokePath(curve, juce::PathStrokeType(2.0f));
        graphics.drawText(settings[drum::eqOn] > 0.5f ? "EQ + HP/LP" : "EQ OFF + HP/LP", 510, 132, 110, 18, juce::Justification::centredLeft);
        if (dynamicResponse.enabled)
        {
            for (int pixel = 664; pixel >= 0; --pixel)
                dynamicArea.lineTo(plot.getX() + pixel, staticY[static_cast<size_t>(pixel)]);
            dynamicArea.closeSubPath();
            graphics.setColour(juce::Colour(0xfff3ac62).withAlpha(0.2f));
            graphics.fillPath(dynamicArea);
            graphics.setColour(juce::Colour(0xfff3ac62));
            graphics.strokePath(dynamicCurve, juce::PathStrokeType(2.0f));
            graphics.drawText("DYN LIVE", 622, 132, 76, 18, juce::Justification::centredRight);
        }
        if (hoverX >= plot.getX() && hoverX <= plot.getRight())
        {
            const auto frequency = 20.0 * std::pow(1000.0, (hoverX - plot.getX()) / plot.getWidth());
            auto magnitude = filterResponse.magnitude(frequency);
            if (settings[drum::eqOn] > 0.5f)
                for (const auto& coefficients : response)
                    magnitude *= coefficients->getMagnitudeForFrequency(juce::jmin(frequency, sampleRate * 0.49), sampleRate);
            magnitude *= dynamicResponse.magnitude(frequency);
            graphics.setColour(juce::Colour(0xffd8e3e1).withAlpha(0.5f));
            graphics.drawVerticalLine(static_cast<int>(hoverX), plot.getY(), plot.getBottom());
            graphics.setColour(juce::Colour(0xffd8e3e1));
            graphics.drawText(frequencyText(frequency) + " / EQ + HP/LP + DYN "
                + juce::String(juce::Decibels::gainToDecibels(magnitude, -100.0), 2) + " dB"
                + (spectrumSwitch.getToggleState() ? " / OUT " + juce::String(spectrumDisplay.magnitudeDb(frequency), 1) + " dBFS" : ""),
                64, 334, 680, 16, juce::Justification::centredLeft);
        }
        const auto meters = liveMain;
        const std::array<juce::String, 4> names { "IN", "OUT", settings[drum::gateOn] > 0.5f ? "EXP" : "EXP -",
            settings[drum::compressorOn] + settings[drum::glueOn] > 0.5f ? "COMP" : "CMP -" };
        for (size_t index = 0; index < meters.size(); ++index)
        {
            const auto horizontal = 790 + static_cast<int>(index) * 54;
            const auto decibels = index < 2 ? juce::Decibels::gainToDecibels(meters[index], -60.0f) : meters[index];
            const auto amount = index < 2 ? juce::jlimit(0.0f, 1.0f, (decibels + 60) / 60)
                : juce::jlimit(0.0f, 1.0f, decibels / 12.0f);
            graphics.setColour(juce::Colour(0xff131819));
            graphics.fillRect(horizontal, 164, 18, 144);
            graphics.setColour(index < 2 ? (meters[index] >= 1 ? juce::Colour(0xffef6d70) : juce::Colour(0xff58d1ad)) : juce::Colour(0xffe7c76b));
            const auto height = static_cast<int>(144 * amount);
            graphics.fillRect(horizontal, 308 - height, 18, height);
            graphics.setColour(juce::Colour(0xffd8e3e1));
            graphics.drawText(names[index], horizontal - 10, 132, 40, 18, juce::Justification::centred);
            graphics.drawText(juce::String(decibels, 1), horizontal - 16, 318, 50, 18, juce::Justification::centred);
            if (index >= 2)
            {
                graphics.setFont(9.0f);
                graphics.drawText("12", horizontal + 22, 160, 24, 12, juce::Justification::centredLeft);
                graphics.drawText("6", horizontal + 22, 232, 24, 12, juce::Justification::centredLeft);
                graphics.drawText("0", horizontal + 22, 298, 24, 12, juce::Justification::centredLeft);
                graphics.setFont(11.0f);
            }
        }
        graphics.restoreState();
        for (size_t index = 0; index < headerIndicators.size(); ++index)
        {
            const auto horizontal = 24 + static_cast<int>(index % 8) * 122;
            const auto vertical = 323 + static_cast<int>(index / 8) * 20;
            graphics.setColour(juce::Colour(0xff3c474a));
            graphics.fillRect(horizontal, vertical, 114, 2);
            graphics.setColour(index == 1 || index == 2 ? juce::Colour(0xfff3ac62) : juce::Colour(0xff58d1ad));
            graphics.fillRect(horizontal, vertical, static_cast<int>(114 * activityLevels[index]), 2);
        }
        if (activePage == 3 && owner.getSettings()[drum::multibandOn] > 0.5f && !diagnostic.getToggleState())
        {
            const auto reductions = owner.getBandMeters();
            for (size_t band = 0; band < 3; ++band)
            {
                const auto horizontal = 24 + static_cast<int>(band) * 282;
                graphics.setColour(juce::Colour(0xff3c474a));
                graphics.fillRect(horizontal + 142, 484, 112, 4);
                graphics.setColour(juce::Colour(0xffe7c76b));
                graphics.fillRect(horizontal + 142, 484, static_cast<int>(112 * juce::jlimit(0.0f, 1.0f, reductions[band] / 12)), 4);
                graphics.drawText("GR " + juce::String(reductions[band], 1) + " dB", horizontal + 142, 464, 112, 18,
                    juce::Justification::centredRight);
            }
        }
        graphics.setColour(juce::Colour(0xffa4b3b5));
        graphics.drawText("v" JucePlugin_VersionString, getWidth() - 120, 725, 96, 24, juce::Justification::centredRight);
    }

    void resized() override
    {
        title.setBounds(24, 12, 274, 30);
        instrumentMenu.setBounds(24, 44, 160, 28);
        context.setBounds(188, 48, 110, 22);
        for (size_t index = 0; index < styles.size(); ++index)
            styles[index].setBounds(310 + static_cast<int>(index) * 96, 14, 94, 26);
        intensityLabel.setBounds(310, 44, 76, 24);
        intensity.setBounds(386, 44, 276, 24);
        diagnostic.setBounds(860, 14, 132, 26);
        for (size_t index = 0; index < pages.size(); ++index)
            pages[index].setBounds(24 + static_cast<int>(index) * 110, 354, 106, 28);
        presenceView.setBounds(464, 354, 146, 28);
        busView.setBounds(614, 354, 66, 28);
        finishPages[0].setBounds(684, 354, 86, 28);
        finishPages[1].setBounds(774, 354, 90, 28);
        for (size_t index = 0; index < headerIndicators.size(); ++index)
            headerIndicators[index].setBounds(24 + static_cast<int>(index % 8) * 122,
                306 + static_cast<int>(index / 8) * 20, 116, 16);
        spectrumSwitch.setButtonText("Spectre");
        spectrumSwitch.setBounds(672, 48, 92, 24);
        analyse.setBounds(24, 728, 96, 26);
        apply.setBounds(128, 728, 96, 26);
        revert.setBounds(232, 728, 88, 26);
        details.setBounds(328, 728, 64, 26);
        autoReport.setBounds(400, 725, 478, 32);
        status.setBounds(24, 396, 830, 316);
        std::array<bool, drum::controlCount> shown {};
        const auto place = [this, &shown](int control, int horizontal, int vertical, int width = 154)
        {
            const auto index = static_cast<size_t>(control);
            shown[index] = true;
            labels[index].setBounds(horizontal, vertical, width, 18);
            sliders[index].setBounds(horizontal, vertical + 18, width, 78);
        };
        const auto toggle = [this, &shown](int control, int horizontal, int vertical, int width = 190)
        {
            shown[static_cast<size_t>(control)] = true;
            toggles[static_cast<size_t>(control)].setBounds(horizontal, vertical, width, 26);
        };
        toggles[drum::bypass].setBounds(714, 14, 120, 26);
        shown[drum::bypass] = true;
        toggles[drum::levelMatchOn].setBounds(766, 48, 140, 24);
        toggles[drum::levelMatchOn].setTooltip("Compensation RMS progressive +/-6 dB avant Output. Comparer avec Bypass ; ni LUFS ni limiteur.");
        shown[drum::levelMatchOn] = true;
        toggles[drum::monitorMonoOn].setBounds(908, 48, 86, 24);
        toggles[drum::monitorMonoOn].setTooltip("Ecoute mono de la sortie. Aucun alignement automatique entre micros.");
        shown[drum::monitorMonoOn] = true;
        place(drum::input, 876, 418, 118);
        place(drum::output, 876, 544, 118);
        if (activePage == 0)
        {
            toggle(drum::filtersOn, 24, 396);
            place(drum::highPass, 100, 452, 230);
            place(drum::lowPass, 430, 452, 230);
        }
        else if (activePage == 1)
        {
            toggle(drum::gateOn, 24, 396);
            const std::array<int, 5> gate { drum::gateThreshold, drum::gateRatio, drum::gateRange, drum::gateAttack, drum::gateRelease };
            for (size_t index = 0; index < gate.size(); ++index)
                place(gate[index], 24 + static_cast<int>(index % 3) * 250, 450 + static_cast<int>(index / 3) * 126, 210);
        }
        else if (activePage == 2)
        {
            toggle(drum::eqOn, 24, 396);
            const std::array<int, 4> frequencies { drum::bodyFrequency, drum::boxFrequency, drum::attackFrequency, drum::airFrequency };
            const std::array<int, 4> gains { drum::bodyGain, drum::boxGain, drum::attackGain, drum::airGain };
            const std::array<int, 4> qualities { drum::bodyQ, drum::boxQ, drum::attackQ, drum::airQ };
            for (size_t index = 0; index < frequencies.size(); ++index)
            {
                const auto horizontal = 24 + static_cast<int>(index) * 182;
                place(frequencies[index], horizontal, 428);
                place(gains[index], horizontal, 526);
                place(qualities[index], horizontal, 624);
            }
            place(drum::eqQ, 752, 624, 108);
        }
        else if (activePage == 3)
        {
            toggle(drum::compressorOn, 24, 396);
            toggle(drum::multibandOn, 230, 396);
            if (owner.getSettings()[drum::multibandOn] > 0.5f)
            {
                const std::array<int, 4> globals { drum::crossoverLow, drum::crossoverHigh, drum::makeup, drum::mix };
                for (size_t index = 0; index < globals.size(); ++index)
                {
                    const auto control = static_cast<size_t>(globals[index]);
                    shown[control] = true;
                    labels[control].setBounds(24 + static_cast<int>(index) * 210, 424, 196, 18);
                    sliders[control].setSliderStyle(juce::Slider::LinearHorizontal);
                    sliders[control].setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
                    sliders[control].setBounds(24 + static_cast<int>(index) * 210, 442, 196, 24);
                }
                for (size_t band = 0; band < 3; ++band)
                {
                    const auto base = static_cast<int>(drum::lowThreshold) + static_cast<int>(band) * 7;
                    toggle(base + 6, 24 + static_cast<int>(band) * 282, 472, 134);
                    for (int control = 0; control < 6; ++control)
                    {
                        place(base + control, 24 + static_cast<int>(band) * 282 + (control % 2) * 132,
                            504 + (control / 2) * 70, 126);
                        sliders[static_cast<size_t>(base + control)].setSliderStyle(juce::Slider::LinearHorizontal);
                        sliders[static_cast<size_t>(base + control)].setTextBoxStyle(juce::Slider::TextBoxRight, false, 76, 22);
                        sliders[static_cast<size_t>(base + control)].setBounds(24 + static_cast<int>(band) * 282 + (control % 2) * 132,
                            526 + (control / 2) * 70, 126, 32);
                    }
                }
            }
            else
            {
                for (const auto control : { drum::makeup, drum::mix })
                {
                    sliders[static_cast<size_t>(control)].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
                    sliders[static_cast<size_t>(control)].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 86, 22);
                }
                const std::array<int, 6> compressor { drum::threshold, drum::ratio, drum::attack, drum::release, drum::makeup, drum::mix };
                for (size_t index = 0; index < compressor.size(); ++index)
                    place(compressor[index], 24 + static_cast<int>(index % 3) * 250, 450 + static_cast<int>(index / 3) * 126, 210);
            }
        }
        else if (activePage == 5)
        {
            toggle(drum::glueOn, 24, 396);
            toggle(drum::ambienceOn, 562, 396);
            const std::array<int, 9> glue { drum::glueThreshold, drum::glueRatio, drum::glueAttack,
                drum::glueRelease, drum::glueMakeup, drum::glueMix, drum::glueSidechain, drum::glueKnee, drum::glueRange };
            for (size_t index = 0; index < glue.size(); ++index)
            {
                const auto horizontal = 24 + static_cast<int>(index % 3) * 172;
                const auto vertical = 434 + static_cast<int>(index / 3) * 92;
                place(glue[index], horizontal, vertical, 160);
                sliders[static_cast<size_t>(glue[index])].setBounds(horizontal, vertical + 18, 160, 70);
            }
            const std::array<int, 5> room { drum::ambienceAmount, drum::ambienceSize, drum::ambiencePredelay,
                drum::ambienceHighPass, drum::ambienceLowPass };
            for (size_t index = 0; index < room.size(); ++index)
            {
                const auto horizontal = 562 + static_cast<int>(index % 2) * 146;
                const auto vertical = 434 + static_cast<int>(index / 2) * 66;
                place(room[index], horizontal, vertical, 140);
                sliders[static_cast<size_t>(room[index])].setSliderStyle(juce::Slider::LinearHorizontal);
                sliders[static_cast<size_t>(room[index])].setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 22);
                sliders[static_cast<size_t>(room[index])].setBounds(horizontal, vertical + 18, 140, 32);
            }
            for (int control = 0; control < 2; ++control)
            {
                const auto index = drum::ambienceCharacter + control;
                const auto horizontal = 562 + ((5 + control) % 2) * 146;
                const auto vertical = 434 + ((5 + control) / 2) * 66;
                place(index, horizontal, vertical, 140);
                sliders[static_cast<size_t>(index)].setSliderStyle(juce::Slider::LinearHorizontal);
                sliders[static_cast<size_t>(index)].setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 22);
                sliders[static_cast<size_t>(index)].setBounds(horizontal, vertical + 18, 140, 32);
            }
        }
        else if (activePage == 6)
        {
            toggle(drum::crushOn, 24, 396);
            toggle(drum::colourOn, 304, 396);
            toggle(drum::clipOn, 584, 396);
            const std::array<int, 11> controls { drum::crushAmount, drum::crushThreshold, drum::crushRatio,
                drum::crushAttack, drum::crushRelease, drum::crushHP, drum::crushLP,
                drum::colourDrive, drum::colourMix, drum::clipCeiling, drum::clipMix };
            for (size_t index = 0; index < controls.size(); ++index)
            {
                const auto horizontal = 24 + static_cast<int>(index % 4) * 210;
                const auto vertical = 428 + static_cast<int>(index / 4) * 88;
                place(controls[index], horizontal, vertical, 196);
                sliders[static_cast<size_t>(controls[index])].setBounds(horizontal, vertical + 18, 196, 66);
            }
        }
        else if (activePage == 7)
        {
            toggle(drum::shapeOn, 24, 396);
            toggle(drum::dynamicOn, 304, 396);
            toggle(drum::autoMeasuredOn, 584, 396);
            const std::array<int, 10> controls { drum::shapeAttack, drum::shapeSustain,
                drum::dynamicLowHz, drum::dynamicLowQ, drum::dynamicLowThreshold, drum::dynamicLowRange,
                drum::dynamicHighHz, drum::dynamicHighQ, drum::dynamicHighThreshold, drum::dynamicHighRange };
            for (size_t index = 0; index < controls.size(); ++index)
                place(controls[index], 24 + static_cast<int>(index % 5) * 168,
                    434 + static_cast<int>(index / 5) * 126, 156);
        }
        else
        {
            toggle(drum::focusOn, 24, 396);
            toggle(drum::cleanupOn, 634, 396);
            toggle(drum::presenceOn, 24, 550);
            toggle(drum::reinforceInvert, 634, 550);
            const std::array<int, 5> detection { drum::focusLow, drum::focusHigh, drum::gateHold,
                drum::gateHysteresis, drum::cleanupDepth };
            const std::array<int, 5> enhancement { drum::reinforceFrequency, drum::reinforceAmount,
                drum::reinforceDecay, drum::transientAmount, drum::saturationDrive };
            for (size_t index = 0; index < detection.size(); ++index)
            {
                place(detection[index], 24 + static_cast<int>(index) * 164, 434, 152);
                place(enhancement[index], 24 + static_cast<int>(index) * 164, 592, 152);
            }
        }
        for (size_t index = 0; index < drum::controlSpecs.size(); ++index)
        {
            const auto visible = index == drum::bypass || index == drum::input || index == drum::output || (!diagnostic.getToggleState()
                && shown[index]);
            sliders[index].setVisible(visible && !drum::controlSpecs[index].toggle);
            labels[index].setVisible(visible && !drum::controlSpecs[index].toggle);
            toggles[index].setVisible(visible && drum::controlSpecs[index].toggle);
        }
        status.setVisible(diagnostic.getToggleState());
        if (diagnostic.getToggleState()) status.toFront(false);
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        hoverX = event.position.y >= 112 && event.position.y <= 300 ? event.position.x : -1;
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override { hoverX = -1; repaint(); }

private:
    void selectPage(int page)
    {
        activePage = page;
        diagnostic.setToggleState(false, juce::dontSendNotification);
        for (size_t index = 0; index < pages.size(); ++index)
            pages[index].setToggleState(static_cast<int>(index) == page, juce::dontSendNotification);
        presenceView.setToggleState(page == 4, juce::dontSendNotification);
        busView.setToggleState(page == 5, juce::dontSendNotification);
        for (size_t index = 0; index < finishPages.size(); ++index)
            finishPages[index].setToggleState(page == 6 + static_cast<int>(index), juce::dontSendNotification);
        resized();
        repaint();
    }

    void timerCallback() override
    {
        if (spectrumSwitch.getToggleState()) spectrumDisplay.update(owner.getSpectrumMailbox());
        owner.updateAutoPreset();
        const auto finishMeters = owner.getFinishMeters();
        if (finishMeters[10] != lastMeterFrame) staleMeterTicks = 0;
        else ++staleMeterTicks;
        lastMeterFrame = finishMeters[10];
        for (size_t index = 0; index < styles.size(); ++index)
            styles[index].setToggleState(static_cast<drum::DrumStyle>(index) == owner.getAutoStyle(), juce::dontSendNotification);
        if (!intensity.isMouseButtonDown()) intensity.setValue(owner.getAutoIntensity() * 100, juce::dontSendNotification);
        analyse.setButtonText(owner.autoPresetBusy() ? "Annuler" : "Analyser");
        analyse.setEnabled(owner.autoPresetBusy() || owner.canEditSettings());
        apply.setEnabled(owner.canApplyAutoPreset());
        revert.setEnabled(owner.canRevertAutoPreset());
        const auto reportText = owner.getAutoPresetText();
        const auto detail = reportText.indexOf(7, " / ");
        autoReport.setText((detail >= 0 ? reportText.substring(0, detail)
            : reportText.upToFirstOccurrenceOf("\n", false, false)).substring(0, 75), juce::dontSendNotification);
        autoReport.setTooltip(owner.getAutoPresetText());
        status.setText(owner.getDiagnosticText(), juce::dontSendNotification);
        context.setText(owner.isBoundToARA() ? (owner.canEditSettings() ? "ARA / Selection active" : "ARA / Aucune region selectionnee")
            : "VST3 / Insert", juce::dontSendNotification);
        const auto settings = owner.getSettings();
        const bool bypassed = settings[drum::bypass] > 0.5f;
        const bool streamFresh = staleMeterTicks <= 3;
        const bool fresh = streamFresh && !bypassed;
        for (size_t index = 0; index < liveMeters.size(); ++index)
        {
            const auto target = fresh ? finishMeters[index] : 0.0f;
            liveMeters[index] = std::abs(target) >= std::abs(liveMeters[index]) ? target
                : liveMeters[index] + 0.35f * (target - liveMeters[index]);
        }
        const auto rawBands = owner.getBandMeters();
        const auto rawMain = owner.getMeters();
        for (size_t index = 0; index < liveMain.size(); ++index)
            liveMain[index] = juce::jmax(streamFresh && (index < 2 || !bypassed) ? rawMain[index] : 0.0f,
                liveMain[index] * 0.65f);
        for (size_t band = 0; band < liveBands.size(); ++band)
        {
            const auto target = fresh ? rawBands[band] : 0.0f;
            liveBands[band] = juce::jmax(target, liveBands[band] * 0.65f);
        }
        const auto db = [](float value) { return juce::String(value, 1) + " dB"; };
        const auto peak = [](float value) {
            return value > 0.000001f ? juce::String(static_cast<int>(std::lround(juce::Decibels::gainToDecibels(value))))
                + " dBFS" : juce::String("-- dBFS");
        };
        const auto hz = [](float value) { return value < 1000 ? juce::String(value, 0) : juce::String(value / 1000, 1) + "k"; };
        const auto report = [&](size_t index, const juce::String& name, bool enabled, const juce::String& text, float activity) {
            headerIndicators[index].setText(name + " " + (bypassed ? "BYP" : enabled ? text : "OFF"), juce::dontSendNotification);
            headerIndicators[index].setColour(juce::Label::textColourId, juce::Colour(enabled && !bypassed ? 0xffd8e3e1 : 0xff748084));
            activityLevels[index] = enabled && !bypassed ? juce::jlimit(0.0f, 1.0f, activity) : 0.0f;
        };
        const auto peakActivity = [](float value) { return value > 0.000001f
            ? juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(value) + 72) / 72) : 0.0f; };
        report(0, "CRUSH", settings[drum::crushOn] > 0.5f, db(liveMeters[0]), liveMeters[0] / 12);
        report(1, "DYN G " + hz(settings[drum::dynamicLowHz]), settings[drum::dynamicOn] > 0.5f,
            db(liveMeters[1]), liveMeters[1] / 6);
        report(2, "DYN A " + hz(settings[drum::dynamicHighHz]), settings[drum::dynamicOn] > 0.5f,
            db(liveMeters[2]), liveMeters[2] / 6);
        report(3, "IMPACT", settings[drum::shapeOn] > 0.5f, db(liveMeters[5]), std::abs(liveMeters[5]) / 6);
        report(4, "COULEUR", settings[drum::colourOn] > 0.5f, peak(liveMeters[6]), peakActivity(liveMeters[6]));
        report(5, "CLIP", settings[drum::clipOn] > 0.5f, peak(liveMeters[7]), peakActivity(liveMeters[7]));
        report(6, "ROOM", settings[drum::ambienceOn] > 0.5f, peak(liveMeters[8]), peakActivity(liveMeters[8]));
        report(7, "A/B", settings[drum::levelMatchOn] > 0.5f, db(liveMeters[3]), std::abs(liveMeters[3]) / 6);
        report(8, "GLUE", settings[drum::glueOn] > 0.5f, db(liveMeters[11]), liveMeters[11] / 12);
        for (size_t band = 0; band < liveBands.size(); ++band)
            report(9 + band, band == 0 ? "MB G" : band == 1 ? "MB M" : "MB A",
                settings[drum::compressorOn] * settings[drum::multibandOn] > 0.5f, db(liveBands[band]), liveBands[band] / 12);
        report(12, "NETT", settings[drum::cleanupOn] > 0.5f, db(liveMeters[12]), liveMeters[12] / 18);
        report(13, "CORPS", settings[drum::presenceOn] > 0.5f, peak(liveMeters[13]), peakActivity(liveMeters[13]));
        report(14, "ROOM GR", settings[drum::ambienceOn] > 0.5f, db(liveMeters[9]), liveMeters[9] / 12);
        report(15, "CORREL", true, juce::String(liveMeters[4], 2), std::abs(liveMeters[4]));
        instrumentMenu.setSelectedId(static_cast<int>(drum::instrumentFromValue(settings[drum::instrument])) + 1, juce::dontSendNotification);
        instrumentMenu.setEnabled(owner.canEditSettings());
        if (lastMultiband != (settings[drum::multibandOn] > 0.5f))
        {
            lastMultiband = settings[drum::multibandOn] > 0.5f;
            resized();
        }
        const auto enabled = owner.canEditSettings();
        for (size_t index = 0; index < settings.size(); ++index)
        {
            if (!sliders[index].isMouseButtonDown())
                sliders[index].setValue(settings[index], juce::dontSendNotification);
            toggles[index].setToggleState(settings[index] > 0.5f, juce::dontSendNotification);
            sliders[index].setEnabled(enabled);
            toggles[index].setEnabled(index == drum::bypass ? owner.canBypass() : enabled);
        }
        toggles[drum::bypass].setTooltip(enabled ? "Bypass de la selection active"
            : owner.canBypass() ? "Bypass de la derniere selection ARA editee, meme sans selection active"
            : "Selectionner une region ARA au moins une fois pour activer Bypass");
        repaint();
    }

    ZeroEffortDrumProcessor& owner;
    juce::LookAndFeel_V4 look;
    juce::TooltipWindow tooltip { this, 650 };
    juce::Label title, status, context;
    juce::ComboBox instrumentMenu;
    juce::TextButton diagnostic;
    juce::TextButton presenceView;
    juce::TextButton busView;
    std::array<juce::TextButton, 2> finishPages;
    std::array<juce::Label, 16> headerIndicators;
    std::array<float, 16> activityLevels {};
    std::array<float, drum::finishMeterCount> liveMeters {};
    std::array<float, 3> liveBands {};
    std::array<float, 4> liveMain {};
    float lastMeterFrame = -1;
    int staleMeterTicks = 0;
    std::array<juce::TextButton, 4> pages;
    juce::ToggleButton spectrumSwitch;
    drum::SpectrumDisplay spectrumDisplay;
    int activePage = 2;
    float hoverX = -1;
    bool lastMultiband = false;
    juce::TextButton analyse, apply, revert;
    juce::Label autoReport;
    std::array<juce::TextButton, 3> styles;
    juce::Slider intensity;
    juce::Label intensityLabel;
    juce::TextButton details;
    std::array<juce::Slider, drum::controlCount> sliders;
    std::array<juce::Label, drum::controlCount> labels;
    std::array<juce::ToggleButton, drum::controlCount> toggles;
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
    juce::String key, text { "Auto / Pret" };
    double rate = 0;
    juce::int64 offset = 0, total = 0;
    unsigned int generation = 0;
    bool busy = false, ready = false, changed = false;
};

ZeroEffortDrumProcessor::~ZeroEffortDrumProcessor() = default;

ZeroEffortDrumProcessor::ZeroEffortDrumProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::mono(), true)
                                     .withOutput("Output", juce::AudioChannelSet::mono(), true)),
      parameters(*this, nullptr, "DrumPrototypeState", createParameters())
{
    autoSession = std::make_unique<AutoSession>();
    for (size_t index = 0; index < controls.size(); ++index)
        controls[index] = parameters.getRawParameterValue(drum::controlSpecs[index].id);
}

juce::AudioProcessorValueTreeState::ParameterLayout ZeroEffortDrumProcessor::createParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& spec : drum::controlSpecs)
    {
        if (juce::String(spec.id) == "drumInstrument")
        {
            juce::StringArray names;
            for (const auto* name : drum::instrumentNames) names.add(name);
            layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { spec.id, 1 }, spec.name, names, 0));
        }
        else if (spec.toggle)
            layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { spec.id, 1 }, spec.name, spec.initial > 0));
        else
            layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { spec.id, 1 }, spec.name,
                juce::NormalisableRange<float> { spec.minimum, spec.maximum, 0.01f, spec.skew }, spec.initial,
                juce::AudioParameterFloatAttributes().withLabel(spec.unit)));
    }
    return layout;
}

void ZeroEffortDrumProcessor::prepareToPlay(double sampleRate, int maximumBlockSize)
{
    spectrumCapture.reset();
    engine.prepare(sampleRate, maximumBlockSize, getTotalNumOutputChannels());
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
    if (!processBlockForARA(buffer, isNonRealtime() ? Realtime::no : Realtime::yes, getPlayHead()))
    {
        autoCapture.push(buffer, getSampleRate());
        drum::Settings settings {};
        for (size_t index = 0; index < settings.size(); ++index)
            settings[index] = controls[index]->load(std::memory_order_relaxed);
        engine.process(buffer, settings);
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
    if (session.key.isEmpty())
    {
        session.text = "Auto / Selectionner un seul evenement audio";
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
            session.text = "Auto / Source indisponible, etiree ou duree hors 2 s - 1 h";
            return;
        }
        session.total = static_cast<juce::int64>(std::llround(duration * session.rate));
        session.analyser.prepare(session.rate);
        session.text = "Auto / Analyse ARA 0 %";
    }
    else
    {
        session.generation = autoCapture.begin();
        session.total = 0;
        session.text = "Auto / Lecture : capture 8 s";
    }
    session.busy = true;
}

void ZeroEffortDrumProcessor::cancelAutoPreset()
{
    autoCapture.cancel();
    if (autoSession->busy) autoSession->text = "Auto / Annule";
    autoSession->busy = false;
}

void ZeroEffortDrumProcessor::updateAutoPreset()
{
    auto& session = *autoSession;
    if ((!session.busy && !session.ready && !session.changed)) return;
    if (session.key != autoTargetKey() || getSettings() != (session.changed ? session.applied : session.before))
    {
        cancelAutoPreset();
        session.ready = session.changed = false;
        session.text = "Auto / Selection ou reglages modifies : analyser a nouveau";
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
            session.text = "Auto / Acces audio interrompu";
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
            session.text = "Auto / Echec de lecture source";
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
            session.text = "Auto / Lecture : capture " + juce::String(autoCapture.progress() * 100, 0) + " %";
            return;
        }
        if (session.total == 0)
        {
            session.total = autoCapture.size();
            session.analyser.prepare(autoCapture.sampleRate());
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
    session.text = juce::String("Auto / ") + (session.phase == 0 ? "Analyse " : session.phase == 1 ? "Calibration " : "Verification ")
        + juce::String(session.total > 0 ? 100.0 * session.offset / session.total : 0.0, 0) + " %";
    if (session.offset == session.total)
    {
        if (session.phase == 0)
            session.proposal = session.analyser.propose(session.before, session.style, session.intensity);
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
            return;
        }
        session.ready = session.proposal.valid;
        session.busy = false;
        session.text = "Auto / " + session.proposal.summary;
        if (session.ready) applyAutoPreset();
    }
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
    autoSession->text = "Auto / " + juce::String(drum::drumStyleName(style)) + " / Analyser";
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
    writeSettings(autoSession->proposal.settings);
    autoSession->applied = getSettings();
    autoSession->ready = false;
    autoSession->changed = true;
    autoSession->text = "Auto / Applique / " + autoSession->proposal.summary;
    return true;
}

bool ZeroEffortDrumProcessor::revertAutoPreset()
{
    if (!canRevertAutoPreset()) return false;
    writeSettings(autoSession->before);
    autoSession->changed = false;
    autoSession->text = "Auto / Reglages precedents retablis";
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
    return new PrototypeEditor(*this);
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
        }
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