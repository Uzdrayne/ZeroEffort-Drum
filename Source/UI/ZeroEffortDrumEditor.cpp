#include "ZeroEffortDrumEditor.h"
#include "DrumControlNames.h"
#include "DrumLookAndFeel.h"
#include "../ARA/ZeroEffortDrumProcessor.h"
#include "../Drum/DrumFilterResponse.h"

namespace drum::ui
{
namespace
{
// Pages follow the real signal order: kit alignment happens first, finishing modules last.
enum Page { kitPage, filtersPage, expanderPage, focusPage, eqPage, compressorPage, busPage, precisionPage, impactPage, pageCount };
const std::array<const char*, pageCount> pageNames { "Kit / Phase", "Filtres", "Expander", "Focus / Presence", "EQ",
    "Compresseur", "Bus", "Precision", "Impact" };
const std::array<const char*, pageCount> pageIds { "pageKit", "pageFilters", "pageExpander", "pageFocus", "pageEq",
    "pageCompressor", "pageBus", "pagePrecision", "pageImpact" };

constexpr int editorWidth = 1100, editorHeight = 760;
const juce::Rectangle<int> graphArea { 16, 72, 796, 228 };
const juce::Rectangle<int> meterArea { 820, 72, 264, 228 };
const juce::Rectangle<float> plotArea { 64.0f, 104.0f, 680.0f, 164.0f };
const juce::Rectangle<int> indicatorArea { 16, 306, 1068, 40 };
const juce::Rectangle<int> tabArea { 16, 352, 1068, 30 };
const juce::Rectangle<int> pageArea { 16, 390, 888, 322 };
const juce::Rectangle<int> levelArea { 912, 390, 172, 322 };

bool isModuleSwitch(int control)
{
    switch (control)
    {
        case drum::filtersOn: case drum::gateOn: case drum::eqOn: case drum::compressorOn: case drum::multibandOn:
        case drum::focusOn: case drum::cleanupOn: case drum::presenceOn: case drum::glueOn: case drum::ambienceOn:
        case drum::crushOn: case drum::colourOn: case drum::clipOn: case drum::shapeOn: case drum::dynamicOn:
        case drum::bypass:
            return true;
        default: return false;
    }
}

juce::String formatValue(double value, const char* unit)
{
    const juce::String suffix(unit);
    if (suffix == "Hz")
        return value >= 1000.0 ? juce::String(value / 1000.0, value >= 10000.0 ? 1 : 2) + " kHz" : juce::String(juce::roundToInt(value)) + " Hz";
    if (suffix == "dB") return juce::String(value, 1) + " dB";
    if (suffix == "ms") return juce::String(value, value < 10.0 ? 2 : value < 100.0 ? 1 : 0) + " ms";
    if (suffix == "%") return juce::String(juce::roundToInt(value)) + " %";
    if (suffix == ":1") return juce::String(value, 1) + ":1";
    return juce::String(value, 2);
}

double parseValue(const juce::String& text, const char* unit)
{
    const auto value = text.retainCharacters("0123456789.,-").replaceCharacter(',', '.').getDoubleValue();
    return juce::String(unit) == "Hz" && text.containsIgnoreCase("k") ? value * 1000.0 : value;
}

juce::String frequencyText(double frequency)
{
    return frequency < 1000 ? juce::String(juce::roundToInt(frequency)) + " Hz" : juce::String(frequency / 1000, 1) + " kHz";
}

// Kit-wide preview shown at the end of an analysis: nothing changes before "Appliquer".
class KitPreview final : public juce::Component
{
public:
    std::function<void(const std::vector<juce::uint32>&)> onApply;
    std::function<void()> onReset, onClose;

    KitPreview()
    {
        setComponentID("kitPreviewPanel");
        applyButton.setComponentID("kitPreviewApply");
        closeButton.setComponentID("kitPreviewClose");
        resetButton.setComponentID("kitPreviewReset");
        applyButton.getProperties().set("primary", true);
        applyButton.setTooltip("Appliquer les corrections cochees, chacune dosee par la Force de sa piste");
        closeButton.setTooltip("Fermer sans rien modifier ; l'apercu reste disponible dans Kit / Phase");
        resetButton.setTooltip("Remettre retard et polarite a zero sur toutes les pistes du groupe");
        applyButton.onClick = [this] {
            std::vector<juce::uint32> selected;
            for (const auto& row : rows)
                if (row.check != nullptr && row.check->getToggleState()) selected.push_back(row.id);
            if (onApply) onApply(selected);
        };
        closeButton.onClick = [this] { if (onClose) onClose(); };
        resetButton.onClick = [this] { if (onReset) onReset(); };
        for (auto* button : { &applyButton, &closeButton, &resetButton }) addAndMakeVisible(*button);
    }

    void show(const kit::Report& report)
    {
        rows.clear();
        scores = report.scores;
        for (const auto& recommendation : report.recommendations)
        {
            Row row;
            row.id = recommendation.memberId;
            row.label = recommendation.label;
            if (recommendation.role == kit::Role::overhead)
                row.proposal = "reference de temps (non modifiee)";
            else if (!recommendation.measured)
                row.proposal = recommendation.note;
            else
            {
                juce::StringArray parts;
                parts.add(recommendation.delayMs >= 0.01f ? "+" + juce::String(recommendation.delayMs, 2) + " ms" : juce::String("0 ms"));
                if (recommendation.invert) parts.add("polarite inversee");
                row.proposal = parts.joinIntoString(", ") + "   (r " + juce::String(recommendation.confidence, 2) + ")";
                row.invert = recommendation.invert;
                if (recommendation.delayMs >= 0.01f || recommendation.invert)
                {
                    row.check = std::make_unique<juce::ToggleButton>(recommendation.label);
                    row.check->setComponentID("kitPreviewRow" + juce::String(recommendation.memberId));
                    row.check->setToggleState(true, juce::dontSendNotification);
                    addAndMakeVisible(*row.check);
                }
                else
                    row.proposal = "deja coherent";
            }
            rows.push_back(std::move(row));
        }
        resized();
        setVisible(true);
        toFront(true);
        repaint();
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.setColour(palette::background.withAlpha(0.92f));
        graphics.fillRect(getLocalBounds());
        drawPanel(graphics, getLocalBounds().reduced(2), "APERCU DES CORRECTIONS DU KIT", palette::amber);
        graphics.setFont(uiFont(12.5f));
        graphics.setColour(palette::textDim);
        juce::String summary;
        if (scores.phaseBefore >= 0)
            summary << "Coherence de phase " << juce::roundToInt(scores.phaseBefore) << " -> " << juce::roundToInt(scores.phaseAfter) << " / 100";
        if (scores.stereo >= 0) summary << "   Stereo " << juce::roundToInt(scores.stereo);
        if (scores.roomBefore >= 0) summary << "   Room " << juce::roundToInt(scores.roomBefore) << " -> " << juce::roundToInt(scores.roomAfter);
        graphics.drawText(summary, 16, 32, getWidth() - 32, 18, juce::Justification::centredLeft, true);
        graphics.setColour(palette::textFaint);
        graphics.setFont(uiFont(11.5f));
        graphics.drawText("Rien n'est modifie avant Appliquer. Retards doses par la Force de chaque piste ; pistes en mode Analyse ignorees.",
            16, 50, getWidth() - 32, 16, juce::Justification::centredLeft, true);
        for (size_t index = 0; index < rows.size() && index < maximumRows; ++index)
        {
            const auto y = rowTop + static_cast<int>(index) * rowHeight;
            const auto& row = rows[index];
            if (row.check == nullptr)
            {
                graphics.setColour(palette::textDim);
                graphics.setFont(uiFont(13.0f, true));
                graphics.drawText(row.label, 73, y, 230, rowHeight, juce::Justification::centredLeft, true);
            }
            graphics.setColour(row.invert ? palette::amber : row.check != nullptr ? palette::text : palette::textFaint);
            graphics.setFont(uiFont(13.0f));
            graphics.drawText(row.proposal, 320, y, getWidth() - 336, rowHeight, juce::Justification::centredLeft, true);
        }
        if (rows.size() > maximumRows)
        {
            graphics.setColour(palette::textFaint);
            graphics.drawText("+ " + juce::String(static_cast<int>(rows.size() - maximumRows)) + " pistes (voir Rapport complet)",
                52, rowTop + static_cast<int>(maximumRows) * rowHeight, 300, 16, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        for (size_t index = 0; index < rows.size(); ++index)
            if (rows[index].check != nullptr)
            {
                rows[index].check->setVisible(index < maximumRows);
                rows[index].check->setBounds(18, rowTop + static_cast<int>(index) * rowHeight + 1, 290, rowHeight - 2);
            }
        const auto y = getHeight() - 42;
        applyButton.setBounds(16, y, 220, 30);
        closeButton.setBounds(244, y, 200, 30);
        resetButton.setBounds(getWidth() - 236, y, 220, 30);
    }

private:
    struct Row
    {
        juce::uint32 id = 0;
        juce::String label, proposal;
        bool invert = false;
        std::unique_ptr<juce::ToggleButton> check;
    };
    static constexpr int rowTop = 72, rowHeight = 25;
    static constexpr size_t maximumRows = 8;
    std::vector<Row> rows;
    kit::Scores scores;
    juce::TextButton applyButton { "Appliquer la selection" }, closeButton { "Fermer sans appliquer" },
        resetButton { "Remettre le kit a zero" };
};

class DrumEditor final : public juce::AudioProcessorEditor,
                         public juce::AudioProcessorEditorARAExtension,
                         private juce::Timer
{
public:
    explicit DrumEditor(ZeroEffortDrumProcessor& processor);
    ~DrumEditor() override;
    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent&) override { hoverX = -1; repaint(); }

private:
    struct PanelInfo { juce::Rectangle<int> bounds; juce::String title; juce::Colour accent; bool active; };

    void timerCallback() override;
    void selectPage(int page);
    bool pageVisible(int page) const;
    void refreshNames();
    void layoutPage(std::array<bool, drum::controlCount>& shown);
    void layoutTabs();
    void paintHeader(juce::Graphics& graphics);
    void paintGraph(juce::Graphics& graphics, const drum::Settings& settings);
    void paintMeters(juce::Graphics& graphics);
    void paintIndicators(juce::Graphics& graphics);
    void paintPage(juce::Graphics& graphics, const drum::Settings& settings);
    void paintKitReport(juce::Graphics& graphics, juce::Rectangle<int> area);
    void showText(const juce::String& title, const juce::String& text);
    void rotary(juce::Slider& slider);
    void horizontal(juce::Slider& slider);
    int instrument() const { return currentInstrument; }

    ZeroEffortDrumProcessor& owner;
    DrumLookAndFeel look;
    juce::TooltipWindow tooltip { this, 650 };
    juce::Label status, context, autoReport, intensityLabel;
    juce::ComboBox instrumentMenu, positionMenu, kitGroupMenu;
    juce::TextButton diagnostic, analyse, apply, revert, details;
    juce::TextButton kitAnalyse, kitApply, kitReport, kitDelayDown, kitDelayUp;
    KitPreview preview;
    std::array<juce::TextButton, 3> kitModes, styles;
    std::array<juce::TextButton, pageCount> tabs;
    juce::Slider intensity, kitStrength, kitDelay;
    juce::Label kitStrengthLabel, kitDelayLabel;
    juce::ToggleButton spectrumSwitch, kitPolarity;
    std::array<juce::Label, 16> headerIndicators;
    std::array<juce::Slider, drum::controlCount> sliders;
    std::array<juce::Label, drum::controlCount> labels;
    std::array<juce::ToggleButton, drum::controlCount> toggles;
    std::vector<PanelInfo> panels;
    drum::SpectrumDisplay spectrumDisplay;
    std::array<float, 16> activityLevels {};
    std::array<float, drum::finishMeterCount> liveMeters {};
    std::array<float, 3> liveBands {};
    std::array<float, 4> liveMain {};
    kit::GroupView kitView;
    juce::uint32 kitMemberId = 0;
    float lastMeterFrame = -1, hoverX = -1;
    int staleMeterTicks = 0, activePage = eqPage, currentInstrument = -1, tickCount = 0;
    bool lastMultiband = false, lastFocusVisible = false, previewPending = false;
};

DrumEditor::DrumEditor(ZeroEffortDrumProcessor& processor)
    : AudioProcessorEditor(processor), AudioProcessorEditorARAExtension(&processor), owner(processor)
{
    setLookAndFeel(&look);
    for (size_t index = 0; index < drum::instrumentNames.size(); ++index)
        instrumentMenu.addItem(drum::instrumentNames[index], static_cast<int>(index) + 1);
    instrumentMenu.setComponentID("drumInstrument");
    instrumentMenu.setTooltip("Element traite par cette instance : nomme les reglages et choisit le profil Auto. Aucun changement audio.");
    instrumentMenu.onChange = [this] {
        owner.setControl(drum::instrument, static_cast<float>(instrumentMenu.getSelectedId() - 1));
        owner.cancelAutoPreset();
        timerCallback();
    };
    addAndMakeVisible(instrumentMenu);
    positionMenu.setComponentID("kitMic");
    positionMenu.setTooltip("Position du micro pour l'analyse du kit (Top/Bottom, In/Out, Gauche/Droite)");
    positionMenu.onChange = [this] {
        if (positionMenu.getSelectedId() > 0)
            owner.setKitControl(ZeroEffortDrumProcessor::kitMic, static_cast<float>(positionMenu.getSelectedId() - 1));
    };
    addChildComponent(positionMenu);
    context.setFont(uiFont(12.0f));
    context.setColour(juce::Label::textColourId, palette::textDim);
    addAndMakeVisible(context);

    for (size_t index = 0; index < styles.size(); ++index)
    {
        auto& button = styles[index];
        button.setButtonText(drum::drumStyleName(static_cast<drum::DrumStyle>(index)));
        button.setComponentID(juce::String("style") + juce::String(static_cast<int>(index)));
        button.setTooltip(index == 0 ? "Correction prudente sans coloration imposee" : "Direction sonore volontaire, pas un defaut detecte");
        button.onClick = [this, index] { owner.setAutoStyle(static_cast<drum::DrumStyle>(index), owner.getAutoIntensity()); timerCallback(); };
        addAndMakeVisible(button);
    }
    intensity.setSliderStyle(juce::Slider::LinearHorizontal);
    intensity.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 20);
    intensity.setRange(0, 100, 1);
    intensity.setTextValueSuffix(" %");
    intensity.setComponentID("autoIntensity");
    intensity.setTooltip("Intensite du prochain preset : douce jusqu'a 50 %, accentuee vers 100 %. Aucun effet avant Appliquer.");
    intensity.onValueChange = [this] {
        owner.setAutoStyle(owner.getAutoStyle(), static_cast<float>(intensity.getValue()) / 100);
        timerCallback();
    };
    addAndMakeVisible(intensity);
    intensityLabel.setText("Intensite", juce::dontSendNotification);
    intensityLabel.setFont(uiFont(12.5f, true));
    intensityLabel.setColour(juce::Label::textColourId, palette::textDim);
    addAndMakeVisible(intensityLabel);

