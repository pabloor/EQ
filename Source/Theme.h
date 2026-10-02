#pragma once
#include <juce_graphics/juce_graphics.h>
#include <array>

// Paleta de un tema visual. El plugin es una "placa frontal" de hardware: chapa con grano, serigrafía en tinta clara,
// capuchones de colores en los knobs y un visor oscuro para la curva.
struct Palette
{
    juce::String name;
    juce::Colour plateTop, plateBottom;   // chapa (degradado vertical)
    float grain;                          // intensidad del grano de la chapa
    juce::Colour ear;                     // orejas de rack y metal oscuro
    juce::Colour ink, inkMuted, line;     // serigrafía: texto, texto secundario y recuadros
    juce::Colour screen, trace;           // visor de la curva y su trazo
    juce::Colour inset, insetText;        // ventanas empotradas con valores
    juce::Colour control;                 // pulsadores y desplegables
    juce::Colour vuFace, vuInk, vuRed;    // medidores VU
    juce::Colour lampOff;                 // lámpara apagada
    juce::Colour accent;                  // resaltado (latón, rojo, ámbar...)
    std::array<juce::Colour, 6> band;     // capuchones de las 6 bandas
};

namespace Themes
{
    constexpr int count = 3;

    inline const std::array<Palette, count>& all()
    {
        static const std::array<Palette, count> palettes = {{
            // Oliva y latón: estilo Pultec
            { "Oliva",
              juce::Colour (0xff67714f), juce::Colour (0xff4f573d), 0.07f,
              juce::Colour (0xff3a412d),
              juce::Colour (0xffefe6c6), juce::Colour (0xffc9c19f), juce::Colour (0xffefe6c6),
              juce::Colour (0xff11130c), juce::Colour (0xffffc65c),
              juce::Colour (0xff1b1e14), juce::Colour (0xfff0c15a),
              juce::Colour (0xff434b35),
              juce::Colour (0xfff1e4b5), juce::Colour (0xff3a2f1c), juce::Colour (0xffb5392b),
              juce::Colour (0xff2a2e1f),
              juce::Colour (0xffd9b04a),
              {{ juce::Colour (0xffe8694a), juce::Colour (0xffd9b04a), juce::Colour (0xfff0e6c4),
                 juce::Colour (0xff7fc4b2), juce::Colour (0xffc9a0d8), juce::Colour (0xff8fb0e0) }} },

            // Gris azulado y capuchones de colores: estilo Neve
            { "Neve",
              juce::Colour (0xff7d8898), juce::Colour (0xff626d7c), 0.08f,
              juce::Colour (0xff353b45),
              juce::Colour (0xfff4f2eb), juce::Colour (0xffd3d7de), juce::Colour (0xfff4f2eb),
              juce::Colour (0xff0c1014), juce::Colour (0xff9df0b4),
              juce::Colour (0xff171c23), juce::Colour (0xffe6f1ff),
              juce::Colour (0xff4d5766),
              juce::Colour (0xfff5f0e1), juce::Colour (0xff22262b), juce::Colour (0xffc0392b),
              juce::Colour (0xff2a303a),
              juce::Colour (0xffd9473a),
              {{ juce::Colour (0xffc9382b), juce::Colour (0xff3d73b0), juce::Colour (0xffe2e2de),
                 juce::Colour (0xff4f9f6e), juce::Colour (0xffdb8b2b), juce::Colour (0xff97a3b6) }} },

            // Grafito y ámbar: estilo Studer
            { "Grafito",
              juce::Colour (0xff323235), juce::Colour (0xff222225), 0.06f,
              juce::Colour (0xff161619),
              juce::Colour (0xffe9e6dc), juce::Colour (0xffa19d90), juce::Colour (0xffe9e6dc),
              juce::Colour (0xff0a0a0c), juce::Colour (0xfff0a640),
              juce::Colour (0xff111113), juce::Colour (0xfff0a640),
              juce::Colour (0xff3b3b3f),
              juce::Colour (0xfff2dca3), juce::Colour (0xff2b2210), juce::Colour (0xffb53a24),
              juce::Colour (0xff29292c),
              juce::Colour (0xfff0a640),
              {{ juce::Colour (0xffe8694a), juce::Colour (0xfff0a640), juce::Colour (0xffd9d5c5),
                 juce::Colour (0xff5fb3b3), juce::Colour (0xffb58ad0), juce::Colour (0xff6f9bdb) }} },
        }};
        return palettes;
    }

    inline const Palette& get (int index) { return all()[(size_t) juce::jlimit (0, count - 1, index)]; }
}
