# Si el juego va lento: cómo recuperar el rendimiento

Última actualización: 2026-09-04

Este documento existe porque el rendimiento de este port se ha perdido ya una
vez **sin ningún síntoma visible**: no hubo defecto gráfico, ni error de
compilación, ni cambio en el renderizador. Sólo el juego yendo más lento. La
causa fue de configuración de la build, y tardó una sesión entera en
encontrarse. Con esta página no debería costar más de un minuto.

---

## Lo primero, siempre

```sh
tools/check-rendimiento.sh
```

Comprueba en un segundo las cuatro causas conocidas y, por cada problema,
imprime el comando exacto que lo corrige. Si dice que todo está bien, el
problema no es de configuración y hay que pasar a **medir** (sección 5).

Acepta otro directorio de build como argumento:
`tools/check-rendimiento.sh build-linux-standalone`.

---

## Las causas conocidas, en orden de probabilidad

### 1. La build está en `Debug`, o sin tipo

`Debug` compila con `-g` y **ninguna** `-O`, es decir `-O0`. Un tipo de build
vacío hace lo mismo. Para un juego eso no es «un poco más lento»: es una
fracción de la velocidad normal.

Se cuela con facilidad porque depurar cualquier otra cosa —audio, un fallo de
carga— invita a reconfigurar en `Debug`, y luego nadie lo devuelve.

```sh
grep '^CMAKE_BUILD_TYPE' build/CMakeCache.txt      # no debe decir Debug
```

