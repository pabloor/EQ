#include "PluginEditor.h"

//==============================================================================
ResponseCurve::ResponseCurve (MedidoresEQAudioProcessor& p) : proc (p)
{
    spectrum.fill (-120.0f);
    setTooltip (EQ::utf8 ("Arrastra un punto: frecuencia y ganancia. Rueda sobre un punto: Q. Doble clic en un punto: activar o desactivar la banda. "
                          "Las asas laterales de la banda enfocada cambian su ancho (Q)."));
    startTimerHz (30);
}

float ResponseCurve::rangeDb() const
{
    return EQ::rangeDbFor ((int) proc.apvts.getRawParameterValue (EQ::rangeId)->load());
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
    const float r = rangeDb();
    return (float) getHeight() * (1.0f - (d + r) / (2.0f * r));
}
float ResponseCurve::dbForY (float y) const
{
    const float r = rangeDb();
    return -r + 2.0f * r * (1.0f - y / (float) getHeight());
}

juce::Point<float> ResponseCurve::nodePos (int b) const
{
    const float f = proc.apvts.getRawParameterValue (EQ::freqId (b))->load();
    const float g = EQ::isCut (b) ? 0.0f : proc.apvts.getRawParameterValue (EQ::gainId (b))->load();
    return { xForFreq (f), juce::jlimit (0.0f, (float) getHeight(), yForDb (g)) };
}

int ResponseCurve::nodeAt (juce::Point<float> p) const
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

bool ResponseCurve::isBell (int b) const
{
    return b == EQ::Bell1 || b == EQ::Bell2
           || (EQ::hasType (b) && proc.apvts.getRawParameterValue (EQ::typeId (b))->load() > 0.5f);
}

// Ancho en octavas <-> Q (campana): BW = 2·asinh(1/(2Q))/ln2
static float octavesForQ (float q)
{
    const float x = 1.0f / (2.0f * q);
    return 2.0f * std::log2 (x + std::sqrt (x * x + 1.0f));
}
static float qForOctaves (float octaves)
{
    return 1.0f / (2.0f * std::sinh (octaves * 0.34657359f));   // ln2/2
}

float ResponseCurve::effectiveQ (int b) const
{
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    const auto s = EQ::readSettings (b, proc.apvts, sr);
    return EQ::styleQ (s.style, EQ::Peak, s.q, s.gainDb);
}

juce::Point<float> ResponseCurve::handlePos (int b, int side) const
{
    const float f = proc.apvts.getRawParameterValue (EQ::freqId (b))->load();
    const float oct = octavesForQ (effectiveQ (b));
    const float fe = juce::jlimit (20.0f, 20000.0f, f * std::pow (2.0f, (float) side * oct * 0.5f));
    return { xForFreq (fe), nodePos (b).y };
}

int ResponseCurve::handleAt (juce::Point<float> p) const
{
    if (focusBand < 0 || ! isBell (focusBand)
        || proc.apvts.getRawParameterValue (EQ::onId (focusBand))->load() < 0.5f)
        return 0;
    if (handlePos (focusBand, -1).getDistanceFrom (p) < 9.0f) return -1;
    if (handlePos (focusBand, +1).getDistanceFrom (p) < 9.0f) return +1;
    return 0;
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

    const int speed = (int) proc.apvts.getRawParameterValue (EQ::analyzerSpeedId)->load();
    const float w = speed == 0 ? 0.12f : (speed == 2 ? 0.6f : 0.3f);   // peso de la medida nueva
    for (size_t i = 0; i < spectrum.size(); ++i)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[i] * 4.0f / (float) fftSize, -120.0f);
        spectrum[i] = spectrum[i] * (1.0f - w) + db * w;
    }
}

