#include "PluginProcessor.h"
#include "PluginEditor.h"

MedidoresEQAudioProcessor::MedidoresEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    // El host puede consultar la latencia antes de preparar la reproducción: se fija ya aquí.
    oversampler.initProcessing (512);
    setLatencySamples (juce::roundToInt (oversampler.getLatencyInSamples()));
}

juce::AudioProcessorValueTreeState::ParameterLayout MedidoresEQAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // Textos con pocos decimales: Hz sin decimales (kHz con 1-2), dB con 1, Q con 1.
    auto hzText = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (roundToInt (v)) + " Hz"; };
    auto hzParse = [] (const String& t) { float v = t.getFloatValue(); return t.containsIgnoreCase ("k") ? v * 1000.0f : v; };
    auto dbText = [] (float v, int) { return (v > 0.04f ? "+" : "") + String (v, 1) + " dB"; };
    auto numParse = [] (const String& t) { return t.getFloatValue(); };
    auto qText = [] (float v, int) { return String (v, 1); };
    auto pctText = [] (float v, int) { return String (roundToInt (v)) + " %"; };
    auto ratioText = [] (float v, int) { return String (v, 1) + ":1"; };
    auto msText = [] (float v, int) { return String (roundToInt (v)) + " ms"; };
    auto thrText = [] (float v, int) { return String (roundToInt (v)) + " dB"; };

    for (int b = 0; b < EQ::NumBands; ++b)
    {
        const auto& info = EQ::bands[b];
        const String name (info.name);

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { EQ::onId (b), 1 }, name + " activa", true));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { EQ::freqId (b), 1 }, name + " frecuencia",
            NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.25f), info.freq,
            AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (hzText).withValueFromStringFunction (hzParse)));

        if (EQ::isCut (b))
        {
            layout.add (std::make_unique<AudioParameterChoice> (
                ParameterID { EQ::slopeId (b), 1 }, name + " pendiente", EQ::slopeNames(), 1));
        }
        else
        {
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { EQ::gainId (b), 1 }, name + " ganancia",
                NormalisableRange<float> (-18.0f, 18.0f, 0.1f), info.gain,
                AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText).withValueFromStringFunction (numParse)));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { EQ::qId (b), 1 }, name + " Q",
                NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.5f), info.q,
                AudioParameterFloatAttributes().withStringFromValueFunction (qText).withValueFromStringFunction (numParse)));

            if (EQ::hasType (b))
                layout.add (std::make_unique<AudioParameterBool> (
                    ParameterID { EQ::typeId (b), 1 }, name + " como campana", false));

            // EQ dinámico: la ganancia (hasta el valor del knob de ganancia) solo se aplica cuando el nivel
            // en la banda supera el umbral.
            layout.add (std::make_unique<AudioParameterBool> (
                ParameterID { EQ::dynId (b), 1 }, name + EQ::utf8 (" din\u00e1mica"), false));
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { EQ::thrId (b), 1 }, name + " umbral",
                NormalisableRange<float> (-60.0f, 0.0f, 1.0f), -24.0f,
                AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (thrText).withValueFromStringFunction (numParse)));
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { EQ::ratioId (b), 1 }, name + " ratio",
                NormalisableRange<float> (1.2f, 10.0f, 0.1f, 0.5f), 3.0f,
                AudioParameterFloatAttributes().withStringFromValueFunction (ratioText).withValueFromStringFunction (numParse)));
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { EQ::attackId (b), 1 }, name + " ataque",
                NormalisableRange<float> (0.5f, 100.0f, 0.5f, 0.5f), 10.0f,
                AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText).withValueFromStringFunction (numParse)));
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { EQ::releaseId (b), 1 }, name + " release",
                NormalisableRange<float> (20.0f, 1000.0f, 1.0f, 0.4f), 150.0f,
                AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction (msText).withValueFromStringFunction (numParse)));
        }

        if (! EQ::isCut (b))   // los filtros de corte no tienen Mid/Side
            layout.add (std::make_unique<AudioParameterChoice> (
                ParameterID { EQ::chId (b), 1 }, name + " canal", EQ::placementNames(), 0));
    }

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { EQ::styleId, 1 }, "Estilo de curva", EQ::styleNames(), 1));

    // Ajustes de la vista (no se automatizan).
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { EQ::analyzerId, 1 }, "Analizador", EQ::analyzerNames(), 1, AudioParameterChoiceAttributes().withAutomatable (false)));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { EQ::analyzerSpeedId, 1 }, "Velocidad analizador", EQ::speedNames(), 1, AudioParameterChoiceAttributes().withAutomatable (false)));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { EQ::rangeId, 1 }, "Rango de la curva", EQ::rangeNames(), 1, AudioParameterChoiceAttributes().withAutomatable (false)));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { EQ::themeId, 1 }, "Tema visual", EQ::themeNames(), 0, AudioParameterChoiceAttributes().withAutomatable (false)));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { EQ::characterId, 1 }, EQ::utf8 ("Car\u00e1cter"), EQ::characterNames(), 1));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { EQ::driveId, 1 }, "Drive",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pctText).withValueFromStringFunction (numParse)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { EQ::mixId, 1 }, EQ::utf8 ("Mezcla saturaci\u00f3n"),
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pctText).withValueFromStringFunction (numParse)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { EQ::inId, 1 }, "Entrada",
        NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText).withValueFromStringFunction (numParse)));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { EQ::outId, 1 }, "Salida",
        NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText).withValueFromStringFunction (numParse)));

    return layout;
}

