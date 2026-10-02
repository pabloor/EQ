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
    const float g = EQ::isCut (b) ? 0.0f : proc.apvts.getRawParameterValue (EQ::gainId (b))->load();
    return { xForFreq (f), yForDb (g) };
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

    // Respuesta: producto de las bandas estéreo, más las de Mid o Side según el canal.
    EQ::BandFilter bands[EQ::NumBands];
    int where[EQ::NumBands];
    bool anyMidSide = false;
    for (int b = 0; b < EQ::NumBands; ++b)
    {
        bands[b] = EQ::makeBand (b, proc.apvts, sr);
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
                    mag *= bands[b].magnitude (f, sr);
            const float db = juce::jlimit (minDb, maxDb, juce::Decibels::gainToDecibels ((float) mag, -60.0f));
            if (i == 0) path.startNewSubPath ((float) i, yForDb (db)); else path.lineTo ((float) i, yForDb (db));
        }
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (2.0f));
    };

    if (anyMidSide)
    {
        drawResponse (1, juce::Colour (0xff4fc3f7));   // Mid
        drawResponse (2, juce::Colour (0xffffb74d));   // Side
        g.setFont (11.0f);
        g.setColour (juce::Colour (0xff4fc3f7));
        g.drawText ("Mid", 8, 6, 40, 14, juce::Justification::left);
        g.setColour (juce::Colour (0xffffb74d));
        g.drawText ("Side", 44, 6, 40, 14, juce::Justification::left);
    }
    else
    {
        drawResponse (0, juce::Colour (0xff4fc3f7));
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
        if (EQ::hasDyn (b) && proc.apvts.getRawParameterValue (EQ::dynId (b))->load() > 0.5f)   // anillo = banda dinámica
            g.drawEllipse (p.x - r - 4, p.y - r - 4, 2 * r + 8, 2 * r + 8, 1.2f);
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

void ResponseCurve::mouseMove (const juce::MouseEvent& e)
{
    const int h = nodeAt (e.position);
    if (h != hovered) { hovered = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void ResponseCurve::mouseExit (const juce::MouseEvent&) { hovered = -1; repaint(); }

void ResponseCurve::mouseDown (const juce::MouseEvent& e)
{
    dragged = nodeAt (e.position);
    if (dragged >= 0) gesture (dragged, true);
}

void ResponseCurve::mouseDrag (const juce::MouseEvent& e)
{
    if (dragged < 0) return;
    setParam (EQ::freqId (dragged), juce::jlimit (20.0f, 20000.0f, freqForX (e.position.x)));
    if (! EQ::isCut (dragged))
        setParam (EQ::gainId (dragged), juce::jlimit (-18.0f, 18.0f, dbForY (e.position.y)));
}

void ResponseCurve::mouseUp (const juce::MouseEvent&)
{
    if (dragged >= 0) gesture (dragged, false);
    dragged = -1;
}

void ResponseCurve::mouseDoubleClick (const juce::MouseEvent& e)
{
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
LevelMeter::LevelMeter (MedidoresEQAudioProcessor& p, bool isInput, const juce::String& title)
    : proc (p), input (isInput), caption (title)
{
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
    }
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff15181d));
    g.fillRoundedRectangle (area, 4.0f);

    g.setColour (juce::Colours::white.withAlpha (0.8f));
    g.setFont (11.0f);
    g.drawText (caption, getLocalBounds().removeFromTop (16), juce::Justification::centred);

    auto readout = getLocalBounds().removeFromBottom (16);
    g.setColour (held > -0.1f ? juce::Colours::red : juce::Colours::white.withAlpha (0.8f));
    g.drawText (held > -99.0f ? juce::String (held, 1) : "--", readout, juce::Justification::centred);

    auto bars = getLocalBounds().reduced (6, 0).withTrimmedTop (20).withTrimmedBottom (20).toFloat();
    const float barW = (bars.getWidth() - 4.0f) / 2.0f;
    const float minDb = -60.0f, maxDb = 6.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = juce::Rectangle<float> (bars.getX() + (float) ch * (barW + 4.0f), bars.getY(), barW, bars.getHeight());
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRect (bar);

        const float frac = juce::jlimit (0.0f, 1.0f, (level[ch] - minDb) / (maxDb - minDb));
        auto fill = bar.withTop (bar.getBottom() - bar.getHeight() * frac);
        juce::ColourGradient grad (juce::Colour (0xffef5350), bar.getX(), bar.getY(),
                                   juce::Colour (0xff66bb6a), bar.getX(), bar.getBottom(), false);
        grad.addColour (0.10, juce::Colour (0xffffee58));    // ~ -0 dB
        grad.addColour (0.25, juce::Colour (0xff66bb6a));
        g.setGradientFill (grad);
        g.fillRect (fill);
    }

    // Marca de 0 dB
    const float y0 = bars.getBottom() - bars.getHeight() * (0.0f - minDb) / (maxDb - minDb);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawHorizontalLine ((int) y0, bars.getX(), bars.getRight());
}