//==============================================================================
void ResponseCurve::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float R = rangeDb();
    g.setColour (Theme::screen);
    g.fillRoundedRectangle (area, 6.0f);

    // Rejilla de frecuencia
    g.setFont (10.0f);
    for (float f : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f })
    {
        g.setColour (Theme::text.withAlpha (0.07f));
        g.drawVerticalLine ((int) xForFreq (f), 0.0f, area.getHeight());
        g.setColour (Theme::muted);
        g.drawText (f >= 1000.f ? juce::String (f / 1000.f) + "k" : juce::String (f),
                    (int) xForFreq (f) + 2, (int) area.getHeight() - 14, 36, 12, juce::Justification::left);
    }

    // Rejilla de dB (el paso depende del rango elegido)
    const float step = R <= 6.0f ? 3.0f : (R <= 12.0f ? 6.0f : 12.0f);
    for (int k = -1; k <= 1; ++k)
    {
        const float d = (float) k * step;
        g.setColour (Theme::text.withAlpha (k == 0 ? 0.25f : 0.07f));
        g.drawHorizontalLine ((int) yForDb (d), 0.0f, area.getWidth());
        g.setColour (Theme::muted);
        g.drawText ((d > 0 ? "+" : "") + juce::String ((int) d), 4, (int) yForDb (d) - 12, 30, 12, juce::Justification::left);
    }

    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;

    // Analizador (pre o post EQ), escala fija de -100 a 0 dBFS
    if ((int) proc.apvts.getRawParameterValue (EQ::analyzerId)->load() != 0)
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
            g.setColour (Theme::text.withAlpha (0.10f));
            g.fillPath (sp);
        }
    }

    // Bandas dinámicas: zona sombreada entre la ganancia máxima y la que se aplica ahora.
    for (int b = 0; b < EQ::NumBands; ++b)
    {
        if (! EQ::hasDyn (b)
            || proc.apvts.getRawParameterValue (EQ::dynId (b))->load() < 0.5f
            || proc.apvts.getRawParameterValue (EQ::onId (b))->load() < 0.5f)
            continue;

        const auto s = EQ::readSettings (b, proc.apvts, sr);
        float cm[6], cl[6];
        EQ::fillCoeffs (s, s.gainDb, sr, cm);
        EQ::fillCoeffs (s, proc.getDynamicGainDb (b), sr, cl);
        const EQ::Coeffs maxC (cm[0], cm[1], cm[2], cm[3], cm[4], cm[5]);
        const EQ::Coeffs liveC (cl[0], cl[1], cl[2], cl[3], cl[4], cl[5]);

        auto yAt = [&] (const EQ::Coeffs& c, double f)
        {
            const float db = juce::Decibels::gainToDecibels ((float) c.getMagnitudeForFrequency (f, sr), -60.0f);
            return yForDb (juce::jlimit (-R, R, db));
        };

        const int w = juce::jmax (2, (int) area.getWidth());
        juce::Path shade;
        for (int i = 0; i < w; i += 3)
        {
            const double f = 20.0 * std::pow (1000.0, (double) i / (w - 1));
            if (i == 0) shade.startNewSubPath ((float) i, yAt (maxC, f)); else shade.lineTo ((float) i, yAt (maxC, f));
        }
        for (int i = ((w - 1) / 3) * 3; i >= 0; i -= 3)
            shade.lineTo ((float) i, yAt (liveC, 20.0 * std::pow (1000.0, (double) i / (w - 1))));
        shade.closeSubPath();
        g.setColour (EQ::bandColours[b].withAlpha (0.28f));
        g.fillPath (shade);
    }

    // Respuesta: producto de las bandas estéreo, más las de Mid o Side según el canal.
    EQ::BandFilter bandFilters[EQ::NumBands];
    int where[EQ::NumBands];
    bool anyMidSide = false;
    for (int b = 0; b < EQ::NumBands; ++b)
    {
        bandFilters[b] = EQ::makeBand (b, proc.apvts, sr);
        where[b] = EQ::placement (b, proc.apvts);
        if (where[b] != 0 && proc.apvts.getRawParameterValue (EQ::onId (b))->load() > 0.5f) anyMidSide = true;
    }

    auto drawResponse = [&] (int channelMask, juce::Colour colour)
    {
        // channelMask: 0 = solo estéreo (L/R), 1 = estéreo + Mid, 2 = estéreo + Side
        juce::Path path;
        const int w = juce::jmax (2, (int) area.getWidth());
        for (int i = 0; i < w; ++i)
        {
            const double f = 20.0 * std::pow (1000.0, (double) i / (w - 1));
            double mag = 1.0;
            for (int b = 0; b < EQ::NumBands; ++b)
                if (where[b] == 0 || where[b] == channelMask)
                    mag *= bandFilters[b].magnitude (f, sr);
            const float db = juce::jlimit (-R, R, juce::Decibels::gainToDecibels ((float) mag, -60.0f));
            if (i == 0) path.startNewSubPath ((float) i, yForDb (db)); else path.lineTo ((float) i, yForDb (db));
        }
        g.setColour (colour.withAlpha (0.14f));   // resplandor, como un trazo fosforescente
        g.strokePath (path, juce::PathStrokeType (7.0f));
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (2.0f));
    };

    if (anyMidSide)
    {
        drawResponse (1, Theme::accent);   // Mid
        drawResponse (2, juce::Colour (0xff4fa6a8));   // Side
        g.setFont (11.0f);
        g.setColour (Theme::accent);
        g.drawText ("Mid", 40, 6, 40, 14, juce::Justification::left);
        g.setColour (juce::Colour (0xff4fa6a8));
        g.drawText ("Side", 76, 6, 40, 14, juce::Justification::left);
    }
    else
    {
        drawResponse (0, Theme::accent);
    }

    // Asas de Q de la banda enfocada (solo campanas)
    if (focusBand >= 0 && isBell (focusBand) && proc.apvts.getRawParameterValue (EQ::onId (focusBand))->load() > 0.5f)
    {
        const auto c = EQ::bandColours[focusBand];
        const auto p = nodePos (focusBand), l = handlePos (focusBand, -1), r = handlePos (focusBand, +1);
        g.setColour (c.withAlpha (0.45f));
        g.drawLine (l.x, p.y, r.x, p.y, 1.0f);
        for (auto h : { l, r })
        {
            g.setColour (Theme::background);
            g.fillEllipse (h.x - 5.0f, h.y - 5.0f, 10.0f, 10.0f);
            g.setColour (c);
            g.drawEllipse (h.x - 5.0f, h.y - 5.0f, 10.0f, 10.0f, 1.6f);
        }
    }

    // Puntos de banda
    for (int b = 0; b < EQ::NumBands; ++b)
    {
        const bool on = proc.apvts.getRawParameterValue (EQ::onId (b))->load() > 0.5f;
        const auto p = nodePos (b);
        const float r = (b == hovered || b == dragged) ? 8.0f : 6.0f;
        g.setColour (EQ::bandColours[b].withAlpha (on ? 1.0f : 0.5f));
        if (on) g.fillEllipse (p.x - r, p.y - r, 2 * r, 2 * r);
        else    g.drawEllipse (p.x - r, p.y - r, 2 * r, 2 * r, 2.0f);

        if (EQ::hasDyn (b) && on && proc.apvts.getRawParameterValue (EQ::dynId (b))->load() > 0.5f)   // anillo = banda dinámica
        {
            g.drawEllipse (p.x - r - 4, p.y - r - 4, 2 * r + 8, 2 * r + 8, 1.2f);

            // Punto blanco = ganancia que se está aplicando ahora mismo (entre 0 dB y el máximo que marca el punto de color).
            const float liveY = juce::jlimit (0.0f, (float) getHeight(), yForDb (proc.getDynamicGainDb (b)));
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawLine (p.x, p.y, p.x, liveY, 1.5f);
            g.setColour (juce::Colours::white);
            g.fillEllipse (p.x - 3.5f, liveY - 3.5f, 7.0f, 7.0f);
        }
    }
}

