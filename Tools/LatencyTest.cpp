// Mide la latencia real del plugin con un impulso y la compara con la que informa al host.
// Con el EQ plano, la latencia debe ser la misma con la saturación apagada, con cinta, con válvula y con mezclas en paralelo.
#include "PluginProcessor.h"

static void setParam (MedidoresEQAudioProcessor& p, const juce::String& id, float value)
{
    if (auto* prm = p.apvts.getParameter (id))
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

struct Case { const char* name; float character, drive, mix; };

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const double sampleRate = 48000.0;
    const int blockSize = 512, impulseAt = 100;

    const Case cases[] = {
        { "limpio",                 0.0f,   0.0f, 100.0f },
        { "cinta, drive 0",         1.0f,   0.0f, 100.0f },
        { "cinta, drive 60",        1.0f,  60.0f, 100.0f },
        { "valvula, drive 60",      2.0f,  60.0f, 100.0f },
        { "cinta, mezcla 50",       1.0f,  60.0f,  50.0f },
        { "valvula, mezcla 30",     2.0f,  60.0f,  30.0f },
        { "valvula, mezcla 0",      2.0f,  60.0f,   0.0f },
    };

    int failures = 0, reported = -1;
    for (const auto& c : cases)
    {
        MedidoresEQAudioProcessor proc;
        proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
        proc.prepareToPlay (sampleRate, blockSize);

        // EQ plano: sin pasos alto/bajo (las demás bandas con 0 dB no hacen nada)
        setParam (proc, "hp_on", 0.0f);
        setParam (proc, "lp_on", 0.0f);
        setParam (proc, "character", c.character);
        setParam (proc, "drive", c.drive);
        setParam (proc, "mix", c.mix);

        reported = proc.getLatencySamples();

        std::vector<float> out;
        for (int block = 0; block < 4; ++block)
        {
            juce::AudioBuffer<float> buffer (2, blockSize);
            buffer.clear();
            if (block == 0)
                for (int ch = 0; ch < 2; ++ch) buffer.setSample (ch, impulseAt, 0.1f);

            juce::MidiBuffer midi;
            proc.processBlock (buffer, midi);
            for (int i = 0; i < blockSize; ++i) out.push_back (buffer.getSample (0, i));
        }

        int peak = 0;
        for (int i = 0; i < (int) out.size(); ++i)
            if (std::abs (out[(size_t) i]) > std::abs (out[(size_t) peak])) peak = i;
        const int measured = peak - impulseAt;

        // Sin saturación la señal solo se retrasa: la medida tiene que coincidir exactamente. Con saturación, el pico de la
        // respuesta al impulso del sobremuestreo puede caer a +-1 muestra del retardo de grupo.
        const bool dryOnly = c.character == 0.0f || c.drive == 0.0f || c.mix == 0.0f;
        const int tolerance = dryOnly ? 0 : 1;
        const bool ok = std::abs (measured - reported) <= tolerance;
        std::printf ("%-22s latencia informada = %d, medida = %d  %s\n", c.name, reported, measured, ok ? "OK" : "FALLO");
        if (! ok) ++failures;
    }

    std::printf ("%s (latencia informada: %d muestras = %.3f ms a 48 kHz)\n",
                 failures == 0 ? "Latencia coherente en todos los casos" : "HAY DESAJUSTES DE LATENCIA",
                 reported, 1000.0 * reported / sampleRate);
    return failures == 0 ? 0 : 1;
}