    diagnostic.setButtonText("Diagnostic");
    diagnostic.setComponentID("diagnostic");
    diagnostic.setClickingTogglesState(true);
    diagnostic.onClick = [this] { resized(); repaint(); };
    diagnostic.setTooltip("Afficher les mesures techniques ARA et la sortie hote");
    addAndMakeVisible(diagnostic);

    for (size_t index = 0; index < tabs.size(); ++index)
    {
        auto& tab = tabs[index];
        tab.setButtonText(pageNames[index]);
        tab.setComponentID(pageIds[index]);
        tab.setTooltip(juce::String("Etape ") + juce::String(static_cast<int>(index) + 1) + " du signal : " + pageNames[index]);
        tab.onClick = [this, index] { selectPage(static_cast<int>(index)); };
        addAndMakeVisible(tab);
    }

    spectrumSwitch.setButtonText("Spectre");
    spectrumSwitch.setComponentID("spectrum");
    spectrumSwitch.setTooltip("FFT de la sortie : echelle droite en dBFS, canal de plus forte amplitude. Aucun effet audio.");
    spectrumSwitch.onClick = [this] {
        owner.getSpectrumMailbox().subscribers.fetch_add(spectrumSwitch.getToggleState() ? 1 : -1);
        repaint();
    };
    addAndMakeVisible(spectrumSwitch);
    spectrumSwitch.setToggleState(true, juce::dontSendNotification);
    owner.getSpectrumMailbox().subscribers.fetch_add(1);

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
        "Nettoyage : attenuation selective de l'aigu, en dB.",
        "Renfort Kick : crete du corps synthetique, en dBFS.",
        "Room : reduction du compresseur de retour, en dB.",
        "Correlation L/R avant ecoute mono : -1 oppose, +1 similaire. L'alignement inter-micros est dans Kit / Phase."
    };
    for (size_t index = 0; index < headerIndicators.size(); ++index)
    {
        auto& indicator = headerIndicators[index];
        indicator.setFont(uiFont(11.0f, true));
        indicator.setJustificationType(juce::Justification::centredLeft);
        indicator.setTooltip(meterTips[index]);
        addAndMakeVisible(indicator);
    }

    for (auto* button : { &analyse, &apply, &revert, &details }) addAndMakeVisible(*button);
    analyse.setButtonText("Analyser");
    apply.setButtonText("Appliquer");
    revert.setButtonText("Revenir");
    details.setButtonText("Bilan");
    analyse.setComponentID("autoAnalyse");
    apply.setComponentID("autoApply");
    revert.setComponentID("autoRevert");
    details.setComponentID("autoDetails");
    analyse.getProperties().set("primary", true);
    analyse.setTooltip("ARA : un evenement. Insert : capturer 8 secondes d'audio entrant.");
    apply.setTooltip("Remplacer les reglages par la proposition prudente");
    revert.setTooltip("Retablir les reglages precedant la derniere application Auto");
    details.setTooltip("Afficher la proposition et les mesures du rendu complet");
    analyse.onClick = [this] {
        if (owner.autoPresetBusy()) owner.cancelAutoPreset(); else owner.startAutoPreset();
        timerCallback();
    };
    apply.onClick = [this] { owner.applyAutoPreset(); timerCallback(); };
    revert.onClick = [this] { owner.revertAutoPreset(); timerCallback(); };
    details.onClick = [this] { showText("Auto / Bilan", owner.getAutoPresetText()); };
    autoReport.setFont(uiFont(12.0f));
    autoReport.setColour(juce::Label::textColourId, palette::textDim);
    addAndMakeVisible(autoReport);
    status.setFont(juce::Font(juce::FontOptions(13.0f)));
    status.setColour(juce::Label::backgroundColourId, palette::panel);
    status.setColour(juce::Label::outlineColourId, palette::outline);
    status.setJustificationType(juce::Justification::topLeft);
    addChildComponent(status);

