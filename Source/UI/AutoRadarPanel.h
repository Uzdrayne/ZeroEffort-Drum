#pragma once
#include "DrumControlNames.h"
#include "DrumLookAndFeel.h"
#include "../Drum/DrumAutoRadar.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace drum::ui
{
// Overlay shown while Auto runs: a polar chart that builds up in three steps (what was measured, what the proposal aims at,
// what was obtained), the list of what changes, and the choice to apply or ignore. It draws what the processor publishes
// (AutoRadarView); it never touches the audio and never decides anything.
class AutoRadarPanel final : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onApply, onIgnore, onCancel, onDetails;

    AutoRadarPanel()
    {
        setComponentID("autoRadarPanel");
        applyButton.setComponentID("autoRadarApply");
        ignoreButton.setComponentID("autoRadarIgnore");
        cancelButton.setComponentID("autoRadarCancel");
        detailsButton.setComponentID("autoRadarDetails");
        applyButton.getProperties().set("primary", true);
        applyButton.setTooltip("Appliquer la proposition aux reglages de cette piste (Revenir permet de la retirer)");
        ignoreButton.setTooltip("Ne rien modifier : les reglages restent comme avant l'analyse");
        cancelButton.setTooltip("Interrompre l'analyse");
        detailsButton.setTooltip("Afficher le bilan complet en texte");
        applyButton.onClick = [this] { if (onApply) onApply(); };
        ignoreButton.onClick = [this] { if (onIgnore) onIgnore(); };
        cancelButton.onClick = [this] { if (onCancel) onCancel(); };
        detailsButton.onClick = [this] { if (onDetails) onDetails(); };
        for (auto* button : { &applyButton, &ignoreButton, &cancelButton, &detailsButton }) addAndMakeVisible(*button);
        shownSource.fill(0.0f); shownTarget.fill(0.0f); shownResult.fill(0.0f);
        setWantsKeyboardFocus(false);
    }

    ~AutoRadarPanel() override { stopTimer(); }

    void open()
    {
        if (isVisible()) return;
        shownSource.fill(0.0f); shownTarget.fill(0.0f); shownResult.fill(0.0f);
        targetSeeded = resultSeeded = false;
        started = juce::Time::getMillisecondCounterHiRes();
        setVisible(true);
        toFront(true);
        startTimerHz(30);
        resized();
    }

    void close()
    {
        stopTimer();
        setVisible(false);
    }

    // Called by the editor every tick with what the processor published.
    void update(const drum::AutoRadarView& newView, const std::vector<drum::Change>& newChanges)
    {
        view = newView;
        changes = newChanges;
        if (view.hasTarget && !targetSeeded) { shownTarget = shownSource; targetSeeded = true; }     // grows out of the measured shape
        if (view.hasResult && !resultSeeded && view.result.any()) { shownResult = shownTarget; resultSeeded = true; }
        applyButton.setEnabled(view.ready);
        ignoreButton.setEnabled(view.ready || view.refused);
        ignoreButton.setButtonText(view.refused ? "Fermer" : "Ignorer");
        applyButton.setVisible(!view.refused);
        cancelButton.setVisible(view.busy);
        ignoreButton.setVisible(!view.busy);
        detailsButton.setVisible(!view.busy);
        repaint();
    }

    // Test access: the curves as drawn, after the animation has settled.
    void settleForTest()
    {
        for (int step = 0; step < 200; ++step) animate();
        repaint();
    }
    const drum::AutoRadarView& currentView() const { return view; }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(palette::background.withAlpha(0.78f));
        const auto box = boxBounds();
        drawPanel(graphics, box, "AUTO / " + (view.title.isNotEmpty() ? view.title.toUpperCase() : juce::String("ANALYSE")), palette::accent);
        paintSteps(graphics, box);
        paintChart(graphics, box);
        paintChanges(graphics, box);
        paintLegend(graphics, box);
    }

    void resized() override
    {
        const auto box = boxBounds();
        const auto y = box.getBottom() - 46;
        applyButton.setBounds(box.getX() + 24, y, 200, 32);
        ignoreButton.setBounds(box.getX() + 234, y, 150, 32);
        cancelButton.setBounds(box.getX() + 24, y, 200, 32);
        detailsButton.setBounds(box.getRight() - 124, y, 100, 32);
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        const auto previous = hoverAxis;
        hoverAxis = axisAt(event.position);
        if (previous != hoverAxis) repaint();
    }
    void mouseExit(const juce::MouseEvent&) override { if (hoverAxis >= 0) { hoverAxis = -1; repaint(); } }

