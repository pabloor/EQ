#include "PluginEditor.h"

//==============================================================================
ResponseCurve::ResponseCurve (MedidoresEQAudioProcessor& p) : proc (p)
{
    spectrum.fill (-120.0f);
    startTimerHz (30);
}

float ResponseCurve::xForFreq (float f) const
{
    return (float) getWidth() * std::log (f / 20.0f) / std::log (1000.0f);
}
float ResponseCurve::freqForX (float x) const
{
    return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, x / (float) getWidth()));
}
float ResponseCurve::yForDb (float d) const
{
    return (float) getHeight() * (1.0f - (d - minDb) / (maxDb - minDb));
}
float ResponseCurve::dbForY (float y) const
{
    return minDb + (maxDb - minDb) * (1.0f - y / (float) getHeight());
}

juce::Point<float> ResponseCurve::nodePos (int b) const
{
    const float f = proc.apvts.getRawParameterValue (EQ::freqId (b))->load();
    const float g = b == EQ::HighPass ? 0.0f : proc.apvts.getRawParameterValue (EQ::gainId (b))->load();
    return { xForFreq (f), yForDb (g) };
}

int ResponseCurve::hitTest (juce::Point<float> p) const
{
    int best = -1;
    float bestDist = 14.0f;
    for (int b = 0; b < EQ::NumBands; ++b)
    {
        const float d = nodePos (b).getDistanceFrom (p);
        if (d < bestDist) { bestDist = d; best = b; }
    }
    return best;
}

//==============================================================================
void ResponseCurve::timerCallback()
{
    updateSpectrum();
    repaint();
}

void ResponseCurve::updateSpectrum()
{
    float tmp[4096];
    bool got = false;
    while (int n = proc.pullAnalyzerSamples (tmp, 4096))
    {
        got = true;
        if (n >= fftSize)
            std::copy (tmp + n - fftSize, tmp + n, ring.begin());
        else
        {
            std::copy (ring.begin() + n, ring.end(), ring.begin());
            std::copy (tmp, tmp + n, ring.end() - n);
        }
    }

    if (! got) { for (auto& s : spectrum) s = juce::jmax (-120.0f, s - 3.0f); return; }

    std::copy (ring.begin(), ring.end(), fftData.begin());
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    for (size_t i = 0; i < spectrum.size(); ++i)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[i] * 4.0f / (float) fftSize, -120.0f);
        spectrum[i] = spectrum[i] * 0.7f + db * 0.3f;
    }
}

//==============================================================================
void ResponseCurve::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff15181d));
    g.fillRoundedRectangle (area, 6.0f);

    // Rejilla
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.setFont (10.0f);
    for (float f : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f })
    {
        g.drawVerticalLine ((int) xForFreq (f), 0.0f, area.getHeight());
        g.drawText (f >= 1000.f ? juce::String (f / 1000.f) + "k" : juce::String (f),
                    (int) xForFreq (f) + 2, (int) area.getHeight() - 14, 36, 12, juce::Justification::left);
    }
    for (float d : { -18.f, -12.f, -6.f, 6.f, 12.f, 18.f })
        g.drawHorizontalLine ((int) yForDb (d), 0.0f, area.getWidth());
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawHorizontalLine ((int) yForDb (0.0f), 0.0f, area.getWidth());

    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;

    // Analizador (post-EQ), escala fija de -100 a 0 dBFS
    {
        juce::Path sp;
        bool started = false;
        for (size_t i = 1; i < spectrum.size(); ++i)
        {
            const float f = (float) i * (float) sr / (float) fftSize;
            if (f < 20.0f || f > 20000.0f) continue;
            const float db = juce::jlimit (-100.0f, 0.0f, spectrum[i]);
            const float x = xForFreq (f), y = area.getHeight() * (-db / 100.0f);
            if (! started) { sp.startNewSubPath (x, area.getHeight()); sp.lineTo (x, y); started = true; }
            else sp.lineTo (x, y);
        }
        if (started)
        {
            sp.lineTo (sp.getCurrentPosition().x, area.getHeight());
            sp.closeSubPath();
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillPath (sp);
        }
    }

    // Respuesta total = producto de las bandas
    EQ::Coeffs::Ptr coeffs[EQ::NumBands];
    for (int b = 0; b < EQ::NumBands; ++b) coeffs[b] = EQ::makeCoeffs (b, proc.apvts, sr);

    juce::Path path;
    const int w = juce::jmax (2, (int) area.getWidth());
    for (int i = 0; i < w; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, (double) i / (w - 1));
        double mag = 1.0;
        for (auto& c : coeffs) mag *= c->getMagnitudeForFrequency (f, sr);
        const float db = juce::jlimit (minDb, maxDb, juce::Decibels::gainToDecibels ((float) mag, -60.0f));
        if (i == 0) path.startNewSubPath ((float) i, yForDb (db)); else path.lineTo ((float) i, yForDb (db));
    }
    g.setColour (juce::Colour (0xff4fc3f7));
    g.strokePath (path, juce::PathStrokeType (2.0f));

    // Puntos de banda
    for (int b = 0; b < EQ::NumBands; ++b)
    {
        const bool on = proc.apvts.getRawParameterValue (EQ::onId (b))->load() > 0.5f;
        const auto p = nodePos (b);
        const float r = (b == hovered || b == dragged) ? 8.0f : 6.0f;
        g.setColour (EQ::bandColours[b].withAlpha (on ? 1.0f : 0.5f));
        if (on) g.fillEllipse (p.x - r, p.y - r, 2 * r, 2 * r);
        else    g.drawEllipse (p.x - r, p.y - r, 2 * r, 2 * r, 2.0f);
    }
}