    for (int group = 0; group < kit::groupCount; ++group)
        kitGroupMenu.addItem("Kit " + juce::String(group + 1), group + 1);
    kitGroupMenu.setComponentID("kitGroup");
    kitGroupMenu.setTooltip("Toutes les pistes d'une meme batterie doivent partager le meme groupe. Un groupe par projet ouvert.");
    kitGroupMenu.onChange = [this] {
        owner.setKitControl(ZeroEffortDrumProcessor::kitGroup, static_cast<float>(kitGroupMenu.getSelectedId() - 1));
        timerCallback();
    };
    addChildComponent(kitGroupMenu);
    const std::array<const char*, 3> modeNames { "Analyse", "Suggerer", "Auto" };
    const std::array<const char*, 3> modeTips { "Afficher les mesures sans jamais modifier cette piste",
        "Proposer la correction : apercu du kit puis Appliquer", "Appliquer automatiquement la correction a la fin de l'analyse" };
    for (size_t index = 0; index < kitModes.size(); ++index)
    {
        kitModes[index].setButtonText(modeNames[index]);
        kitModes[index].setComponentID("kitMode" + juce::String(static_cast<int>(index)));
        kitModes[index].setTooltip(modeTips[index]);
        kitModes[index].onClick = [this, index] {
            owner.setKitControl(ZeroEffortDrumProcessor::kitMode, static_cast<float>(index));
            timerCallback();
        };
        addChildComponent(kitModes[index]);
    }
    const auto setupKnob = [this](juce::Slider& slider, juce::Label& label, const juce::String& id, const juce::String& name,
                                  double maximum, const char* unit, ZeroEffortDrumProcessor::KitControl control) {
        rotary(slider);
        slider.setRange(0.0, maximum, 0.01);
        slider.setComponentID(id);
        slider.textFromValueFunction = [unit](double value) { return formatValue(value, unit); };
        slider.valueFromTextFunction = [unit](const juce::String& text) { return parseValue(text, unit); };
        slider.onValueChange = [this, &slider, control] { owner.setKitControl(control, static_cast<float>(slider.getValue())); };
        label.setText(name, juce::dontSendNotification);
        label.setFont(uiFont(12.5f, true));
        label.setColour(juce::Label::textColourId, palette::textDim);
        label.setJustificationType(juce::Justification::centred);
        addChildComponent(slider);
        addChildComponent(label);
    };
    setupKnob(kitStrength, kitStrengthLabel, "kitStrength", "Force", 100.0, "%", ZeroEffortDrumProcessor::kitStrength);
    kitStrength.setDoubleClickReturnValue(true, 100.0);
    kitStrength.setTooltip("Part de la correction de temps appliquee : 0 % aucune, 50 % partielle (garde du realisme), 100 % complete");
    setupKnob(kitDelay, kitDelayLabel, "kitDelayMs", "Delai", kit::maxDelayMs, "ms", ZeroEffortDrumProcessor::kitDelay);
    kitDelay.setDoubleClickReturnValue(true, 0.0);
    kitDelay.setTooltip("Retard applique a cette piste en debut de chaine (aucune latence ajoutee)");
    kitPolarity.setButtonText("POLARITE INVERSEE");
    kitPolarity.setComponentID("kitPolarity");
    kitPolarity.getProperties().set("module", true);
    kitPolarity.getProperties().set("accent", palette::amber.toString());
    kitPolarity.setTooltip("Inverse la polarite de cette piste en debut de chaine");
    kitPolarity.onClick = [this] { owner.setKitControl(ZeroEffortDrumProcessor::kitPolarity, kitPolarity.getToggleState() ? 1.0f : 0.0f); };
    addChildComponent(kitPolarity);
    kitAnalyse.setButtonText("Analyser le kit");
    kitAnalyse.setComponentID("kitAnalyse");
    kitAnalyse.getProperties().set("primary", true);
    kitAnalyse.setTooltip("Lancer l'ecoute commune de toutes les pistes du groupe, puis lire le projet (20 a 45 s de jeu)");
    kitAnalyse.onClick = [this] {
        if (kitView.analysing) owner.stopKitAnalysis();
        else
        {
            owner.startKitAnalysis();
            previewPending = true;
        }
        timerCallback();
    };
    kitApply.setButtonText("Apercu et application");
    kitApply.setComponentID("kitApply");
    kitApply.setTooltip("Voir les corrections proposees pour tout le kit et choisir celles a appliquer");
    kitApply.onClick = [this] { preview.show(kitView.report); };
    kitReport.setButtonText("Rapport complet");
    kitReport.setComponentID("kitReport");
    kitReport.onClick = [this] {
        juce::String text = kitView.status + "\n\n" + kitView.report.lines.joinIntoString("\n");
        const auto& scores = kitView.report.scores;
        if (scores.phaseBefore >= 0)
            text += "\n\nCoherence de phase : " + juce::String(juce::roundToInt(scores.phaseBefore)) + " -> "
                + juce::String(juce::roundToInt(scores.phaseAfter)) + " / 100";
        if (scores.stereo >= 0) text += "\nCoherence stereo : " + juce::String(juce::roundToInt(scores.stereo)) + " / 100";
        if (scores.roomBefore >= 0)
            text += "\nCompatibilite room : " + juce::String(juce::roundToInt(scores.roomBefore)) + " -> "
                + juce::String(juce::roundToInt(scores.roomAfter)) + " / 100";
        showText("Kit / Rapport", text);
    };
    for (auto* button : { &kitAnalyse, &kitApply, &kitReport, &kitDelayDown, &kitDelayUp }) addChildComponent(*button);
    kitDelayDown.setButtonText("-");
    kitDelayUp.setButtonText("+");
    kitDelayDown.setComponentID("kitDelayDown");
    kitDelayUp.setComponentID("kitDelayUp");
    for (auto* button : { &kitDelayDown, &kitDelayUp })
    {
        const auto sign = button == &kitDelayUp ? 1.0f : -1.0f;
        button->setTooltip("Reglage fin : un echantillon de retard");
        button->onClick = [this, sign] {
            const auto rate = owner.getSampleRate() > 0 ? owner.getSampleRate() : 48000.0;
            const auto step = static_cast<float>(1000.0 / rate);
            const auto value = owner.getKitControl(ZeroEffortDrumProcessor::kitDelay) + sign * step;
            owner.setKitControl(ZeroEffortDrumProcessor::kitDelay, juce::jlimit(0.0f, kit::maxDelayMs, value));
            timerCallback();
        };
    }
    preview.onApply = [this](const std::vector<juce::uint32>& members) { owner.requestKitApply(members); preview.setVisible(false); timerCallback(); };
    preview.onReset = [this] { owner.requestKitReset(); preview.setVisible(false); timerCallback(); };
    preview.onClose = [this] { preview.setVisible(false); };
    addChildComponent(preview);

    for (size_t index = 0; index < drum::controlSpecs.size(); ++index)
    {
        const auto& spec = drum::controlSpecs[index];
        if (index >= drum::kitGroup) continue;
        if (spec.toggle)
        {
            auto& toggle = toggles[index];
            toggle.setComponentID(spec.id);
            if (isModuleSwitch(static_cast<int>(index))) toggle.getProperties().set("module", true);
            if (index == drum::bypass) toggle.getProperties().set("accent", palette::red.toString());
            toggle.onClick = [this, index] {
                owner.setControl(static_cast<int>(index), toggles[index].getToggleState() ? 1.0f : 0.0f);
                if (index == drum::multibandOn || index == drum::focusOn || index == drum::cleanupOn || index == drum::presenceOn)
                {
                    resized();
                    repaint();
                }
            };
            addChildComponent(toggle);
            continue;
        }
        if (index == drum::instrument || index == drum::autoStyleOption || index == drum::autoIntensityOption) continue;
        auto& slider = sliders[index];
        rotary(slider);
        slider.setComponentID(spec.id);
        slider.setRange(spec.minimum, spec.maximum, 0.01);
        slider.setSkewFactor(spec.skew);
        slider.setDoubleClickReturnValue(true, spec.initial);
        if (spec.minimum < 0 && spec.maximum > 0) slider.getProperties().set("origin", 0.0);
        const auto* unit = spec.unit;
        slider.textFromValueFunction = [unit](double value) { return formatValue(value, unit); };
        slider.valueFromTextFunction = [unit](const juce::String& text) { return parseValue(text, unit); };
        if (index >= drum::bodyQ && index <= drum::airQ)
        {
            slider.textFromValueFunction = [](double value) { return value <= 0 ? juce::String("Global") : juce::String(value, 2); };
            slider.valueFromTextFunction = [](const juce::String& text) { return text.equalsIgnoreCase("Global") ? 0.0 : text.getDoubleValue(); };
        }
        slider.onValueChange = [this, index] { owner.setControl(static_cast<int>(index), static_cast<float>(sliders[index].getValue())); };
        if (index == drum::dynamicLowHz || index == drum::dynamicHighHz || index == drum::dynamicLowQ || index == drum::dynamicHighQ
            || index == drum::dynamicLowThreshold || index == drum::dynamicHighThreshold
            || index == drum::dynamicLowRange || index == drum::dynamicHighRange)
            slider.getProperties().set("accent", palette::amber.toString());
        auto& label = labels[index];
        label.setComponentID(juce::String(spec.id) + ".label");
        label.setJustificationType(juce::Justification::centred);
        label.setFont(uiFont(12.5f, true));
        label.setColour(juce::Label::textColourId, palette::textDim);
        label.setInterceptsMouseClicks(false, false);
        addChildComponent(slider);
        addChildComponent(label);
    }
    toggles[drum::bypass].setVisible(true);

    kitMemberId = owner.getKitMemberId();
    sendLookAndFeelChange();
    for (auto& slider : sliders) slider.updateText();
    kitStrength.updateText();
    kitDelay.updateText();
    setSize(editorWidth, editorHeight);
    refreshNames();
    selectPage(eqPage);
    timerCallback();
    startTimerHz(20);
}

DrumEditor::~DrumEditor()
{
    if (spectrumSwitch.getToggleState()) owner.getSpectrumMailbox().subscribers.fetch_sub(1);
    owner.cancelAutoPreset();
    setLookAndFeel(nullptr);
}

void DrumEditor::rotary(juce::Slider& slider)
{
    if (slider.getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag) return;
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 92, 18);
}

void DrumEditor::horizontal(juce::Slider& slider)
{
    if (slider.getSliderStyle() == juce::Slider::LinearHorizontal) return;
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 66, 20);
}

