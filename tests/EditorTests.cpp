#include "ARA/ZeroEffortDrumProcessor.h"
#include "UI/AutoRadarPanel.h"
#include "Drum/DrumEngine.h"
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename Type>
Type* child(juce::Component& root, const juce::String& id)
{
    return dynamic_cast<Type*>(root.findChildWithID(id));
}

template <typename Type>
std::vector<Type*> visible(juce::Component& root)
{
    std::vector<Type*> result;
    for (auto* component : root.getChildren())
        if (auto* typed = dynamic_cast<Type*>(component))
            if (typed->isVisible()) result.push_back(typed);
    return result;
}

juce::TextButton& tab(juce::Component& editor, const juce::String& id)
{
    auto* button = child<juce::TextButton>(editor, id);
    require(button != nullptr && button->isVisible(), "Every processing step must have a visible tab");
    return *button;
}

void open(juce::Component& editor, const juce::String& id)
{
    auto& button = tab(editor, id);
    button.onClick();
    require(button.getToggleState(), "Selected tab must be highlighted");
    for (auto* other : visible<juce::TextButton>(editor))
        if (other->getComponentID().startsWith("page") && other != &button)
            require(!other->getToggleState(), "Section tabs must be mutually exclusive");
}

void toggle(juce::Component& editor, const juce::String& id, bool state)
{
    auto* button = child<juce::ToggleButton>(editor, id);
    require(button != nullptr, "Module switch must exist");
    button->setToggleState(state, juce::dontSendNotification);
    button->onClick();
}

// Visible sliders of the current page: inside the editor, clear of the footer, never overlapping
// each other and never covered by a caption.
int checkPage(juce::Component& editor)
{
    std::vector<juce::Rectangle<int>> bounds;
    int count = 0;
    for (auto* slider : visible<juce::Slider>(editor))
    {
        if (slider->getY() < 380) continue;
        ++count;
        require(editor.getLocalBounds().contains(slider->getBounds()), "Every control must fit the editor");
        require(slider->getBottom() <= 712, "Page controls must stay above the Auto footer");
        for (const auto& previous : bounds)
            require(!previous.intersects(slider->getBounds()), "Visible page controls must never overlap");
        for (auto* label : visible<juce::Label>(editor))
            if (label->getY() >= 380 && label->getComponentID().endsWith(".label"))
                require(!label->getBounds().intersects(slider->getBounds()), "Captions must not cover adjacent controls");
        bounds.push_back(slider->getBounds());
    }
    return count;
}

void writeSnapshot(juce::Component& editor, const juce::File& file)
{
    juce::FileOutputStream stream(file);
    require(stream.openedOk(), "Snapshot output must open");
    stream.setPosition(0);
    stream.truncate();
    require(juce::PNGImageFormat().writeImageToStream(editor.createComponentSnapshot(editor.getLocalBounds()), stream),
        "Editor snapshot must encode");
}

void processTone(ZeroEffortDrumProcessor& processor, int blocks, const std::vector<std::pair<double, double>>& partials)
{
    juce::AudioBuffer<float> audio(1, 256);
    juce::MidiBuffer midi;
    for (int block = 0; block < blocks; ++block)
    {
        for (int sample = 0; sample < 256; ++sample)
        {
            const auto time = (block * 256 + sample) / 48000.0;
            double value = 0;
            for (const auto& [frequency, amplitude] : partials)
                value += amplitude * std::sin(juce::MathConstants<double>::twoPi * frequency * time);
            audio.setSample(0, sample, static_cast<float>(value));
        }
        processor.processBlock(audio, midi);
    }
}
}


