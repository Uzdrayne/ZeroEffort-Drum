// Promotional screenshots: real recordings from wav_test_file go through the real processor,
// Auto presets are measured on them and the kit analysis runs on the four real tracks.
// Usage: ZeroEffortDrumTests --screenshots <directory>   (not part of CTest)
#include "ARA/ZeroEffortDrumProcessor.h"
#include "Drum/DrumAutoPreset.h"
#include "Kit/KitRegistry.h"
#include <iostream>
#include <stdexcept>

namespace
{
constexpr double screenshotRate = 48000.0;
constexpr int block = 512;

void ensure(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::vector<float> loadTrack(const juce::File& file, double seconds, double offset)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    ensure(reader != nullptr && reader->sampleRate == screenshotRate, "Screenshot WAV must be a readable 48 kHz file");
    const auto start = static_cast<juce::int64>(offset * screenshotRate);
    const auto length = static_cast<int>(juce::jmin<juce::int64>(static_cast<juce::int64>(seconds * screenshotRate), reader->lengthInSamples - start));
    juce::AudioBuffer<float> buffer(1, length);
    ensure(reader->read(&buffer, 0, length, start, true, false), "Screenshot WAV must read");
    return { buffer.getReadPointer(0), buffer.getReadPointer(0) + length };
}

class PlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying(true);
        info.setTimeInSamples(position);
        info.setBpm(120.0);
        return info;
    }
    juce::int64 position = 0;
};

// Measures the track with the real Auto analyser and applies the proposal (kit settings kept).
void applyAuto(ZeroEffortDrumProcessor& processor, int instrument, const std::vector<float>& audio,
               drum::DrumStyle style, float intensity)
{
    processor.setControl(drum::instrument, static_cast<float>(instrument));
    processor.setAutoStyle(style, intensity);
    auto analyser = std::make_unique<drum::DrumAnalyser>();
    analyser->prepare(screenshotRate);
    analyser->push(audio.data(), static_cast<int>(audio.size()));
    const auto proposal = analyser->propose(processor.getSettings(), style, intensity);
    ensure(proposal.valid, "Auto proposal on the reference recording must be valid");
    for (int control = 0; control < drum::kitGroup; ++control)
        if (control != drum::bypass && control != drum::autoStyleOption && control != drum::autoIntensityOption)
            processor.setControl(control, proposal.settings[static_cast<size_t>(control)]);
}

