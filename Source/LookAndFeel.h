#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Estilo propio del plugin: hardware analógico vintage (grafito cálido, latón y crema, knobs con relieve,
// lámparas en vez de cuadros y medidores segmentados).
namespace Theme
{
    inline const juce::Colour background { 0xff15120e };
    inline const juce::Colour backgroundTop { 0xff1d1914 };
    inline const juce::Colour panel      { 0xff25211b };
    inline const juce::Colour panelTop   { 0xff2e2922 };
    inline const juce::Colour control    { 0xff2f2a23 };
    inline const juce::Colour outline    { 0xff4a4338 };
    inline const juce::Colour text       { 0xffeee4cc };   // crema
    inline const juce::Colour muted      { 0xff9b917b };
    inline const juce::Colour accent     { 0xfff0a640 };   // ámbar
    inline const juce::Colour screen     { 0xff0f0d0a };   // fondo de la pantalla de la curva y los medidores
}

class EQLookAndFeel : public juce::LookAndFeel_V4
{
public:
    EQLookAndFeel()
    {
        setDefaultSansSerifTypefaceName ("Futura");

        setColour (juce::ResizableWindow::backgroundColourId, Theme::background);
        setColour (juce::Label::textColourId, Theme::muted);
        setColour (juce::Slider::textBoxTextColourId, Theme::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::ComboBox::backgroundColourId, Theme::control);
        setColour (juce::ComboBox::textColourId, Theme::text);
        setColour (juce::ComboBox::outlineColourId, Theme::outline);
        setColour (juce::ComboBox::arrowColourId, Theme::accent);
        setColour (juce::PopupMenu::backgroundColourId, Theme::panel);
        setColour (juce::PopupMenu::textColourId, Theme::text);
        setColour (juce::PopupMenu::headerTextColourId, Theme::muted);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::outline);
        setColour (juce::TextButton::buttonColourId, Theme::control);
        setColour (juce::TextButton::textColourOffId, Theme::text);
        setColour (juce::TextButton::textColourOnId, Theme::screen);
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

    // Etiquetas pequeñas, en mayúsculas y con algo de espaciado, como las serigrafías de un equipo.
    juce::Font getLabelFont (juce::Label&) override        { return juce::Font (juce::FontOptions (11.5f)).withExtraKerningFactor (0.08f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override  { return juce::Font (juce::FontOptions (13.0f)); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::Font (juce::FontOptions (13.0f)); }
    juce::Font getPopupMenuFont() override                 { return juce::Font (juce::FontOptions (14.0f)); }

    // Knob de relieve: corona de marcas que se encienden hasta el valor, cuerpo con degradado y aguja.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        const auto c = bounds.getCentre();
        const auto colour = s.findColour (juce::Slider::rotarySliderFillColourId);
        const float toAngle = startAngle + pos * (endAngle - startAngle);

        // Intervalo encendido: desde el principio, o desde el centro si el parámetro tiene signo.
        float fromAngle = startAngle;
        if (s.getMinimum() < 0.0 && s.getMaximum() > 0.0)
            fromAngle = startAngle + (float) s.valueToProportionOfLength (0.0) * (endAngle - startAngle);
        const float lo = juce::jmin (fromAngle, toAngle) - 0.02f, hi = juce::jmax (fromAngle, toAngle) + 0.02f;

        constexpr int numTicks = 15;
        const float tickOuter = radius, tickInner = radius - juce::jmax (3.0f, radius * 0.12f);
        for (int i = 0; i < numTicks; ++i)
        {
            const float a = startAngle + (endAngle - startAngle) * (float) i / (float) (numTicks - 1);
            const bool lit = a >= lo && a <= hi;
            const float sa = std::sin (a), ca = -std::cos (a);   // 0 = arriba, sentido horario
            g.setColour (lit ? colour : Theme::outline);
            g.drawLine (c.x + sa * tickInner, c.y + ca * tickInner, c.x + sa * tickOuter, c.y + ca * tickOuter, lit ? 2.2f : 1.4f);
        }

        const float bodyR = tickInner - 3.0f;
        if (bodyR < 4.0f) return;

        // Sombra y cuerpo
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (c.x - bodyR + 1.0f, c.y - bodyR + 2.0f, bodyR * 2.0f, bodyR * 2.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff59513f), c.x - bodyR * 0.5f, c.y - bodyR * 0.7f,
                                                 juce::Colour (0xff1c1913), c.x + bodyR * 0.6f, c.y + bodyR * 0.9f, false));
        g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour (juce::Colour (0xff0d0b08));
        g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.2f);

        // Tapa interior
        const float capR = bodyR * 0.66f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3429), c.x, c.y - capR,
                                                 juce::Colour (0xff2a251d), c.x, c.y + capR, false));
        g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f, 1.0f);

        // Aguja
        juce::Path pointer;
        pointer.addRoundedRectangle (-1.5f, -bodyR + 2.5f, 3.0f, bodyR * 0.62f, 1.5f);
        g.setColour (Theme::text);
        g.fillPath (pointer, juce::AffineTransform::rotation (toAngle).translated (c.x, c.y));
    }

    // Interruptor como lámpara: piloto redondo con brillo, encendido en el color de la banda.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const float d = juce::jmin (14.0f, r.getHeight() - 6.0f);
        const juce::Rectangle<float> lamp (r.getX() + 3.0f, r.getCentreY() - d / 2.0f, d, d);
        const auto colour = b.findColour (juce::ToggleButton::tickColourId);
        const bool on = b.getToggleState();

        if (on)   // resplandor
        {
            g.setColour (colour.withAlpha (0.22f));
            g.fillEllipse (lamp.expanded (3.0f));
        }
        g.setGradientFill (juce::ColourGradient (on ? colour.brighter (0.5f) : juce::Colour (0xff3b342a), lamp.getCentreX() - d * 0.2f, lamp.getY() + d * 0.15f,
                                                 on ? colour.darker (0.4f) : juce::Colour (0xff1d1a15), lamp.getCentreX(), lamp.getBottom(), true));
        g.fillEllipse (lamp);
        g.setColour (juce::Colour (0xff0d0b08));
        g.drawEllipse (lamp, 1.2f);
        if (on)   // brillo
        {
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.fillEllipse (lamp.getX() + d * 0.2f, lamp.getY() + d * 0.12f, d * 0.32f, d * 0.2f);
        }
        if (highlighted)
        {
            g.setColour (Theme::text.withAlpha (0.4f));
            g.drawEllipse (lamp.expanded (1.5f), 1.0f);
        }

        g.setColour (Theme::text.withAlpha (b.isEnabled() ? 1.0f : 0.4f));
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (d + 11.0f), juce::Justification::centredLeft, true);
    }
};