**Arreglo:**

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
```

`RelWithDebInfo` es el tipo correcto para el día a día: optimiza igual que
`Release` y conserva los símbolos que hacen legible un *backtrace*.

Señal rápida sin mirar nada: el binario de `Debug` pesa unos 29 MB; el
optimizado, unos 6 MB sin símbolos.

Desde el 2026-09-04 esto se previene solo. Un tipo vacío pasa a
`RelWithDebInfo`, y configurar `Debug` imprime un aviso de que el rendimiento
no será representativo.

### 2. Las opciones de optimización no llegan a todos los objetivos

**Ésta es la que costó una sesión, y la razón principal de este documento.**

El proyecto compila en dos objetivos:

| Objetivo | Qué contiene | Peso en el tiempo por frame |
|---|---|---|
| `pikmin_legacy` | Todo el juego decompilado: `renderall()`, recorrido de escena, matrices, colisiones, IA, `dgxGraphics` | **El grueso** |
| `pikmin_pc` | `pc_port/`: traductor GX, audio, ventana | El resto |

El 2026-09-03, preparando el port a Windows, las fuentes decompiladas se
sacaron del ejecutable a su propia biblioteca (`add_library(pikmin_legacy ...)`).
Las tres opciones de optimización —`-O3` explícito, `-march=native` e
IPO/LTO— se quedaron atadas al objetivo `pikmin_pc`, que a partir de ese
momento sólo contenía `pc_port/`.

Resultado: **todo el juego** perdió el juego de instrucciones de la máquina y
el LTO, y la frontera de biblioteca impidió además que el traductor GX
siguiera alineándose dentro del código de juego. Justo la mitad donde está el
trabajo. Y nada lo señalaba.

```sh
grep -m1 CXX_FLAGS build/CMakeFiles/pikmin_legacy.dir/flags.make
grep -m1 CXX_FLAGS build/CMakeFiles/pikmin_pc.dir/flags.make
```

Los dos deben traer `-O3`, y en una build local también `-march=native` y
`-flto`.

**Arreglo:** en `CMakeLists.txt`, la lista `PIKMIN_OPTIMIZED_TARGETS` debe
nombrar todos los objetivos que ejecutan código de juego. Los tres bloques que
siguen (`-O3`, `-march=native`, IPO) iteran sobre ella.

> **Regla que deja este fallo.** Cualquier reorganización de objetivos de CMake
> —dividir uno, crear una biblioteca, añadir una plataforma— debe comprobarse
> leyendo el `flags.make` de cada objetivo, **no** el `CMakeLists.txt`. Un
> objetivo nuevo nace sin las opciones del anterior, y el único síntoma es el
> tiempo de frame. Para eso está `tools/check-rendimiento.sh`.

### 3. Un interruptor de diagnóstico quedó puesto

Todos van apagados por defecto y todos frenan el juego a propósito. Es fácil
dejar uno exportado en la terminal después de depurar.

| Variable | Qué hace cuando está puesta |
|---|---|
| `PIKMIN_BATCH=0` | Desactiva la agrupación de primitivas: vuelve a ~25.000 draws por frame en vez de ~479. **El más caro de todos.** |
| `PIKMIN_UBERSHADER=1` | Vuelve al shader genérico. Correcto pero muy lento: los menús caen de 60 a ~17 FPS. |
| `PIKMIN_TEV_SPECIALIZE=0` | Lo mismo por la otra puerta. |
| `PIKMIN_WILD_VERTS=1` | Sondas por vértice en el bucle más interno del renderizador. Costaba al título más de la mitad de su tasa de frames. |
| `PIKMIN_GL_CHECK=1` | Comprobación de error de OpenGL por cada escritura de uniform. |
| `PIKMIN_NO_UNIFORM_CACHE=1` | Sube todos los uniforms siempre. |
| `PIKMIN_TICK_STATS=1`, `PIKMIN_PERF_STATS=1` | Instrumentación. Barata, pero no es gratis. |
| `PIKMIN_AUDIO_TRACE_ALL=1`, `PIKMIN_DUMP_SHADERS=1`, `PIKMIN_REPLAY_TEST=1` | Trazas y experimentos. |

```sh
env | grep PIKMIN_        # no debería salir nada
```

### 4. Ajustes del jugador

En el menú **F1**, dentro del juego:

- `renderScale` por encima de 1 renderiza a más resolución que la pantalla y
  luego reduce. Cuesta caro y casi no se ve.
- `fpsMode = 1` pide 60 Hz en gameplay. Es la opción experimental; el
  comportamiento original del juego son 30.
- `vsync` no cambia el trabajo por frame, pero sí cómo se percibe: con `vsync`
  activo, no llegar al frame significa caer al escalón siguiente de golpe.

`pikmin_settings.conf` es del usuario y no está bajo control de versiones.

---

## 5. Si la configuración está bien y aun así va lento: medir

La lección más cara de este proyecto: **instrumentar antes que teorizar.** En
PERF-NATIVE-003 se quemaron cinco hipótesis plausibles antes de que una sonda
enseñara la causa real.

```sh
PIKMIN_TICK_STATS=1 ./build/bin/pikmin 2>&1 | grep -A9 "PC tick"
```

Reparte el tiempo por tick entre `update`, `renderall`, `doneRender` y, dentro
del envío, `gl:uniforms`, `gl:vbo` y `gl:draw`. Informa además de draws por
frame y vértices por draw.

```sh
PIKMIN_PERF_STATS=1 ./build/bin/pikmin 2>&1 | grep "PERF GPU"
```

Separa el tiempo de GPU en `scene` y `blit` con consultas asíncronas, sin
`glFinish` que contamine la medida.

**Cómo leerlo.** El presupuesto son 16,6 ms a 60 Hz y 33,3 a 30 Hz.

- Si `renderall` es alto pero la GPU está ociosa → el cuello es la emisión
  (CPU). Mira draws por frame: por encima de unos pocos miles, algo ha roto
  la agrupación.
- Si la GPU está saturada → mira `renderScale` y la especialización TEV.
- Si `update` es alto → es lógica de juego, y sería nuevo: en las mediciones
  de 2026-09-01 daba 0,01 ms, o sea nada.

---

## Cifras de referencia

Medidas en la máquina de desarrollo, **GTX 1050 4 GB sobre Ubuntu / Mesa**, a
1920x1080 con `renderScale = 1`. Sirven de listón: si lo tuyo se parece, la
build está sana.

| Medida | Valor esperado |
|---|---|
| GPU, escena | 3,7 – 5,9 ms |
| GPU, blit | ~1,0 ms |
| `renderall` p95, gameplay con agrupación | ~16,4 ms |
| Draws por frame, escena cargada | ~479 |
| Programas TEV generados en todo el recorrido | ~15 |
| Menús, título, selección de mapa | 60 FPS |
| Gameplay | 30 FPS de origen, 60 con `fpsMode = 1` |

Sin agrupación de primitivas esa misma escena costaba 66,5 ms y 25.800 draws.
Sin especialización TEV, los menús iban a 17-18 FPS.

---

## Volver a una build buena conocida

El versionado de este proyecto son copias locales, no git.

```sh
tools/snapshot.sh list
tools/snapshot.sh restore snapshots/20260904-002102-perf-native-007-optimizacion-recuperada.tar.zst
cmake --build build -j"$(nproc)"
```

`restore` guarda antes el estado actual como `pre-restore`, así que restaurar
nunca puede ser lo que pierda trabajo. Sólo toca fuentes: `build/`, los assets
y las partidas guardadas quedan intactos.

Snapshots de referencia:

| Snapshot | Qué es |
|---|---|
| `20260904-002102-perf-native-007-optimizacion-recuperada` | Optimización recuperada. **Punto de partida actual.** |
| `20260904-001606-antes-rescate-rendimiento` | Justo antes de ese arreglo, por si hay que comparar. |
| `20260901-185758-estable-60-30` | 60 FPS en menús / 30 en gameplay, antes de la agrupación de primitivas. |

---

## Antes de dar por buena una optimización

Vale para lo que venga después. De `60FPS_FAILED_ATTEMPTS.md` y de
PERF-NATIVE-003:

1. Un contador de FPS **no** es validación. Hay que ver color, geometría, UI,
   animación y velocidad real. Un segundo render que «funciona» ha dejado ya el
   juego en blanco y negro.
2. Una suite offline que pasa **no** demuestra equivalencia visual.
3. Todo trabajo experimental va detrás de un interruptor apagado por defecto,
   para que revertir sea cambiar un ajuste y no restaurar archivos.
4. Snapshot antes de empezar.

---

## Dónde está el resto

- `ROADMAP.md` — fase 8, tareas PERF-NATIVE-001 a 007, con el historial y las
  pruebas de cada una.
- `SIGUIENTE_SESION.md` — mediciones detalladas y el siguiente candidato de
  optimización sin implementar (cachear `compute_batch_state_key()`).
- `60FPS_FAILED_ATTEMPTS.md` — lo que ya se probó para los 60 FPS en gameplay y
  por qué falló. Leerlo antes de tocar temporización o presentación.
