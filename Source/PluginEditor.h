#pragma once
#include "PluginProcessor.h"
#include "Presets.h"
#include "LookAndFeel.h"

// Curva de respuesta + analizador de espectro.
//  - Arrastrar un punto: frecuencia y ganancia. Rueda sobre un punto: Q. Doble clic: activa/desactiva la banda.
//  - Las asas laterales de la banda enfocada (campanas) cambian su ancho (Q).
//  - En las bandas dinámicas, la zona sombreada muestra cuánta ganancia se está aplicando.
class ResponseCurve : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
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

    void setFocusBand (int band) { focusBand = band; }   // la banda cuyas asas de Q se muestran

private:
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder;

    void timerCallback() override;
    void updateSpectrum();

    float rangeDb() const;
    float xForFreq (float f) const;
    float freqForX (float x) const;
    float yForDb (float d) const;
    float dbForY (float y) const;
    juce::Point<float> nodePos (int band) const;
    int nodeAt (juce::Point<float> p) const;
    bool isBell (int band) const;
    float effectiveQ (int band) const;
    juce::Point<float> handlePos (int band, int side) const;   // side: -1 izquierda, +1 derecha
    int handleAt (juce::Point<float> p) const;                 // 0 = ninguna
    void setParam (const juce::String& id, float realValue);
    juce::RangedAudioParameter* param (const juce::String& id) const { return proc.apvts.getParameter (id); }
    void gesture (int band, bool begin);
    void qGesture (int band, bool begin);

    MedidoresEQAudioProcessor& proc;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, (size_t) fftSize> ring {};
    std::array<float, (size_t) fftSize * 2> fftData {};
    std::array<float, (size_t) fftSize / 2> spectrum {};   // dB suavizados

    int hovered = -1, dragged = -1, focusBand = -1, dragHandle = 0;
};

// Medidor de pico de dos canales (entrada o salida), de -60 a +6 dB, con escala, retención de pico y máximo en cifras.
class LevelMeter : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    LevelMeter (MedidoresEQAudioProcessor& p, bool isInput, const juce::String& title);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { held = -100.0f; hold[0] = hold[1] = -100.0f; }   // clic: borra los picos

private:
    void timerCallback() override;

    MedidoresEQAudioProcessor& proc;
    bool input;
    juce::String caption;
    float level[2] { -100.0f, -100.0f }, hold[2] { -100.0f, -100.0f };
    int holdFrames[2] { 0, 0 };
    float held = -100.0f;
};

// Barra de una banda dinámica: cuánto de su ganancia máxima se está aplicando ahora mismo.
class DynMeter : public juce::Component, private juce::Timer
{
public:
    DynMeter (MedidoresEQAudioProcessor& p, int bandIndex) : proc (p), band (bandIndex) { startTimerHz (30); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }
    MedidoresEQAudioProcessor& proc;
    int band;
};

class MedidoresEQAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit MedidoresEQAudioProcessorEditor (MedidoresEQAudioProcessor&);
    ~MedidoresEQAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    ResponseCurve& getCurve() { return curve; }

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

    void addKnob (Knob& k, const juce::String& paramId, const juce::String& text, int textBoxWidth, juce::Colour colour, const juce::String& tip);
    void addCombo (juce::ComboBox& box, std::unique_ptr<ComboAttachment>& att, const juce::String& paramId, const juce::StringArray& items);
    void refreshPresets (const juce::String& select = {});
    void presetChosen();
    void askPresetName();
    void askDeletePreset();

    EQLookAndFeel laf;   // el primero: se destruye el último
    MedidoresEQAudioProcessor& proc;
    PresetManager presets;
    juce::TooltipWindow tooltipWindow { this, 500 };

    juce::ComboBox presetBox;
    juce::TextButton saveButton { "Guardar" }, deleteButton { "Borrar" };
    juce::StringArray factoryNames, userNames;   // los ids del desplegable se reparten entre ambas listas

    // Ajustes de la vista
    juce::ComboBox analyzerBox, speedBox, rangeBox;
    juce::Label analyzerLabel, speedLabel, rangeLabel;
    std::unique_ptr<ComboAttachment> analyzerAttachment, speedAttachment, rangeAttachment;

    ResponseCurve curve;
    LevelMeter inMeter, outMeter;
    juce::ToggleButton toggles[EQ::NumBands];
    std::unique_ptr<ButtonAttachment> toggleAttachments[EQ::NumBands];
    Knob knobs[EQ::NumBands][3];   // [banda][0=frecuencia, 1=ganancia, 2=Q]
    juce::ComboBox slopeBox[EQ::NumBands], placementBox[EQ::NumBands];
    juce::TextButton typeButton[EQ::NumBands];   // shelf -> campana (solo en los shelves)
    std::unique_ptr<ComboAttachment> slopeAttachments[EQ::NumBands], placementAttachments[EQ::NumBands];
    std::unique_ptr<ButtonAttachment> typeAttachments[EQ::NumBands];
    Knob inKnob, outKnob, driveKnob;
    juce::ComboBox characterBox;
    std::unique_ptr<ComboAttachment> characterAttachment;
    juce::ComboBox styleBox;
    std::unique_ptr<ComboAttachment> styleAttachment;
    juce::Label characterLabel, styleLabel;

    // EQ dinámico: botón por banda, con su medidor, umbral, ratio, ataque y release.
    juce::ToggleButton dynToggle[EQ::NumBands];
    std::unique_ptr<DynMeter> dynMeter[EQ::NumBands];
    std::unique_ptr<ButtonAttachment> dynAttachments[EQ::NumBands];
    Knob thrKnob[EQ::NumBands], ratioKnob[EQ::NumBands], attackKnob[EQ::NumBands], releaseKnob[EQ::NumBands];

    // Paneles de fondo (se calculan en resized y se dibujan en paint)
    juce::Rectangle<int> bandPanel[EQ::NumBands], gainPanel, characterPanel;
    int panelTitleY = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MedidoresEQAudioProcessorEditor)
};