//==============================================================================
void ResponseCurve::setParam (const juce::String& id, float v)
{
    if (auto* p = param (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

void ResponseCurve::gesture (int b, bool begin)
{
    juce::StringArray ids { EQ::freqId (b) };
    if (! EQ::isCut (b)) ids.add (EQ::gainId (b));
    for (auto& id : ids)
        if (auto* p = param (id))
        {
            if (begin) p->beginChangeGesture(); else p->endChangeGesture();
        }
}

void ResponseCurve::qGesture (int b, bool begin)
{
    if (auto* p = param (EQ::qId (b)))
    {
        if (begin) p->beginChangeGesture(); else p->endChangeGesture();
    }
}

void ResponseCurve::mouseMove (const juce::MouseEvent& e)
{
    if (handleAt (e.position) != 0)
    {
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        return;
    }

    const int h = nodeAt (e.position);
    if (h != hovered)
    {
        hovered = h;
        if (h >= 0) focusBand = h;
        repaint();
    }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void ResponseCurve::mouseExit (const juce::MouseEvent&) { hovered = -1; repaint(); }

void ResponseCurve::mouseDown (const juce::MouseEvent& e)
{
    dragHandle = handleAt (e.position);
    if (dragHandle != 0)
    {
        qGesture (focusBand, true);
        return;
    }

    dragged = nodeAt (e.position);
    if (dragged >= 0)
    {
        focusBand = dragged;
        gesture (dragged, true);
    }
}

void ResponseCurve::mouseDrag (const juce::MouseEvent& e)
{
    if (dragHandle != 0)
    {
        // El ancho que marca el ratón fija la Q efectiva; se descuenta el factor del estilo para obtener la Q del parámetro.
        const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
        const auto s = EQ::readSettings (focusBand, proc.apvts, sr);
        const float oct = juce::jlimit (0.05f, 6.0f, 2.0f * std::abs (std::log2 (freqForX (e.position.x) / s.freq)));
        const float k = EQ::styleQ (s.style, EQ::Peak, 1.0f, s.gainDb);
        setParam (EQ::qId (focusBand), juce::jlimit (0.1f, 10.0f, qForOctaves (oct) / k));
        return;
    }

    if (dragged < 0) return;
    setParam (EQ::freqId (dragged), juce::jlimit (20.0f, 20000.0f, freqForX (e.position.x)));
    if (! EQ::isCut (dragged))
        setParam (EQ::gainId (dragged), juce::jlimit (-18.0f, 18.0f, dbForY (e.position.y)));
}

void ResponseCurve::mouseUp (const juce::MouseEvent&)
{
    if (dragHandle != 0 && focusBand >= 0) qGesture (focusBand, false);
    if (dragged >= 0) gesture (dragged, false);
    dragged = -1;
    dragHandle = 0;
}

void ResponseCurve::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (handleAt (e.position) != 0) return;
    const int b = nodeAt (e.position);
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
    const int b = nodeAt (e.position);
    if (b < 0 || EQ::isCut (b)) return;   // los pasos alto/bajo no tienen Q: su pendiente se elige abajo
    if (auto* p = param (EQ::qId (b)))
    {
        const float q = proc.apvts.getRawParameterValue (EQ::qId (b))->load() * std::exp (wheel.deltaY);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (0.1f, 10.0f, q)));
        p->endChangeGesture();
    }
}

//==============================================================================
void DynMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::screen);
    g.fillRoundedRectangle (r, 3.0f);

    const bool on = proc.apvts.getRawParameterValue (EQ::dynId (band))->load() > 0.5f
                    && proc.apvts.getRawParameterValue (EQ::onId (band))->load() > 0.5f;
    const float maxDb = proc.apvts.getRawParameterValue (EQ::gainId (band))->load();
    const float liveDb = proc.getDynamicGainDb (band);

    if (on && std::abs (maxDb) > 0.05f)
    {
        const float frac = juce::jlimit (0.0f, 1.0f, std::abs (liveDb) / std::abs (maxDb));
        g.setColour (EQ::bandColours[band].withAlpha (0.75f));
        g.fillRoundedRectangle (r.withWidth (r.getWidth() * frac), 3.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (on ? 1.0f : 0.4f));
    g.setFont (11.0f);
    g.drawText (on ? juce::String (liveDb, 1) + " / " + juce::String (maxDb, 1) + " dB" : juce::String ("apagada"),
                getLocalBounds(), juce::Justification::centred);
}

