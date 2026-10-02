#pragma once
#include "PluginProcessor.h"
#include "Presets.h"

// Curva de respuesta + analizador de espectro. Los puntos de cada banda se arrastran
// (frecuencia/ganancia), la rueda cambia la Q y el doble clic activa/desactiva la banda.
class ResponseCurve : public juce::Component, private juce::Timer
{
public:
    explicit ResponseCurve (MedidoresEQAudioProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder;
    static constexpr float minDb = -24.0f, maxDb = 24.0f;

    void timerCallback() override;
    void updateSpectrum();

    float xForFreq (float f) const;
    float freqForX (float x) const;
    float yForDb (float d) const;
    float dbForY (float y) const;
    juce::Point<float> nodePos (int band) const;
    int nodeAt (juce::Point<float> p) const;
    void setParam (const juce::String& id, float realValue);
    juce::RangedAudioParameter* param (const juce::String& id) const { return proc.apvts.getParameter (id); }
    void gesture (int band, bool begin);

    MedidoresEQAudioProcessor& proc;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, (size_t) fftSize> ring {};
    std::array<float, (size_t) fftSize * 2> fftData {};
    std::array<float, (size_t) fftSize / 2> spectrum {};   // dB suavizados

    int hovered = -1, dragged = -1;
};

// Medidor de pico de dos canales (entrada o salida), de -60 a +6 dB, con el pico máximo en cifras.
class LevelMeter : public juce::Component, private juce::Timer
{
public:
    LevelMeter (MedidoresEQAudioProcessor& p, bool isInput, const juce::String& title);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { held = -100.0f; }   // clic: borra el pico máximo

private:
    void timerCallback() override;

    MedidoresEQAudioProcessor& proc;
    bool input;
    juce::String caption;
    float level[2] { -100.0f, -100.0f };
    float held = -100.0f;
};

class MedidoresEQAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit MedidoresEQAudioProcessorEditor (MedidoresEQAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void addKnob (Knob& k, const juce::String& paramId, const juce::String& text, const juce::String& suffix);
    void addCombo (juce::ComboBox& box, std::unique_ptr<ComboAttachment>& att, const juce::String& paramId, const juce::StringArray& items);
    void refreshPresets (const juce::String& select = {});
    void presetChosen();
    void askPresetName();
    void askDeletePreset();

    MedidoresEQAudioProcessor& proc;
    PresetManager presets;

    juce::ComboBox presetBox;
    juce::TextButton saveButton { "Guardar" }, deleteButton { "Borrar" };
    juce::StringArray factoryNames, userNames;   // los ids del desplegable se reparten entre ambas listas

    ResponseCurve curve;
    LevelMeter inMeter, outMeter;
    juce::ToggleButton toggles[EQ::NumBands];
    std::unique_ptr<ButtonAttachment> toggleAttachments[EQ::NumBands];
    Knob knobs[EQ::NumBands][3];   // [banda][0=frecuencia, 1=ganancia, 2=Q]
    juce::ComboBox slopeBox[EQ::NumBands], placementBox[EQ::NumBands];
    juce::TextButton typeButton[EQ::NumBands];   // shelf -> campana (solo en los shelves)
    std::unique_ptr<ComboAttachment> slopeAttachments[EQ::NumBands], placementAttachments[EQ::NumBands];
    std::unique_ptr<ButtonAttachment> typeAttachments[EQ::NumBands];
    Knob outKnob, driveKnob;
    juce::ComboBox characterBox;
    std::unique_ptr<ComboAttachment> characterAttachment;
    juce::ToggleButton propQButton { "Q proporcional" };
    std::unique_ptr<ButtonAttachment> propQAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MedidoresEQAudioProcessorEditor)
};