void DrumEditor::showText(const juce::String& title, const juce::String& text)
{
    auto report = std::make_unique<juce::TextEditor>();
    report->setMultiLine(true);
    report->setReadOnly(true);
    report->setScrollbarsShown(true);
    report->setFont(juce::Font(juce::FontOptions(14.0f)));
    report->setColour(juce::TextEditor::backgroundColourId, palette::panel);
    report->setColour(juce::TextEditor::textColourId, palette::text);
    report->setText(text);
    report->setSize(780, 560);
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = title;
    options.dialogBackgroundColour = palette::background;
    options.content.setOwned(report.release());
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void DrumEditor::refreshNames()
{
    const auto role = juce::jmax(0, currentInstrument);
    for (size_t index = 0; index < drum::controlSpecs.size(); ++index)
    {
        const auto control = static_cast<int>(index);
        const auto& spec = drum::controlSpecs[index];
        if (spec.toggle)
        {
            toggles[index].setButtonText(switchName(control));
            toggles[index].setTooltip(juce::String(spec.name) + " : activer / desactiver");
            continue;
        }
        labels[index].setText(knobName(control, role), juce::dontSendNotification);
        auto tip = longName(control, role) + " / " + spec.name + (juce::String(spec.unit).isNotEmpty() ? " (" + juce::String(spec.unit) + ")" : "");
        if (index >= drum::bodyQ && index <= drum::airQ) tip += " / 0 = Q global, double-clic pour heriter";
        sliders[index].setTooltip(tip);
    }
    toggles[drum::levelMatchOn].setTooltip("Compensation RMS progressive +/-6 dB avant Sortie. Comparer avec Bypass ; ni LUFS ni limiteur.");
    toggles[drum::monitorMonoOn].setTooltip("Ecoute mono de la sortie. L'alignement entre micros est dans Kit / Phase.");
    const auto focusName = focusPageName(role);
    tabs[focusPage].setButtonText(focusName.isNotEmpty() ? focusName : "Focus / Presence");

    positionMenu.clear(juce::dontSendNotification);
    const auto positions = kit::positionNames(role);
    for (int index = 0; index < positions.size(); ++index)
        if (positions[index].isNotEmpty()) positionMenu.addItem(positions[index], index + 1);
    positionMenu.setVisible(positionMenu.getNumItems() > 0);
}

bool DrumEditor::pageVisible(int page) const
{
    if (page != focusPage) return true;
    const auto settings = owner.getSettings();
    return focusPageName(juce::jmax(0, currentInstrument)).isNotEmpty()
        || settings[drum::focusOn] + settings[drum::cleanupOn] + settings[drum::presenceOn] > 0.5f;
}

void DrumEditor::layoutTabs()
{
    std::vector<int> visible;
    for (int page = 0; page < pageCount; ++page)
        if (pageVisible(page)) visible.push_back(page);
    for (auto& tab : tabs) tab.setVisible(false);
    const auto gap = 6;
    const auto width = (tabArea.getWidth() - gap * (static_cast<int>(visible.size()) - 1)) / static_cast<int>(visible.size());
    for (size_t index = 0; index < visible.size(); ++index)
    {
        auto& tab = tabs[static_cast<size_t>(visible[index])];
        tab.setBounds(tabArea.getX() + static_cast<int>(index) * (width + gap), tabArea.getY(), width, tabArea.getHeight());
        tab.setVisible(true);
    }
}

void DrumEditor::selectPage(int page)
{
    activePage = juce::jlimit(0, pageCount - 1, page);
    if (activePage != kitPage) preview.setVisible(false);
    diagnostic.setToggleState(false, juce::dontSendNotification);
    for (size_t index = 0; index < tabs.size(); ++index)
        tabs[index].setToggleState(static_cast<int>(index) == activePage, juce::dontSendNotification);
    resized();
    repaint();
}

void DrumEditor::resized()
{
    instrumentMenu.setBounds(232, 16, 150, 30);
    positionMenu.setBounds(388, 16, 140, 30);
    context.setBounds(232, 48, 300, 16);
    for (size_t index = 0; index < styles.size(); ++index)
        styles[index].setBounds(548 + static_cast<int>(index) * 82, 10, 78, 26);
    intensityLabel.setBounds(548, 40, 64, 22);
    intensity.setBounds(612, 40, 182, 22);
    diagnostic.setBounds(812, 16, 104, 30);
    toggles[drum::bypass].setBounds(930, 18, 120, 26);
    spectrumSwitch.setBounds(graphArea.getRight() - 112, graphArea.getY() + 6, 100, 22);
    for (size_t index = 0; index < headerIndicators.size(); ++index)
        headerIndicators[index].setBounds(indicatorArea.getX() + 10 + static_cast<int>(index % 8) * 132,
            indicatorArea.getY() + 2 + static_cast<int>(index / 8) * 19, 124, 14);
    analyse.setBounds(16, 722, 104, 28);
    apply.setBounds(126, 722, 104, 28);
    revert.setBounds(236, 722, 96, 28);
    details.setBounds(338, 722, 72, 28);
    autoReport.setBounds(420, 720, 560, 32);
    layoutTabs();
    if (!tabs[static_cast<size_t>(activePage)].isVisible())
    {
        activePage = eqPage;
        for (size_t index = 0; index < tabs.size(); ++index)
            tabs[index].setToggleState(static_cast<int>(index) == activePage, juce::dontSendNotification);
    }

    std::array<bool, drum::controlCount> shown {};
    shown[drum::bypass] = true;
    panels.clear();
    layoutPage(shown);
    const bool diagnosing = diagnostic.getToggleState();
    for (size_t index = 0; index < drum::controlSpecs.size(); ++index)
    {
        const auto visible = index == drum::bypass || (!diagnosing && shown[index]);
        sliders[index].setVisible(visible && !drum::controlSpecs[index].toggle);
        labels[index].setVisible(visible && !drum::controlSpecs[index].toggle);
        toggles[index].setVisible(visible && drum::controlSpecs[index].toggle);
    }
    const bool kitVisible = activePage == kitPage && !diagnosing;
    for (juce::Component* component : { static_cast<juce::Component*>(&kitGroupMenu), static_cast<juce::Component*>(&kitStrength),
                                        static_cast<juce::Component*>(&kitStrengthLabel), static_cast<juce::Component*>(&kitDelay),
                                        static_cast<juce::Component*>(&kitDelayLabel), static_cast<juce::Component*>(&kitPolarity),
                                        static_cast<juce::Component*>(&kitAnalyse), static_cast<juce::Component*>(&kitApply),
                                        static_cast<juce::Component*>(&kitReport), static_cast<juce::Component*>(&kitDelayDown),
                                        static_cast<juce::Component*>(&kitDelayUp) })
        component->setVisible(kitVisible);
    for (auto& mode : kitModes) mode.setVisible(kitVisible);
    preview.setBounds(pageArea.getUnion(levelArea));
    if (preview.isVisible()) preview.toFront(false);
    status.setBounds(pageArea.getUnion(levelArea));
    status.setVisible(diagnosing);
    if (diagnosing) status.toFront(false);
}

void DrumEditor::layoutPage(std::array<bool, drum::controlCount>& shown)
{
    const auto knob = [this, &shown](int control, int x, int y, int width = 92, int height = 80) {
        const auto index = static_cast<size_t>(control);
        shown[index] = true;
        rotary(sliders[index]);
        labels[index].setBounds(x, y, width, 16);
        sliders[index].setBounds(x, y + 16, width, height);
    };
    const auto line = [this, &shown](int control, int x, int y, int width) {
        const auto index = static_cast<size_t>(control);
        shown[index] = true;
        horizontal(sliders[index]);
        labels[index].setJustificationType(juce::Justification::centredLeft);
        labels[index].setBounds(x, y, width, 15);
        sliders[index].setBounds(x, y + 15, width, 22);
    };
    const auto row = [this, &shown](int control, int x, int y, int width) {
        const auto index = static_cast<size_t>(control);
        shown[index] = true;
        horizontal(sliders[index]);
        labels[index].setJustificationType(juce::Justification::centredLeft);
        labels[index].setBounds(x, y, 62, 22);
        sliders[index].setBounds(x + 62, y, width - 62, 22);
    };
    const auto toggle = [this, &shown](int control, int x, int y, int width = 230) {
        shown[static_cast<size_t>(control)] = true;
        toggles[static_cast<size_t>(control)].setBounds(x, y, width, 24);
    };
    const auto panel = [this](int x, int width, const juce::String& title = {}, juce::Colour accent = palette::accent) {
        panels.push_back({ { x, pageArea.getY(), width, pageArea.getHeight() }, title, accent, true });
        return juce::Rectangle<int>(x, pageArea.getY(), width, pageArea.getHeight());
    };
    for (auto& label : labels) label.setJustificationType(juce::Justification::centred);
    const auto settings = owner.getSettings();
    const auto family = familyOf(juce::jmax(0, currentInstrument));

    panels.push_back({ levelArea, "NIVEAUX", palette::accent, true });
    knob(drum::input, levelArea.getX() + 18, levelArea.getY() + 34, 136, 90);
    knob(drum::output, levelArea.getX() + 18, levelArea.getY() + 146, 136, 90);
    toggle(drum::levelMatchOn, levelArea.getX() + 16, levelArea.getY() + 262, 150);
    toggle(drum::monitorMonoOn, levelArea.getX() + 16, levelArea.getY() + 290, 150);

    switch (activePage)
    {
        case kitPage:
        {
            panel(16, 268, "IDENTITE DU KIT");
            panel(292, 236, "CORRECTION", palette::amber);
            panel(536, 368, "RAPPORT DU KIT");
            kitGroupMenu.setBounds(30, 446, 240, 30);
            for (size_t index = 0; index < kitModes.size(); ++index)
                kitModes[index].setBounds(30 + static_cast<int>(index) * 81, 560, 77, 26);
            kitStrengthLabel.setBounds(104, 596, 92, 16);
            kitStrength.setBounds(104, 612, 92, 84);
            kitPolarity.setBounds(306, 428, 214, 24);
            kitDelayLabel.setBounds(352, 462, 116, 16);
            kitDelay.setBounds(352, 478, 116, 100);
            kitDelayDown.setBounds(308, 508, 38, 26);
            kitDelayUp.setBounds(474, 508, 38, 26);
            kitAnalyse.setBounds(306, 598, 208, 30);
            kitApply.setBounds(306, 634, 208, 28);
            kitReport.setBounds(306, 668, 208, 28);
            break;
        }
        case filtersPage:
        {
            panel(16, 888);
            toggle(drum::filtersOn, 30, 396);
            knob(drum::highPass, 52, 440, 150, 120);
            knob(drum::lowPass, 232, 440, 150, 120);
            break;
        }
        case expanderPage:
        {
            panel(16, 888);
            toggle(drum::gateOn, 30, 396);
            const std::array<int, 5> gate { drum::gateThreshold, drum::gateRatio, drum::gateRange, drum::gateAttack, drum::gateRelease };
            for (size_t index = 0; index < gate.size(); ++index)
                knob(gate[index], 40 + static_cast<int>(index) * 160, 444, 120, 100);
            break;
        }
        case focusPage:
        {
            const bool kickLayout = family != Family::snare && family != Family::tom;
            panel(16, 300);
            toggle(drum::focusOn, 30, 396, 260);
            knob(drum::focusLow, 46, 436, 110, 84);
            knob(drum::focusHigh, 172, 436, 110, 84);
            knob(drum::gateHold, 46, 566, 110, 84);
            knob(drum::gateHysteresis, 172, 566, 110, 84);
            if (kickLayout)
            {
                panel(324, 170);
                toggle(drum::cleanupOn, 338, 396, 150);
                knob(drum::cleanupDepth, 354, 436, 110, 84);
                panel(502, 402, {}, palette::amber);
                toggle(drum::presenceOn, 516, 396, 260);
                knob(drum::reinforceFrequency, 520, 436, 110, 84);
                knob(drum::reinforceAmount, 646, 436, 110, 84);
                knob(drum::reinforceDecay, 772, 436, 110, 84);
                knob(drum::transientAmount, 520, 566, 110, 84);
                knob(drum::saturationDrive, 646, 566, 110, 84);
                toggle(drum::reinforceInvert, 772, 610, 128);
            }
            else
            {
                panel(324, 580);
                toggle(drum::cleanupOn, 338, 396, 200);
                knob(drum::cleanupDepth, 354, 436, 120, 96);
            }
            break;
        }
        case eqPage:
        {
            panel(16, 888);
            toggle(drum::eqOn, 30, 396);
            const std::array<int, 4> frequencies { drum::bodyFrequency, drum::boxFrequency, drum::attackFrequency, drum::airFrequency };
            const std::array<int, 4> gains { drum::bodyGain, drum::boxGain, drum::attackGain, drum::airGain };
            const std::array<int, 4> qualities { drum::bodyQ, drum::boxQ, drum::attackQ, drum::airQ };
            for (size_t index = 0; index < frequencies.size(); ++index)
            {
                const auto x = 26 + static_cast<int>(index) * 194;
                knob(frequencies[index], x + 4, 474, 88);
                knob(gains[index], x + 94, 474, 88);
                knob(qualities[index], x + 49, 576, 88);
            }
            knob(drum::eqQ, 806, 576, 90);
            break;
        }
        case compressorPage:
        {
            panel(16, 888);
            toggle(drum::compressorOn, 30, 396, 220);
            toggle(drum::multibandOn, 262, 396, 200);
            if (settings[drum::multibandOn] > 0.5f)
            {
                const std::array<int, 4> globals { drum::crossoverLow, drum::crossoverHigh, drum::makeup, drum::mix };
                for (size_t index = 0; index < globals.size(); ++index)
                    line(globals[index], 32 + static_cast<int>(index) * 216, 428, 196);
                for (int band = 0; band < 3; ++band)
                {
                    const auto x = 26 + band * 292;
                    const auto base = static_cast<int>(drum::lowThreshold) + band * 7;
                    toggle(base + 6, x + 196, 480, 80);
                    for (int control = 0; control < 6; ++control)
                        row(base + control, x + 10, 508 + control * 26, 264);
                }
            }
            else
            {
                const std::array<int, 6> compressor { drum::threshold, drum::ratio, drum::attack, drum::release, drum::makeup, drum::mix };
                for (size_t index = 0; index < compressor.size(); ++index)
                    knob(compressor[index], 32 + static_cast<int>(index) * 126, 444, 110, 100);
            }
            break;
        }
        case busPage:
        {
            panel(16, 520);
            panel(544, 360, {}, palette::blue);
            toggle(drum::glueOn, 30, 396);
            toggle(drum::ambienceOn, 558, 396);
            const std::array<int, 9> glue { drum::glueThreshold, drum::glueRatio, drum::glueAttack, drum::glueRelease, drum::glueMakeup,
                drum::glueMix, drum::glueSidechain, drum::glueKnee, drum::glueRange };
            for (size_t index = 0; index < glue.size(); ++index)
                knob(glue[index], 28 + static_cast<int>(index % 5) * 100 + (index >= 5 ? 50 : 0),
                    432 + static_cast<int>(index / 5) * 136, 96, 84);
            const std::array<int, 7> room { drum::ambienceAmount, drum::ambienceSize, drum::ambiencePredelay, drum::ambienceHighPass,
                drum::ambienceLowPass, drum::ambienceCharacter, drum::ambienceCompression };
            for (size_t index = 0; index < room.size(); ++index)
                line(room[index], 560, 428 + static_cast<int>(index) * 39, 330);
            break;
        }
        case precisionPage:
        {
            panel(16, 250);
            panel(274, 630, {}, palette::amber);
            toggle(drum::shapeOn, 30, 396, 230);
            knob(drum::shapeAttack, 30, 440, 108, 96);
            knob(drum::shapeSustain, 146, 440, 108, 96);
            toggle(drum::autoMeasuredOn, 30, 664, 228);
            toggle(drum::dynamicOn, 288, 396);
            const std::array<int, 8> dynamic { drum::dynamicLowHz, drum::dynamicLowQ, drum::dynamicLowThreshold, drum::dynamicLowRange,
                drum::dynamicHighHz, drum::dynamicHighQ, drum::dynamicHighThreshold, drum::dynamicHighRange };
            for (size_t index = 0; index < dynamic.size(); ++index)
                knob(dynamic[index], 424 + static_cast<int>(index % 4) * 118, 430 + static_cast<int>(index / 4) * 138, 104, 84);
            break;
        }
        case impactPage:
        {
            panel(16, 516);
            panel(540, 176, {}, palette::amber);
            panel(724, 180, {}, palette::red);
            toggle(drum::crushOn, 30, 396);
            toggle(drum::colourOn, 554, 396, 150);
            toggle(drum::clipOn, 738, 396, 160);
            const std::array<int, 7> crush { drum::crushAmount, drum::crushThreshold, drum::crushRatio, drum::crushAttack,
                drum::crushRelease, drum::crushHP, drum::crushLP };
            for (size_t index = 0; index < crush.size(); ++index)
                knob(crush[index], 30 + static_cast<int>(index % 4) * 124 + (index >= 4 ? 62 : 0),
                    436 + static_cast<int>(index / 4) * 136, 104, 84);
            knob(drum::colourDrive, 576, 436, 104, 84);
            knob(drum::colourMix, 576, 572, 104, 84);
            knob(drum::clipCeiling, 762, 436, 104, 84);
            knob(drum::clipMix, 762, 572, 104, 84);
            break;
        }
        default: break;
    }
}

void DrumEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(palette::background);
    const auto settings = owner.getSettings();
    paintHeader(graphics);
    drawPanel(graphics, graphArea, {});
    paintGraph(graphics, settings);
    drawPanel(graphics, meterArea, "NIVEAUX / REDUCTION");
    paintMeters(graphics);
    drawPanel(graphics, indicatorArea, {}, palette::accent, false);
    paintIndicators(graphics);
    if (!diagnostic.getToggleState()) paintPage(graphics, settings);
    graphics.setColour(palette::header);
    graphics.fillRect(0, 716, getWidth(), getHeight() - 716);
    graphics.setColour(palette::outline);
    graphics.drawHorizontalLine(716, 0.0f, static_cast<float>(getWidth()));
    graphics.setColour(palette::textFaint);
    graphics.setFont(uiFont(12.0f));
    graphics.drawText("v" JucePlugin_VersionString, getWidth() - 110, 722, 94, 28, juce::Justification::centredRight);
}