//==============================================================================
LevelMeter::LevelMeter (MedidoresEQAudioProcessor& p, bool isInput, const juce::String& title)
    : proc (p), input (isInput), caption (title)
{
    setTooltip (EQ::utf8 ("Pico por canal. Clic: borrar los picos."));
    startTimerHz (30);
}

void LevelMeter::timerCallback()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        const float peak = input ? proc.takeInputPeak (ch) : proc.takeOutputPeak (ch);
        const float db = juce::Decibels::gainToDecibels (peak, -100.0f);
        level[ch] = juce::jmax (db, level[ch] - 1.5f);   // caída ~45 dB/s
        held = juce::jmax (held, db);

        if (db >= hold[ch]) { hold[ch] = db; holdFrames[ch] = 0; }
        else if (++holdFrames[ch] > 45) hold[ch] = juce::jmax (-100.0f, hold[ch] - 0.8f);   // retiene ~1,5 s y cae
    }
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (Theme::screen);
    g.fillRoundedRectangle (area, 6.0f);

    g.setColour (Theme::text);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (caption, getLocalBounds().removeFromTop (20), juce::Justification::centred);

    auto readout = getLocalBounds().removeFromBottom (20);
    g.setColour (held > -0.1f ? juce::Colours::red : Theme::text);
    g.drawText (held > -99.0f ? juce::String (held, 1) : "--", readout, juce::Justification::centred);

    auto body = getLocalBounds().withTrimmedTop (22).withTrimmedBottom (22).reduced (4, 0).toFloat();
    auto scale = body.removeFromLeft (24.0f);
    const float barW = (body.getWidth() - 4.0f) / 2.0f;
    const float minDb = -60.0f, maxDb = 6.0f;
    auto yFor = [&] (float db) { return body.getBottom() - body.getHeight() * (juce::jlimit (minDb, maxDb, db) - minDb) / (maxDb - minDb); };

    // Barras segmentadas, como un medidor de LEDs de un equipo analógico
    const float segH = 3.0f, segGap = 1.5f;
    const int numSeg = juce::jmax (1, (int) (body.getHeight() / (segH + segGap)));
    for (int ch = 0; ch < 2; ++ch)
    {
        const float bx = body.getX() + (float) ch * (barW + 4.0f);
        for (int s = 0; s < numSeg; ++s)
        {
            const float db = minDb + (maxDb - minDb) * ((float) s + 0.5f) / (float) numSeg;
            const float sy = body.getBottom() - (float) (s + 1) * (segH + segGap) + segGap;
            const bool lit = db <= level[ch];
            const auto base = db > 0.0f ? juce::Colour (0xffe5533d) : (db > -6.0f ? juce::Colour (0xfff0a640) : juce::Colour (0xff9db55e));
            g.setColour (lit ? base : Theme::text.withAlpha (0.06f));
            g.fillRect (bx, sy, barW, segH);
        }

        if (hold[ch] > minDb)   // retención de pico
        {
            g.setColour (Theme::text);
            g.fillRect (bx, yFor (hold[ch]) - 1.0f, barW, 2.0f);
        }
    }

    // Escala en dB
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    for (float db : { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f })
    {
        const float y = yFor (db);
        g.setColour (Theme::text.withAlpha (db == 0.0f ? 0.4f : 0.10f));
        g.drawHorizontalLine ((int) y, body.getX(), body.getRight());
        g.setColour (Theme::muted);
        g.drawText (juce::String ((int) db), scale.withY (y - 6.0f).withHeight (12.0f), juce::Justification::centredRight);
    }
}

