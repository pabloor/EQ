#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>

// Ecualizador de 6 bandas: paso alto, shelf de graves, dos campanas, shelf de agudos y paso bajo.
//  - Pasos alto/bajo con pendiente ajustable (6/12/24/48 dB/oct).
//  - Cada banda puede actuar sobre el estéreo completo, solo sobre el Mid o solo sobre el Side.
//  - Los shelves pueden conmutarse a campana.
//  - Las bandas que no son de corte pueden ser dinámicas (la ganancia depende del nivel en esa banda).
//  - Estilo de curva (Moderna, Clásica, Americana, Vintage): cómo cambia la Q con la ganancia.
namespace EQ
{
    enum Band { HighPass, LowShelf, Bell1, Bell2, HighShelf, LowPass, NumBands };
    constexpr int MaxStages = 4;   // 48 dB/oct = 4 biquads

    inline bool isCut (int b)  { return b == HighPass || b == LowPass; }
    inline bool hasType (int b) { return b == LowShelf || b == HighShelf; }   // activado = campana en vez de shelf
    inline bool hasDyn (int b)  { return ! isCut (b); }

    // JUCE interpreta los textos entre comillas (const char*) como ASCII: todo texto con tildes, ñ, ¿ o … debe pasar por aquí.
    inline juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

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
    inline juce::String typeId  (int b) { return juce::String (bands[b].id) + "_type"; }
    inline juce::String dynId   (int b) { return juce::String (bands[b].id) + "_dyn"; }
    inline juce::String thrId   (int b) { return juce::String (bands[b].id) + "_thr"; }
    inline juce::String ratioId (int b) { return juce::String (bands[b].id) + "_ratio"; }
    inline juce::String attackId (int b)  { return juce::String (bands[b].id) + "_attack"; }
    inline juce::String releaseId (int b) { return juce::String (bands[b].id) + "_release"; }
    inline const char* inId = "in_gain";
    inline const char* outId = "out_gain";
    inline const char* driveId = "drive";
    inline const char* mixId = "mix";   // mezcla seco/saturado (saturación en paralelo)
    inline const char* characterId = "character";
    inline const char* styleId = "style";
    inline const char* analyzerId = "an_mode";    // 0 apagado, 1 post-EQ, 2 pre-EQ
    inline const char* analyzerSpeedId = "an_speed";
    inline const char* rangeId = "view_range";    // rango vertical de la curva: ±6, ±12, ±24 dB

    inline juce::StringArray slopeNames()     { return { "6 dB/oct", "12 dB/oct", "24 dB/oct", "48 dB/oct" }; }
    inline juce::StringArray placementNames() { return { utf8 ("Estéreo"), "Mid", "Side" }; }
    inline juce::StringArray characterNames() { return { "Limpio", "Cinta", utf8 ("Válvula") }; }
    inline juce::StringArray styleNames()     { return { "Moderna", utf8 ("Clásica"), "Americana", "Vintage" }; }
    inline juce::StringArray analyzerNames()  { return { "Apagado", "Post-EQ", "Pre-EQ" }; }
    inline juce::StringArray speedNames()     { return { "Lenta", "Media", utf8 ("Rápida") }; }
    inline juce::StringArray rangeNames()     { return { utf8 ("\u00b16 dB"), utf8 ("\u00b112 dB"), utf8 ("\u00b124 dB") }; }
    inline float rangeDbFor (int index)       { return index == 0 ? 6.0f : (index == 2 ? 24.0f : 12.0f); }
    // Ajustes de vista que no se automatizan ni deben cambiar al cargar un preset.
    inline bool isViewParam (const juce::String& id) { return id == analyzerId || id == analyzerSpeedId || id == rangeId; }

    inline const juce::Colour bandColours[NumBands] = {
        juce::Colour (0xffd9603f), juce::Colour (0xffe8a23c), juce::Colour (0xff9db55e),
        juce::Colour (0xff4fa6a8), juce::Colour (0xffb07fc4), juce::Colour (0xff6f9bdb) };

    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    //==========================================================================
    // Filtros de campana y shelf (fórmulas RBJ, las mismas que usa JUCE). Se calculan aquí a mano para poder
    // escribirlos en el hilo de audio sin reservar memoria (EQ dinámico).
    enum Shape { Peak, LowShelfShape, HighShelfShape };

    inline void biquad (Shape shape, double sampleRate, float freq, float q, float gainDb, float out[6])
    {
        const float A = std::sqrt (juce::Decibels::decibelsToGain (gainDb));
        const float omega = juce::MathConstants<float>::twoPi * juce::jmax (freq, 2.0f) / (float) sampleRate;
        const float coso = std::cos (omega), sino = std::sin (omega);

        if (shape == Peak)
        {
            const float alpha = sino / (q * 2.0f), c2 = -2.0f * coso;
            out[0] = 1.0f + alpha * A; out[1] = c2; out[2] = 1.0f - alpha * A;
            out[3] = 1.0f + alpha / A; out[4] = c2; out[5] = 1.0f - alpha / A;
            return;
        }

        const float am1 = A - 1.0f, ap1 = A + 1.0f, beta = sino * std::sqrt (A) / q, am1c = am1 * coso;
        if (shape == LowShelfShape)
        {
            out[0] = A * (ap1 - am1c + beta);  out[1] = A * 2.0f * (am1 - ap1 * coso);  out[2] = A * (ap1 - am1c - beta);
            out[3] = ap1 + am1c + beta;        out[4] = -2.0f * (am1 + ap1 * coso);     out[5] = ap1 + am1c - beta;
        }
        else
        {
            out[0] = A * (ap1 + am1c + beta);  out[1] = A * -2.0f * (am1 + ap1 * coso); out[2] = A * (ap1 + am1c - beta);
            out[3] = ap1 - am1c + beta;        out[4] = 2.0f * (am1 - ap1 * coso);      out[5] = ap1 - am1c - beta;
        }
    }

