# Medidores EQ (plugin Audio Unit / VST3)

Ecualizador de 5 bandas hecho con [JUCE](https://juce.com): paso alto, shelf de graves, dos campanas, shelf de agudos y ganancia de salida. Cada banda tiene frecuencia, ganancia, Q y bypass.

La ventana muestra la curva de respuesta total sobre un analizador de espectro (post-EQ). Los puntos de banda se arrastran (frecuencia y ganancia), la rueda del ratón sobre un punto cambia su Q y el doble clic lo activa o desactiva.

Genera tres formatos: **Audio Unit** (Logic, GarageBand, Ableton, etc.), **VST3** y una app **Standalone** para probar sin DAW.

## Compilar (macOS)

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

JUCE se descarga solo la primera vez. Los resultados quedan en `build/MedidoresEQ_artefacts/Release/` (`AU/Medidores EQ.component`, `VST3/Medidores EQ.vst3`, `Standalone/Medidores EQ.app`).

Para instalar el AU: copia `Medidores EQ.component` a `~/Library/Audio/Plug-Ins/Components/` y, si el DAW no lo ve, ejecuta `killall -9 AudioComponentRegistrar`. Prueba la validación con `auval -v aufx Meq1 Polv`.

El workflow `.github/workflows/build-plugin.yml` compila en un runner de macOS y deja los binarios como artefacto. Sin firmar: la primera vez hay que quitar la cuarentena con `xattr -dr com.apple.quarantine "Medidores EQ.component"`.

## Estado

Primera versión, sin probar aún en un DAW.