//==============================================================================
MedidoresEQAudioProcessorEditor::MedidoresEQAudioProcessorEditor (MedidoresEQAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), presets (p.apvts), curve (p),
      inMeter (p, true, "Entrada"), outMeter (p, false, "Salida")
{
    addAndMakeVisible (presetBox);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (deleteButton);
    presetBox.onChange = [this] { presetChosen(); };
    saveButton.onClick = [this] { askPresetName(); };
    deleteButton.onClick = [this] { askDeletePreset(); };
    refreshPresets();

    addAndMakeVisible (curve);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    for (int b = 0; b < EQ::NumBands; ++b)
    {
        toggles[b].setButtonText (EQ::bands[b].name);
        toggles[b].setColour (juce::ToggleButton::tickColourId, EQ::bandColours[b]);
        toggleAttachments[b] = std::make_unique<ButtonAttachment> (proc.apvts, EQ::onId (b), toggles[b]);
        addAndMakeVisible (toggles[b]);

        addKnob (knobs[b][0], EQ::freqId (b), "Frec");
        if (EQ::isCut (b))
            addCombo (slopeBox[b], slopeAttachments[b], EQ::slopeId (b), EQ::slopeNames());
        else
        {
            addKnob (knobs[b][1], EQ::gainId (b), "Gan");
            addKnob (knobs[b][2], EQ::qId (b), "Q");
        }
        if (EQ::hasType (b))
        {
            typeButton[b].setButtonText ("Campana");
            typeButton[b].setClickingTogglesState (true);
            typeButton[b].setColour (juce::TextButton::buttonOnColourId, EQ::bandColours[b].darker (0.3f));
            typeAttachments[b] = std::make_unique<ButtonAttachment> (proc.apvts, EQ::typeId (b), typeButton[b]);
            addAndMakeVisible (typeButton[b]);
        }
        addCombo (placementBox[b], placementAttachments[b], EQ::chId (b), EQ::placementNames());

        if (EQ::hasDyn (b))
        {
            dynToggle[b].setButtonText (EQ::utf8 ("Din\u00e1mica"));
            dynToggle[b].setColour (juce::ToggleButton::tickColourId, EQ::bandColours[b]);
            dynAttachments[b] = std::make_unique<ButtonAttachment> (proc.apvts, EQ::dynId (b), dynToggle[b]);
            addAndMakeVisible (dynToggle[b]);
            addKnob (thrKnob[b], EQ::thrId (b), "Umbral", 52);
            addKnob (ratioKnob[b], EQ::ratioId (b), "Ratio", 52);
        }
    }
    addKnob (inKnob, EQ::inId, "Entrada");
    addKnob (outKnob, EQ::outId, "Salida");
    addKnob (driveKnob, EQ::driveId, "Drive");
    addKnob (attackKnob, EQ::attackId, "Ataque", 52);
    addKnob (releaseKnob, EQ::releaseId, "Release", 52);
    addCombo (characterBox, characterAttachment, EQ::characterId, EQ::characterNames());
    addCombo (styleBox, styleAttachment, EQ::styleId, EQ::styleNames());
    characterLabel.setText (EQ::utf8 ("Car\u00e1cter"), juce::dontSendNotification);
    styleLabel.setText ("Estilo de curva", juce::dontSendNotification);
    for (auto* l : { &characterLabel, &styleLabel })
    {
        l->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    }

    setSize (980, 770);
}

