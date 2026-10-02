#include "PluginProcessor.h"
#include "PluginEditor.h"

MedidoresEQAudioProcessor::MedidoresEQAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout MedidoresEQAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    for (int b = 0; b < EQ::NumBands; ++b)
    {
        const auto& info = EQ::bands[b];
        const String name (info.name);

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { EQ::onId (b), 1 }, name + " activa", true));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { EQ::freqId (b), 1 }, name + " frecuencia",
            NormalisableRange<float> (20.0f, 20000.0f, 0.0f, 0.25f), info.freq,
            AudioParameterFloatAttributes().withLabel ("Hz")));

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
                AudioParameterFloatAttributes().withLabel ("dB")));

            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { EQ::qId (b), 1 }, name + " Q",
                NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.5f), info.q));
        }

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { EQ::chId (b), 1 }, name + " canal", EQ::placementNames(), 0));
    }

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { EQ::outId, 1 }, "Salida",
        NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

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
    updateFilters (true);   // antes de preparar: así cada filtro ya tiene coeficientes de orden 2

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 1 };
    for (auto& band : filters)
        for (auto& stage : band)
            for (auto& f : stage) f.prepare (spec);

    juce::dsp::ProcessSpec outSpec { sampleRate, (juce::uint32) samplesPerBlock, (juce::uint32) getTotalNumOutputChannels() };
    outGain.prepare (outSpec);
    outGain.setRampDurationSeconds (0.02);
}

// Recalcula los coeficientes solo de las bandas cuyos parámetros han cambiado.
void MedidoresEQAudioProcessor::updateFilters (bool force)
{
    for (int b = 0; b < EQ::NumBands; ++b)
    {
        auto read = [&] (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); };
        const std::array<float, 6> now {
            read (EQ::onId (b)), read (EQ::freqId (b)),
            EQ::isCut (b) ? read (EQ::slopeId (b)) : read (EQ::gainId (b)),
            EQ::isCut (b) ? 0.0f : read (EQ::qId (b)), 0.0f, 0.0f };

        if (! force && now == lastParams[b]) continue;
        lastParams[b] = now;

        const auto bf = EQ::makeBand (b, apvts, currentRate);
        numStages[b] = bf.numStages;
        for (int s = 0; s < bf.numStages; ++s)
            for (int ch = 0; ch < 2; ++ch)
                filters[b][s][ch].coefficients = bf.stage[s];
    }

    outGain.setGainDecibels (apvts.getRawParameterValue (EQ::outId)->load());
}

void MedidoresEQAudioProcessor::processBand (juce::AudioBuffer<float>& buffer, int b)
{
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int n = buffer.getNumSamples();
    const int where = numCh < 2 ? 0 : EQ::placement (b, apvts);

    auto run = [&] (int ch)
    {
        juce::dsp::AudioBlock<float> block (buffer);
        auto one = block.getSingleChannelBlock ((size_t) ch);
        juce::dsp::ProcessContextReplacing<float> ctx (one);
        for (int s = 0; s < numStages[b]; ++s) filters[b][s][ch].process (ctx);
    };

    if (where == 0)
    {
        for (int ch = 0; ch < numCh; ++ch) run (ch);
        return;
    }

    // Mid/Side: canal 0 = Mid, canal 1 = Side mientras dura el filtrado.
    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (l[i] + r[i]), s = 0.5f * (l[i] - r[i]);
        l[i] = m; r[i] = s;
    }
    run (where - 1);
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

    measure (inPeak);
    updateFilters (false);

    for (int b = 0; b < EQ::NumBands; ++b) processBand (buffer, b);

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    outGain.process (ctx);

    measure (outPeak);
    pushAnalyzerSamples (buffer);
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