void DrumEditor::paintHeader(juce::Graphics& graphics)
{
    graphics.setColour(palette::header);
    graphics.fillRect(0, 0, getWidth(), 64);
    graphics.setColour(palette::outline);
    graphics.drawHorizontalLine(64, 0.0f, static_cast<float>(getWidth()));
    graphics.setColour(palette::text);
    graphics.setFont(uiFont(23.0f, true));
    graphics.drawText("ZEROEFFORT", 18, 12, 150, 30, juce::Justification::centredLeft);
    graphics.setColour(palette::accent);
    graphics.setFont(uiFont(23.0f));
    graphics.drawText("DRUM", 159, 12, 66, 30, juce::Justification::centredLeft);
    graphics.setColour(palette::textFaint);
    graphics.setFont(uiFont(11.5f));
    graphics.drawText(roleHint(juce::jmax(0, currentInstrument)), 20, 42, 210, 16, juce::Justification::centredLeft, true);
}

void DrumEditor::paintGraph(juce::Graphics& graphics, const drum::Settings& settings)
{
    const auto plot = plotArea;
    graphics.setFont(uiFont(11.0f));
    const auto gainY = [&](double decibels) {
        return plot.getCentreY() - static_cast<float>(juce::jlimit(-18.0, 18.0, decibels)) * plot.getHeight() / 36;
    };
    graphics.setColour(juce::Colour(0xff151b1e));
    graphics.fillRoundedRectangle(plot.expanded(2.0f), 6.0f);
    for (int decibels : { -18, -12, -6, 0, 6, 12, 18 })
    {
        graphics.setColour(palette::outline.withAlpha(0.7f));
        graphics.drawHorizontalLine(static_cast<int>(gainY(decibels)), plot.getX(), plot.getRight());
        graphics.setColour(palette::textDim);
        graphics.drawText(juce::String(decibels > 0 ? "+" : "") + juce::String(decibels),
            24, static_cast<int>(gainY(decibels)) - 8, 34, 16, juce::Justification::centredRight);
    }
    for (int level : { 0, -20, -40, -60, -80, -100 })
        graphics.drawText(juce::String(level), static_cast<int>(plot.getRight()) + 6,
            static_cast<int>(plot.getY() - level * plot.getHeight() / 100) - 8, 40, 16, juce::Justification::centredLeft);
    for (float frequency : { 30.0f, 60.0f, 100.0f, 300.0f, 1000.0f, 3000.0f, 10000.0f })
    {
        const auto x = plot.getX() + plot.getWidth() * std::log(frequency / 20.0f) / std::log(1000.0f);
        graphics.setColour(palette::outline.withAlpha(0.7f));
        graphics.drawVerticalLine(static_cast<int>(x), plot.getY(), plot.getBottom());
        graphics.setColour(palette::textDim);
        graphics.drawText(frequency < 1000 ? juce::String(juce::roundToInt(frequency)) : juce::String(juce::roundToInt(frequency / 1000)) + "k",
            static_cast<int>(x) - 20, static_cast<int>(plot.getBottom()) + 2, 40, 14, juce::Justification::centred);
    }
    graphics.setColour(palette::textFaint);
    graphics.drawHorizontalLine(static_cast<int>(plot.getCentreY()), plot.getX(), plot.getRight());

    const auto sampleRate = owner.getSampleRate() > 0 ? owner.getSampleRate() : 48000.0;
    const drum::DrumFilterResponse filterResponse(settings, sampleRate);
    const drum::DrumDynamicResponse dynamicResponse(settings, sampleRate, { liveMeters[1], liveMeters[2] });
    const auto filterColour = juce::Colour(0xffe7c76b);
    std::array<juce::dsp::IIR::Coefficients<float>::Ptr, 4> response;
    const std::array<std::pair<int, int>, 4> responseBands {{ { drum::bodyFrequency, drum::bodyGain },
        { drum::boxFrequency, drum::boxGain }, { drum::attackFrequency, drum::attackGain }, { drum::airFrequency, drum::airGain } }};
    for (size_t index = 0; index < response.size(); ++index)
        response[index] = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate,
            juce::jmin(settings[static_cast<size_t>(responseBands[index].first)], static_cast<float>(sampleRate * 0.45)),
            drum::bandQ(settings, index), juce::Decibels::decibelsToGain(settings[static_cast<size_t>(responseBands[index].second)]));

    const auto stateText = filterResponse.enabled ? " / 12 dB/oct" : " / 12 dB/oct / OFF";
    graphics.setFont(uiFont(12.0f, true));
    graphics.setColour(filterResponse.enabled ? filterColour : palette::textFaint);
    graphics.drawText("HP " + frequencyText(filterResponse.highCutoff) + stateText, 64, 80, 200, 18, juce::Justification::centredLeft);
    graphics.setColour(filterResponse.enabled ? juce::Colour(0xffe8909a) : palette::textFaint);
    graphics.drawText("LP " + frequencyText(filterResponse.lowCutoff) + stateText, 270, 80, 200, 18, juce::Justification::centredLeft);
    graphics.setColour(settings[drum::eqOn] > 0.5f ? palette::accent : palette::textFaint);
    graphics.drawText(settings[drum::eqOn] > 0.5f ? "EQ + HP/LP" : "EQ OFF + HP/LP", 478, 80, 110, 18, juce::Justification::centredLeft);
    if (dynamicResponse.enabled)
    {
        graphics.setColour(palette::amber);
        graphics.drawText("DYN LIVE", 590, 80, 80, 18, juce::Justification::centredLeft);
    }

    const auto width = static_cast<int>(plot.getWidth());
    juce::Path curve, highCurve, lowCurve, spectrumCurve, dynamicCurve, dynamicArea;
    std::vector<float> staticY(static_cast<size_t>(width) + 1);
    for (int pixel = 0; pixel <= width; ++pixel)
    {
        const auto frequency = 20.0 * std::pow(1000.0, static_cast<double>(pixel) / width);
        double magnitude = filterResponse.magnitude(frequency);
        if (settings[drum::eqOn] > 0.5f)
            for (const auto& coefficients : response)
                magnitude *= coefficients->getMagnitudeForFrequency(juce::jmin(frequency, sampleRate * 0.49), sampleRate);
        const auto x = plot.getX() + static_cast<float>(pixel);
        const auto y = gainY(juce::Decibels::gainToDecibels(magnitude, -24.0));
        staticY[static_cast<size_t>(pixel)] = y;
        const auto liveY = gainY(juce::Decibels::gainToDecibels(magnitude * dynamicResponse.magnitude(frequency), -24.0));
        const auto highY = gainY(juce::Decibels::gainToDecibels(filterResponse.highMagnitude(frequency), -100.0));
        const auto lowY = gainY(juce::Decibels::gainToDecibels(filterResponse.lowMagnitude(frequency), -100.0));
        const auto spectrumY = plot.getY() - juce::jlimit(-100.0f, 0.0f, spectrumDisplay.magnitudeDb(frequency)) * plot.getHeight() / 100;
        if (pixel == 0)
        {
            curve.startNewSubPath(x, y);
            dynamicCurve.startNewSubPath(x, liveY);
            dynamicArea.startNewSubPath(x, liveY);
            highCurve.startNewSubPath(x, highY);
            lowCurve.startNewSubPath(x, lowY);
            spectrumCurve.startNewSubPath(x, spectrumY);
        }
        else
        {
            curve.lineTo(x, y);
            dynamicCurve.lineTo(x, liveY);
            dynamicArea.lineTo(x, liveY);
            highCurve.lineTo(x, highY);
            lowCurve.lineTo(x, lowY);
            spectrumCurve.lineTo(x, spectrumY);
        }
    }
    graphics.saveState();
    graphics.reduceClipRegion(plot.toNearestInt());
    if (spectrumSwitch.getToggleState())
    {
        auto spectrumFill = spectrumCurve;
        spectrumFill.lineTo(plot.getRight(), plot.getBottom());
        spectrumFill.lineTo(plot.getX(), plot.getBottom());
        spectrumFill.closeSubPath();
        graphics.setGradientFill(juce::ColourGradient(palette::blue.withAlpha(0.28f), 0.0f, plot.getY(),
            palette::blue.withAlpha(0.03f), 0.0f, plot.getBottom(), false));
        graphics.fillPath(spectrumFill);
        graphics.setColour(palette::blue.withAlpha(0.65f));
        graphics.strokePath(spectrumCurve, juce::PathStrokeType(1.0f));
    }
    if (filterResponse.enabled)
    {
        for (const auto cutoff : { filterResponse.highCutoff, filterResponse.lowCutoff })
        {
            const auto x = plot.getX() + plot.getWidth() * static_cast<float>(std::log(cutoff / 20.0) / std::log(1000.0));
            graphics.setColour(filterColour.withAlpha(0.45f));
            graphics.drawLine(x, plot.getY(), x, plot.getBottom(), 1.0f);
        }
        graphics.setColour(filterColour.withAlpha(0.6f));
        graphics.strokePath(highCurve, juce::PathStrokeType(1.4f));
        graphics.setColour(juce::Colour(0xffe8909a).withAlpha(0.6f));
        graphics.strokePath(lowCurve, juce::PathStrokeType(1.4f));
    }
    auto curveFill = curve;
    curveFill.lineTo(plot.getRight(), plot.getCentreY());
    curveFill.lineTo(plot.getX(), plot.getCentreY());
    curveFill.closeSubPath();
    graphics.setColour(palette::accent.withAlpha(0.10f));
    graphics.fillPath(curveFill);
    graphics.setColour(palette::accent.withAlpha(0.22f));
    graphics.strokePath(curve, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    graphics.setColour(palette::accent);
    graphics.strokePath(curve, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (dynamicResponse.enabled)
    {
        for (int pixel = width; pixel >= 0; --pixel)
            dynamicArea.lineTo(plot.getX() + pixel, staticY[static_cast<size_t>(pixel)]);
        dynamicArea.closeSubPath();
        graphics.setColour(palette::amber.withAlpha(0.2f));
        graphics.fillPath(dynamicArea);
        graphics.setColour(palette::amber);
        graphics.strokePath(dynamicCurve, juce::PathStrokeType(2.0f));
    }
    graphics.restoreState();
    if (hoverX >= plot.getX() && hoverX <= plot.getRight())
    {
        const auto frequency = 20.0 * std::pow(1000.0, (hoverX - plot.getX()) / plot.getWidth());
        auto magnitude = filterResponse.magnitude(frequency);
        if (settings[drum::eqOn] > 0.5f)
            for (const auto& coefficients : response)
                magnitude *= coefficients->getMagnitudeForFrequency(juce::jmin(frequency, sampleRate * 0.49), sampleRate);
        magnitude *= dynamicResponse.magnitude(frequency);
        graphics.setColour(palette::text.withAlpha(0.45f));
        graphics.drawVerticalLine(static_cast<int>(hoverX), plot.getY(), plot.getBottom());
        graphics.setColour(palette::text);
        graphics.setFont(uiFont(12.0f));
        graphics.drawText(frequencyText(frequency) + " / EQ + HP/LP + DYN " + juce::String(juce::Decibels::gainToDecibels(magnitude, -100.0), 2)
            + " dB" + (spectrumSwitch.getToggleState() ? " / OUT " + juce::String(spectrumDisplay.magnitudeDb(frequency), 1) + " dBFS" : ""),
            static_cast<int>(plot.getX()), 280, 600, 16, juce::Justification::centredLeft);
    }
}

void DrumEditor::paintMeters(juce::Graphics& graphics)
{
    const auto settings = owner.getSettings();
    const std::array<juce::String, 4> names { "IN", "OUT", settings[drum::gateOn] > 0.5f ? "EXP" : "EXP -",
        settings[drum::compressorOn] + settings[drum::glueOn] > 0.5f ? "COMP" : "CMP -" };
    const auto top = meterArea.getY() + 50, height = 140;
    for (size_t index = 0; index < liveMain.size(); ++index)
    {
        const auto x = meterArea.getX() + 30 + static_cast<int>(index) * 58;
        const auto decibels = index < 2 ? juce::Decibels::gainToDecibels(liveMain[index], -60.0f) : liveMain[index];
        const auto amount = index < 2 ? juce::jlimit(0.0f, 1.0f, (decibels + 60) / 60) : juce::jlimit(0.0f, 1.0f, decibels / 12.0f);
        const auto bar = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(top), 16.0f, static_cast<float>(height));
        graphics.setColour(juce::Colour(0xff11171a));
        graphics.fillRoundedRectangle(bar, 4.0f);
        const auto colour = index < 2 ? (liveMain[index] >= 1 ? palette::red : palette::accent) : juce::Colour(0xffe7c76b);
        const auto filled = index < 2 ? bar.withTrimmedTop(bar.getHeight() * (1.0f - amount)) : bar.withHeight(bar.getHeight() * amount);
        if (amount > 0.0f)
        {
            graphics.setColour(colour.withAlpha(0.18f));
            graphics.fillRoundedRectangle(filled.expanded(3.0f, 0.0f), 4.0f);
            graphics.setGradientFill(juce::ColourGradient(colour, 0.0f, bar.getY(), colour.withMultipliedBrightness(0.6f), 0.0f, bar.getBottom(), false));
            graphics.fillRoundedRectangle(filled, 4.0f);
        }
        graphics.setColour(palette::textDim);
        graphics.setFont(uiFont(11.5f, true));
        graphics.drawText(names[index], x - 14, top - 18, 44, 14, juce::Justification::centred);
        graphics.setColour(palette::text);
        graphics.setFont(uiFont(11.5f));
        graphics.drawText(juce::String(decibels, 1), x - 16, top + height + 4, 48, 14, juce::Justification::centred);
        graphics.setColour(palette::textFaint);
        graphics.setFont(uiFont(9.5f));
        const auto scale = index < 2 ? std::array<int, 3> { 0, -30, -60 } : std::array<int, 3> { 0, 6, 12 };
        for (size_t mark = 0; mark < scale.size(); ++mark)
        {
            const auto y = index < 2 ? top + static_cast<int>(mark) * height / 2 : top + height - static_cast<int>(mark) * height / 2;
            graphics.drawText(juce::String(std::abs(scale[mark])), x + 18, y - 6, 22, 12, juce::Justification::centredLeft);
        }
    }
}

