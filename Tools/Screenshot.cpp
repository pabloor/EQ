// Dibuja el editor del plugin con un estado de ejemplo y lo guarda como PNG (se usa en CI para ver la interfaz).
#include "PluginProcessor.h"
#include "PluginEditor.h"

static void setParam (MedidoresEQAudioProcessor& p, const juce::String& id, float value)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto outFile = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "screenshot.png");

    MedidoresEQAudioProcessor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    // Estado de ejemplo: un ajuste de voz con una banda dinámica (de-esser) y algo de saturación.
    setParam (proc, "hp_freq", 80.0f);   setParam (proc, "hp_slope", 2.0f);
    setParam (proc, "ls_freq", 110.0f);  setParam (proc, "ls_gain", 3.5f);
    setParam (proc, "b1_freq", 320.0f);  setParam (proc, "b1_gain", -4.0f);  setParam (proc, "b1_q", 1.4f);
    setParam (proc, "b2_freq", 3200.0f); setParam (proc, "b2_gain", -8.0f);  setParam (proc, "b2_q", 2.5f);
    setParam (proc, "b2_dyn", 1.0f);     setParam (proc, "b2_thr", -34.0f);  setParam (proc, "b2_ratio", 4.0f);
    setParam (proc, "b2_attack", 3.0f);  setParam (proc, "b2_release", 80.0f);
    setParam (proc, "hs_freq", 9000.0f); setParam (proc, "hs_gain", 3.0f);
    setParam (proc, "lp_freq", 18000.0f);
    setParam (proc, "drive", 30.0f);     setParam (proc, "character", 2.0f);
    setParam (proc, "in_gain", 2.0f);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    auto* e = dynamic_cast<MedidoresEQAudioProcessorEditor*> (editor.get());
    if (e == nullptr) return 3;
    e->getCurve().setFocusBand (EQ::Bell2);   // muestra las asas de Q de la banda dinámica
    e->setDynamicsOpen (true);

    // Audio de ejemplo (ruido filtrado + una banda a 3,2 kHz) para llenar el analizador y los medidores.
    juce::Random random (1234);
    float lp1 = 0.0f, lp2 = 0.0f;
    double phase = 0.0;
    for (int block = 0; block < 60; ++block)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        for (int i = 0; i < 512; ++i)
        {
            const float white = random.nextFloat() * 2.0f - 1.0f;
            lp1 += 0.08f * (white - lp1);
            lp2 += 0.35f * (lp1 - lp2);
            const float tone = 0.05f * std::sin ((float) phase);
            phase += juce::MathConstants<double>::twoPi * 3200.0 / 48000.0;
            const float s = 0.9f * lp2 + tone;
            buffer.setSample (0, i, s);
            buffer.setSample (1, i, 0.9f * s);
        }
        juce::MidiBuffer midi;
        proc.processBlock (buffer, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }

    auto save = [] (juce::Image image, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream stream (file);
        if (! stream.openedOk()) return false;
        juce::PNGImageFormat png;
        return png.writeImageToStream (image, stream);
    };

    // 1) ajustes de dinámica desplegados; 2) ventana compacta
    bool ok = save (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f), outFile);

    e->setDynamicsOpen (false);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (80);
    ok = save (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f),
               outFile.getParentDirectory().getChildFile (outFile.getFileNameWithoutExtension() + "-compacto.png")) && ok;

    return ok ? 0 : 2;
}