// Plays audio in real-time-like slices so the editor timer animates meters, spectrum and curves,
// and stops just after the loudest hit of the last second for lively meters.
void play(ZeroEffortDrumProcessor& processor, const std::vector<float>& audio, juce::int64 from, double seconds, PlayHead* head = nullptr)
{
    auto end = juce::jmin<juce::int64>(static_cast<juce::int64>(audio.size()), from + static_cast<juce::int64>(seconds * screenshotRate));
    juce::int64 loudest = end - 1;
    for (auto index = juce::jmax(from, end - static_cast<juce::int64>(screenshotRate)); index < end; ++index)
        if (std::abs(audio[static_cast<size_t>(index)]) > std::abs(audio[static_cast<size_t>(loudest)])) loudest = index;
    end = juce::jmin(end, loudest + 1800);
    juce::AudioBuffer<float> buffer(1, block);
    juce::MidiBuffer midi;
    int count = 0;
    for (auto position = from; position + block <= end; position += block)
    {
        if (head != nullptr) head->position = position;
        buffer.copyFrom(0, 0, audio.data() + position, block);
        processor.processBlock(buffer, midi);
        if (++count % 4 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil(8);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
}

void open(juce::Component& editor, const juce::String& page)
{
    auto* tab = dynamic_cast<juce::Button*>(editor.findChildWithID(page));
    ensure(tab != nullptr && tab->isVisible(), "Screenshot page must be visible");
    tab->onClick();
}

void save(juce::Component& editor, const juce::File& file)
{
    editor.repaint();
    const auto image = editor.createComponentSnapshot(editor.getLocalBounds(), true, 2.0f);
    file.deleteFile();
    juce::FileOutputStream stream(file);
    ensure(stream.openedOk() && juce::PNGImageFormat().writeImageToStream(image, stream), "Screenshot must encode");
    std::cout << "  " << file.getFileName() << "\n";
}

std::unique_ptr<ZeroEffortDrumProcessor> makeProcessor(const juce::String& trackName)
{
    auto processor = std::make_unique<ZeroEffortDrumProcessor>();
    processor->setRateAndBufferSizeDetails(screenshotRate, block);
    processor->prepareToPlay(screenshotRate, block);
    juce::AudioProcessor::TrackProperties properties;
    properties.name = trackName;
    processor->updateTrackProperties(properties);
    return processor;
}
}

void makeScreenshots(const juce::File& directory)
{
    const juce::File wavs(ZED_WAV_DIRECTORY);
    ensure(directory.createDirectory(), "Screenshot directory must be writable");
    const auto kick = loadTrack(wavs.getChildFile("Kick_12.wav"), 40.0, 20.0);
    const auto snare = loadTrack(wavs.getChildFile("Snare_12.wav"), 40.0, 20.0);
    const auto left = loadTrack(wavs.getChildFile("OvH L_12.wav"), 40.0, 20.0);
    const auto right = loadTrack(wavs.getChildFile("OvH R_12.wav"), 40.0, 20.0);
    std::vector<float> mix(kick.size());
    for (size_t index = 0; index < mix.size(); ++index)
        mix[index] = 0.45f * (kick[index] + snare[index] + left[index] + right[index]);
    std::cout << "Screenshots in " << directory.getFullPathName() << "\n";

    {
        auto processor = makeProcessor("Kick");
        processor->setKitControl(ZeroEffortDrumProcessor::kitGroup, 7);
        applyAuto(*processor, 0, kick, drum::DrumStyle::rock, 0.85f);
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
        play(*processor, kick, 0, 3.0);
        save(*editor, directory.getChildFile("01_Kick_EQ.png"));
        processor->setControl(drum::presenceOn, 1);
        processor->setControl(drum::focusOn, 1);
        open(*editor, "pageFocus");
        play(*processor, kick, static_cast<juce::int64>(3 * screenshotRate), 2.5);
        save(*editor, directory.getChildFile("02_Kick_Focus_Presence.png"));
        processor->setControl(drum::compressorOn, 1);
        processor->setControl(drum::multibandOn, 1);
        processor->setControl(drum::lowThreshold, -34);
        processor->setControl(drum::midThreshold, -40);
        processor->setControl(drum::highThreshold, -42);
        open(*editor, "pageCompressor");
        play(*processor, kick, static_cast<juce::int64>(6 * screenshotRate), 2.5);
        save(*editor, directory.getChildFile("03_Kick_Compresseur_Multibande.png"));
    }
    {
        auto processor = makeProcessor("Snare");
        processor->setKitControl(ZeroEffortDrumProcessor::kitGroup, 7);
        applyAuto(*processor, 1, snare, drum::DrumStyle::rock, 0.9f);
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
        play(*processor, snare, 0, 3.0);
        save(*editor, directory.getChildFile("04_Snare_EQ.png"));
        processor->setControl(drum::gateOn, 1);
        open(*editor, "pageExpander");
        play(*processor, snare, static_cast<juce::int64>(3 * screenshotRate), 2.5);
        save(*editor, directory.getChildFile("05_Snare_Expander.png"));
        processor->setControl(drum::cleanupOn, 1);
        processor->setControl(drum::focusOn, 1);
        open(*editor, "pageFocus");
        play(*processor, snare, static_cast<juce::int64>(6 * screenshotRate), 2.5);
        save(*editor, directory.getChildFile("06_Snare_Nettoyage.png"));
        processor->setControl(drum::shapeOn, 1);
        processor->setControl(drum::shapeAttack, 4.5f);
        processor->setControl(drum::shapeSustain, -2.0f);
        processor->setControl(drum::dynamicOn, 1);
        processor->setControl(drum::dynamicLowThreshold, -38);
        processor->setControl(drum::dynamicHighThreshold, -42);
        open(*editor, "pagePrecision");
        play(*processor, snare, static_cast<juce::int64>(9 * screenshotRate), 2.5);
        save(*editor, directory.getChildFile("07_Snare_Precision.png"));
        processor->setControl(drum::crushOn, 1);
        processor->setControl(drum::crushThreshold, -38);
        processor->setControl(drum::colourOn, 1);
        processor->setControl(drum::clipOn, 1);
        processor->setControl(drum::clipCeiling, -8);
        open(*editor, "pageImpact");
        play(*processor, snare, static_cast<juce::int64>(12 * screenshotRate), 2.5);
        save(*editor, directory.getChildFile("08_Snare_Impact.png"));
    }
    {
        auto processor = makeProcessor("OH L");
        processor->setKitControl(ZeroEffortDrumProcessor::kitGroup, 7);
        applyAuto(*processor, 7, left, drum::DrumStyle::natural, 0.7f);
        processor->setKitControl(ZeroEffortDrumProcessor::kitMic, kit::positionLeft);
        processor->setControl(drum::filtersOn, 1);
        processor->setControl(drum::lowPass, 16000);
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
        open(*editor, "pageFilters");
        play(*processor, left, 0, 3.0);
        save(*editor, directory.getChildFile("09_Overheads_Filtres.png"));
    }
    {
        auto processor = makeProcessor("Drums Bus");
        processor->setKitControl(ZeroEffortDrumProcessor::kitGroup, 7);
        applyAuto(*processor, 9, mix, drum::DrumStyle::rock, 0.85f);
        processor->setControl(drum::glueOn, 1);
        processor->setControl(drum::ambienceOn, 1);
        processor->setControl(drum::ambienceAmount, 12);
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
        open(*editor, "pageBus");
        play(*processor, mix, 0, 3.0);
        save(*editor, directory.getChildFile("10_Bus_Glue_Ambiance.png"));
    }

    // Kit / Phase: four real tracks, four instances, one shared analysis.
    juce::SharedResourcePointer<kit::Registry> registry;
    PlayHead head;
    std::array<std::unique_ptr<ZeroEffortDrumProcessor>, 4> tracks {
        makeProcessor("Kick"), makeProcessor("Snare"), makeProcessor("OH L"), makeProcessor("OH R") };
    const std::array<const std::vector<float>*, 4> audio { &kick, &snare, &left, &right };
    const std::array<int, 4> instruments { 0, 1, 7, 7 };
    const std::array<int, 4> positions { kit::positionMain, kit::positionMain, kit::positionLeft, kit::positionRight };
    for (size_t index = 0; index < tracks.size(); ++index)
    {
        applyAuto(*tracks[index], instruments[index], *audio[index], drum::DrumStyle::rock, 0.8f);
        tracks[index]->setKitControl(ZeroEffortDrumProcessor::kitGroup, 0);
        tracks[index]->setKitControl(ZeroEffortDrumProcessor::kitMic, static_cast<float>(positions[index]));
        tracks[index]->setPlayHead(&head);
        tracks[index]->kitTick();
    }
    std::unique_ptr<juce::AudioProcessorEditor> editor(tracks[1]->createEditor());
    open(*editor, "pageKit");
    if (auto* analyse = dynamic_cast<juce::Button*>(editor->findChildWithID("kitAnalyse"))) analyse->onClick();
    juce::AudioBuffer<float> buffer(1, block);
    juce::MidiBuffer midi;
    bool finished = false;
    juce::int64 position = 0;
    for (; position + block <= static_cast<juce::int64>(kick.size()) && !finished; position += block)
    {
        head.position = position;
        for (size_t index = 0; index < tracks.size(); ++index)
        {
            buffer.copyFrom(0, 0, audio[index]->data() + position, block);
            tracks[index]->processBlock(buffer, midi);
        }
        if ((position / block) % 16 == 0)
        {
            registry->runOnce();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
            finished = registry->view(0).report.final;
        }
    }
    ensure(finished, "Kit analysis of the reference recording must finish");
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
    save(*editor, directory.getChildFile("11_Kit_Phase_Apercu.png"));
    if (auto* close = dynamic_cast<juce::Button*>(editor->findChildWithID("kitPreviewPanel")->findChildWithID("kitPreviewClose")))
        close->onClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    save(*editor, directory.getChildFile("12_Kit_Phase_Rapport.png"));
    if (auto* apply = dynamic_cast<juce::Button*>(editor->findChildWithID("kitApply"))) apply->onClick();
    if (auto* confirm = dynamic_cast<juce::Button*>(editor->findChildWithID("kitPreviewPanel")->findChildWithID("kitPreviewApply")))
        confirm->onClick();
    for (auto& track : tracks) track->kitTick();
    juce::AudioBuffer<float> after(1, block);
    for (int step = 0; step < 240 && position + block <= static_cast<juce::int64>(kick.size()); ++step, position += block)
    {
        head.position = position;
        for (size_t index = 0; index < tracks.size(); ++index)
        {
            after.copyFrom(0, 0, audio[index]->data() + position, block);
            tracks[index]->processBlock(after, midi);
        }
        if (step % 4 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil(8);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
    save(*editor, directory.getChildFile("13_Kit_Phase_Applique.png"));
    for (auto& track : tracks) track->releaseResources();
}