void DrumEditor::paintIndicators(juce::Graphics& graphics)
{
    for (size_t index = 0; index < headerIndicators.size(); ++index)
    {
        const auto bounds = headerIndicators[index].getBounds();
        graphics.setColour(palette::track);
        graphics.fillRoundedRectangle(static_cast<float>(bounds.getX()), static_cast<float>(bounds.getBottom()), 118.0f, 2.0f, 1.0f);
        graphics.setColour(index == 1 || index == 2 ? palette::amber : palette::accent);
        graphics.fillRoundedRectangle(static_cast<float>(bounds.getX()), static_cast<float>(bounds.getBottom()),
            118.0f * activityLevels[index], 2.0f, 1.0f);
    }
}

void DrumEditor::paintPage(juce::Graphics& graphics, const drum::Settings& settings)
{
    for (const auto& info : panels) drawPanel(graphics, info.bounds, info.title, info.accent, info.active);
    const auto role = juce::jmax(0, currentInstrument);
    const auto caption = [&](const juce::String& text, int x, int y, int width, juce::Colour colour = palette::textDim,
                             float size = 12.5f, bool bold = false) {
        graphics.setColour(colour);
        graphics.setFont(uiFont(size, bold));
        graphics.drawText(text, x, y, width, 18, juce::Justification::centredLeft, true);
    };
    const auto subPanel = [&](juce::Rectangle<int> area) {
        graphics.setColour(juce::Colour(0xff161d20));
        graphics.fillRoundedRectangle(area.toFloat(), 8.0f);
        graphics.setColour(palette::outline.withAlpha(0.7f));
        graphics.drawRoundedRectangle(area.toFloat().reduced(0.5f), 8.0f, 1.0f);
    };
    switch (activePage)
    {
        case kitPage:
        {
            const auto identity = owner.getKitIdentity();
            caption("GROUPE", 30, 426, 200, palette::textFaint, 11.5f, true);
            caption("MICRO", 30, 484, 200, palette::textFaint, 11.5f, true);
            caption(identity.role == kit::Role::none ? juce::String("Hors kit (bus maitre)") : identity.name, 30, 502, 240, palette::text, 14.0f, true);
            caption(juce::String("Role : ") + kit::roleName(identity.role), 30, 520, 240, palette::textDim, 12.0f);
            caption("MODE", 30, 540, 200, palette::textFaint, 11.5f, true);
            const auto* own = owner.findOwnRecommendation(kitView.report);
            juce::String proposal = "Propose : analyser le kit";
            if (own != nullptr && own->measured && own->role != kit::Role::overhead)
                proposal = "Propose : +" + juce::String(own->delayMs, 2) + " ms" + (own->invert ? ", polarite inversee" : "");
            else if (own != nullptr && own->role == kit::Role::overhead)
                proposal = "Reference de temps : pas de retard";
            caption(proposal, 306, 578, 220, own != nullptr && own->invert ? palette::amber : palette::textDim, 11.5f, true);
            paintKitReport(graphics, { 536, 390, 368, 322 });
            break;
        }
        case filtersPage:
        {
            const auto hints = filterHints(role);
            caption(roleHint(role), 420, 440, 460, palette::text, 14.0f, true);
            caption("Coupe-bas : " + juce::String(hints[0]), 420, 470, 460);
            caption("Coupe-haut : " + juce::String(hints[1]), 420, 492, 460);
            caption("Deux filtres 12 dB/oct en tete de chaine, avant l'expander et l'EQ.", 420, 530, 460, palette::textFaint, 12.0f);
            caption("Courbes jaune / rose dans le graphique.", 420, 550, 460, palette::textFaint, 12.0f);
            break;
        }
        case expanderPage:
            caption(expanderDiscouraged(role) ? "Deconseille sur ce role : l'expander couperait les decroissances naturelles."
                : "Reduit la repisse entre les frappes ; plage et ratio doux pour garder les ghost notes.",
                40, 596, 820, expanderDiscouraged(role) ? palette::amber : palette::textDim, 13.0f);
            break;
        case focusPage:
            if (familyOf(role) == Family::snare || familyOf(role) == Family::tom)
                caption("Attenue l'aigu de la repisse (cymbales) entre les frappes detectees.", 500, 452, 390, palette::textDim, 12.5f);
            break;
        case eqPage:
        {
            const auto bands = eqBands(role);
            const std::array<const char*, 4> ranges { "30 - 200 Hz", "150 Hz - 1.2 kHz", "1 - 8 kHz", "4 - 16 kHz" };
            for (size_t index = 0; index < bands.size(); ++index)
            {
                const auto x = 26 + static_cast<int>(index) * 194;
                subPanel({ x, 428, 186, 276 });
                caption(juce::String(bands[index].name).toUpperCase(), x + 12, 434, 170, palette::accent, 14.0f, true);
                caption(juce::String(bands[index].hint) + " / " + ranges[index], x + 12, 452, 172, palette::textFaint, 11.0f);
            }
            subPanel({ 802, 428, 96, 276 });
            caption("GLOBAL", 814, 434, 80, palette::textDim, 13.0f, true);
            break;
        }
        case compressorPage:
        {
            if (settings[drum::multibandOn] > 0.5f)
            {
                const auto reductions = liveBands;
                const std::array<const char*, 3> names { "GRAVE", "MEDIUM", "AIGU" };
                for (size_t band = 0; band < 3; ++band)
                {
                    const auto x = 26 + static_cast<int>(band) * 292;
                    subPanel({ x, 476, 284, 228 });
                    caption(names[band], x + 12, 482, 120, palette::accent, 13.5f, true);
                    const auto meter = juce::Rectangle<float>(static_cast<float>(x + 12), 688.0f, 260.0f, 6.0f);
                    graphics.setColour(palette::track);
                    graphics.fillRoundedRectangle(meter, 3.0f);
                    graphics.setColour(juce::Colour(0xffe7c76b));
                    graphics.fillRoundedRectangle(meter.withWidth(meter.getWidth() * juce::jlimit(0.0f, 1.0f, reductions[band] / 12)), 3.0f);
                    caption("GR " + juce::String(reductions[band], 1) + " dB", x + 12, 666, 260, palette::textDim, 11.5f);
                }
            }
            else
            {
                const auto meter = juce::Rectangle<float>(812.0f, 444.0f, 18.0f, 200.0f);
                graphics.setColour(juce::Colour(0xff11171a));
                graphics.fillRoundedRectangle(meter, 4.0f);
                const auto amount = juce::jlimit(0.0f, 1.0f, liveMain[3] / 12.0f);
                graphics.setColour(juce::Colour(0xffe7c76b));
                graphics.fillRoundedRectangle(meter.withHeight(meter.getHeight() * amount), 4.0f);
                caption("GR", 806, 420, 40, palette::textDim, 12.0f, true);
                caption(juce::String(liveMain[3], 1) + " dB", 794, 650, 90, palette::text, 12.0f);
            }
            break;
        }
        case precisionPage:
        {
            const auto names = dynamicBands(role);
            for (int band = 0; band < 2; ++band)
            {
                caption(juce::String(names[static_cast<size_t>(band)]).toUpperCase(), 290, 470 + band * 138, 128, palette::amber, 14.0f, true);
                caption(band == 0 ? "80 Hz - 2 kHz" : "2 - 16 kHz", 290, 488 + band * 138, 128, palette::textFaint, 11.0f);
                caption("GR " + juce::String(liveMeters[static_cast<size_t>(1 + band)], 1) + " dB", 290, 508 + band * 138, 128, palette::text, 12.0f);
            }
            break;
        }
        default: break;
    }
}