    // Estilo de curva: cómo cambia la Q con la ganancia (solo campanas; el Vintage además da resonancia a los shelves).
    //   0 Moderna:   Q constante.
    //   1 Clásica:   la campana se ensancha al subir la ganancia y se estrecha al bajarla.
    //   2 Americana: al revés: se estrecha al subir y se ensancha al bajar (Q proporcional).
    //   3 Vintage:   como la Clásica, y los shelves con un pequeño rebote antes de la curva.
    inline float styleQ (int style, Shape shape, float q, float gainDb)
    {
        if (shape == Peak)
        {
            if (style == 1 || style == 3) q *= std::exp (-0.066f * gainDb);
            else if (style == 2)          q *= std::exp ( 0.066f * gainDb);
        }
        else if (style == 3)
            q *= 1.3f;
        return juce::jlimit (0.05f, 30.0f, q);
    }

    // Parámetros actuales de una banda de campana/shelf.
    struct Settings
    {
        Shape shape = Peak;
        float freq = 1000.0f, gainDb = 0.0f, q = 1.0f;
        int style = 1;
    };

    inline Settings readSettings (int b, const juce::AudioProcessorValueTreeState& apvts, double sampleRate)
    {
        auto read = [&] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
        Settings s;
        s.freq = juce::jmin (read (freqId (b)), (float) (sampleRate * 0.49));
        s.gainDb = read (gainId (b));
        s.q = read (qId (b));
        s.style = (int) read (styleId);
        const bool bell = b == Bell1 || b == Bell2 || (hasType (b) && read (typeId (b)) > 0.5f);
        s.shape = bell ? Peak : (b == LowShelf ? LowShelfShape : HighShelfShape);
        return s;
    }

    inline void fillCoeffs (const Settings& s, float gainDb, double sampleRate, float out[6])
    {
        biquad (s.shape, sampleRate, s.freq, styleQ (s.style, s.shape, s.q, gainDb), gainDb, out);
    }

    //==========================================================================
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

        if (isCut (b))
        {
            const float f = juce::jmin (apvts.getRawParameterValue (freqId (b))->load(), (float) (sampleRate * 0.49));
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

        const auto s = readSettings (b, apvts, sampleRate);
        float c[6];
        fillCoeffs (s, s.gainDb, sampleRate, c);
        bf.stage[0] = new Coeffs (c[0], c[1], c[2], c[3], c[4], c[5]);
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

    // Ganancia que está aplicando ahora mismo una banda dinámica (dB; 0 si no es dinámica). La lee el editor.
    float getDynamicGainDb (int band) const { return dynGainDb[band].load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateFilters (bool force);
    void processBand (juce::AudioBuffer<float>&, int band);
    void runFilters (juce::AudioBuffer<float>&, int band, int where, int numCh, int start, int len);
    void updateDynamic (juce::AudioBuffer<float>&, int band, int where, int numCh, int start, int len);
    void saturate (juce::AudioBuffer<float>&);

    using Filter = juce::dsp::IIR::Filter<float>;
    Filter filters[EQ::NumBands][EQ::MaxStages][2];   // [banda][etapa][canal]
    int numStages[EQ::NumBands] {};
    std::array<float, 8> lastParams[EQ::NumBands] {};  // para recalcular coeficientes solo si algo cambia

    // EQ dinámico: detector (filtro + seguidor de envolvente) por banda, con su umbral, ratio, ataque y release.
    struct Dynamic
    {
        bool on = false;
        EQ::Settings settings;
        float threshold = -24.0f, ratio = 3.0f;
        float attackCoef = 0.0f, releaseCoef = 0.0f;
        float envelope = 0.0f;
        Filter detector;
    };
    Dynamic dynamic[EQ::NumBands];
    static constexpr int dynamicBlock = 32;   // cada cuántas muestras se actualiza el filtro dinámico

    juce::dsp::Gain<float> inGain, outGain;

    // Saturación analógica (cinta/válvula) con sobremuestreo 2x.
    juce::dsp::Oversampling<float> oversampler { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false };
    int maxBlockSize = 512;
    juce::AudioBuffer<float> dryBuffer;   // señal sin saturar, para la mezcla en paralelo
    float lastAmount = 0.0f;
    bool satWasActive = false;
    float dcX[2] {}, dcY[2] {};
    double currentRate = 44100.0;

    std::atomic<float> inPeak[2] { 0.0f, 0.0f }, outPeak[2] { 0.0f, 0.0f };
    std::atomic<float> dynGainDb[EQ::NumBands] {};

    juce::AbstractFifo analyzerFifo { 16384 };
    std::array<float, 16384> analyzerData {};
    void pushAnalyzerSamples (const juce::AudioBuffer<float>&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MedidoresEQAudioProcessor)
};