//==============================================================================
void ResponseCurve::setParam (const juce::String& id, float v)
{
    if (auto* p = param (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

void ResponseCurve::gesture (int b, bool begin)
{
    for (auto id : { EQ::freqId (b), EQ::gainId (b) })
        if (auto* p = param (id))
        {
            if (begin) p->beginChangeGesture(); else p->endChangeGesture();
        }
}

void ResponseCurve::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitTest (e.position);
    if (h != hovered) { hovered = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void ResponseCurve::mouseExit (const juce::MouseEvent&) { hovered = -1; repaint(); }

void ResponseCurve::mouseDown (const juce::MouseEvent& e)
{
    dragged = hitTest (e.position);
    if (dragged >= 0) gesture (dragged, true);
}

void ResponseCurve::mouseDrag (const juce::MouseEvent& e)
{
    if (dragged < 0) return;
    setParam (EQ::freqId (dragged), juce::jlimit (20.0f, 20000.0f, freqForX (e.position.x)));
    if (dragged != EQ::HighPass)
        setParam (EQ::gainId (dragged), juce::jlimit (-18.0f, 18.0f, dbForY (e.position.y)));
}

void ResponseCurve::mouseUp (const juce::MouseEvent&)
{
    if (dragged >= 0) gesture (dragged, false);
    dragged = -1;
}

void ResponseCurve::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = hitTest (e.position);
    if (b < 0) return;
    if (auto* p = param (EQ::onId (b)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
}

void ResponseCurve::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int b = hitTest (e.position);
    if (b < 0) return;
    if (auto* p = param (EQ::qId (b)))
    {
        const float q = proc.apvts.getRawParameterValue (EQ::qId (b))->load() * std::exp (wheel.deltaY);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (0.1f, 10.0f, q)));
        p->endChangeGesture();
    }
}

//==============================================================================
MedidoresEQAudioProcessorEditor::MedidoresEQAudioProcessorEditor (MedidoresEQAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), curve (p)
{
    addAndMakeVisible (curve);

    for (int b = 0; b < EQ::NumBands; ++b)
    {
        toggles[b].setButtonText (EQ::bands[b].name);
        toggles[b].setColour (juce::ToggleButton::tickColourId, EQ::bandColours[b]);
        toggleAttachments[b] = std::make_unique<ButtonAttachment> (proc.apvts, EQ::onId (b), toggles[b]);
        addAndMakeVisible (toggles[b]);

        addKnob (knobs[b][0], EQ::freqId (b), "Frec", " Hz");
        if (b != EQ::HighPass) addKnob (knobs[b][1], EQ::gainId (b), "Gan", " dB");
        addKnob (knobs[b][2], EQ::qId (b), "Q", "");
    }
    addKnob (outKnob, EQ::outId, "Salida", " dB");

    setSize (680, 540);
}

void MedidoresEQAudioProcessorEditor::addKnob (Knob& k, const juce::String& id, const juce::String& text, const juce::String& suffix)
{
    k.slider.setTextValueSuffix (suffix);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.attachment = std::make_unique<SliderAttachment> (proc.apvts, id, k.slider);
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
}

void MedidoresEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff22262c));
}

void MedidoresEQAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    curve.setBounds (area.removeFromTop (240));
    area.removeFromTop (8);

    const int colW = area.getWidth() / (EQ::NumBands + 1);
    auto toggleRow = area.removeFromTop (26);
    for (int b = 0; b < EQ::NumBands; ++b)
        toggles[b].setBounds (toggleRow.getX() + b * colW + 6, toggleRow.getY(), colW - 6, toggleRow.getHeight());

    const int rowH = area.getHeight() / 3;
    auto place = [] (Knob& k, juce::Rectangle<int> r)
    {
        k.label.setBounds (r.removeFromTop (16));
        k.slider.setBounds (r);
    };

    for (int b = 0; b < EQ::NumBands; ++b)
        for (int row = 0; row < 3; ++row)
            if (! (b == EQ::HighPass && row == 1))
                place (knobs[b][row], { area.getX() + b * colW, area.getY() + row * rowH, colW, rowH });

    place (outKnob, { area.getX() + EQ::NumBands * colW, area.getY(), colW, rowH });
}