bool MedidoresEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == layouts.getMainInputChannelSet();
}

void MedidoresEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    maxBlockSize = juce::jmax (1, samplesPerBlock);
    oversampler.initProcessing ((size_t) maxBlockSize);
    dryBuffer.setSize (2, maxBlockSize);

    // Latencia fija del plugin: la del sobremuestreo (entera). El EQ es de fase mínima y no añade latencia.
    const int latency = juce::roundToInt (oversampler.getLatencyInSamples());
    latencyDelay = (float) latency;
    setLatencySamples (latency);
    dryDelay.prepare ({ sampleRate, (juce::uint32) samplesPerBlock, 2 });
    dryDelay.setDelay (latencyDelay);
    dryDelay.reset();
    satWasActive = false;
    lastAmount = 0.0f;
    updateFilters (true);   // antes de preparar: así cada filtro ya tiene coeficientes de orden 2

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    for (auto& band : filters)
        for (auto& stage : band)
            for (auto& f : stage) f.prepare (spec);
    for (auto& d : dynamic) { d.detector.prepare (spec); d.envelope = 0.0f; }

    juce::dsp::ProcessSpec outSpec { sampleRate, (juce::uint32) samplesPerBlock, (juce::uint32) getTotalNumOutputChannels() };
    inGain.prepare (outSpec);
    inGain.setRampDurationSeconds (0.02);
    outGain.prepare (outSpec);
    outGain.setRampDurationSeconds (0.02);
}

