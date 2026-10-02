#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Paleta y estilo propio del plugin (tema oscuro).
namespace Theme
{
    inline const juce::Colour background { 0xff1b1e23 };
    inline const juce::Colour panel      { 0xff23272e };
    inline const juce::Colour control    { 0xff2b3038 };
    inline const juce::Colour outline    { 0xff3a404a };
    inline const juce::Colour text       { 0xffdfe3ea };
    inline const juce::Colour muted      { 0xff8a919c };
    inline const juce::Colour accent     { 0xff4fc3f7 };
}

class EQLookAndFeel : public juce::LookAndFeel_V4
{
public:
    EQLookAndFeel()
    {
        setDefaultSansSerifTypefaceName ("Helvetica Neue");

        setColour (juce::ResizableWindow::backgroundColourId, Theme::background);
        setColour (juce::Label::textColourId, Theme::muted);
        setColour (juce::Slider::textBoxTextColourId, Theme::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::backgroundColourId, Theme::control);
        setColour (juce::ComboBox::textColourId, Theme::text);
        setColour (juce::ComboBox::outlineColourId, Theme::outline);
        setColour (juce::ComboBox::arrowColourId, Theme::muted);
        setColour (juce::PopupMenu::backgroundColourId, Theme::panel);
        setColour (juce::PopupMenu::textColourId, Theme::text);
        setColour (juce::PopupMenu::headerTextColourId, Theme::muted);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::outline);
        setColour (juce::TextButton::buttonColourId, Theme::control);
        setColour (juce::TextButton::textColourOffId, Theme::text);
        setColour (juce::TextButton::textColourOnId, Theme::background);
        setColour (juce::ToggleButton::textColourId, Theme::text);
        setColour (juce::TooltipWindow::backgroundColourId, Theme::panel);
        setColour (juce::TooltipWindow::textColourId, Theme::text);
        setColour (juce::TooltipWindow::outlineColourId, Theme::outline);
        setColour (juce::AlertWindow::backgroundColourId, Theme::panel);
        setColour (juce::AlertWindow::textColourId, Theme::text);
        setColour (juce::TextEditor::backgroundColourId, Theme::control);
        setColour (juce::TextEditor::textColourId, Theme::text);
        setColour (juce::TextEditor::outlineColourId, Theme::outline);
    }

    juce::Font getLabelFont (juce::Label&) override        { return juce::Font (juce::FontOptions (12.5f)); }
    juce::Font getComboBoxFont (juce::ComboBox&) override  { return juce::Font (juce::FontOptions (13.0f)); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::Font (juce::FontOptions (13.0f)); }
    juce::Font getPopupMenuFont() override                 { return juce::Font (juce::FontOptions (14.0f)); }

    // Knob con arco de valor del color de la banda. Los parámetros con signo (ganancias) rellenan desde el centro.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        const auto centre = bounds.getCentre();
        const float lineW = juce::jmax (2.5f, radius * 0.17f);
        const float arcR = radius - lineW * 0.5f;
        const float toAngle = startAngle + pos * (endAngle - startAngle);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (Theme::outline);
        g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

        float fromAngle = startAngle;
        if (s.getMinimum() < 0.0 && s.getMaximum() > 0.0)
            fromAngle = startAngle + (float) s.valueToProportionOfLength (0.0) * (endAngle - startAngle);

        const float a0 = juce::jmin (fromAngle, toAngle), a1 = juce::jmax (fromAngle, toAngle);
        if (a1 - a0 > 0.01f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, a0, a1, true);
            g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
            g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        }

        const float bodyR = arcR - lineW * 1.2f;
        if (bodyR > 3.0f)
        {
            g.setColour (Theme::control);
            g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
            g.setColour (Theme::outline);
            g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

            juce::Path pointer;
            pointer.addRoundedRectangle (-1.5f, -bodyR + 2.0f, 3.0f, bodyR * 0.5f, 1.5f);
            g.setColour (Theme::text);
            g.fillPath (pointer, juce::AffineTransform::rotation (toAngle).translated (centre.x, centre.y));
        }
    }

    // Interruptor con un piloto del color de la banda.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const float d = juce::jmin (14.0f, r.getHeight() - 6.0f);
        const juce::Rectangle<float> led (r.getX() + 3.0f, r.getCentreY() - d / 2.0f, d, d);
        const auto colour = b.findColour (juce::ToggleButton::tickColourId);

        g.setColour (b.getToggleState() ? colour : Theme::outline);
        g.fillRoundedRectangle (led, 3.0f);
        if (highlighted)
        {
            g.setColour (Theme::text.withAlpha (0.5f));
            g.drawRoundedRectangle (led, 3.0f, 1.0f);
        }

        g.setColour (Theme::text.withAlpha (b.isEnabled() ? 1.0f : 0.4f));
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (d + 10.0f), juce::Justification::centredLeft, true);
    }
};