//==============================================================================
MedidoresEQAudioProcessorEditor::MedidoresEQAudioProcessorEditor (MedidoresEQAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), presets (p.apvts), curve (p),
      inMeter (p, true, "Entrada"), outMeter (p, false, "Salida")
{
    setLookAndFeel (&laf);

    addAndMakeVisible (presetBox);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (deleteButton);
    presetBox.setTooltip (EQ::utf8 ("Presets de fábrica y de usuario."));
    saveButton.setTooltip (EQ::utf8 ("Guarda los ajustes actuales como preset de usuario."));
    presetBox.onChange = [this] { presetChosen(); };
    saveButton.onClick = [this] { askPresetName(); };
    deleteButton.onClick = [this] { askDeletePreset(); };
    refreshPresets();

    // Ajustes de la vista
    addCombo (analyzerBox, analyzerAttachment, EQ::analyzerId, EQ::analyzerNames());
    addCombo (speedBox, speedAttachment, EQ::analyzerSpeedId, EQ::speedNames());
    addCombo (rangeBox, rangeAttachment, EQ::rangeId, EQ::rangeNames());
    analyzerLabel.setText (juce::String ("Analizador").toUpperCase(), juce::dontSendNotification);
    speedLabel.setText (juce::String ("Velocidad").toUpperCase(), juce::dontSendNotification);
    rangeLabel.setText (juce::String ("Rango").toUpperCase(), juce::dontSendNotification);
    for (auto* l : { &analyzerLabel, &speedLabel, &rangeLabel })
    {
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (l);
    }
    analyzerBox.setTooltip (EQ::utf8 ("Espectro de la señal: apagado, después del EQ (post) o antes (pre)."));
    speedBox.setTooltip (EQ::utf8 ("Rapidez con la que se actualiza el espectro."));
    rangeBox.setTooltip (EQ::utf8 ("Rango vertical de la curva (solo cambia lo que se ve)."));

    addAndMakeVisible (curve);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    const auto doubleClick = EQ::utf8 (" Doble clic: valor por defecto.");
    for (int b = 0; b < EQ::NumBands; ++b)
    {
        const auto colour = EQ::bandColours[b];

        toggles[b].setButtonText (EQ::bands[b].name);
        toggles[b].setColour (juce::ToggleButton::tickColourId, colour);
        toggles[b].setTooltip (EQ::utf8 ("Activa o desactiva la banda."));
        toggleAttachments[b] = std::make_unique<ButtonAttachment> (proc.apvts, EQ::onId (b), toggles[b]);
        addAndMakeVisible (toggles[b]);

        addKnob (knobs[b][0], EQ::freqId (b), "Frec", 70, colour, EQ::utf8 ("Frecuencia de la banda."));
        if (EQ::isCut (b))
        {
            addCombo (slopeBox[b], slopeAttachments[b], EQ::slopeId (b), EQ::slopeNames());
            slopeBox[b].setTooltip (EQ::utf8 ("Pendiente del filtro: más dB por octava, corte más brusco."));
        }
        else
        {
            addKnob (knobs[b][1], EQ::gainId (b), "Gan", 70, colour, EQ::utf8 ("Ganancia de la banda. Con la dinámica activada es el máximo."));
            addKnob (knobs[b][2], EQ::qId (b), "Q", 70, colour, EQ::utf8 ("Ancho de la banda: más Q, más estrecha."));
        }
        if (EQ::hasType (b))
        {
            typeButton[b].setButtonText ("Campana");
            typeButton[b].setClickingTogglesState (true);
            typeButton[b].setColour (juce::TextButton::buttonOnColourId, colour);
            typeButton[b].setTooltip (EQ::utf8 ("Convierte el shelf en una campana."));
            typeAttachments[b] = std::make_unique<ButtonAttachment> (proc.apvts, EQ::typeId (b), typeButton[b]);
            addAndMakeVisible (typeButton[b]);
        }
        addCombo (placementBox[b], placementAttachments[b], EQ::chId (b), EQ::placementNames());
        placementBox[b].setTooltip (EQ::utf8 ("Dónde actúa la banda: en el estéreo completo, solo en el Mid o solo en el Side."));

        if (EQ::hasDyn (b))
        {
            dynToggle[b].setButtonText (EQ::utf8 ("Dinámica"));
            dynToggle[b].setColour (juce::ToggleButton::tickColourId, colour);
            dynToggle[b].setTooltip (EQ::utf8 ("La ganancia solo se aplica cuando el nivel en esta banda supera el umbral."));
            dynAttachments[b] = std::make_unique<ButtonAttachment> (proc.apvts, EQ::dynId (b), dynToggle[b]);
            addAndMakeVisible (dynToggle[b]);
            dynMeter[b] = std::make_unique<DynMeter> (proc, b);
            addAndMakeVisible (*dynMeter[b]);
            addKnob (thrKnob[b], EQ::thrId (b), "Umbral", 52, colour, EQ::utf8 ("Nivel en la banda a partir del cual actúa la dinámica."));
            addKnob (ratioKnob[b], EQ::ratioId (b), "Ratio", 52, colour, EQ::utf8 ("Cuánto responde la dinámica al superar el umbral."));
            addKnob (attackKnob[b], EQ::attackId (b), "Ataque", 52, colour, EQ::utf8 ("Rapidez con la que la dinámica empieza a actuar."));
            addKnob (releaseKnob[b], EQ::releaseId (b), "Release", 52, colour, EQ::utf8 ("Rapidez con la que la banda vuelve a su ganancia normal."));
        }
    }
    (void) doubleClick;

    addKnob (inKnob, EQ::inId, "Entrada", 70, Theme::accent, EQ::utf8 ("Ganancia de entrada, antes del EQ."));
    addKnob (outKnob, EQ::outId, "Salida", 70, Theme::accent, EQ::utf8 ("Ganancia de salida, después de la saturación."));
    addKnob (driveKnob, EQ::driveId, "Drive", 70, juce::Colour (0xffe8a23c), EQ::utf8 ("Cantidad de saturación (0 % = limpio)."));
    addCombo (characterBox, characterAttachment, EQ::characterId, EQ::characterNames());
    addCombo (styleBox, styleAttachment, EQ::styleId, EQ::styleNames());
    characterBox.setTooltip (EQ::utf8 ("Tipo de saturación: limpio, cinta o válvula."));
    styleBox.setTooltip (EQ::utf8 ("Cómo cambia la Q de las campanas con la ganancia."));
    characterLabel.setText (EQ::utf8 ("CAR\u00c1CTER"), juce::dontSendNotification);   // las tildes no pasan por toUpperCase()
    styleLabel.setText (juce::String ("Estilo de curva").toUpperCase(), juce::dontSendNotification);
    for (auto* l : { &characterLabel, &styleLabel })
    {
        l->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    }

    // Los ajustes de la dinámica (umbral, ratio, ataque, release) se despliegan: la ventana crece al abrirlos.
    dynOpen = (bool) proc.apvts.state.getProperty ("dynOpen", false);
    dynExpandButton.setClickingTogglesState (false);
    dynExpandButton.setTooltip (EQ::utf8 ("Muestra u oculta los ajustes de la din\u00e1mica de cada banda. La ventana se agranda al desplegarlos."));
    dynExpandButton.onClick = [this] { setDynamicsOpen (! dynOpen); };
    addAndMakeVisible (dynExpandButton);
    setDynamicsOpen (dynOpen);
}