void DrumEditor::paintKitReport(juce::Graphics& graphics, juce::Rectangle<int> area)
{
    auto content = area.reduced(14, 0).withTrimmedTop(34).withTrimmedBottom(10);
    const auto& report = kitView.report;
    graphics.setFont(uiFont(13.0f, true));
    graphics.setColour(kitView.analysing ? palette::accent : palette::text);
    graphics.drawFittedText(kitView.status, content.removeFromTop(34), juce::Justification::topLeft, 2, 0.85f);
    const auto progressBar = content.removeFromTop(8).toFloat();
    graphics.setColour(palette::track);
    graphics.fillRoundedRectangle(progressBar, 3.0f);
    if (report.progress > 0)
    {
        graphics.setColour(palette::accent);
        graphics.fillRoundedRectangle(progressBar.withWidth(progressBar.getWidth() * juce::jlimit(0.0f, 1.0f, report.progress)), 3.0f);
    }
    content.removeFromTop(8);
    const auto* mine = owner.findOwnRecommendation(report);
    graphics.setFont(uiFont(12.5f, true));
    graphics.setColour(palette::textDim);
    juce::String mineText = "Cette piste : ";
    if (mine == nullptr) mineText += report.recommendations.empty() ? "pas encore analysee" : "hors de la derniere analyse";
    else if (!mine->measured) mineText += mine->note;
    else if (mine->role == kit::Role::overhead) mineText += "reference du kit";
    else
    {
        juce::StringArray parts;
        if (mine->delayMs >= 0.01f) parts.add("retarder " + juce::String(mine->delayMs, 2) + " ms");
        if (mine->invert) parts.add("inverser la polarite");
        mineText += parts.isEmpty() ? juce::String("deja coherente") : parts.joinIntoString(", ");
    }
    graphics.setColour(palette::text);
    graphics.drawFittedText(mineText, content.removeFromTop(18), juce::Justification::centredLeft, 1, 0.8f);
    content.removeFromTop(6);

    auto list = content.removeFromTop(118);
    graphics.setFont(uiFont(12.0f));
    std::vector<const kit::MemberView*> shownMembers;
    int silent = 0;
    for (const auto& member : kitView.members)
        if (member.hasAudio || member.id == kitMemberId || report.find(member.id) != nullptr) shownMembers.push_back(&member);
        else ++silent;
    std::stable_sort(shownMembers.begin(), shownMembers.end(), [](const auto* a, const auto* b) { return a->live && !b->live; });
    const auto maximumRows = silent > 0 ? 5 : 6;
    int row = 0;
    for (const auto* entry : shownMembers)
    {
        const auto& member = *entry;
        if (row == maximumRows)
        {
            graphics.setColour(palette::textFaint);
            graphics.drawText("+ " + juce::String(static_cast<int>(shownMembers.size()) - maximumRows) + " autres pistes",
                list.removeFromTop(17), juce::Justification::centredLeft);
            break;
        }
        auto line = list.removeFromTop(19);
        graphics.setColour(member.live ? palette::accent : palette::textFaint);
        graphics.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({ static_cast<float>(line.getX() + 4), static_cast<float>(line.getCentreY()) }));
        line.removeFromLeft(14);
        const auto* recommendation = report.find(member.id);
        juce::String right;
        if (recommendation != nullptr && recommendation->measured)
        {
            if (recommendation->role == kit::Role::overhead) right = "reference";
            else
            {
                right = recommendation->delayMs >= 0.01f ? "+" + juce::String(recommendation->delayMs, 2) + " ms" : juce::String("0 ms");
                if (recommendation->invert) right += "  INV";
            }
        }
        else if (member.identity.role == kit::Role::none) right = "hors kit";
        graphics.setColour(member.id == kitMemberId ? palette::text : palette::textDim);
        graphics.drawText(member.identity.name, line.withTrimmedRight(110), juce::Justification::centredLeft, true);
        graphics.setColour(recommendation != nullptr && recommendation->invert ? palette::amber : palette::text);
        graphics.drawText(right, line.removeFromRight(108), juce::Justification::centredRight);
        ++row;
    }
    if (silent > 0)
    {
        graphics.setColour(palette::textFaint);
        graphics.drawText(juce::String(silent) + " instance(s) sans lecture, ignoree(s) par l'analyse",
            list.removeFromTop(17), juce::Justification::centredLeft, true);
    }
    if (kitView.members.empty())
    {
        graphics.setColour(palette::textFaint);
        graphics.drawText("Aucune autre piste dans ce groupe", list.removeFromTop(18), juce::Justification::centredLeft);
    }

    const auto& scores = report.scores;
    const std::array<std::tuple<const char*, float, float>, 3> gauges {{
        { "PHASE", scores.phaseBefore, scores.phaseAfter },
        { "STEREO", scores.stereo, scores.stereo },
        { "ROOM", scores.roomBefore, scores.roomAfter } }};
    const auto gaugeWidth = area.getWidth() / 3;
    for (size_t index = 0; index < gauges.size(); ++index)
    {
        const auto& [name, before, after] = gauges[index];
        const auto centre = juce::Point<float>(static_cast<float>(area.getX() + gaugeWidth * static_cast<int>(index) + gaugeWidth / 2),
            static_cast<float>(area.getBottom() - 62));
        const auto radius = 28.0f;
        const auto start = juce::MathConstants<float>::pi * 1.22f, end = juce::MathConstants<float>::pi * 2.78f;
        juce::Path track;
        track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, start, end, true);
        graphics.setColour(palette::track);
        graphics.strokePath(track, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (after >= 0)
        {
            const auto colour = after >= 75 ? palette::accent : after >= 50 ? palette::amber : palette::red;
            DrumLookAndFeel::drawGlowArc(graphics, centre, radius, start, start + (end - start) * after / 100.0f, colour, 5.0f);
        }
        if (before >= 0 && std::abs(after - before) > 0.5f)
        {
            const auto marker = centre.getPointOnCircumference(radius, start + (end - start) * before / 100.0f);
            graphics.setColour(palette::text);
            graphics.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(marker));
        }
        graphics.setColour(palette::text);
        graphics.setFont(uiFont(17.0f, true));
        graphics.drawText(after >= 0 ? juce::String(juce::roundToInt(after)) : juce::String("--"),
            juce::Rectangle<float>(60.0f, 22.0f).withCentre(centre).toNearestInt(), juce::Justification::centred);
        graphics.setColour(palette::textDim);
        graphics.setFont(uiFont(11.5f, true));
        const auto labelArea = juce::Rectangle<int>(static_cast<int>(centre.x) - 60, static_cast<int>(centre.y) + 30, 120, 14);
        graphics.drawText(name, labelArea, juce::Justification::centred);
        if (before >= 0 && std::abs(after - before) > 0.5f)
        {
            graphics.setColour(palette::textFaint);
            graphics.setFont(uiFont(11.0f));
            graphics.drawText("avant " + juce::String(juce::roundToInt(before)), labelArea.translated(0, 14), juce::Justification::centred);
        }
    }
}