// The Auto radar overlay: opens with the analysis, builds its three curves, waits for Appliquer or Ignorer.
void testAutoRadarFlow(const juce::File& imageFile)
{
    ZeroEffortDrumProcessor processor;
    processor.setRateAndBufferSizeDetails(48000, 256);
    processor.prepareToPlay(48000, 256);
    processor.setAutoStyle(drum::DrumStyle::rock, 0.8f);
    std::unique_ptr<juce::AudioProcessorEditor> editorOwner(processor.createEditor());
    auto& editor = *editorOwner;
    auto* panel = child<juce::Component>(editor, "autoRadarPanel");
    require(panel != nullptr && !panel->isVisible(), "The radar panel must exist and stay hidden until Auto starts");

    const auto runAuto = [&](const std::function<void(int)>& onTick) {
        child<juce::TextButton>(editor, "autoAnalyse")->onClick();
        require(panel->isVisible(), "Starting Auto must open the radar panel");
        juce::AudioBuffer<float> audio(1, 256);
        juce::MidiBuffer midi;
        for (int block = 0; block < 1500; ++block)
        {
            for (int sample = 0; sample < 256; ++sample)
            {
                const auto time = static_cast<double>(block * 256 + sample) / 48000.0;
                const auto age = std::fmod(time, 0.5);
                audio.setSample(0, sample, static_cast<float>(0.6 * std::exp(-age * 24) * std::sin(juce::MathConstants<double>::twoPi * 62 * time)
                    + 0.25 * std::exp(-std::fmod(time + 0.25, 0.5) * 20) * std::sin(juce::MathConstants<double>::twoPi * 3100 * time)));
            }
            processor.processBlock(audio, midi);
            if (block % 50 == 0)
            {
                processor.updateAutoPreset();
                onTick(-1);
            }
        }
        for (int step = 0; step < 200 && processor.autoPresetBusy(); ++step)
        {
            processor.updateAutoPreset();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(55);
            onTick(step);
        }
    };

    const auto before = processor.getSettings();
    std::vector<int> phases;
    juce::StringArray written;
    const auto snapshot = [&](const juce::String& name) {
        auto* radarPanel = dynamic_cast<drum::ui::AutoRadarPanel*>(panel);
        require(radarPanel != nullptr, "The radar panel must be an AutoRadarPanel");
        radarPanel->settleForTest();
        writeSnapshot(editor, imageFile.getSiblingFile("AutoRadar_" + name + ".png"));
        written.add(name);
    };
    runAuto([&](int step) {
        if (step < 0) juce::MessageManager::getInstance()->runDispatchLoopUntil(30);   // let the editor timer feed the panel
        const auto radar = processor.getAutoRadar();
        if (phases.empty() || phases.back() != static_cast<int>(radar.phase)) phases.push_back(static_cast<int>(radar.phase));
        const auto name = juce::String("phase") + juce::String(static_cast<int>(radar.phase));
        if (radar.progress > 0.6f && !written.contains(name) && radar.phase != drum::AutoRadarView::finished) snapshot(name);
    });
    require(phases.size() >= 3 && phases.front() == 0 && phases[1] == 1 && phases[2] == 2, "The radar must go through analysis, calculation and verification");
    const auto radar = processor.getAutoRadar();
    require(radar.ready && radar.phase == drum::AutoRadarView::finished && radar.hasTarget && radar.hasResult
        && radar.source.any() && radar.target.any() && radar.result.any(), "A finished Auto must publish the three curves");
    require(std::abs(radar.result.raw[4] - radar.source.raw[4]) > 0.05f || std::abs(radar.result.raw[7] - radar.source.raw[7]) > 0.05f,
        "The obtained curve must differ from the source: the proposal does something");
    snapshot("done");
    require(panel->isVisible() && processor.getSettings() == before && processor.canApplyAutoPreset(),
        "The proposal must wait: panel open, settings untouched, Apply available");
    auto* apply = child<juce::TextButton>(*panel, "autoRadarApply");
    auto* ignore = child<juce::TextButton>(*panel, "autoRadarIgnore");
    require(apply != nullptr && ignore != nullptr && apply->isVisible() && apply->isEnabled() && ignore->isEnabled(),
        "Appliquer and Ignorer must be available once the proposal is ready");
    require(!processor.getAutoChanges().empty(), "The list of changes must not be empty for a real proposal");

    ignore->onClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
    require(!panel->isVisible() && processor.getSettings() == before && !processor.canApplyAutoPreset(),
        "Ignorer must close the panel and leave every setting untouched");

    runAuto([](int) {});
    require(panel->isVisible() && processor.getSettings() == before, "A second run must wait as well");
    apply->onClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
    require(!panel->isVisible() && processor.getSettings() != before && processor.canRevertAutoPreset(),
        "Appliquer must apply the proposal, close the panel and offer Revenir");

    // A refused analysis (silence) explains itself and closes with Close, without touching the settings
    processor.revertAutoPreset();
    const auto untouched = processor.getSettings();
    child<juce::TextButton>(editor, "autoAnalyse")->onClick();
    {
        juce::AudioBuffer<float> silence(1, 256);
        silence.clear();
        juce::MidiBuffer midi;
        for (int block = 0; block < 1500; ++block)
        {
            processor.processBlock(silence, midi);
            if (block % 50 == 0) processor.updateAutoPreset();
        }
        for (int step = 0; step < 200 && processor.autoPresetBusy(); ++step) processor.updateAutoPreset();
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
    require(panel->isVisible() && processor.getAutoRadar().refused && !processor.getAutoRadar().ready,
        "A refused analysis must keep the panel open to say why");
    require(!child<juce::TextButton>(*panel, "autoRadarIgnore")->getButtonText().compare("Close"), "A refusal is closed with Close");
    require(!child<juce::TextButton>(*panel, "autoRadarApply")->isVisible(), "A refusal has nothing to apply");
    writeSnapshot(editor, imageFile.getSiblingFile("AutoRadar_refused.png"));
    child<juce::TextButton>(*panel, "autoRadarIgnore")->onClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
    require(!panel->isVisible() && processor.getSettings() == untouched, "Close must close the panel and change nothing");

    // Cancelling while it runs closes the panel without applying anything
    processor.revertAutoPreset();
    child<juce::TextButton>(editor, "autoAnalyse")->onClick();
    child<juce::TextButton>(editor, "autoAnalyse")->onClick();     // the same button reads "Annuler" while Auto runs
    juce::MessageManager::getInstance()->runDispatchLoopUntil(80);
    require(!panel->isVisible() && !processor.autoPresetBusy(), "Cancelling must close the panel");
    std::cout << "PASS: Auto radar panel (3 phases, proposal waits, Ignorer / Appliquer / cancel)" << std::endl;
}
void testEditor(ZeroEffortDrumProcessor& processor, const juce::File& imageFile)
{
    {
        std::unique_ptr<juce::AudioProcessorEditor> editorOwner(processor.createEditor());
        require(editorOwner != nullptr && editorOwner->getWidth() == 1100 && editorOwner->getHeight() == 760,
            "Editor must have a stable initial layout");
        auto& editor = *editorOwner;

        for (int index = 0; index < 3; ++index)
        {
            auto* style = child<juce::TextButton>(editor, "style" + juce::String(index));
            require(style != nullptr && editor.getLocalBounds().contains(style->getBounds()), "Every musical profile must be visible");
        }
        child<juce::TextButton>(editor, "style2")->onClick();
        require(processor.getAutoStyle() == drum::DrumStyle::metal, "Profile button must reach the Auto engine");
        for (const auto* id : { "autoAnalyse", "autoApply", "autoRevert" })
        {
            auto* button = child<juce::TextButton>(editor, id);
            require(button != nullptr && button->isVisible() && editor.getLocalBounds().contains(button->getBounds()),
                "Auto workflow must expose all three commands");
            if (juce::String(id) != "autoAnalyse") require(!button->isEnabled(), "Apply and rollback must start disabled");
        }
        auto* intensity = child<juce::Slider>(editor, "autoIntensity");
        require(intensity != nullptr, "Intensity control must be visible");
        intensity->setValue(60, juce::sendNotificationSync);
        require(std::abs(processor.getAutoIntensity() - 0.6f) < 0.0001f, "Intensity slider must reach Auto options");

        const juce::StringArray signalOrder { "Kit / Phase", "Filters", "Expander", "Focus / Presence", "EQ",
            "Compressor", "Bus", "Precision", "Impact" };
        std::vector<juce::TextButton*> tabs;
        for (auto* button : visible<juce::TextButton>(editor))
            if (button->getComponentID().startsWith("page")) tabs.push_back(button);
        std::sort(tabs.begin(), tabs.end(), [](auto* a, auto* b) { return a->getX() < b->getX(); });
        require(static_cast<int>(tabs.size()) == signalOrder.size(), "Kick must expose every processing step");
        for (size_t index = 0; index < tabs.size(); ++index)
        {
            require(tabs[index]->getButtonText() == signalOrder[static_cast<int>(index)], "Tabs must follow the signal order");
            require(tabs[index]->getY() >= 346 && editor.getLocalBounds().contains(tabs[index]->getBounds()),
                "Section tabs must remain below the permanent graph and meters");
        }

        require(tab(editor, "pageEq").getToggleState(), "EQ must remain the default page");
        int rotaries = 0, switches = 0;
        for (auto* slider : visible<juce::Slider>(editor))
            if (slider->getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag) ++rotaries;
        for (auto* button : visible<juce::ToggleButton>(editor)) { juce::ignoreUnused(button); ++switches; }
        require(rotaries == 15 && switches == 5, "EQ page must expose bands, Q, gains, A/B, mono, spectrum and bypass");
        auto* bodyGain = child<juce::Slider>(editor, "bodyDb");
        require(bodyGain != nullptr && bodyGain->getTooltip().startsWith("Weight"), "Kick EQ bands must use Kick vocabulary");
        bodyGain->setValue(3.0, juce::sendNotificationSync);
        require(processor.getSettings()[drum::bodyGain] == 3.0f, "Visible EQ control must update the real processor parameter");
        bodyGain->setValue(0.0, juce::sendNotificationSync);
        toggle(editor, "eqOn", true);
        require(processor.getSettings()[drum::eqOn] == 1.0f, "Visible module activation must update the real processor parameter");
        toggle(editor, "eqOn", false);
        auto* bodyQ = child<juce::Slider>(editor, "bodyQ");
        bodyQ->setValue(2.3, juce::sendNotificationSync);
        require(std::abs(processor.getSettings()[drum::bodyQ] - 2.3f) < 0.001f, "Independent Q control must reach the real processor");
        bodyQ->setValue(0, juce::sendNotificationSync);
        require(bodyQ->getTextFromValue(0).trim() == "Global", "Inherited Q must display Global, never an invalid zero Q");

        const std::array<std::pair<const char*, int>, 8> expected {{ { "pageFilters", 4 }, { "pageExpander", 7 },
            { "pageFocus", 12 }, { "pageEq", 15 }, { "pageCompressor", 8 }, { "pageBus", 18 }, { "pagePrecision", 12 }, { "pageImpact", 13 } }};
        for (const auto& [id, count] : expected)
        {
            open(editor, id);
            require(checkPage(editor) == count, "Each section must expose all and only its controls plus Input/Output");
        }
        open(editor, "pageFocus");
        require(visible<juce::ToggleButton>(editor).size() == 8, "Kick Focus/Presence must expose its switches, A/B, mono, spectrum and bypass");
        child<juce::Slider>(editor, "kickReinforcePercent")->setValue(42, juce::sendNotificationSync);
        require(processor.getSettings()[drum::reinforceAmount] == 42, "Presence UI must update the real body reinforcement parameter");

        open(editor, "pageCompressor");
        toggle(editor, "multibandOn", true);
        require(processor.getSettings()[drum::multibandOn] == 1, "Multiband mode UI must activate the real processor path");
        require(checkPage(editor) == 24, "Multiband page must expose eighteen band controls, four globals plus Input and Output");
        child<juce::Slider>(editor, "midThresholdDb")->setValue(-33, juce::sendNotificationSync);
        require(std::abs(processor.getSettings()[drum::midThreshold] + 33) < 0.001f, "Per-band threshold UI must reach the processor");
        toggle(editor, "multibandOn", false);

        auto* menu = child<juce::ComboBox>(editor, "drumInstrument");
        require(menu != nullptr && menu->getNumItems() == 10, "Instrument menu must expose all ten drum roles");
        const auto choose = [&](int id) { menu->setSelectedId(id, juce::dontSendNotification); menu->onChange(); };
        const auto before = processor.getSettings();
        choose(10);
        auto expectedSettings = before;
        expectedSettings[drum::instrument] = 9;
        require(processor.getSettings() == expectedSettings, "Choosing Master must not alter any audio treatment");
        auto* position = child<juce::ComboBox>(editor, "kitMic");
        choose(2);
        require(bodyGain->getTooltip().startsWith("Body") && tab(editor, "pageFocus").getButtonText() == "Cleanup",
            "Snare must rename EQ bands and the cleanup page");
        require(position != nullptr && position->isVisible() && position->getNumItems() == 2, "Snare must offer Top / Bottom");
        open(editor, "pageFocus");
        require(checkPage(editor) == 7, "Snare cleanup page must hide the synthetic Kick body controls");
        choose(7);
        require(child<juce::TextButton>(editor, "pageFocus")->isVisible() == false && !position->isVisible(),
            "Hi-hat must hide Kick/Snare-only steps and microphone positions");
        require(bodyGain->getTooltip().startsWith("Bleed"), "Hi-hat EQ must use cymbal vocabulary");
        choose(8);
        require(position->isVisible() && position->getNumItems() == 3, "Overheads must offer Stereo / Left / Right");
        choose(1);

        open(editor, "pageBus");
        auto* bypass = child<juce::ToggleButton>(editor, "drumBypass");
        require(bypass != nullptr && bypass->isVisible() && bypass->isEnabled() && processor.canBypass(), "Bypass must remain clickable");
        toggle(editor, "drumBypass", true);
        require(processor.getSettings()[drum::bypass] == 1, "Clickable Bypass must update the real processor");
        toggle(editor, "drumBypass", false);
        toggle(editor, "spectrum", false);
        require(processor.getSpectrumMailbox().subscribers.load() == 0, "Disabling spectrum must stop capture");
        toggle(editor, "spectrum", true);
        require(processor.getSpectrumMailbox().subscribers.load() == 1, "Editor must subscribe to real output spectrum");

        open(editor, "pageKit");
        auto* group = child<juce::ComboBox>(editor, "kitGroup");
        require(group != nullptr && group->isVisible() && group->getNumItems() == kit::groupCount, "Kit page must expose the kit groups");
        group->setSelectedId(3, juce::dontSendNotification);
        group->onChange();
        require(processor.getKitControl(ZeroEffortDrumProcessor::kitGroup) == 2, "Kit group menu must reach the processor");
        child<juce::TextButton>(editor, "kitMode2")->onClick();
        require(processor.getKitMode() == kit::Mode::automatic, "Kit mode buttons must reach the processor");
        child<juce::TextButton>(editor, "kitMode1")->onClick();
        child<juce::Slider>(editor, "kitDelayMs")->setValue(1.5, juce::sendNotificationSync);
        require(std::abs(processor.getKitControl(ZeroEffortDrumProcessor::kitDelay) - 1.5f) < 0.01f, "Kit delay knob must reach the processor");
        toggle(editor, "kitPolarity", true);
        require(processor.getKitControl(ZeroEffortDrumProcessor::kitPolarity) > 0.5f, "Kit polarity switch must reach the processor");
        toggle(editor, "kitPolarity", false);
        child<juce::Slider>(editor, "kitDelayMs")->setValue(0.0, juce::sendNotificationSync);
        require(child<juce::TextButton>(editor, "kitAnalyse")->isVisible() && !child<juce::TextButton>(editor, "kitApply")->isEnabled(),
            "Kit analysis must be startable while Apply waits for a report");
        checkPage(editor);
        group->setSelectedId(1, juce::dontSendNotification);
        group->onChange();
        open(editor, "pageEq");
    }

    ZeroEffortDrumProcessor visual;
    visual.setRateAndBufferSizeDetails(48000, 256);
    visual.setControl(drum::filtersOn, 1);
    visual.setControl(drum::highPass, 35);
    visual.setControl(drum::lowPass, 12000);
    visual.setControl(drum::eqOn, 1);
    visual.setControl(drum::bodyFrequency, 80);
    visual.setControl(drum::bodyGain, 3.5f);
    visual.setControl(drum::boxGain, -4);
    visual.setControl(drum::attackGain, 3);
    visual.setControl(drum::bodyQ, 2);
    visual.setControl(drum::boxQ, 1.4f);
    visual.setControl(drum::attackQ, 1.5f);
    visual.setControl(drum::eqQ, 1);
    std::unique_ptr<juce::AudioProcessorEditor> visualEditor(visual.createEditor());
    auto& editor = *visualEditor;
    const auto blankSpectrum = editor.createComponentSnapshot(editor.getLocalBounds());
    visual.prepareToPlay(48000, 256);
    processTone(visual, 32, { { 80.0, 0.12 }, { 3000.0, 0.03 } });
    juce::MessageManager::getInstance()->runDispatchLoopUntil(70);
    const auto image = editor.createComponentSnapshot(editor.getLocalBounds());
    require(image.isValid(), "Kick editor snapshot must render");
    int intensityTrackPixels = 0;
    for (int x = 612; x < 760; ++x)
        for (int y = 40; y < 62; ++y)
            if (image.getPixelAt(x, y) == juce::Colour(0xff58d1ad)) ++intensityTrackPixels;
    require(intensityTrackPixels > 100, "Full intensity must display a visible filled track, not only a thumb");
    int changedSpectrumPixels = 0;
    for (int x = 64; x < 744; ++x)
        for (int y = 104; y < 268; ++y)
            if (image.getPixelAt(x, y) != blankSpectrum.getPixelAt(x, y)) ++changedSpectrumPixels;
    require(changedSpectrumPixels > 1000, "Real processor output must produce a nonblank moving spectrum in the editor");
    writeSnapshot(editor, imageFile);
    for (const auto& [id, name] : { std::pair<const char*, const char*> { "pageFocus", "KickPresence" }, { "pageFilters", "KickFiltres" },
                                    { "pageExpander", "KickExpander" }, { "pageCompressor", "KickCompresseur" }, { "pageKit", "KitPhase" } })
    {
        open(editor, id);
        writeSnapshot(editor, imageFile.getSiblingFile(juce::String(name) + ".png"));
    }
    open(editor, "pageCompressor");
    visual.setControl(drum::compressorOn, 1);
    toggle(editor, "multibandOn", true);
    visual.setControl(drum::lowThreshold, -40);
    visual.setControl(drum::midThreshold, -40);
    visual.setControl(drum::highThreshold, -40);
    processTone(visual, 48, { { 80.0, 0.14 }, { 600.0, 0.08 }, { 3500.0, 0.05 } });
    juce::MessageManager::getInstance()->runDispatchLoopUntil(70);
    require(visual.getBandMeters()[0] > 1 && visual.getBandMeters()[1] > 1 && visual.getBandMeters()[2] > 1,
        "Live multiband UI meters must receive actual reductions from all three bands");
    writeSnapshot(editor, imageFile.getSiblingFile("KickMultibande.png"));

    visual.setControl(drum::instrument, 9);
    visual.setControl(drum::compressorOn, 0);
    visual.setControl(drum::glueOn, 1);
    visual.setControl(drum::glueThreshold, -30);
    visual.setControl(drum::ambienceOn, 1);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(70);
    open(editor, "pageBus");
    processTone(visual, 48, { { 80.0, 0.2 }, { 600.0, 0.08 }, { 3500.0, 0.05 } });
    juce::MessageManager::getInstance()->runDispatchLoopUntil(70);
    require(visual.getMeters()[1] > 0.01f && visual.getMeters()[3] > 0.1f, "Bus capture must show real nonzero output and Glue reduction");
    writeSnapshot(editor, imageFile.getSiblingFile("DrumMasterBus.png"));

    visual.setControl(drum::crushOn, 1);
    visual.setControl(drum::colourOn, 1);
    visual.setControl(drum::clipOn, 1);
    visual.setControl(drum::clipCeiling, -12);
    visual.setControl(drum::clipMix, 50);
    visual.setControl(drum::dynamicOn, 1);
    visual.setControl(drum::dynamicLowThreshold, -40);
    visual.setControl(drum::dynamicHighThreshold, -40);
    visual.setControl(drum::crushThreshold, -40);
    processTone(visual, 96, { { 350.0, 0.3 }, { 7000.0, 0.2 } });
    const auto finishMeters = visual.getFinishMeters();
    require(finishMeters[0] > 1 && finishMeters[1] > 1 && finishMeters[2] > 1,
        "Finishing GUI meters must receive actual Crush and both dynamic EQ reductions");
    require(finishMeters[6] > 0 && finishMeters[7] > 0 && finishMeters[8] > 0,
        "Harmonic and room meters must measure real residual and return signals");
    juce::MessageManager::getInstance()->runDispatchLoopUntil(70);
    const auto activeDynamicImage = editor.createComponentSnapshot(editor.getLocalBounds());
    int activeIndicators = 0;
    for (auto* label : visible<juce::Label>(editor))
        if (label->getText().startsWith("CRUSH ") || label->getText().startsWith("DYN L ")
            || label->getText().startsWith("COLOR ") || label->getText().startsWith("ROOM "))
        {
            ++activeIndicators;
            require(label->getY() >= 300 && label->getBottom() <= 346 && !label->getText().contains("OFF"),
                "Active finishing indications must stay beside the upper graph");
        }
    require(activeIndicators == 5, "Upper indicators must expose Crush, dynamic EQ, colour, room and room compression states");
    for (const auto& [id, name] : { std::pair<const char*, const char*> { "pageImpact", "DrumImpact" }, { "pagePrecision", "DrumPrecision" } })
    {
        open(editor, id);
        writeSnapshot(editor, imageFile.getSiblingFile(juce::String(name) + ".png"));
    }
    visual.setControl(drum::ambienceOn, 0);
    processTone(visual, 192, {});
    juce::MessageManager::getInstance()->runDispatchLoopUntil(850);
    const auto quietDynamicImage = editor.createComponentSnapshot(editor.getLocalBounds());
    int movingCurvePixels = 0;
    const auto orange = [](juce::Colour colour) {
        return colour.getRed() > 230 && colour.getGreen() > 140 && colour.getGreen() < 190 && colour.getBlue() < 130;
    };
    for (int x = 64; x < 744; ++x)
        for (int y = 104; y < 268; ++y)
            if (activeDynamicImage.getPixelAt(x, y) != quietDynamicImage.getPixelAt(x, y)
                && (orange(activeDynamicImage.getPixelAt(x, y)) || orange(quietDynamicImage.getPixelAt(x, y)))) ++movingCurvePixels;
    require(movingCurvePixels > 120, "Dynamic EQ curve must visibly move with actual audio reductions");
    for (auto* label : visible<juce::Label>(editor))
        if (label->getText().startsWith("DYN L "))
            require(label->getText().contains("0.0 dB") && !label->getText().contains("OFF"),
                "Stopped processing must decay to zero without confusing silence with module OFF");
    writeSnapshot(editor, imageFile.getSiblingFile("DrumDynamicQuiet.png"));
    visual.releaseResources();
    std::cout << "PASS: editor pages in signal order, role-aware names, kit page and live snapshots\n";
}