// Recalcula los coeficientes solo de las bandas cuyos parámetros han cambiado.
void MedidoresEQAudioProcessor::updateFilters (bool force)
{
    auto read = [&] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };

    for (int b = 0; b < EQ::NumBands; ++b)
    {
        const std::array<float, 8> now {
            read (EQ::onId (b)), read (EQ::freqId (b)),
            EQ::isCut (b) ? read (EQ::slopeId (b)) : read (EQ::gainId (b)),
            EQ::isCut (b) ? 0.0f : read (EQ::qId (b)), read (EQ::styleId),
            EQ::hasType (b) ? read (EQ::typeId (b)) : 0.0f,
            EQ::hasDyn (b) ? read (EQ::dynId (b)) : 0.0f, 0.0f };

        if (! force && now == lastParams[b]) continue;
        lastParams[b] = now;

        const auto bf = EQ::makeBand (b, apvts, currentRate);
        numStages[b] = bf.numStages;
        for (int s = 0; s < bf.numStages; ++s)
            for (int ch = 0; ch < 2; ++ch)
                filters[b][s][ch].coefficients = bf.stage[s];

        if (EQ::hasDyn (b))
        {
            auto& d = dynamic[b];
            d.on = now[6] > 0.5f && now[0] > 0.5f;
            d.settings = EQ::readSettings (b, apvts, currentRate);
            // Detector: paso de banda en las campanas, paso bajo en el shelf de graves, paso alto en el de agudos.
            d.detector.coefficients = d.settings.shape == EQ::Peak ? EQ::Coeffs::makeBandPass (currentRate, d.settings.freq, juce::jmax (0.5f, d.settings.q))
                                    : d.settings.shape == EQ::LowShelfShape ? EQ::Coeffs::makeLowPass (currentRate, d.settings.freq, 0.707f)
                                                                           : EQ::Coeffs::makeHighPass (currentRate, d.settings.freq, 0.707f);
        }
    }

    for (int b = 0; b < EQ::NumBands; ++b)
        if (EQ::hasDyn (b))
        {
            auto& d = dynamic[b];
            d.threshold = read (EQ::thrId (b));
            d.ratio = juce::jmax (1.01f, read (EQ::ratioId (b)));
            d.attackCoef  = std::exp (-1.0f / (juce::jmax (0.1f, read (EQ::attackId (b)))  * 0.001f * (float) currentRate));
            d.releaseCoef = std::exp (-1.0f / (juce::jmax (0.1f, read (EQ::releaseId (b))) * 0.001f * (float) currentRate));
        }

    inGain.setGainDecibels (read (EQ::inId));
    outGain.setGainDecibels (read (EQ::outId));
}

void MedidoresEQAudioProcessor::runFilters (juce::AudioBuffer<float>& buffer, int b, int where, int numCh, int start, int len)
{
    juce::dsp::AudioBlock<float> block (buffer);
    auto run = [&] (int ch)
    {
        auto one = block.getSingleChannelBlock ((size_t) ch).getSubBlock ((size_t) start, (size_t) len);
        juce::dsp::ProcessContextReplacing<float> ctx (one);
        for (int s = 0; s < numStages[b]; ++s) filters[b][s][ch].process (ctx);
    };

    if (where == 0) for (int ch = 0; ch < numCh; ++ch) run (ch);
    else            run (where - 1);   // canal 0 = Mid, canal 1 = Side mientras dura el filtrado
}

// EQ dinámico: mide el nivel de la señal en la banda y ajusta la ganancia del filtro.
// Con el nivel por encima del umbral, la ganancia sube (o baja) progresivamente según el ratio,
// hasta el valor del knob de ganancia; por debajo del umbral la banda queda plana.
void MedidoresEQAudioProcessor::updateDynamic (juce::AudioBuffer<float>& buffer, int b, int where, int numCh, int start, int len)
{
    auto& d = dynamic[b];
    const float* in0 = buffer.getReadPointer (0);
    const float* in1 = buffer.getReadPointer (numCh > 1 ? 1 : 0);
    const float* mono = where == 0 ? nullptr : (where == 1 ? in0 : in1);

    float env = d.envelope;
    for (int i = start; i < start + len; ++i)
    {
        const float x = mono != nullptr ? mono[i] : (numCh > 1 ? 0.5f * (in0[i] + in1[i]) : in0[i]);
        const float r = std::abs (d.detector.processSample (x));
        env = r > env ? r + d.attackCoef * (env - r) : r + d.releaseCoef * (env - r);
    }
    d.envelope = env;

    const float over = juce::Decibels::gainToDecibels (env, -100.0f) - d.threshold;
    const float reduction = over > 0.0f ? juce::jmin (over * (1.0f - 1.0f / d.ratio), std::abs (d.settings.gainDb)) : 0.0f;
    const float gainDb = d.settings.gainDb >= 0.0f ? reduction : -reduction;
    dynGainDb[b].store (gainDb);

    // Escribe los coeficientes en el objeto compartido por los dos canales, sin reservar memoria.
    float c[6];
    EQ::fillCoeffs (d.settings, gainDb, currentRate, c);
    auto* raw = filters[b][0][0].coefficients->coefficients.getRawDataPointer();
    const float inv = 1.0f / c[3];
    raw[0] = c[0] * inv; raw[1] = c[1] * inv; raw[2] = c[2] * inv; raw[3] = c[4] * inv; raw[4] = c[5] * inv;
}

