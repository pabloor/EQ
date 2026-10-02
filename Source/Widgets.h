#pragma once
#include "PluginProcessor.h"
#include "LookAndFeel.h"

// Controles de hardware que sustituyen a los desplegables y los medidores de barra.

// Pulsadores de posiciones fijas en una fila (p. ej. Estéreo | Mid | Side, o 6 | 12 | 24 | 48 dB/oct): el activo se hunde y enciende su piloto.
class SegmentedButtons : public juce::Component, public juce::SettableTooltipClient
{
public:
    SegmentedButtons (juce::RangedAudioParameter& param, const juce::StringArray& labels, juce::Colour lampColour);
    void setLampColour (juce::Colour c) { lamp = c; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void select (int index);
    juce::StringArray labels;
    juce::Colour lamp;
    int current = 0;
    juce::ParameterAttachment attachment;
};

// Selector rotativo de posiciones: un knob con las posiciones impresas alrededor, como el de un equipo de rack.
class RotarySwitch : public juce::Component, public juce::SettableTooltipClient
{
public:
    RotarySwitch (juce::RangedAudioParameter& param, const juce::String& title, const juce::StringArray& labels, juce::Colour capColour);
    void setCapColour (juce::Colour c) { cap = c; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { setFromPoint (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { setFromPoint (e.position); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    static constexpr float startAngle = -2.356f, endAngle = 2.356f;   // -135º .. +135º desde arriba, en sentido horario
    struct Geometry { juce::Point<float> c; float r, lx, ly; };
    Geometry geometry() const;
    float angleFor (int index) const;
    void setFromPoint (juce::Point<float> p);
    void select (int index);

    juce::String title;
    juce::StringArray labels;
    juce::Colour cap;
    int current = 0;
    juce::ParameterAttachment attachment;
};

// Dos medidores VU de aguja (L y R, apilados) para la entrada o la salida, con balística, piloto de pico y pico máximo en cifras.
class VUPair : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    VUPair (MedidoresEQAudioProcessor& p, bool isInput);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { held = -100.0f; }   // clic: borra el pico máximo

private:
    void timerCallback() override;
    void drawFace (juce::Graphics&, juce::Rectangle<float> face, const juce::String& tag, float level, bool led);

    MedidoresEQAudioProcessor& proc;
    bool input;
    float level[2] { 0.0f, 0.0f };
    int ledFrames[2] { 0, 0 };
    float held = -100.0f;
};

// Chapa frontal con grano de cepillado y viñeteado (se genera una vez por tamaño y tema).
juce::Image makePlate (int width, int height, const Palette& palette);

// Tornillo de cabeza ranurada.
void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius, float slotAngle);