int MedidoresEQAudioProcessorEditor::windowHeight (bool dynamicsOpen)
{
    return dynamicsOpen ? 840 : 702;   // 190 px de ajustes de dinámica frente a 52 px de la fila compacta
}

void MedidoresEQAudioProcessorEditor::setDynamicsOpen (bool open)
{
    dynOpen = open;
    proc.apvts.state.setProperty ("dynOpen", open, nullptr);
    dynExpandButton.setButtonText (open ? EQ::utf8 ("\u25be  Ocultar ajustes de din\u00e1mica") : EQ::utf8 ("\u25b8  Ajustes de din\u00e1mica"));

    for (int b = 0; b < EQ::NumBands; ++b)
        if (EQ::hasDyn (b))
            for (auto* k : { &thrKnob[b], &ratioKnob[b], &attackKnob[b], &releaseKnob[b] })
            {
                k->slider.setVisible (open);
                k->label.setVisible (open);
            }

    setSize (980, windowHeight (open));
}

MedidoresEQAudioProcessorEditor::~MedidoresEQAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void MedidoresEQAudioProcessorEditor::addKnob (Knob& k, const juce::String& id, const juce::String& text, int textBoxWidth,
                                               juce::Colour colour, const juce::String& tip)
{
    // El texto del valor (unidades y decimales) lo da el propio parámetro.
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, textBoxWidth, 18);
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    // Caja del valor discreta (sin el borde blanco por defecto).
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, Theme::text);
    k.slider.setTooltip (tip + EQ::utf8 (" Doble clic: valor por defecto."));
    k.label.setText (text.toUpperCase(), juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.attachment = std::make_unique<SliderAttachment> (proc.apvts, id, k.slider);
    if (auto* p = proc.apvts.getParameter (id))
        k.slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
}