void MedidoresEQAudioProcessor::processBand (juce::AudioBuffer<float>& buffer, int b)
{
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int n = buffer.getNumSamples();
    const int where = numCh < 2 ? 0 : EQ::placement (b, apvts);
    const bool dyn = EQ::hasDyn (b) && dynamic[b].on;
    if (EQ::hasDyn (b) && ! dyn) dynGainDb[b].store (0.0f);

    float* l = buffer.getWritePointer (0);
    float* r = numCh > 1 ? buffer.getWritePointer (1) : nullptr;

    if (where != 0)   // Mid/Side: canal 0 = Mid, canal 1 = Side
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (l[i] + r[i]), s = 0.5f * (l[i] - r[i]);
            l[i] = m; r[i] = s;
        }

    const int step = dyn ? dynamicBlock : juce::jmax (1, n);
    for (int start = 0; start < n; start += step)
    {
        const int len = juce::jmin (step, n - start);
        if (dyn) updateDynamic (buffer, b, where, numCh, start, len);
        runFilters (buffer, b, where, numCh, start, len);
    }

    if (where != 0)
        for (int i = 0; i < n; ++i)
        {
            const float m = l[i], s = r[i];
            l[i] = m + s; r[i] = m - s;
        }
}

void MedidoresEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    auto measure = [&] (std::atomic<float>* peaks)
    {
        for (int ch = 0; ch < juce::jmin (buffer.getNumChannels(), 2); ++ch)
            peaks[ch].store (juce::jmax (peaks[ch].load(), buffer.getMagnitude (ch, 0, buffer.getNumSamples())));
    };

    updateFilters (false);

    // Ganancia de entrada antes de todo; el medidor de entrada mide ya con ella aplicada (lo que llega al EQ).
    {
        juce::dsp::AudioBlock<float> inBlock (buffer);
        juce::dsp::ProcessContextReplacing<float> inCtx (inBlock);
        inGain.process (inCtx);
    }
    measure (inPeak);

    const int analyzerMode = (int) apvts.getRawParameterValue (EQ::analyzerId)->load();
    if (analyzerMode == 2) pushAnalyzerSamples (buffer);   // pre-EQ

    for (int b = 0; b < EQ::NumBands; ++b) processBand (buffer, b);

    saturate (buffer);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    outGain.process (ctx);

    measure (outPeak);
    if (analyzerMode == 1) pushAnalyzerSamples (buffer);   // post-EQ
}

