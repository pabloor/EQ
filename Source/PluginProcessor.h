#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

// Ecualizador de 5 bandas: paso alto, shelf grave, dos campanas y shelf agudo, más ganancia de salida.
namespace EQ
{
    enum Band { HighPass, LowShelf, Bell1, Bell2, HighShelf, NumBands };

    struct BandInfo { const char* id; const char* name; float freq; float gain; float q; };

    inline const BandInfo bands[NumBands] = {
        { "hp", "Paso alto",  20.0f,    0.0f, 0.707f },
        { "ls", "Graves",     100.0f,   0.0f, 0.707f },
        { "b1", "Medio 1",    400.0f,   0.0f, 1.0f },
        { "b2", "Medio 2",    2500.0f,  0.0f, 1.0f },
        { "hs", "Agudos",     8000.0f,  0.0f, 0.707f },
    };

    inline juce::String freqId (int b) { return juce::String (bands[b].id) + "_freq"; }
    inline juce::String gainId (int b) { return juce::String (bands[b].id) + "_gain"; }
    inline juce::String qId    (int b) { return juce::String (bands[b].id) + "_q"; }
    inline juce::String onId   (int b) { return juce::String (bands[b].id) + "_on"; }
    inline const char* outId = "out_gain";

    inline const juce::Colour bandColours[NumBands] = {
        juce::Colour (0xffef5350), juce::Colour (0xffffa726), juce::Colour (0xff66bb6a),
        juce::Colour (0xff42a5f5), juce::Colour (0xffab47bc) };

    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    // Coeficientes de una banda a partir de los parámetros actuales (también los usa el editor para dibujar la curva).
    inline Coeffs::Ptr makeCoeffs (int b, const juce::AudioProcessorValueTreeState& apvts, double sampleRate)
    {
        // Banda desactivada: filtro neutro (la curva dibujada también lo refleja).
        if (apvts.getRawParameterValue (onId (b))->load() < 0.5f)
            return new Coeffs (1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);

        const float f = apvts.getRawParameterValue (freqId (b))->load();
        const float g = apvts.getRawParameterValue (gainId (b))->load();
        const float q = apvts.getRawParameterValue (qId (b))->load();
        const float gain = juce::Decibels::decibelsToGain (g);
        switch (b)
        {
            case HighPass:  return Coeffs::makeHighPass (sampleRate, f, q);
            case LowShelf:  return Coeffs::makeLowShelf (sampleRate, f, q, gain);
            case HighShelf: return Coeffs::makeHighShelf (sampleRate, f, q, gain);
            default:        return Coeffs::makePeakFilter (sampleRate, f, q, gain);
        }
    }
}

class MedidoresEQAudioProcessor : public juce::AudioProcessor
{
public:
    MedidoresEQAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Medidores EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // Muestras (mono, ya ecualizadas) para el analizador de espectro del editor.
    int pullAnalyzerSamples (float* dest, int maxSamples);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateFilters();

    using Filter = juce::dsp::IIR::Filter<float>;
    using Stereo = juce::dsp::ProcessorDuplicator<Filter, EQ::Coeffs>;
    Stereo filters[EQ::NumBands];
    juce::dsp::Gain<float> outGain;
    double currentRate = 44100.0;

    juce::AbstractFifo analyzerFifo { 16384 };
    std::array<float, 16384> analyzerData {};
    void pushAnalyzerSamples (const juce::AudioBuffer<float>&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MedidoresEQAudioProcessor)
};