void MedidoresEQAudioProcessorEditor::addCombo (juce::ComboBox& box, std::unique_ptr<ComboAttachment>& att,
                                                const juce::String& id, const juce::StringArray& items)
{
    box.addItemList (items, 1);
    att = std::make_unique<ComboAttachment> (proc.apvts, id, box);
    addAndMakeVisible (box);
}

//==============================================================================
// Desplegable de presets: los ids van de 1 en adelante, primero los de fábrica y luego los de usuario.
void MedidoresEQAudioProcessorEditor::refreshPresets (const juce::String& select)
{
    factoryNames = presets.factoryNames();
    userNames = presets.userNames();

    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading (EQ::utf8 ("Fábrica"));
    for (int i = 0; i < factoryNames.size(); ++i) presetBox.addItem (factoryNames[i], 1 + i);
    if (userNames.size() > 0)
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("Usuario");
        for (int i = 0; i < userNames.size(); ++i) presetBox.addItem (userNames[i], 1001 + i);
    }
    presetBox.setTextWhenNothingSelected (EQ::utf8 ("Presets…"));

    const int idx = userNames.indexOf (select);
    if (idx >= 0) presetBox.setSelectedId (1001 + idx, juce::dontSendNotification);
    deleteButton.setEnabled (idx >= 0);
}

void MedidoresEQAudioProcessorEditor::presetChosen()
{
    const int id = presetBox.getSelectedId();
    if (id >= 1001) presets.loadUser (userNames[id - 1001]);
    else if (id >= 1) presets.loadFactory (factoryNames[id - 1]);
    deleteButton.setEnabled (id >= 1001);
}

void MedidoresEQAudioProcessorEditor::askPresetName()
{
    auto* w = new juce::AlertWindow ("Guardar preset", "Nombre del preset:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", "", "");
    w->addButton ("Guardar", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancelar", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<MedidoresEQAudioProcessorEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (result != 1 || safe == nullptr) return;
        const auto name = w->getTextEditorContents ("name").trim();
        if (safe->presets.saveUser (name))
            safe->refreshPresets (name);
    }), true);
}

void MedidoresEQAudioProcessorEditor::askDeletePreset()
{
    const int id = presetBox.getSelectedId();
    if (id < 1001) return;
    const auto name = userNames[id - 1001];

    juce::Component::SafePointer<MedidoresEQAudioProcessorEditor> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Borrar preset",
                                        EQ::utf8 ("¿Borrar el preset \"") + name + "\"?", "Borrar", "Cancelar", this,
                                        juce::ModalCallbackFunction::create ([safe, name] (int result)
    {
        if (result != 1 || safe == nullptr) return;
        safe->presets.removeUser (name);
        safe->refreshPresets();
    }));
}

//==============================================================================
void MedidoresEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (Theme::backgroundTop, 0.0f, 0.0f, Theme::background, 0.0f, (float) getHeight(), false));
    g.fillAll();

    auto drawPanel = [&g] (juce::Rectangle<float> r, juce::Colour strip)
    {
        g.setGradientFill (juce::ColourGradient (Theme::panelTop, r.getX(), r.getY(), Theme::panel, r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (juce::Colour (0xff0d0b08));
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));   // brillo del canto superior
        g.drawLine (r.getX() + 8.0f, r.getY() + 1.5f, r.getRight() - 8.0f, r.getY() + 1.5f, 1.0f);
        g.setColour (strip);
        g.fillRoundedRectangle (r.getX() + 14.0f, r.getY() + 5.0f, r.getWidth() - 28.0f, 3.0f, 1.5f);
    };

    for (int b = 0; b < EQ::NumBands; ++b)
        drawPanel (bandPanel[b].toFloat(), EQ::bandColours[b]);

    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    for (auto* panel : { &gainPanel, &characterPanel })
    {
        drawPanel (panel->toFloat(), Theme::accent);
        g.setColour (Theme::text);
        g.drawText (panel == &gainPanel ? EQ::utf8 ("GANANCIA") : EQ::utf8 ("SATURACI\u00d3N"),
                    panel->getX(), panelTitleY + 4, panel->getWidth(), 22, juce::Justification::centred);
    }
}

void MedidoresEQAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (30);
    presetBox.setBounds (top.removeFromLeft (240));
    top.removeFromLeft (8);
    saveButton.setBounds (top.removeFromLeft (80));
    top.removeFromLeft (6);
    deleteButton.setBounds (top.removeFromLeft (80));

    // Ajustes de la vista, a la derecha
    rangeBox.setBounds (top.removeFromRight (84));
    rangeLabel.setBounds (top.removeFromRight (50));
    top.removeFromRight (8);
    speedBox.setBounds (top.removeFromRight (84));
    speedLabel.setBounds (top.removeFromRight (66));
    top.removeFromRight (8);
    analyzerBox.setBounds (top.removeFromRight (90));
    analyzerLabel.setBounds (top.removeFromRight (72));
    area.removeFromTop (8);

    auto curveRow = area.removeFromTop (230);
    outMeter.setBounds (curveRow.removeFromRight (76));
    curveRow.removeFromRight (6);
    inMeter.setBounds (curveRow.removeFromRight (76));
    curveRow.removeFromRight (6);
    curve.setBounds (curveRow);
    area.removeFromTop (8);

    const int colW = area.getWidth() / (EQ::NumBands + 2);   // bandas + columna de ganancias + columna de saturación
    auto toggleRow = area.removeFromTop (26);
    for (int b = 0; b < EQ::NumBands; ++b)
        toggles[b].setBounds (toggleRow.getX() + b * colW + 8, toggleRow.getY() + 2, colW - 12, toggleRow.getHeight());

    auto comboRow = area.removeFromBottom (58);
    for (int b = 0; b < EQ::NumBands; ++b)
    {
        placementBox[b].setBounds (comboRow.getX() + b * colW + 10, comboRow.getY() + 32, colW - 20, 24);
        if (EQ::hasType (b))
            typeButton[b].setBounds (comboRow.getX() + b * colW + 10, comboRow.getY() + 4, colW - 20, 24);
    }

    auto dynRow = area.removeFromBottom (dynOpen ? 190 : 52);   // botón Dinámica + medidor (+ 4 knobs en 2x2 al desplegar)
    const int rowH = area.getHeight() / 3;
    auto place = [] (Knob& k, juce::Rectangle<int> r)
    {
        k.label.setBounds (r.removeFromTop (16));
        k.slider.setBounds (r);
    };

    for (int b = 0; b < EQ::NumBands; ++b)
    {
        place (knobs[b][0], { area.getX() + b * colW, area.getY(), colW, rowH });
        if (EQ::isCut (b))
            slopeBox[b].setBounds (area.getX() + b * colW + 10, area.getY() + rowH + rowH / 2 - 12, colW - 20, 24);
        else
        {
            place (knobs[b][1], { area.getX() + b * colW, area.getY() + rowH, colW, rowH });
            place (knobs[b][2], { area.getX() + b * colW, area.getY() + 2 * rowH, colW, rowH });

            const int x = dynRow.getX() + b * colW;
            dynToggle[b].setBounds (x + 8, dynRow.getY() + 2, colW - 12, 24);
            dynMeter[b]->setBounds (x + 10, dynRow.getY() + 28, colW - 20, 16);
            place (thrKnob[b],     { x,            dynRow.getY() + 48,  colW / 2, 70 });
            place (ratioKnob[b],   { x + colW / 2, dynRow.getY() + 48,  colW / 2, 70 });
            place (attackKnob[b],  { x,            dynRow.getY() + 120, colW / 2, 70 });
            place (releaseKnob[b], { x + colW / 2, dynRow.getY() + 120, colW / 2, 70 });
        }
    }

    const int gainCol = area.getX() + EQ::NumBands * colW;
    const int characterCol = gainCol + colW;
    dynExpandButton.setBounds (gainCol + 10, dynRow.getY() + 4, 2 * colW - 20, 30);
    place (inKnob,    { gainCol, area.getY(), colW, rowH });
    place (outKnob,   { gainCol, area.getY() + rowH, colW, rowH });
    place (driveKnob, { characterCol, area.getY(), colW, rowH });

    characterLabel.setBounds (characterCol, area.getY() + rowH, colW, 16);
    characterBox.setBounds (characterCol + 10, area.getY() + rowH + 18, colW - 20, 24);
    styleLabel.setBounds (characterCol, area.getY() + rowH + 46, colW, 16);
    styleBox.setBounds (characterCol + 10, area.getY() + rowH + 64, colW - 20, 24);

    // Paneles de fondo: de la fila de interruptores al último desplegable
    const int panelTop = toggleRow.getY() - 3, panelBottom = comboRow.getBottom() + 3;
    for (int b = 0; b < EQ::NumBands; ++b)
        bandPanel[b] = { area.getX() + b * colW + 2, panelTop, colW - 4, panelBottom - panelTop };
    gainPanel      = { gainCol + 2, panelTop, colW - 4, panelBottom - panelTop };
    characterPanel = { characterCol + 2, panelTop, colW - 4, panelBottom - panelTop };
    panelTitleY = toggleRow.getY() + 2;
}
