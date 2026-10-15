#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace drum::ui
{
namespace palette
{
inline const juce::Colour background { 0xff11161a };
inline const juce::Colour header { 0xff161d21 };
inline const juce::Colour panel { 0xff1b2327 };
inline const juce::Colour panelRaised { 0xff222b30 };
inline const juce::Colour outline { 0xff2c373c };
inline const juce::Colour track { 0xff2a3438 };
inline const juce::Colour accent { 0xff58d1ad };
inline const juce::Colour accentDim { 0xff2f7f6b };
inline const juce::Colour amber { 0xfff3ac62 };
inline const juce::Colour red { 0xffef6d70 };
inline const juce::Colour blue { 0xff829dc1 };
inline const juce::Colour text { 0xffe8edeb };
inline const juce::Colour textDim { 0xff93a4a8 };
inline const juce::Colour textFaint { 0xff5f6d71 };
}

inline juce::Font uiFont(float height, bool bold = false)
{
    return juce::Font(juce::FontOptions("Bahnschrift", height, bold ? juce::Font::bold : juce::Font::plain));
}

// Visual language inspired by modern dark plug-ins: glowing value arcs around dark knobs,
// dotted outer scale, pill switches for module power and segmented pill tabs.
class DrumLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    DrumLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, palette::background);
        setColour(juce::Slider::textBoxTextColourId, palette::text);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, palette::accentDim);
        setColour(juce::Slider::rotarySliderFillColourId, palette::accent);
        setColour(juce::Slider::trackColourId, palette::accent);
        setColour(juce::Slider::thumbColourId, palette::text);
        setColour(juce::Label::textColourId, palette::text);
        setColour(juce::ToggleButton::textColourId, palette::text);
        setColour(juce::ToggleButton::tickColourId, palette::accent);
        setColour(juce::TextButton::buttonColourId, palette::panelRaised);
        setColour(juce::TextButton::buttonOnColourId, palette::accent);
        setColour(juce::TextButton::textColourOffId, palette::textDim);
        setColour(juce::TextButton::textColourOnId, juce::Colour(0xff10181a));
        setColour(juce::ComboBox::backgroundColourId, palette::panelRaised);
        setColour(juce::ComboBox::outlineColourId, palette::outline);
        setColour(juce::ComboBox::textColourId, palette::text);
        setColour(juce::ComboBox::arrowColourId, palette::accent);
        setColour(juce::PopupMenu::backgroundColourId, palette::panel);
        setColour(juce::PopupMenu::textColourId, palette::text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, palette::accentDim);
        setColour(juce::PopupMenu::highlightedTextColourId, palette::text);
        setColour(juce::TooltipWindow::backgroundColourId, palette::panelRaised);
        setColour(juce::TooltipWindow::textColourId, palette::text);
        setColour(juce::TooltipWindow::outlineColourId, palette::outline);
        setColour(juce::TextEditor::backgroundColourId, palette::panel);
        setColour(juce::TextEditor::textColourId, palette::text);
        setColour(juce::TextEditor::outlineColourId, palette::outline);
        setColour(juce::TextEditor::focusedOutlineColourId, palette::accent);
        setColour(juce::ScrollBar::thumbColourId, palette::outline);
    }

    static void drawGlowArc(juce::Graphics& graphics, juce::Point<float> centre, float radius, float from, float to,
                            juce::Colour colour, float thickness)
    {
        if (std::abs(to - from) < 0.001f) return;
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, juce::jmin(from, to), juce::jmax(from, to), true);
        for (const auto& [width, alpha] : { std::pair<float, float> { thickness * 3.2f, 0.08f }, { thickness * 2.0f, 0.16f } })
        {
            graphics.setColour(colour.withAlpha(alpha));
            graphics.strokePath(arc, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        graphics.setColour(colour);
        graphics.strokePath(arc, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawRotarySlider(juce::Graphics& graphics, int x, int y, int width, int height, float position,
                          float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
            static_cast<float>(width), static_cast<float>(height)).reduced(2.0f);
        const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const bool enabled = slider.isEnabled();
        const auto accent = (slider.getProperties().contains("accent")
            ? juce::Colour::fromString(slider.getProperties()["accent"].toString()) : palette::accent)
            .withMultipliedAlpha(enabled ? 1.0f : 0.35f);
        const auto angle = startAngle + position * (endAngle - startAngle);

        const auto dots = 25;
        for (int dot = 0; dot < dots; ++dot)
        {
            const auto dotAngle = startAngle + (endAngle - startAngle) * static_cast<float>(dot) / (dots - 1);
            const auto lit = dotAngle <= angle + 0.001f;
            const auto point = centre.getPointOnCircumference(radius - 1.5f, dotAngle);
            graphics.setColour(lit ? accent.withAlpha(0.75f) : palette::textFaint.withAlpha(0.45f));
            graphics.fillEllipse(juce::Rectangle<float>(2.2f, 2.2f).withCentre(point));
        }

        const auto arcRadius = radius - 7.5f;
        const auto thickness = juce::jmax(2.5f, radius * 0.085f);
        juce::Path track;
        track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        graphics.setColour(palette::track);
        graphics.strokePath(track, juce::PathStrokeType(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        auto originAngle = startAngle;
        if (slider.getProperties().contains("origin"))
        {
            const auto origin = static_cast<double>(slider.getProperties()["origin"]);
            originAngle = startAngle + static_cast<float>(slider.valueToProportionOfLength(origin)) * (endAngle - startAngle);
        }
        drawGlowArc(graphics, centre, arcRadius, originAngle, angle, accent, thickness);

        const auto bodyRadius = arcRadius - thickness - 3.0f;
        const auto body = juce::Rectangle<float>(bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre(centre);
        graphics.setColour(juce::Colours::black.withAlpha(0.45f));
        graphics.fillEllipse(body.translated(0.0f, 2.5f).expanded(1.5f));
        graphics.setGradientFill(juce::ColourGradient(juce::Colour(0xff3b464b), centre.x, body.getY(),
            juce::Colour(0xff161c1f), centre.x, body.getBottom(), false));
        graphics.fillEllipse(body);
        graphics.setColour(juce::Colour(0xff4a565b).withAlpha(0.8f));
        graphics.drawEllipse(body.reduced(0.5f), 1.0f);
        graphics.setGradientFill(juce::ColourGradient(juce::Colour(0x22ffffff), centre.x, body.getY(),
            juce::Colour(0x00ffffff), centre.x, centre.y, false));
        graphics.fillEllipse(body.reduced(bodyRadius * 0.12f));

        const auto inner = centre.getPointOnCircumference(bodyRadius * 0.28f, angle);
        const auto outer = centre.getPointOnCircumference(bodyRadius * 0.82f, angle);
        graphics.setColour(enabled ? palette::text : palette::textFaint);
        graphics.drawLine({ inner, outer }, juce::jmax(2.0f, radius * 0.06f));
    }

    void drawLinearSlider(juce::Graphics& graphics, int x, int y, int width, int height, float sliderPos,
                          float, float, juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style != juce::Slider::LinearHorizontal)
        {
            LookAndFeel_V4::drawLinearSlider(graphics, x, y, width, height, sliderPos, 0, 0, style, slider);
            return;
        }
        const auto centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        const auto left = static_cast<float>(x) + 2.0f, right = static_cast<float>(x + width) - 2.0f;
        const auto accent = slider.isEnabled() ? palette::accent : palette::textFaint;
        graphics.setColour(palette::track);
        graphics.fillRoundedRectangle(left, centreY - 2.5f, right - left, 5.0f, 2.5f);
        auto origin = left;
        if (slider.getProperties().contains("origin"))
            origin = static_cast<float>(slider.getPositionOfValue(static_cast<double>(slider.getProperties()["origin"])));
        const auto from = juce::jmin(origin, sliderPos), to = juce::jmax(origin, sliderPos);
        graphics.setColour(accent.withAlpha(0.18f));
        graphics.fillRoundedRectangle(from, centreY - 5.0f, to - from, 10.0f, 5.0f);
        graphics.setColour(accent);
        graphics.fillRoundedRectangle(from, centreY - 2.5f, to - from, 5.0f, 2.5f);
        const auto thumb = juce::Rectangle<float>(13.0f, 13.0f).withCentre({ sliderPos, centreY });
        graphics.setColour(juce::Colours::black.withAlpha(0.4f));
        graphics.fillEllipse(thumb.translated(0.0f, 1.5f));
        graphics.setColour(palette::text);
        graphics.fillEllipse(thumb);
        graphics.setColour(accent);
        graphics.drawEllipse(thumb.reduced(1.0f), 2.0f);
    }

    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override
    {
        auto layout = LookAndFeel_V4::getSliderLayout(slider);
        if (slider.getSliderStyle() == juce::Slider::RotaryHorizontalVerticalDrag
            && slider.getTextBoxPosition() == juce::Slider::TextBoxBelow)
        {
            auto bounds = slider.getLocalBounds();
            layout.textBoxBounds = bounds.removeFromBottom(18);
            layout.sliderBounds = bounds;
        }
        return layout;
    }

    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* label = LookAndFeel_V4::createSliderTextBox(slider);
        label->setFont(uiFont(12.5f));
        label->setColour(juce::Label::textColourId, palette::text.withAlpha(0.9f));
        label->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        label->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        return label;
    }

    void drawToggleButton(juce::Graphics& graphics, juce::ToggleButton& button, bool highlighted, bool) override
    {
        const auto bounds = button.getLocalBounds().toFloat();
        const bool on = button.getToggleState();
        const bool module = button.getProperties().contains("module");
        const auto switchWidth = module ? 34.0f : 30.0f, switchHeight = module ? 18.0f : 16.0f;
        const auto pill = juce::Rectangle<float>(switchWidth, switchHeight)
            .withPosition(bounds.getX() + 2.0f, bounds.getCentreY() - switchHeight * 0.5f);
        const auto accent = button.getProperties().contains("accent")
            ? juce::Colour::fromString(button.getProperties()["accent"].toString()) : palette::accent;
        const auto alpha = button.isEnabled() ? 1.0f : 0.4f;
        if (on)
        {
            graphics.setColour(accent.withAlpha(0.22f * alpha));
            graphics.fillRoundedRectangle(pill.expanded(2.5f), (switchHeight + 5.0f) * 0.5f);
        }
        graphics.setColour((on ? accent : palette::track).withMultipliedAlpha(alpha));
        graphics.fillRoundedRectangle(pill, switchHeight * 0.5f);
        if (highlighted)
        {
            graphics.setColour(palette::text.withAlpha(0.25f));
            graphics.drawRoundedRectangle(pill, switchHeight * 0.5f, 1.0f);
        }
        const auto knob = juce::Rectangle<float>(switchHeight - 4.0f, switchHeight - 4.0f)
            .withCentre({ on ? pill.getRight() - switchHeight * 0.5f : pill.getX() + switchHeight * 0.5f, pill.getCentreY() });
        graphics.setColour((on ? juce::Colour(0xff10181a) : palette::textDim).withMultipliedAlpha(alpha));
        graphics.fillEllipse(knob);
        graphics.setColour((on ? palette::text : palette::textDim).withMultipliedAlpha(alpha));
        graphics.setFont(uiFont(module ? 13.5f : 12.5f, module));
        graphics.drawFittedText(button.getButtonText(), bounds.withTrimmedLeft(switchWidth + 9.0f).toNearestInt(),
            juce::Justification::centredLeft, 1, 0.8f);
    }

    void drawButtonBackground(juce::Graphics& graphics, juce::Button& button, const juce::Colour&,
                              bool highlighted, bool down) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        const bool on = button.getToggleState();
        const bool primary = button.getProperties().contains("primary");
        const auto radius = bounds.getHeight() * 0.5f;
        const auto alpha = button.isEnabled() ? 1.0f : 0.45f;
        if (on)
        {
            graphics.setColour(palette::accent.withAlpha(0.18f * alpha));
            graphics.fillRoundedRectangle(bounds.expanded(1.5f), radius + 1.5f);
            graphics.setColour(palette::accent.withMultipliedAlpha(alpha));
            graphics.fillRoundedRectangle(bounds, radius);
            return;
        }
        auto fill = primary ? palette::accentDim.withAlpha(0.35f) : palette::panelRaised;
        if (highlighted) fill = fill.brighter(0.12f);
        if (down) fill = fill.brighter(0.2f);
        graphics.setColour(fill.withMultipliedAlpha(alpha));
        graphics.fillRoundedRectangle(bounds, radius);
        graphics.setColour((primary ? palette::accent : palette::outline).withMultipliedAlpha(alpha));
        graphics.drawRoundedRectangle(bounds, radius, primary ? 1.4f : 1.0f);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override
    {
        return uiFont(juce::jmin(14.0f, static_cast<float>(buttonHeight) * 0.52f), true);
    }

    void drawButtonText(juce::Graphics& graphics, juce::TextButton& button, bool, bool) override
    {
        const auto on = button.getToggleState();
        const auto primary = button.getProperties().contains("primary");
        auto colour = on ? juce::Colour(0xff10181a) : primary ? palette::accent : palette::textDim;
        if (!button.isEnabled()) colour = colour.withMultipliedAlpha(0.5f);
        graphics.setColour(colour);
        graphics.setFont(getTextButtonFont(button, button.getHeight()));
        graphics.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(6, 0),
            juce::Justification::centred, 1, 0.75f);
    }

    void drawComboBox(juce::Graphics& graphics, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
        graphics.setColour(palette::panelRaised.withMultipliedAlpha(box.isEnabled() ? 1.0f : 0.5f));
        graphics.fillRoundedRectangle(bounds, 7.0f);
        graphics.setColour(box.hasKeyboardFocus(true) ? palette::accent : palette::outline);
        graphics.drawRoundedRectangle(bounds, 7.0f, 1.0f);
        juce::Path arrow;
        const auto centreX = static_cast<float>(width) - 14.0f, centreY = static_cast<float>(height) * 0.5f;
        arrow.startNewSubPath(centreX - 4.5f, centreY - 2.0f);
        arrow.lineTo(centreX, centreY + 2.5f);
        arrow.lineTo(centreX + 4.5f, centreY - 2.0f);
        graphics.setColour(palette::accent.withMultipliedAlpha(box.isEnabled() ? 1.0f : 0.4f));
        graphics.strokePath(arrow, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override { return uiFont(14.0f, true); }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(8, 1, box.getWidth() - 30, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
    }

    juce::Font getPopupMenuFont() override { return uiFont(14.0f); }
};

// Draws a rounded module panel with an optional uppercase title.
inline void drawPanel(juce::Graphics& graphics, juce::Rectangle<int> area, const juce::String& title,
                      juce::Colour accent = palette::accent, bool active = true)
{
    const auto bounds = area.toFloat();
    graphics.setColour(juce::Colours::black.withAlpha(0.35f));
    graphics.fillRoundedRectangle(bounds.translated(0.0f, 2.0f), 10.0f);
    graphics.setGradientFill(juce::ColourGradient(palette::panelRaised, bounds.getX(), bounds.getY(),
        palette::panel, bounds.getX(), bounds.getBottom(), false));
    graphics.fillRoundedRectangle(bounds, 10.0f);
    graphics.setColour(palette::outline);
    graphics.drawRoundedRectangle(bounds.reduced(0.5f), 10.0f, 1.0f);
    if (active)
    {
        graphics.setColour(accent.withAlpha(0.55f));
        graphics.fillRoundedRectangle(bounds.getX() + 14.0f, bounds.getY(), 26.0f, 2.0f, 1.0f);
    }
    if (title.isNotEmpty())
    {
        graphics.setColour(palette::textDim);
        graphics.setFont(uiFont(13.0f, true));
        graphics.drawText(title, area.withHeight(30).reduced(14, 0), juce::Justification::centredLeft);
    }
}
}