void MedidoresEQAudioProcessorEditor::addKnob (Knob& k, const juce::String& id, const juce::String& text, int textBoxWidth)
{
    // El texto del valor (unidades y decimales) lo da el propio parámetro.
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, textBoxWidth, 18);
    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.attachment = std::make_unique<SliderAttachment> (proc.apvts, id, k.slider);
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
    presetBox.addSectionHeading (EQ::utf8 ("F\u00e1brica"));
    for (int i = 0; i < factoryNames.size(); ++i) presetBox.addItem (factoryNames[i], 1 + i);
    if (userNames.size() > 0)
    {
        presetBox.addSeparator();
        presetBox.addSectionHeading ("Usuario");
        for (int i = 0; i < userNames.size(); ++i) presetBox.addItem (userNames[i], 1001 + i);
    }
    presetBox.setTextWhenNothingSelected (EQ::utf8 ("Presets\u2026"));

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
                                        EQ::utf8 ("\u00bfBorrar el preset \"") + name + "\"?", "Borrar", "Cancelar", this,
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
    g.fillAll (juce::Colour (0xff22262c));
}

void MedidoresEQAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto top = area.removeFromTop (28);
    presetBox.setBounds (top.removeFromLeft (240));
    top.removeFromLeft (8);
    saveButton.setBounds (top.removeFromLeft (80));
    top.removeFromLeft (6);
    deleteButton.setBounds (top.removeFromLeft (80));
    area.removeFromTop (8);

    auto curveRow = area.removeFromTop (240);
    outMeter.setBounds (curveRow.removeFromRight (56));
    curveRow.removeFromRight (6);
    inMeter.setBounds (curveRow.removeFromRight (56));
    curveRow.removeFromRight (6);
    curve.setBounds (curveRow);
    area.removeFromTop (8);

    const int colW = area.getWidth() / (EQ::NumBands + 2);   // bandas + columna de ganancias + columna de carácter
    auto toggleRow = area.removeFromTop (26);
    for (int b = 0; b < EQ::NumBands; ++b)
        toggles[b].setBounds (toggleRow.getX() + b * colW + 6, toggleRow.getY(), colW - 6, toggleRow.getHeight());

    auto comboRow = area.removeFromBottom (58);
    for (int b = 0; b < EQ::NumBands; ++b)
    {
        placementBox[b].setBounds (comboRow.getX() + b * colW + 8, comboRow.getY() + 32, colW - 16, 24);
        if (EQ::hasType (b))
            typeButton[b].setBounds (comboRow.getX() + b * colW + 8, comboRow.getY() + 4, colW - 16, 24);
    }

    auto dynRow = area.removeFromBottom (112);   // botón Dinámica + knobs de umbral y ratio
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
            slopeBox[b].setBounds (area.getX() + b * colW + 8, area.getY() + rowH + rowH / 2 - 12, colW - 16, 24);
        else
        {
            place (knobs[b][1], { area.getX() + b * colW, area.getY() + rowH, colW, rowH });
            place (knobs[b][2], { area.getX() + b * colW, area.getY() + 2 * rowH, colW, rowH });

            const int x = dynRow.getX() + b * colW;
            dynToggle[b].setBounds (x + 6, dynRow.getY() + 2, colW - 6, 24);
            place (thrKnob[b],   { x, dynRow.getY() + 28, colW / 2, 84 });
            place (ratioKnob[b], { x + colW / 2, dynRow.getY() + 28, colW / 2, 84 });
        }
    }

    const int gainCol = area.getX() + EQ::NumBands * colW;
    const int characterCol = gainCol + colW;
    place (inKnob, { gainCol, area.getY(), colW, rowH });
    place (outKnob, { gainCol, area.getY() + rowH, colW, rowH });
    place (driveKnob, { characterCol, area.getY(), colW, rowH });

    characterLabel.setBounds (characterCol, area.getY() + rowH, colW, 16);
    characterBox.setBounds (characterCol + 8, area.getY() + rowH + 18, colW - 16, 24);
    styleLabel.setBounds (characterCol, area.getY() + rowH + 46, colW, 16);
    styleBox.setBounds (characterCol + 8, area.getY() + rowH + 64, colW - 16, 24);

    // Ataque y release son globales para todas las bandas dinámicas: van en la fila de dinámica.
    place (attackKnob,  { gainCol, dynRow.getY() + 28, colW / 2, 84 });
    place (releaseKnob, { gainCol + colW / 2, dynRow.getY() + 28, colW / 2, 84 });
}
