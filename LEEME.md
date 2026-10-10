# Phantom Asym

Efecto para el master que imita el "entrecortado" entre el low end y el resto de la mezcla que dan los saturadores asimétricos, sin saturar nada ni agregar nivel.

## Qué hace (modo principal: "1 cycle yes / 1 no")

1. Separa la señal en **sub** (por debajo del crossover) y **resto** con un filtro Linkwitz-Riley (al sumarlos vuelven a dar la señal original).
2. Detecta cada **ciclo completo** del sub (lóbulo positivo + lóbulo negativo).
3. **Silencia el resto durante un ciclo entero, deja pasar el siguiente, silencia el siguiente**, y así alternando.
   Con un sub a 50 Hz son 25 silencios por segundo (un compresor con sidechain da 100).
4. El sub pasa siempre intacto. El resto solo se atenúa, nunca se amplifica.

Entre golpes de bombo, cuando el sub se apaga, el contador se reinicia: cada golpe arranca con el mismo patrón.

## Controles

| Control | Qué hace |
|---|---|
| **Sub Crossover** | Hasta qué frecuencia se considera "sub" (30 a 200 Hz) |
| **Depth** | Cuánto baja el resto en el ciclo silenciado (100 % = silencio total, como en el dibujo) |
| **Sensitivity** | Nivel del sub (dBFS) a partir del cual se detectan los ciclos. Si el efecto no arranca o se dispara con ruido, ajustá esto |
| **Smooth** | Suavizado de las entradas y salidas del silencio (0.1 a 20 ms). Muy bajo puede dar clicks |
| **Mode** | "1 cycle yes / 1 no" (el principal) o "Half-wave" (una bajada por cada ciclo, versión anterior) |
| **First Cycle** | En cada golpe de bombo, si el primer ciclo se silencia o pasa |
| **Polarity** | Define dónde empieza cada ciclo (en el semiciclo positivo o en el negativo). Probá los dos |
| **Output** | Volumen final |

## Compilar e instalar

1. Subí **todo el contenido de esta carpeta** (incluida `.github`) a un repositorio público de GitHub.
2. En **Actions** se compila solo (10 a 15 min).
3. Descargá de **Artifacts** la versión de Windows o Mac.
4. Windows: `Phantom Asym.vst3` a `C:\Program Files\Common Files\VST3\`
   Mac: `.vst3` a `~/Library/Audio/Plug-Ins/VST3/` y `.component` a `~/Library/Audio/Plug-Ins/Components/`
   (en Mac, si lo bloquea: `xattr -cr` sobre el archivo en la Terminal).

## Notas

- El silencio dura exactamente un ciclo del sub, así que cuanto más grave el sub, más largo. Con 30 Hz son unos 33 ms.
- No agrega latencia relevante ni offset DC, y no puede subir el pico máximo.
- El control se toma del promedio de L y R, y la misma bajada se aplica a los dos canales.
- Cortes bruscos de volumen a frecuencia de sub generan bandas laterales alrededor del resto: esa es la textura que se oye.