// Saturación después del EQ (las bandas muy subidas "empujan" el saturador, como en un equipo analógico).
//  Cinta:   tanh(a·x)/a, simétrica: armónicos impares, compresión suave.
//  Válvula: tanh(a·x + b) con polarización b, asimétrica: añade armónicos pares (más calidez).
// Ambas tienden a la identidad cuando el Drive tiende a 0 y llevan una compensación parcial de volumen.
// Se calcula a 2x de la frecuencia de muestreo para que los armónicos no se plieguen.
//
// Latencia: el sobremuestreo retrasa la señal (número entero de muestras, que se informa al host). Para que la latencia sea
// SIEMPRE la misma, también con la saturación apagada o con la mezcla en paralelo, la señal seca pasa por una línea de retardo
// del mismo tamaño: así seco y saturado están alineados y no hay efecto peine.
void MedidoresEQAudioProcessor::saturate (juce::AudioBuffer<float>& buffer)
{
    const int character = (int) apvts.getRawParameterValue (EQ::characterId)->load();
    const float drive = apvts.getRawParameterValue (EQ::driveId)->load() / 100.0f;
    const float amount = drive * drive * 6.0f;   // "a": 0 = limpio
    const float mix = apvts.getRawParameterValue (EQ::mixId)->load() / 100.0f;   // 1 = todo saturado, 0 = todo seco

    const bool active = character != 0 && amount > 1e-3f && mix > 1e-3f;
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);

    if (active && ! satWasActive)
    {
        oversampler.reset();
        for (int ch = 0; ch < 2; ++ch) dcX[ch] = dcY[ch] = 0.0f;
        lastAmount = amount;
    }
    satWasActive = active;

    const float bias = 0.3f;
    const float tanhBias = std::tanh (bias);
    const float biasSlope = 1.0f - tanhBias * tanhBias;
    const float dcCoeff = 1.0f - juce::MathConstants<float>::twoPi * 5.0f / (float) currentRate;

    juce::dsp::AudioBlock<float> whole (buffer);
    auto channels = whole.getSubsetChannelBlock (0, (size_t) numCh);

    for (int start = 0; start < buffer.getNumSamples(); start += maxBlockSize)
    {
        const int len = juce::jmin (maxBlockSize, buffer.getNumSamples() - start);
        auto sub = channels.getSubBlock ((size_t) start, (size_t) len);

        // Señal seca retardada la misma latencia que la saturada (se hace siempre para que la línea esté al día).
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = sub.getChannelPointer ((size_t) ch);
            auto* dry = dryBuffer.getWritePointer (ch);
            for (int i = 0; i < len; ++i)
            {
                dryDelay.pushSample (ch, d[i]);
                dry[i] = dryDelay.popSample (ch, latencyDelay);
            }
        }

        if (! active)
        {
            for (int ch = 0; ch < numCh; ++ch)
                std::copy_n (dryBuffer.getReadPointer (ch), len, sub.getChannelPointer ((size_t) ch));
            lastAmount = amount;
            continue;
        }

        // Cantidad interpolada dentro del bloque para evitar saltos al mover el Drive.
        const float a0 = lastAmount, a1 = amount;
        auto up = oversampler.processSamplesUp (sub);
        const int n = (int) up.getNumSamples();
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* d = up.getChannelPointer ((size_t) ch);
            for (int i = 0; i < n; ++i)
            {
                const float a = juce::jmax (1e-3f, a0 + (a1 - a0) * (float) i / (float) n);
                const float makeup = std::sqrt (1.0f + a);
                if (character == 1)
                    d[i] = std::tanh (a * d[i]) / a * makeup;
                else
                    d[i] = (std::tanh (a * d[i] + bias) - tanhBias) / (a * biasSlope) * makeup;
            }
        }
        oversampler.processSamplesDown (sub);

        if (character == 2)   // la asimetría genera un poco de continua: se quita con un paso alto a ~5 Hz
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* d = sub.getChannelPointer ((size_t) ch);
                for (int i = 0; i < len; ++i)
                {
                    const float x = d[i];
                    const float y = x - dcX[ch] + dcCoeff * dcY[ch];
                    dcX[ch] = x; dcY[ch] = y;
                    d[i] = y;
                }
            }

        if (mix < 0.999f)   // saturación en paralelo
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* d = sub.getChannelPointer ((size_t) ch);
                const auto* dry = dryBuffer.getReadPointer (ch);
                for (int i = 0; i < len; ++i) d[i] = dry[i] * (1.0f - mix) + d[i] * mix;
            }

        lastAmount = amount;
    }
}

void MedidoresEQAudioProcessor::pushAnalyzerSamples (const juce::AudioBuffer<float>& buffer)
{
    const int n = juce::jmin (buffer.getNumSamples(), analyzerFifo.getFreeSpace());
    if (n <= 0) return;

    const int chans = buffer.getNumChannels();
    int src = 0;
    auto scope = analyzerFifo.write (n);
    auto copy = [&] (int start, int size)
    {
        for (int i = 0; i < size; ++i, ++src)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < chans; ++ch) sum += buffer.getSample (ch, src);
            analyzerData[(size_t) (start + i)] = sum / (float) chans;
        }
    };
    copy (scope.startIndex1, scope.blockSize1);
    copy (scope.startIndex2, scope.blockSize2);
}

int MedidoresEQAudioProcessor::pullAnalyzerSamples (float* dest, int maxSamples)
{
    const int n = juce::jmin (maxSamples, analyzerFifo.getNumReady());
    if (n <= 0) return 0;

    auto scope = analyzerFifo.read (n);
    std::copy_n (analyzerData.begin() + scope.startIndex1, scope.blockSize1, dest);
    std::copy_n (analyzerData.begin() + scope.startIndex2, scope.blockSize2, dest + scope.blockSize1);
    return n;
}

void MedidoresEQAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void MedidoresEQAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* MedidoresEQAudioProcessor::createEditor()
{
    return new MedidoresEQAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MedidoresEQAudioProcessor();
}
