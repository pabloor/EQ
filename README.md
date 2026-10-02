# Medidores EQ (plugin Audio Unit / VST3)

Ecualizador de 6 bandas hecho con [JUCE](https://juce.com): paso alto, shelf de graves, dos campanas, shelf de agudos y paso bajo, con ganancia de compensación de salida (±12 dB).

- **Shelves conmutables:** los shelves de graves y agudos pueden pasar a campana desde su desplegable *Tipo*.
- **Pendiente ajustable:** los pasos alto y bajo tienen 6, 12, 24 o 48 dB/octava (Butterworth).
- **Mid/Side por banda:** cada banda actúa sobre el estéreo, solo sobre el Mid o solo sobre el Side. Si hay bandas en Mid/Side, la curva muestra la respuesta de cada uno.
- **Curva y analizador:** curva de respuesta total sobre un analizador de espectro (post-EQ). Los puntos de banda se arrastran (frecuencia y ganancia), la rueda del ratón sobre un punto cambia su Q y el doble clic lo activa o desactiva.
- **Medidores de entrada y salida:** pico por canal, con el máximo en cifras (clic para borrarlo).
- **Carácter analógico:** saturación después del EQ, a elegir entre *Limpio*, *Cinta* (simétrica, armónicos impares, compresión suave) y *Válvula* (asimétrica, añade armónicos pares), con knob de **Drive** (0 % = limpio) y sobremuestreo 2x para evitar aliasing. Con Drive a 0 o en *Limpio* el sobremuestreo se salta y no añade latencia; con saturación activa añade ~1 muestra de latencia, que no se reporta al host. Además, **Q proporcional**: las campanas se ensanchan al subir la ganancia y se estrechan al bajarla (la Q casi se duplica o se reduce a la mitad a ±12 dB), al estilo de algunos EQ analógicos.
- **Presets:** de fábrica y de usuario (se guardan en `~/Library/Application Support/Medidores EQ/Presets`).

Genera tres formatos: **Audio Unit** (Logic, GarageBand, Ableton, etc.), **VST3** y una app **Standalone** para probar sin DAW.

## Compilar (macOS)

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

JUCE se descarga solo la primera vez. Los resultados quedan en `build/MedidoresEQ_artefacts/Release/` (`AU/Medidores EQ.component`, `VST3/Medidores EQ.vst3`, `Standalone/Medidores EQ.app`).

Para instalar el AU: copia `Medidores EQ.component` a `~/Library/Audio/Plug-Ins/Components/` y, si el DAW no lo ve, ejecuta `killall -9 AudioComponentRegistrar`. Prueba la validación con `auval -v aufx Meq1 Polv`.

El workflow `.github/workflows/build-plugin.yml` se lanza solo a mano (Actions → Run workflow), porque los minutos de macOS cuentan x10 en repos privados. Por defecto compila solo para Apple Silicon (marca «universal» para añadir Intel), usa ccache y deja los ZIP de AU y VST3 como artefacto. Sin firmar: la primera vez hay que quitar la cuarentena con `xattr -dr com.apple.quarantine "Medidores EQ.component"`.

## Estado

Compila y pasa `auval` y `pluginval` en CI (ver el workflow). Pendiente de probar a oído en un DAW.
