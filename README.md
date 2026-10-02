# Medidores EQ (plugin Audio Unit / VST3)

Ecualizador de 6 bandas hecho con [JUCE](https://juce.com): paso alto, shelf de graves, dos campanas, shelf de agudos y paso bajo, con ganancia de compensación de salida (±12 dB).

- **Entrada y salida:** ganancia de entrada (antes del EQ) y de salida (después de la saturación), de ±12 dB cada una. El medidor de entrada mide ya con la ganancia de entrada aplicada.
- **Shelves conmutables:** los shelves de graves y agudos pueden pasar a campana con el botón *Campana*.
- **Pendiente ajustable:** los pasos alto y bajo tienen 6, 12, 24 o 48 dB/octava (Butterworth).
- **Mid/Side por banda:** cada banda actúa sobre el estéreo, solo sobre el Mid o solo sobre el Side. Si hay bandas en Mid/Side, la curva muestra la respuesta de cada uno.
- **Curva y analizador:** curva de respuesta total sobre un analizador de espectro (post-EQ). Los puntos de banda se arrastran (frecuencia y ganancia), la rueda del ratón sobre un punto cambia su Q y el doble clic lo activa o desactiva.
- **Medidores de entrada y salida:** de LEDs, pico por canal con retención y el máximo en cifras (clic para borrarlo). Van en las columnas de la derecha, junto a sus knobs de ganancia, y la curva ocupa todo el ancho.
- **Carácter analógico:** saturación después del EQ, a elegir entre *Limpio*, *Cinta* (simétrica, armónicos impares, compresión suave) y *Válvula* (asimétrica, añade armónicos pares), con knob de **Drive** (0 % = limpio) y de **Mezcla** (por debajo de 100 % es saturación en paralelo: se mezcla la señal sin saturar con la saturada) y sobremuestreo 2x para evitar aliasing. El sobremuestreo usa filtros FIR de **fase lineal** e introduce una latencia (de decenas de muestras, menos de 1 ms) que el plugin **informa al host** y que es **siempre la misma**: con la saturación apagada, en *Limpio* o con la mezcla en paralelo, la señal seca pasa por un retardo igual, así que seco y saturado están alineados en todo el espectro y el DAW puede compensarla (sin efecto peine en mezclas en paralelo). El EQ es de fase mínima y no añade latencia. Una prueba automática de CI lo comprueba con un impulso.
- **Estilo de curva:** *Moderna* (Q constante), *Clásica* (la campana se ensancha al subir la ganancia y se estrecha al bajarla), *Americana* (al revés: se estrecha al subir) y *Vintage* (como la Clásica, con un pequeño rebote de resonancia en los shelves).
- **EQ dinámico por banda:** el botón *Dinámica* de cada campana o shelf hace que su ganancia (el valor del knob de ganancia es el máximo) solo se aplique cuando el nivel en esa banda supera el **umbral**, de forma progresiva según el **ratio**. Con ganancia negativa actúa como compresor de banda (de-esser, control de resonancias); con positiva, como expansor de banda. Cada banda tiene su propio **umbral**, **ratio**, **ataque** y **release**. Un anillo alrededor del punto de la curva marca las bandas dinámicas; el punto blanco que se mueve sobre ella y la barra de cada banda muestran en directo cuánto de la ganancia máxima se está aplicando.
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
