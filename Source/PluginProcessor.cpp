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

        if (b != EQ::HighPass)
        {
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { EQ::gainId (b), 1 }, name + " ganancia",
                NormalisableRange<float> (-18.0f, 18.0f, 0.1f), info.gain,
                AudioParameterFloatAttributes().withLabel ("dB")));
        }

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { EQ::qId (b), 1 }, name + " Q",
            NormalisableRange<float> (0.1f, 10.0f, 0.01f, 0.5f), info.q));
    }

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { EQ::outId, 1 }, "Salida",
        NormalisableRange<float> (-18.0f, 18.0f, 0.1f), 0.0f,
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
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, (juce::uint32) getTotalNumOutputChannels() };
    for (auto& f : filters) f.prepare (spec);
    outGain.prepare (spec);
    outGain.setRampDurationSeconds (0.02);
    updateFilters();
}

void MedidoresEQAudioProcessor::updateFilters()
{
    for (int b = 0; b < EQ::NumBands; ++b)
        *filters[b].state = *EQ::makeCoeffs (b, apvts, currentRate);

    outGain.setGainDecibels (apvts.getRawParameterValue (EQ::outId)->load());
}

void MedidoresEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    updateFilters();

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    for (auto& f : filters) f.process (ctx);
    outGain.process (ctx);

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