private:
    static constexpr size_t axes = drum::radarAxisCount;

    juce::Rectangle<int> boxBounds() const
    {
        return juce::Rectangle<int>(860, 620).withCentre(getLocalBounds().getCentre()).constrainedWithin(getLocalBounds());
    }
    juce::Rectangle<int> chartBounds() const { const auto box = boxBounds(); return { box.getX() + 18, box.getY() + 72, 500, 470 }; }
    juce::Point<float> centre() const { return chartBounds().toFloat().getCentre(); }
    float radius() const { return 160.0f; }

    // Axis 0 at the top, clockwise
    float angleOf(size_t axis) const { return juce::MathConstants<float>::twoPi * static_cast<float>(axis) / static_cast<float>(axes) - juce::MathConstants<float>::halfPi; }
    juce::Point<float> pointAt(size_t axis, float unit, float scale = 1.0f) const
    {
        const auto distance = radius() * scale * (0.05f + 0.95f * unit);
        return { centre().x + distance * std::cos(angleOf(axis)), centre().y + distance * std::sin(angleOf(axis)) };
    }

    int axisAt(juce::Point<float> position) const
    {
        const auto offset = position - centre();
        if (offset.getDistanceFromOrigin() < 24.0f || offset.getDistanceFromOrigin() > radius() + 60.0f) return -1;
        auto angle = std::atan2(offset.y, offset.x) + juce::MathConstants<float>::halfPi;
        if (angle < 0) angle += juce::MathConstants<float>::twoPi;
        const auto index = static_cast<int>(std::lround(angle / (juce::MathConstants<float>::twoPi / static_cast<float>(axes))));
        return index % static_cast<int>(axes);
    }

    void timerCallback() override { animate(); repaint(chartBounds()); }

    void animate()
    {
        const auto follow = [](std::array<float, axes>& shown, const drum::RadarCurve& curve) {
            for (size_t axis = 0; axis < axes; ++axis)
            {
                const auto goal = curve.unit(axis);
                shown[axis] += 0.2f * ((std::isfinite(goal) ? goal : 0.0f) - shown[axis]);
            }
        };
        follow(shownSource, view.source);
        if (view.hasTarget) follow(shownTarget, view.target);
        if (view.hasResult && resultSeeded) follow(shownResult, view.result);
    }

    static juce::String valueText(const drum::RadarAxis& axis, float value)
    {
        if (!std::isfinite(value)) return "n/d";
        const juce::String unit(axis.unit);
        if (unit == "ms") return juce::String(juce::roundToInt(value)) + " ms";
        if (unit == "/s") return juce::String(value, 1) + " /s";
        return juce::String(value, 1) + " dB";
    }

    static juce::String controlText(int control, float value)
    {
        const auto& spec = drum::controlSpecs[static_cast<size_t>(control)];
        if (spec.toggle) return value > 0.5f ? "ON" : "OFF";
        const juce::String unit(spec.unit);
        if (unit == "Hz") return value >= 1000.0f ? juce::String(value / 1000.0f, 2) + " kHz" : juce::String(juce::roundToInt(value)) + " Hz";
        if (unit == "dB") return juce::String(value, 1) + " dB";
        if (unit == "ms") return juce::String(value, value < 10.0f ? 2 : value < 100.0f ? 1 : 0) + " ms";
        if (unit == "%") return juce::String(juce::roundToInt(value)) + " %";
        if (unit == ":1") return juce::String(value, 1) + ":1";
        return juce::String(value, 2);
    }

    void paintSteps(juce::Graphics& graphics, juce::Rectangle<int> box) const
    {
        const std::array<const char*, 3> names { "1  Analyse", "2  Calcul", "3  Verification" };
        const std::array<juce::Colour, 3> colours { palette::blue, palette::amber, palette::accent };
        const auto step = static_cast<int>(view.phase);
        for (int index = 0; index < 3; ++index)
        {
            const juce::Rectangle<int> chip(box.getX() + 24 + index * 150, box.getY() + 38, 142, 24);
            const bool done = step > index || view.phase == drum::AutoRadarView::finished;
            const bool current = step == index && view.phase != drum::AutoRadarView::finished;
            graphics.setColour(colours[static_cast<size_t>(index)].withAlpha(current ? 0.30f : done ? 0.16f : 0.05f));
            graphics.fillRoundedRectangle(chip.toFloat(), 12.0f);
            graphics.setColour(colours[static_cast<size_t>(index)].withAlpha(current || done ? 0.9f : 0.3f));
            graphics.drawRoundedRectangle(chip.toFloat().reduced(0.5f), 12.0f, 1.0f);
            if (current)
            {
                graphics.setColour(colours[static_cast<size_t>(index)].withAlpha(0.8f));
                graphics.fillRoundedRectangle(chip.toFloat().withWidth(chip.getWidth() * juce::jlimit(0.04f, 1.0f, view.progress)), 12.0f);
                graphics.setColour(palette::background);
            }
            else graphics.setColour(done ? palette::text : palette::textFaint);
            graphics.setFont(uiFont(12.5f, true));
            graphics.drawText(names[static_cast<size_t>(index)], chip, juce::Justification::centred);
        }
        if (view.refused)
        {
            graphics.setColour(palette::red);
            graphics.setFont(uiFont(12.5f));
            graphics.drawFittedText(view.message.upToFirstOccurrenceOf("\n", false, false), { box.getX() + 24, box.getY() + 66, box.getWidth() - 48, 20 },
                juce::Justification::centredLeft, 1);
        }
    }

    void paintCurve(juce::Graphics& graphics, const std::array<float, axes>& shown, const drum::RadarCurve& curve, juce::Colour colour,
                    float fillAlpha, float lineAlpha, bool dots) const
    {
        juce::Path path;
        for (size_t axis = 0; axis < axes; ++axis)
        {
            const auto point = pointAt(axis, shown[axis]);
            if (axis == 0) path.startNewSubPath(point); else path.lineTo(point);
        }
        path.closeSubPath();
        graphics.setColour(colour.withAlpha(fillAlpha));
        graphics.fillPath(path);
        graphics.setColour(colour.withAlpha(lineAlpha));
        graphics.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));
        if (!dots) return;
        for (size_t axis = 0; axis < axes; ++axis)
        {
            const auto point = pointAt(axis, shown[axis]);
            const bool available = std::isfinite(curve.unit(axis));
            graphics.setColour(colour.withAlpha(available ? 0.35f * lineAlpha : 0.15f));
            graphics.fillEllipse(point.x - 7.0f, point.y - 7.0f, 14.0f, 14.0f);
            graphics.setColour(available ? colour.withAlpha(lineAlpha) : palette::textFaint);
            graphics.fillEllipse(point.x - 3.5f, point.y - 3.5f, 7.0f, 7.0f);
        }
    }

    void paintChart(juce::Graphics& graphics, juce::Rectangle<int> box) const
    {
        juce::ignoreUnused(box);
        const auto middle = centre();
        // rings and spokes
        for (int ring = 1; ring <= 4; ++ring)
        {
            graphics.setColour(palette::outline.withAlpha(ring == 4 ? 0.9f : 0.55f));
            const auto r = radius() * (0.05f + 0.95f * static_cast<float>(ring) / 4.0f);
            graphics.drawEllipse(middle.x - r, middle.y - r, 2 * r, 2 * r, 1.0f);
        }
        for (size_t axis = 0; axis < axes; ++axis)
        {
            graphics.setColour(palette::outline.withAlpha(static_cast<int>(axis) == hoverAxis ? 1.0f : 0.6f));
            graphics.drawLine({ pointAt(axis, 0.0f, 0.0f), pointAt(axis, 1.0f) }, static_cast<int>(axis) == hoverAxis ? 1.6f : 1.0f);
            const auto label = pointAt(axis, 1.0f, 1.0f) + juce::Point<float>(std::cos(angleOf(axis)), std::sin(angleOf(axis))) * 26.0f;
            graphics.setColour(static_cast<int>(axis) == hoverAxis ? palette::text : axis < 7 ? palette::textDim : palette::amber.withAlpha(0.8f));
            graphics.setFont(uiFont(12.0f, static_cast<int>(axis) == hoverAxis));
            graphics.drawText(drum::radarAxes[axis].label, juce::Rectangle<float>(80.0f, 18.0f).withCentre(label).toNearestInt(), juce::Justification::centred);
        }
        // a sweep while the analysis runs
        if (view.busy)
        {
            const auto angle = static_cast<float>(std::fmod((juce::Time::getMillisecondCounterHiRes() - started) / 2400.0, 1.0)) * juce::MathConstants<float>::twoPi;
            juce::ColourGradient glow(palette::accent.withAlpha(0.20f), middle, palette::accent.withAlpha(0.0f), middle + juce::Point<float>(radius(), 0.0f), true);
            graphics.setGradientFill(glow);
            graphics.drawLine({ middle, middle + juce::Point<float>(std::cos(angle), std::sin(angle)) * radius() }, 2.0f);
        }
        const bool later = view.phase != drum::AutoRadarView::analysis;
        paintCurve(graphics, shownSource, view.source, palette::blue, later ? 0.10f : 0.26f, later ? 0.45f : 1.0f, true);
        if (view.hasTarget)
            paintCurve(graphics, shownTarget, view.target, palette::amber, view.hasResult ? 0.08f : 0.22f, view.hasResult ? 0.5f : 1.0f, !view.hasResult);
        if (view.hasResult && resultSeeded)
            paintCurve(graphics, shownResult, view.result, palette::accent, 0.26f, 1.0f, true);
        graphics.setColour(palette::textFaint);
        graphics.setFont(uiFont(10.5f));
        graphics.drawText("Centre = faible, bord = fort (echelle propre a chaque axe)", chartBounds().withTrimmedTop(chartBounds().getHeight() - 16), juce::Justification::centred);
    }

    void paintLegend(juce::Graphics& graphics, juce::Rectangle<int> box) const
    {
        const auto corner = juce::Rectangle<int>(box.getX() + 24, box.getY() + 92, 170, 64);
        const std::array<std::pair<const char*, juce::Colour>, 3> entries {{ { "Source (mesure)", palette::blue },
            { "Vise (calcul)", palette::amber }, { "Obtenu (rendu)", palette::accent } }};
        const std::array<bool, 3> shown { true, view.hasTarget, view.hasResult };
        for (size_t index = 0; index < entries.size(); ++index)
        {
            const auto y = corner.getY() + static_cast<int>(index) * 20;
            graphics.setColour(entries[index].second.withAlpha(shown[index] ? 1.0f : 0.25f));
            graphics.fillEllipse(static_cast<float>(corner.getX()), static_cast<float>(y + 4), 10.0f, 10.0f);
            graphics.setColour(shown[index] ? palette::text : palette::textFaint);
            graphics.setFont(uiFont(12.0f));
            graphics.drawText(entries[index].first, corner.getX() + 18, y, 150, 18, juce::Justification::centredLeft);
        }
        const auto info = juce::Rectangle<int>(box.getX() + 540, box.getY() + 478, 296, 84);
        graphics.setColour(palette::outline);
        graphics.drawHorizontalLine(info.getY() - 6, static_cast<float>(info.getX()), static_cast<float>(info.getRight()));
        if (hoverAxis >= 0)
        {
            const auto axis = static_cast<size_t>(hoverAxis);
            const auto& spec = drum::radarAxes[axis];
            graphics.setColour(palette::text);
            graphics.setFont(uiFont(12.5f, true));
            graphics.drawText(spec.label, info.getX(), info.getY(), 290, 18, juce::Justification::centredLeft);
            graphics.setColour(palette::textDim);
            graphics.setFont(uiFont(11.5f));
            graphics.drawFittedText(spec.meaning, { info.getX(), info.getY() + 18, 290, 30 }, juce::Justification::topLeft, 2);
            juce::String values = "Source " + valueText(spec, view.source.raw[axis]);
            if (view.hasTarget) values << "  /  Vise " << valueText(spec, view.target.raw[axis]);
            if (view.hasResult && view.result.any()) values << "  /  Obtenu " << valueText(spec, view.result.raw[axis]);
            graphics.setColour(palette::text);
            graphics.drawFittedText(values, { info.getX(), info.getY() + 52, 290, 30 }, juce::Justification::topLeft, 2);
        }
        else
        {
            graphics.setColour(palette::textFaint);
            graphics.setFont(uiFont(11.5f));
            graphics.drawFittedText("Passez la souris sur un axe pour lire ses valeurs.", info.withHeight(30), juce::Justification::topLeft, 2);
        }
    }

    void paintChanges(juce::Graphics& graphics, juce::Rectangle<int> box) const
    {
        const auto area = juce::Rectangle<int>(box.getX() + 540, box.getY() + 74, 296, 390);
        const auto heading = [&](const juce::String& text, int y) {
            graphics.setColour(palette::textDim);
            graphics.setFont(uiFont(12.5f, true));
            graphics.drawText(text, area.getX(), y, area.getWidth(), 20, juce::Justification::centredLeft);
            graphics.setColour(palette::outline);
            graphics.drawHorizontalLine(y + 22, static_cast<float>(area.getX()), static_cast<float>(area.getRight()));
        };
        if (view.refused) return;   // nothing was proposed: the message above says why
        heading("MODULES", area.getY());
        std::vector<const drum::Change*> modules, values;
        for (const auto& change : changes) (drum::controlSpecs[change.control].toggle ? modules : values).push_back(&change);
        if (changes.empty())
        {
            graphics.setColour(palette::textFaint);
            graphics.setFont(uiFont(12.0f));
            graphics.drawFittedText(view.phase == drum::AutoRadarView::analysis ? "La proposition apparait apres l'analyse."
                : "Aucun reglage ne change de facon notable.", area.withTrimmedTop(32).withHeight(40), juce::Justification::topLeft, 2);
            return;
        }
        // module switches as pills: green when switched on, amber when switched off
        constexpr int pillWidth = 94, pillHeight = 20;
        for (size_t index = 0; index < modules.size(); ++index)
        {
            const auto& change = *modules[index];
            const juce::Rectangle<int> pill(area.getX() + static_cast<int>(index % 3) * (pillWidth + 4), area.getY() + 30 + static_cast<int>(index / 3) * (pillHeight + 4), pillWidth, pillHeight);
            const auto on = change.after > 0.5f;
            graphics.setColour((on ? palette::accent : palette::amber).withAlpha(0.16f));
            graphics.fillRoundedRectangle(pill.toFloat(), 10.0f);
            graphics.setColour((on ? palette::accent : palette::amber).withAlpha(0.8f));
            graphics.drawRoundedRectangle(pill.toFloat().reduced(0.5f), 10.0f, 1.0f);
            graphics.setColour(palette::text);
            graphics.setFont(uiFont(9.5f, true));
            graphics.drawText((on ? "" : "- ") + drum::ui::switchName(static_cast<int>(change.control)), pill.reduced(2, 0), juce::Justification::centred, true);
        }
        const auto rows = static_cast<int>((modules.size() + 2) / 3);
        const auto valuesTop = area.getY() + 30 + rows * (pillHeight + 4) + 10;
        heading("REGLAGES", valuesTop);
        const auto rowHeight = 28;
        for (size_t index = 0; index < values.size() && index < 6; ++index)
        {
            const auto& change = *values[index];
            const auto y = valuesTop + 30 + static_cast<int>(index) * rowHeight;
            const bool up = change.after > change.before;
            graphics.setColour(palette::text);
            graphics.setFont(uiFont(12.0f));
            graphics.drawText(drum::ui::longName(static_cast<int>(change.control), view.role), area.getX(), y, 150, 16, juce::Justification::centredLeft, true);
            graphics.setColour(palette::textDim);
            graphics.setFont(uiFont(11.0f));
            graphics.drawText(controlText(static_cast<int>(change.control), change.before) + "  >  " + controlText(static_cast<int>(change.control), change.after),
                area.getX() + 150, y, 146, 16, juce::Justification::centredRight, true);
            const auto width = juce::jmax(3.0f, 290.0f * juce::jlimit(0.0f, 1.0f, change.significance * 4.0f));
            graphics.setColour((up ? palette::accent : palette::amber).withAlpha(0.75f));
            graphics.fillRoundedRectangle(static_cast<float>(area.getX()), static_cast<float>(y + 18), width, 4.0f, 2.0f);
        }
    }
    drum::AutoRadarView view;
    std::vector<drum::Change> changes;
    std::array<float, axes> shownSource {}, shownTarget {}, shownResult {};
    bool targetSeeded = false, resultSeeded = false;
    int hoverAxis = -1;
    double started = 0;
    juce::TextButton applyButton { "Appliquer" }, ignoreButton { "Ignorer" }, cancelButton { "Annuler l'analyse" }, detailsButton { "Bilan" };
};
}