void DrumEditor::mouseMove(const juce::MouseEvent& event)
{
    hoverX = plotArea.contains(event.position) ? event.position.x : -1.0f;
    repaint(graphArea);
}

void DrumEditor::timerCallback()
{
    if (spectrumSwitch.getToggleState()) spectrumDisplay.update(owner.getSpectrumMailbox());
    owner.updateAutoPreset();
    const auto settings = owner.getSettings();
    const auto role = static_cast<int>(drum::instrumentFromValue(settings[drum::instrument]));
    const auto focusVisible = pageVisible(focusPage);
    if (role != currentInstrument)
    {
        currentInstrument = role;
        refreshNames();
        lastFocusVisible = focusVisible;
        resized();
    }
    else if (focusVisible != lastFocusVisible || lastMultiband != (settings[drum::multibandOn] > 0.5f))
    {
        lastFocusVisible = focusVisible;
        lastMultiband = settings[drum::multibandOn] > 0.5f;
        resized();
    }
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
    autoReport.setText((detail >= 0 ? reportText.substring(0, detail) : reportText.upToFirstOccurrenceOf("\n", false, false)).substring(0, 80),
        juce::dontSendNotification);
    autoReport.setTooltip(reportText);
    status.setText(owner.getDiagnosticText(), juce::dontSendNotification);
    context.setText(owner.isBoundToARA() ? (owner.canEditSettings() ? "ARA / Selection active" : "ARA / Aucune region selectionnee")
        : "VST3 / Insert", juce::dontSendNotification);

    const bool bypassed = settings[drum::bypass] > 0.5f;
    const bool streamFresh = staleMeterTicks <= 3;
    const bool fresh = streamFresh && !bypassed;
    for (size_t index = 0; index < liveMeters.size(); ++index)
    {
        const auto target = fresh ? finishMeters[index] : 0.0f;
        liveMeters[index] = std::abs(target) >= std::abs(liveMeters[index]) ? target : liveMeters[index] + 0.35f * (target - liveMeters[index]);
    }
    const auto rawBands = owner.getBandMeters();
    const auto rawMain = owner.getMeters();
    for (size_t index = 0; index < liveMain.size(); ++index)
        liveMain[index] = juce::jmax(streamFresh && (index < 2 || !bypassed) ? rawMain[index] : 0.0f, liveMain[index] * 0.65f);
    for (size_t band = 0; band < liveBands.size(); ++band)
        liveBands[band] = juce::jmax(fresh ? rawBands[band] : 0.0f, liveBands[band] * 0.65f);
    const auto db = [](float value) { return juce::String(value, 1) + " dB"; };
    const auto peak = [](float value) {
        return value > 0.000001f ? juce::String(static_cast<int>(std::lround(juce::Decibels::gainToDecibels(value)))) + " dBFS" : juce::String("-- dBFS");
    };
    const auto hz = [](float value) { return value < 1000 ? juce::String(juce::roundToInt(value)) : juce::String(value / 1000, 1) + "k"; };
    const auto report = [&](size_t index, const juce::String& name, bool enabled, const juce::String& text, float activity) {
        headerIndicators[index].setText(name + " " + (bypassed ? "BYP" : enabled ? text : "OFF"), juce::dontSendNotification);
        headerIndicators[index].setColour(juce::Label::textColourId, enabled && !bypassed ? palette::text : palette::textFaint);
        activityLevels[index] = enabled && !bypassed ? juce::jlimit(0.0f, 1.0f, activity) : 0.0f;
    };
    const auto peakActivity = [](float value) {
        return value > 0.000001f ? juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(value) + 72) / 72) : 0.0f;
    };
    report(0, "CRUSH", settings[drum::crushOn] > 0.5f, db(liveMeters[0]), liveMeters[0] / 12);
    report(1, "DYN G " + hz(settings[drum::dynamicLowHz]), settings[drum::dynamicOn] > 0.5f, db(liveMeters[1]), liveMeters[1] / 6);
    report(2, "DYN A " + hz(settings[drum::dynamicHighHz]), settings[drum::dynamicOn] > 0.5f, db(liveMeters[2]), liveMeters[2] / 6);
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

    instrumentMenu.setSelectedId(role + 1, juce::dontSendNotification);
    instrumentMenu.setEnabled(owner.canEditSettings());
    const auto enabled = owner.canEditSettings();
    for (size_t index = 0; index < settings.size(); ++index)
    {
        if (!sliders[index].isMouseButtonDown()) sliders[index].setValue(settings[index], juce::dontSendNotification);
        toggles[index].setToggleState(settings[index] > 0.5f, juce::dontSendNotification);
        sliders[index].setEnabled(enabled);
        toggles[index].setEnabled(index == drum::bypass ? owner.canBypass() : enabled);
    }
    toggles[drum::bypass].setTooltip(enabled ? "Bypass de la selection active (inclut la correction Kit / Phase)"
        : owner.canBypass() ? "Bypass de la derniere selection ARA editee, meme sans selection active"
        : "Selectionner une region ARA au moins une fois pour activer Bypass");

    positionMenu.setSelectedId(static_cast<int>(std::lround(owner.getKitControl(ZeroEffortDrumProcessor::kitMic))) + 1, juce::dontSendNotification);
    kitGroupMenu.setSelectedId(static_cast<int>(std::lround(owner.getKitControl(ZeroEffortDrumProcessor::kitGroup))) + 1, juce::dontSendNotification);
    const auto mode = static_cast<int>(owner.getKitMode());
    for (size_t index = 0; index < kitModes.size(); ++index)
        kitModes[index].setToggleState(static_cast<int>(index) == mode, juce::dontSendNotification);
    if (!kitStrength.isMouseButtonDown()) kitStrength.setValue(owner.getKitControl(ZeroEffortDrumProcessor::kitStrength), juce::dontSendNotification);
    if (!kitDelay.isMouseButtonDown()) kitDelay.setValue(owner.getKitControl(ZeroEffortDrumProcessor::kitDelay), juce::dontSendNotification);
    kitPolarity.setToggleState(owner.getKitControl(ZeroEffortDrumProcessor::kitPolarity) > 0.5f, juce::dontSendNotification);
    for (juce::Component* component : { static_cast<juce::Component*>(&kitGroupMenu), static_cast<juce::Component*>(&positionMenu),
                                        static_cast<juce::Component*>(&kitStrength), static_cast<juce::Component*>(&kitDelay),
                                        static_cast<juce::Component*>(&kitPolarity) })
        component->setEnabled(enabled);
    for (auto& button : kitModes) button.setEnabled(enabled);
    if (tickCount++ % 4 == 0 || kitView.members.empty())
    {
        kitView = owner.getKitView();
        kitMemberId = owner.getKitMemberId();
        kitAnalyse.setButtonText(kitView.analysing ? "Arreter l'analyse" : "Analyser le kit");
        kitApply.setEnabled(kitView.report.final && kitView.report.valid);
        if (previewPending && !kitView.analysing && kitView.report.final)
        {
            previewPending = false;
            if (kitView.report.valid)
            {
                selectPage(kitPage);
                preview.show(kitView.report);
            }
        }
        kitReport.setEnabled(!kitView.report.lines.isEmpty() || kitView.status.isNotEmpty());
    }
    repaint();
}

}

juce::AudioProcessorEditor* createEditor(ZeroEffortDrumProcessor& processor)
{
    return new DrumEditor(processor);
}
}
