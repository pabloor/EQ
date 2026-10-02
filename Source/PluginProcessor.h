#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

// Ecualizador de 6 bandas: paso alto, shelf de graves, dos campanas, shelf de agudos y paso bajo.
// Los pasos alto/bajo tienen pendiente ajustable (6/12/24/48 dB/oct) y cada banda puede actuar
// sobre el canal estéreo completo, solo sobre el Mid o solo sobre el Side.
namespace EQ
{
    enum Band { HighPass, LowShelf, Bell1, Bell2, HighShelf, LowPass, NumBands };
    constexpr int MaxStages = 4;   // 48 dB/oct = 4 biquads

    inline bool isCut (int b) { return b == HighPass || b == LowPass; }

    struct BandInfo { const char* id; const char* name; float freq; float gain; float q; };

    inline const BandInfo bands[NumBands] = {
        { "hp", "Paso alto",  20.0f,    0.0f, 0.707f },
        { "ls", "Graves",     100.0f,   0.0f, 0.707f },
        { "b1", "Medio 1",    400.0f,   0.0f, 1.0f },
        { "b2", "Medio 2",    2500.0f,  0.0f, 1.0f },
        { "hs", "Agudos",     8000.0f,  0.0f, 0.707f },
        { "lp", "Paso bajo",  20000.0f, 0.0f, 0.707f },
    };

    inline juce::String freqId  (int b) { return juce::String (bands[b].id) + "_freq"; }
    inline juce::String gainId  (int b) { return juce::String (bands[b].id) + "_gain"; }
    inline juce::String qId     (int b) { return juce::String (bands[b].id) + "_q"; }
    inline juce::String onId    (int b) { return juce::String (bands[b].id) + "_on"; }
    inline juce::String slopeId (int b) { return juce::String (bands[b].id) + "_slope"; }
    inline juce::String chId    (int b) { return juce::String (bands[b].id) + "_ch"; }
    inline const char* outId = "out_gain";

    inline juce::StringArray slopeNames()     { return { "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct" }; }
    inline juce::StringArray placementNames() { return { "Estéreo", "Mid", "Side" }; }

    inline const juce::Colour bandColours[NumBands] = {
        juce::Colour (0xffef5350), juce::Colour (0xffffa726), juce::Colour (0xff66bb6a),
        juce::Colour (0xff42a5f5), juce::Colour (0xffab47bc), juce::Colour (0xff26c6da) };

    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    // Filtros (en cascada) de una banda. Un solo biquad salvo en los pasos alto/bajo con pendiente alta.
    struct BandFilter
    {
        Coeffs::Ptr stage[MaxStages];
        int numStages = 1;

        double magnitude (double freq, double sampleRate) const
        {
            double m = 1.0;
            for (int i = 0; i < numStages; ++i) m *= stage[i]->getMagnitudeForFrequency (freq, sampleRate);
            return m;
        }
    };

    // Filtro de una banda a partir de los parámetros actuales (también lo usa el editor para dibujar la curva).
    inline BandFilter makeBand (int b, const juce::AudioProcessorValueTreeState& apvts, double sampleRate)
    {
        BandFilter bf;

        // Banda desactivada: filtro neutro.
        if (apvts.getRawParameterValue (onId (b))->load() < 0.5f)
        {
            bf.stage[0] = new Coeffs (1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
            return bf;
        }

        const float f = juce::jmin (apvts.getRawParameterValue (freqId (b))->load(), (float) (sampleRate * 0.49));

        if (isCut (b))
        {
            const bool hp = (b == HighPass);
            const int slope = juce::jlimit (0, 3, (int) apvts.getRawParameterValue (slopeId (b))->load());

            if (slope == 0)
            {
                // 6 dB/oct: filtro de primer orden escrito como biquad (así todos los filtros son de orden 2).
                const float k = std::tan (juce::MathConstants<float>::pi * f / (float) sampleRate);
                bf.stage[0] = hp ? new Coeffs (1.0f, -1.0f, 0.0f, 1.0f + k, k - 1.0f, 0.0f)
                                 : new Coeffs (k, k, 0.0f, 1.0f + k, k - 1.0f, 0.0f);
            }
            else
            {
                // Butterworth de orden 2, 4 u 8: una Q distinta por cada biquad de la cascada.
                const int order = 1 << slope;
                bf.numStages = order / 2;
                for (int k = 1; k <= bf.numStages; ++k)
                {
                    const float q = 1.0f / (2.0f * std::sin ((2.0f * (float) k - 1.0f) * juce::MathConstants<float>::pi / (2.0f * (float) order)));
                    bf.stage[k - 1] = hp ? Coeffs::makeHighPass (sampleRate, f, q)
                                         : Coeffs::makeLowPass  (sampleRate, f, q);
                }
            }
            return bf;
        }

        const float g = apvts.getRawParameterValue (gainId (b))->load();
        const float q = apvts.getRawParameterValue (qId (b))->load();
        const float gain = juce::Decibels::decibelsToGain (g);
        switch (b)
        {
            case LowShelf:  bf.stage[0] = Coeffs::makeLowShelf  (sampleRate, f, q, gain); break;
            case HighShelf: bf.stage[0] = Coeffs::makeHighShelf (sampleRate, f, q, gain); break;
            default:        bf.stage[0] = Coeffs::makePeakFilter (sampleRate, f, q, gain); break;
        }
        return bf;
    }

    // Dónde actúa la banda: 0 = estéreo (L/R), 1 = Mid, 2 = Side.
    inline int placement (int b, const juce::AudioProcessorValueTreeState& apvts)
    {
        return juce::jlimit (0, 2, (int) apvts.getRawParameterValue (chId (b))->load());
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

    // Pico de cada canal desde la última lectura (lineal). Lo lee el editor para los medidores.
    float takeInputPeak (int channel)  { return inPeak[channel & 1].exchange (0.0f); }
    float takeOutputPeak (int channel) { return outPeak[channel & 1].exchange (0.0f); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateFilters (bool force);
    void processBand (juce::AudioBuffer<float>&, int band);

    using Filter = juce::dsp::IIR::Filter<float>;
    Filter filters[EQ::NumBands][EQ::MaxStages][2];   // [banda][etapa][canal]
    int numStages[EQ::NumBands] {};
    std::array<float, 6> lastParams[EQ::NumBands] {};  // para recalcular coeficientes solo si algo cambia
    juce::dsp::Gain<float> outGain;
    double currentRate = 44100.0;

    std::atomic<float> inPeak[2] { 0.0f, 0.0f }, outPeak[2] { 0.0f, 0.0f };

    juce::AbstractFifo analyzerFifo { 16384 };
    std::array<float, 16384> analyzerData {};
    void pushAnalyzerSamples (const juce::AudioBuffer<float>&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MedidoresEQAudioProcessor)
};
