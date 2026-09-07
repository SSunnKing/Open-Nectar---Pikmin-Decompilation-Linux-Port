# Siguiente sesión — agrupación de primitivas (PERF-NATIVE-002)

Documento de traspaso escrito el 2026-09-01. Contiene todo lo necesario para
retomar el trabajo en un chat vacío, sin depender del historial anterior.

---

## 1. El proyecto

Port nativo a Linux de Pikmin (GameCube, 2001), construido sobre la
decompilación projectPiki, en `/home/sunking/Documentos/antigravity/pikmin/`.

Objetivo del usuario, en sus palabras: que el juego «pueda ir en cualquier
ordenador ya que es un juego antiguo, la optimización debe ser impecable y
nativa». De momento, 60 FPS a 1080p. Su equipo es una **GTX 1050 4 GB sobre
Ubuntu**, así que lo que se mida ahí es el listón realista.

### Reglas de trabajo, aprendidas y confirmadas

- **El usuario no usa git ni GitHub.** El repositorio `.git` que existe lo puso
  otra IA; conserva la procedencia de la decompilación original y no estorba,
  pero **no es la red de seguridad**: `pc_port/` entero está sin rastrear. El
  versionado real son los snapshots de `tools/snapshot.sh`. No proponerle
  commits, ramas ni PRs.
- **El usuario ejecuta y prueba el juego; el agente no lo ejecuta.** Las
  mediciones llegan porque él lanza el binario y pega el log.
- **Nada de ROMs ni assets en el repositorio** (ver LEGAL.md).
  `pikmin_settings.conf` es suyo y está en .gitignore.
- **Quiere avanzar paso a paso, sin dejar nada a medias, y poder revertir con
  facilidad.** Antes de cada cambio grande: `tools/snapshot.sh save <nombre>`.
- **Lección de método, cara de aprender:** instrumentar antes que teorizar. En
  PERF-NATIVE-003 se quemaron cinco hipótesis plausibles sobre un fallo de color
  antes de que una sonda revelara la causa real (una escritura de sampler contra
  el programa equivocado). Medir primero.

## 2. Estado actual: la build de referencia

**Punto de partida de la próxima sesión:**
`snapshots/20260903-074128-estado-whoo-medido.tar.zst`

Punto de partida anterior, por si hiciera falta volver:
`snapshots/20260902-195110-antes-fix-jam-registros.tar.zst` (antes del arreglo
del intérprete JAM) y `snapshots/20260902-001557-fin-sesion-remap-revertido.tar.zst`.

Validado en juego a 2026-09-02: batching activo, **60 FPS reales en gameplay**
(opción del menú F1, apagada por defecto), 1080p nativo a escala 1x, cinemáticas
a velocidad correcta, sin fallos gráficos, y el escenario ya no se pierde al
cambiar de guardado. Con los 60 FPS apagados el juego corre a los 60/30 del
`setFrameClamp` original.

Snapshots anteriores de referencia, por si hiciera falta bisecar:

- `20260901-185758-estable-60-30.tar.zst` — antes de todo el trabajo de hoy.
- `20260901-232542-fix-volumen-escala.tar.zst` — antes de tocar la resolución
  de muestras de audio.

Restaurar: `tools/snapshot.sh restore <archivo>` (guarda automáticamente el
estado actual antes de sobrescribir), luego `cmake --build build -j$(nproc)`.

Compilar y probar:

```
cmake --build build -j$(nproc)
cd build && ctest        # 14/14 deben pasar
```

## 2.bis Rendimiento recuperado (2026-09-04)

> Guía completa y de consulta rápida: **`RENDIMIENTO.md`**, con
> `tools/check-rendimiento.sh` como primer paso.


Si el juego va más lento de lo que dicen las cifras de este documento, lo
primero que hay que mirar no es el renderizador sino cómo se compiló:

```sh
grep -m1 CXX_FLAGS build/CMakeFiles/pikmin_legacy.dir/flags.make   # debe traer -O3
grep '^CMAKE_BUILD_TYPE' build/CMakeCache.txt                       # no debe ser Debug
```

`pikmin_legacy` es todo el juego decompilado y ahí está el grueso del trabajo
por frame. Cuando se separó del ejecutable para el port a Windows, se quedó sin
`-O3`, sin `-march=native` y sin LTO, porque esas opciones estaban atadas a
`pikmin_pc`. Arreglado en PERF-NATIVE-007: van a los dos objetivos. Además, un
tipo de build vacío ya no equivale a `-O0`, y `Debug` avisa.

Para reconfigurar:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
```

**Siguiente candidato medible, no implementado.** `compute_batch_state_key()`
recorre unos 600-900 bytes de estado por primitiva GX, es decir del orden de
20 MB de hash por frame pesado. Cachear la clave tras un contador de época que
incrementen los setters de estado la dejaría en una comparación, pero un setter
olvidado da corrupción dependiente del contenido, que es exactamente el modo de
fallo que costó cinco hipótesis en PERF-NATIVE-003. No hacerlo a ciegas: medir
antes con `PIKMIN_TICK_STATS=1` sobre la build ya arreglada, y si compensa,
implementarlo con un modo de verificación que recalcule y compare.

## 3. Lo que ya está medido (no repetir)

Instrumentación propia, activada con `PIKMIN_TICK_STATS=1`. Vive en
`pc_port/timing/pc_tick_profiler.{h,cpp}` (+ test), con puntos de medida en
`PlugPikiApp::idle()` (`src/plugPikiColin/plugPiki.cpp`) y dentro de
`pc_gfx_end()` (`pc_port/gl/pc_gfx.cpp`). No cambia comportamiento y no lee el
reloj cuando está apagada.

Uso: `PIKMIN_TICK_STATS=1 ./build/bin/pikmin 2>&1 | grep -A9 "PC tick"`

### Resultados en gameplay (mediana, y p95 entre paréntesis)

| Región | ms | Lectura |
|---|---|---|
| `update` (lógica del juego) | **0,01** | Irrelevante. Descartada como obstáculo. |
| `renderall` | 13 (33) | Todo el coste está aquí. |
| `doneRender` | 0,00 | Envío asíncrono; la espera vive en `waitRetrace`. |
| — `gl:uniforms` | 3,4 (8,4) | ~400 ns por draw pese a la caché de valores. |
| — `gl:vbo` | 2,2 (6,6) | Un `glBufferSubData` por primitiva. |
| — `gl:draw` | 1,1 (2,7) | |
| — resto de `renderall` | ~7 (~15) | Sin instrumentar: recorrido de escena y ensamblado de vértices. |

| Contador | p50 | p95 |
|---|---|---|
| `gl:draws/frame` | **8.300** | 27.376 |
| `gl:verts/draw` | **9,3** | ~15 |

GPU, medida aparte con `PIKMIN_PERF_STATS=1`: **3,7–5,9 ms** a 1080p nativo.

### Paso 1 completado: longitud de racha (medido el 2026-09-01)

Sonda propia en `pc_gfx_end`, misma variable `PIKMIN_TICK_STATS`. Filas
`gl:draws/run`, `gl:longest run` y `gl:%breaks=glstate`. Escena de prueba: tres
bosses y 70 Pikmin, que es donde el rendimiento se hundía a 11 FPS.

| Escena | draws/frame | draws/run p50 | p95 | p99 | racha máxima |
|---|---|---|---|---|---|
| Menú / título | 600 | 1,5 | 1,5 | — | 199 |
| Gameplay normal | 8.500 | 19,5 | 59 | 80 | 1.902 |
| Tres bosses | 27.000 | 19,5 | 59 | 80 | 4.866 |

**Las rachas son largas y no se degradan bajo carga.** Con 27.000 draws siguen
midiendo ~20 de mediana y 59-80 en los frames pesados: la agrupación deja el
peor caso en ~460 draws. Cada boss añadía ~6.500 draws de ~10 vértices, que es
exactamente el eje que ataca esta tarea.

Las cifras de menú (1,5 draws/run sobre 600 draws de 160 vértices) no
preocupan: ahí el juego ya emite mallas grandes y no hay nada que agrupar.

**Roturas por estado GL: 24%, no 90%.** La primera pasada midió 90% porque el
contador de época incrementaba *antes* de la guarda de redundancia que los
setters ya tienen. Corregido, la época sólo se mueve cuando el estado GL cambia
de verdad. Es decir: la inmensa mayoría de las reprogramaciones de estado del
juego son escrituras redundantes que el port ya descarta, y no impiden agrupar.

Aviso al leer logs de la sonda: el hash de estado cuesta lo suyo y se carga a
`gl:uniforms`, así que en esas ejecuciones `renderall`, `tick` y los FPS salen
inflados y **no** son comparables con la línea base. Las filas de rachas sí.

### PERF-NATIVE-002 terminado: resultados (2026-09-01)

Medido en la segunda zona (stage1), que es más pesada que la de la línea base
original: ~25.800 primitivas por frame en combate contra las ~8.300 de antes.
A/B con `PIKMIN_BATCH=0`, que reproduce exactamente el comportamiento previo.

| Frames pesados (~25.800 prim.) | sin batching | con batching |
|---|---|---|
| `renderall` p95 | 66,5 ms | **16,4 ms** |
| `gl:uniforms` p95 | 48,4 | 7,6 |
| `gl:vbo` p95 | 5,51 | 1,46 |
| `gl:draw` p95 | 2,62 | 0,40 |
| draws/frame | 25.800 | **479** |
| primitivas por lote, p95 | — | 62 |

**La escena más cargada entra en el presupuesto de 60 Hz (16,4 contra 16,7 ms).**

Dos correcciones que costaron una iteración cada una, por si vuelven a aparecer:

1. **El hash de la clave de estado pasó de diagnóstico a ruta caliente sin que
   nadie lo notara.** Mientras era sonda corría sólo con `PIKMIN_TICK_STATS=1`;
   en cuanto el batcher lo usó para decidir lotes, corrió siempre: 25.000 veces
   por frame recorriendo 2 KB byte a byte, 42 ms/frame. Arreglado hasheando de
   ocho en ocho bytes y sustituyendo el contenido de las matrices por un
   contador de revisión por ranura (`sPosMtxGen`, `sNrmMtxGen`, `sTexMtxGen`,
   `sProjMtxGen`), que cambia sólo al cargarlas.
2. **`gl:prims/batch` marcaba 1,00** porque dividía entre `sSubmitDraws`, que
   tras el cambio cuenta lotes y no primitivas. Ahora divide entre
   `sSubmitPrims`.

Nota al leer la tabla: `gl:uniforms` ya no mide sobre todo uniforms (sólo se
escriben ~479 juegos por frame). Lo que queda ahí dentro es mayoritariamente el
hash de clave. Es el siguiente candidato si hiciera falta más margen.

### Conclusiones firmes

1. **La lógica del juego no es el obstáculo.** 0,01 ms por tick. Podría correr a
   240 Hz. Toda la hipótesis previa de que la IA de los Pikmin o la física
   impedirían los 60 Hz queda descartada por medición.
2. **El cuello de botella es la emisión de dibujo.** Ocho mil llamadas por frame
   de nueve vértices cada una: la sobrecarga de driver supera al trabajo real.
   La GPU está ociosa el 85 % del tiempo. No es el hardware ni la resolución.
3. **Agrupar es viable: las rachas dan ~20 draws de mediana y 60-80 bajo
   carga**, y el 76% de las roturas son de material, no de estado GL. El diseño
   del apartado 4 se mantiene sin cambios.
4. **La build actual va justa incluso a 30 FPS**: p95 de 33,0 ms contra un
   periodo de 33,3. Optimizar esto no sirve sólo para llegar a 60; también saca
   a los 30 actuales del límite, que es justo lo que hace falta para el objetivo
   de «que ruede en cualquier ordenador».

## 4. La tarea: agrupar primitivas en lotes

El port traduce cada par `GXBegin`/`GXEnd` en un draw independiente. Muchísimas
primitivas consecutivas comparten material, textura, matrices y configuración
TEV. Acumulando en el mismo búfer las que comparten estado y emitiendo un solo
`glDrawArrays` al final, los tres costes caen a la vez: un upload en vez de
miles, un juego de uniforms en vez de miles, y cientos de draws en vez de 8.000.

### Dónde se toca

- `pc_port/gl/pc_gfx.cpp`, `pc_gfx_end()` — empieza en la línea ~2930.
  - ~2948 `use_program_for_current_state()` y a continuación el bloque de
    uniforms, que se extiende hasta ~3355.
  - ~3356 `const size_t vertexBytes` — subida al VBO.
  - ~3384 `glDrawArrays(...)` — la emisión.
- `pc_gfx_begin()` (~2620) prepara `sVertexStream` y `sCurrentPrimType`.
- El vértice es `struct Vertex` (línea ~305), 72 bytes, con `static_assert`.

### Pasos, en orden

**TODOS LOS PASOS HECHOS.** Ver «PERF-NATIVE-002 terminado» más arriba.
El diseño final se apartó del plan en un punto importante: no hace falta vaciar
antes de cada mutación de estado de material. Los uniforms de un lote se
escriben al abrirlo y el dibujo ocurre después, así que basta con (a) vaciar
antes de tocar GL directamente y (b) comparar la clave de estado en cada
primitiva. El estado de material se detecta por valor, no por llamada — lo cual
era imprescindible, porque el 90% de las reprogramaciones del juego son
redundantes y vaciar en cada llamada habría dejado los lotes en una primitiva.

Puntos de vaciado reales (`pc_gfx_flush_batch`): los siete setters de estado GL
(viewport, scissor, z-mode, blend, cull, color update, alpha update) —siempre
*después* de su guarda de redundancia—, más `pc_gfx_present`, `copy_disp`, las
dos rutas de subida de textura, `set_shader_specialisation`, el replay de
display lists y el gancho de fin de tick.

`PIKMIN_BATCH=0` desactiva la agrupación y restaura el comportamiento anterior
primitiva a primitiva: es el A/B para cualquier defecto visual que se sospeche
del batcher.

1. ~~**Sonda de rachas.**~~ HECHO.

   Texto original:
   **Sonda de rachas — hacer esto primero, antes de escribir el batcher.**
   Toda la ganancia depende de que el juego emita rachas de primitivas
   consecutivas con el mismo estado. Si las rachas resultan cortas, el diseño
   cambia por completo. La comprobación es barata: calcular la clave de estado
   en `pc_gfx_end`, comparar con la anterior y contar la longitud media de
   racha, informando por `PIKMIN_TICK_STATS`. **Pedir al usuario que lo ejecute
   y decidir con el dato en la mano.**

2. **Clave de estado.** Debe cubrir *todo* lo que lee el bloque de uniforms:
   programa TEV, matriz de proyección, `sCurrentPosMtxId` y
   `sVerticesPretransformed`, colores de material y estado de canales, comparación
   alfa, registros TEV, konst, `numStages`, `fastPath`, modos y matrices de
   texgen, más las texturas ligadas y el estado GL (blend, depth, cull, scissor).
   Cualquier cosa que se olvide es corrupción visual.

3. **Conversión a triángulos.** Strips, fans y quads pasan a `GL_TRIANGLES` al
   acumular, para que primitivas consecutivas puedan fundirse. Líneas y puntos
   llevan lotes propios.

4. **Sólo primitivas consecutivas, sin reordenar.** Preserva exactamente el
   orden de mezcla, que es lo que hace el cambio verificable a ojo.

5. **Puntos de vaciado. Éste es el riesgo real del trabajo.** Hay que vaciar el
   lote pendiente antes de *cualquier* mutación de estado GL hecha fuera de
   `pc_gfx_end`: blend, depth, ligado de textura, viewport, scissor, ligado de
   framebuffer, `glClear`, lecturas de vuelta — y al terminar el frame. Un punto
   de vaciado olvidado produce corrupción visual dependiente del contenido, que
   es exactamente la clase de fallo que costó cinco hipótesis en
   PERF-NATIVE-003. Conviene enumerarlos exhaustivamente con grep antes de
   escribir el batcher, no después.

### Estimación

Si el subtotal GL se vuelve despreciable, `renderall` pasa de 13 → ~7 ms en el
caso típico y de 33 → ~15 ms en el p95. El típico entra holgado en los 16,6 ms y
el peor caso entra ajustado: **los 60 FPS en gameplay salen**, quedando el otro
medio `renderall` (recorrido de escena y ensamblado de vértices, aún sin
instrumentar) como margen de reserva si hiciera falta.

## 5. Trabajo del 2026-09-01 / 02 (fuera de PERF-NATIVE-002)

### Audio: volumen y sonidos perdidos (2026-09-01)

**1. Música y efectos al 7,9%.** Los ajustes de volumen son deslizadores 0..10
(`ogTitle.cpp` los mueve con `if (mBgmVol < 10) mBgmVol++`), pero
`Jac_SetBGMVolume`/`Jac_SetSEVolume` dividían entre 127. Los buses BGM y SE se
quedaban en 10/127. La música de cinemáticas va por el bus STREAM, que sí
llegaba a 1,0: de ahí que sonara ~13x más fuerte. Medido con picos reales por
bus: gameplay 1.044 de 32767, cinemática 32.426.

**2. Sonidos que no sonaban.** Muestras buscadas en el sistema de ondas 0 que
viven en el 2. Descartado por medición, en este orden: voces agotadas (cero
rechazos y cero robos), escena equivocada (ninguna aparecía en otra escena),
sistema por defecto del banco (esa rama no se ejecuta nunca: las regiones sí
declaran sistema) y percusión (las que fallan son melódicas, programas 0-3).

Se probó una **reparación sobre el fallo** (buscar el sistema que sí tiene la
muestra y recordarlo por banco; los bancos 0 y 1 declaraban sistema 0 y
resolvían al 2). **REVERTIDA**: no arreglaba los sonidos que faltaban -- ésos no
llegan a pedirse, ver el punto 3 -- y además hacía sonar muestras equivocadas,
que era el riesgo previsto de elegir por id entre sistemas distintos. El
diagnóstico se conserva; la sustitución no.

Conclusión útil que queda: **esas muestras se perdían de verdad**, y el
emparejamiento banco↔WSYS no está en ninguno de los dos catálogos. Leerlo del
contenedor AAF/BAA original es el arreglo correcto y sigue pendiente.

**Queda abierto:** no se pudo determinar si el 0 significa «sin especificar»
(como el -1) o si el emparejamiento banco↔WSYS debería leerse del contenedor
AAF/BAA original — los `.bx` de WSYS e IBNK se cargan por separado y ninguno
menciona al otro. Leer esa asociación del AAF sería el arreglo definitivo.

**3 y 4. Las secuencias de efectos morían a media ejecución — RESUELTO
(2026-09-02).** Los dos fallos tenían la misma raíz: el intérprete JAM del port
decodificaba a mano la longitud de los operandos de cada opcode, y dos de esas
decodificaciones estaban mal. Un solo byte de más desincroniza el flujo entero.

La verdad de referencia estaba en el propio repositorio y no se había mirado:
`src/jaudio/jammain_2.c` es el intérprete original decompilado, con
`Jam_WriteRegParam`, `Jam_ReadRegDirect`, `RegCmd_Process`, `Cmd_Process` y la
tabla `Arglist`. Los tres arreglos salen de ahí.

**a) `A9 98 01` — la forma de fuente 8 bajo operación extendida es la constante
−1 y no consume byte.** El port leía un operando que no existe. El código real
en el byte 4085 de `pikise.jam` es:

    4085: A9 98 01        r1 = aleatorio(0xFFFF)
    4088: A9 34 01 03     r1 &= 3
    4092: C4 C0 01 ...    llamada por tabla de 4 entradas, índice r1

Es decir, **elegir al azar uno de cuatro filtros IIR**. Con el byte de más, r1
llegaba valiendo un número grande y la llamada por tabla saltaba a basura: ése
era exactamente el `opcode 0xC4` del byte 4092 que se veía en el juego. La
sonda de traza confirmó el valor 72 como índice, el mismo que reproduce el
banco de pruebas.

**b) `Jam_ReadRegDirect` tiene registros que son vistas calculadas.** El port
leía un array plano. Los que faltaban: `0x2C` = máscara de pistas hijas vivas
(las secuencias la usan para buscar una ranura libre antes de abrir una pista),
`0x2D` = qué voces están paradas, `0x30` = contador del bucle en curso, más el
remapeo de `0x20`/`0x21` a banco y de `0x2E`/`0x2F` a prioridad, la escritura de
`0x22` sobre r0:r1 y la multiplicación, que deja su resultado de 32 bits en el
par r4:r5 y no en el registro nombrado.

**c) La familia `0xB0` ahora se decodifica con la tabla `Arglist` original.**
El port tenía dos ramas escritas a mano que sólo entendían cuatro comandos y
mataban la secuencia ante cualquier otro. Con la tabla, los operandos de
*cualquier* comando se consumen con la longitud correcta, así que un comando no
implementado se puede saltar sin desalinear el flujo. Eso arregla el
`0xB0`/`0xFD` del byte 3696 (que era simplemente «tempo desde el registro r4»)
y cierra la clase entera de fallo.

**Medición, antes y después.** Banco de pruebas propio que recorre todos los
identificadores de sonido de las seis pistas que despachan efectos, con un
reproductor nuevo por sonido. Vive ya dentro de `pc_jam_test` (`SE sweep`), así
que `ctest` lo vigila:

| | antes | después |
|---|---|---|
| sonidos que suenan | 64 | **95** |
| muertes de la secuencia | 31 | **0** |

Las 22 secuencias del archivo, música incluida, emiten **exactamente los mismos
eventos** que antes del cambio: mismo instante, tono, velocidad, banco,
programa, volumen, paneo y tono. El arreglo no toca el BGM.

**Fallo cometido y corregido en el camino, porque cuesta poco repetirlo.** La
primera versión del arreglo cambió el banco de toda la música a 0. Causa: el
registro 6 *es* el banco en la unión del original, pero en el port la autoridad
es `Track::bankProgram`, y una pista hija lo hereda al crearse **sin que se
copie su fichero de registros**. Al pasar a leer el banco del registro 6 se
perdía lo heredado. Ahora `readRegDirect` devuelve `bankProgram` para los
registros 6, 0x20 y 0x21.

La lección de método es sobre la comprobación, no sobre el código: la primera
verificación contó *cuántas* notas emitía cada secuencia, y salió idéntica,
porque el número de notas no cambia cuando lo que cambia es el instrumento. Al
comparar el flujo de eventos completo el fallo saltó en la primera línea.
**Cuando se toca un intérprete, hay que diferenciar la salida entera, no un
resumen de ella.** `pc_jam_test` guarda ahora las dos propiedades: `SE sweep` y
`inherited bank test`.

Nota sobre los rangos: las tablas de despacho tienen 38 entradas para la pista
de sistema y 15 para la de Olimar. Con identificadores por encima de ésos la
secuencia indexa bytes ajenos y salta a cualquier sitio — y el original hacía
exactamente lo mismo, así que no es un fallo del port. Si en el juego vuelve a
aparecer una muerte de secuencia, lo primero que hay que mirar es qué
identificador se envió.

**d) El volcado de traza se imprimía sin condición** en cada fallo, dieciséis
líneas por vez, fuera de la guarda de `PIKMIN_AUDIO_STATS`. Ya está dentro. Es
la tercera vez que aparece el mismo error de método en este proyecto.

**Lección de método (repetida hoy, conviene no repetirla más):** los avisos de
JAM salían sin límite por `printf` -- cientos por segundo -- y **colgaban el
juego**, que se confundió con un crash del port. Ahora están detrás de
`PIKMIN_AUDIO_STATS` y topados a 8. Es el mismo error que el hash de la sonda de
rachas, que dejó de ser diagnóstico al pasar a ruta caliente: **una sonda no
puede alterar lo que mide.**

### Auditoría del camino de instrumentos contra la decompilación (2026-09-02)

Revisión de `pc_instrument_bank.cpp` y `pc_audio_play_note` contra `bankdrv.c`,
`bankread.c`, `noteon.c` y `oneshot.c`. Cuatro resultados, dos de ellos cierran
preguntas que llevaban abiertas.

**1. La percusión se transponía por tecla — ARREGLADO, y es el fallo audible.**
`Play_1shot` transpone (`currentPitch = basePitch * C5BASE_PITCHTABLE[...]`),
pero `Play_1shot_Perc` hace `currentPitch = basePitch` y punto: en percusión la
tecla elige *qué* tambor se golpea, no a qué tono. El port aplicaba la
transposición a todo. Medido sobre las notas reales de las 22 secuencias: de
**269 notas de percusión, 54 sonaban desafinadas**, con factores de tono entre
**0,25x y 2,0x**. Las otras 215 se salvaban por casualidad, porque su tecla
coincidía con la raíz de la muestra.

Detalle de formato, por si hace falta volver: los programas **0xE4–0xEF son
percusión** (`BANK_TEST_PERC_OFFSET = 0x80 + 0x64`), 0xF0 en adelante son
osciladores, y 0x80–0xE3 son «voces», que este banco no usa.

**2. Las regiones aplanadas podían caer en la región de tecla equivocada —
ARREGLADO, pero latente.** `Bank_GetInstVmap` se compromete con la primera
región de tecla que cubre la nota y busca la velocidad **sólo dentro de ella**;
si ninguna encaja, silencio. El port guardaba las regiones en una lista plana y
buscaba tecla y velocidad a la vez, así que un fallo de velocidad se colaba en
la región de tecla siguiente y tocaba una muestra de otra zona del teclado. Se
comprobaron las **3.318.256** selecciones posibles de este banco: ninguna
cambia, porque aquí toda lista de velocidades termina en 127. El fallo era real
pero no se disparaba con estos datos. Arreglado igualmente.

**3. Los 3 IBNK que «no cargaban» no son un fallo — CERRADO.** Las ranuras 3, 6
y 8 de la tabla de `pikibank.bx` tienen tamaño 0: esos bancos no están en el
archivo. 19 de 22 es el número correcto.

Consecuencia real y única: `d_end2.jam` (secuencia 6, música de fin de día) pide
el banco virtual 6, que no existe, y pierde **125 de sus notas**. Ninguna otra
secuencia lo usa. Si esa música suena incompleta, es esto, y es cuestión de
datos, no de código: habría que averiguar de dónde saca el original ese banco.

**4. El emparejamiento banco↔WSYS — CERRADO, y el port ya lo hacía bien.**
Estaba en la decompilación todo el tiempo, en `WaveidToWavegroup` (waveread.c):
el identificador de muestra es de 32 bits y **su mitad alta es el sistema de
ondas**; si vale 0xFFFF se usa como respaldo el índice de banco que llega a
`Play_1shot`, que `noteon.c` ya ha convertido de virtual a **físico**. Es decir,
el emparejamiento es **posicional, por ranura**: banco físico N ↔ sistema de
ondas físico N. No hay que leer nada del contenedor AAF/BAA. Comprobado sobre
las notas reales de las 22 secuencias: **0 muestras sin resolver**.

**Localizado y sin arreglar: la percusión no aplica su paneo por tecla.**
`Play_1shot_Perc` hace `panMatrices[1].values[0] = perc->panTable[tecla]/127`
(tabla en `Perc_+0x288`), y el port no lee esa tabla. No es lo mismo que el
paneo de pista: son fuentes distintas que `PanCalc` combina, y la del note-on no
se sobrescribe después. No lo he tocado porque falta un dato para hacerlo bien:
`calc_sw_table` decide **cuáles** de las tres fuentes se suman para cada
`panCalcType`, y hay que descifrar esa tabla antes de mezclar nada. Es colocación
estéreo de la batería, no afinación: bastante menos grave que el punto 1.

### Sonidos de acciones de los Pikmin: diagnóstico (2026-09-02)

Síntoma: los Pikmin no hacen ruido al romper plantas, llevar objetos o coger
bombas, y de vez en cuando suena algo que no viene a cuento, incluida una
melodía de trompetas.

**Medido.** Barrido de las 281 acciones de evento del juego (las siete clases de
`kPCEventOffsets`) contra el reproductor real: **100 no producen ninguna nota**.
El desglose por tipo está en la salida de `pc_jam_test` («positional event
coverage» y los `zero-actions` por tipo).

**Causa localizada.** El camino del juego es
`Creature::playEventSound` → `SeContext::playSound` → `Jac_PlayEventAction`
→ `Jal_SendCmdQueue_Force(&EVENT[i].cmdQueue, slot<<12 | cmd)`. Dos cosas
comprobadas:

- La tabla `ACTION_STATUS` del port es **transcripción exacta** de la original:
  282 filas, cero diferencias. El formato `slot<<12 | cmd` también coincide. Ahí
  no está el fallo.
- El port entrega ese valor con
  `sEventJamPlayer.writeChildPort(indiceDeEvento, 0, valor)`, es decir al hijo
  número *i* de la **raíz**. Pero la raíz de `pikise.jam` sólo abre 6 hijos
  —comprobado: 0, 2, 7, 8, 9 y 10— y **sólo el 0 es la pista de eventos**
  (se anuncia con `ConnectName 1, 15` en el byte 5479 y despacha por tablas
  desde los puertos 0 a 3). Los demás son otras máquinas: 9 es el despacho de
  efectos de sistema y 10 el de Olimar/Pikmin, cuyo puerto 0 es un **id de
  sonido** que indexa tablas de 38 y 15 entradas.

Consecuencia, y explica los dos síntomas a la vez: los eventos que caen en las
ranuras 1, 3, 4, 5, 6 y 11-15 **no suenan** (no hay pista), y los que caen en
2, 7, 8, 9 y 10 escriben un comando de evento (hasta 0x11F, más la ranura en el
nibble alto) en el hueco de un id de sonido, se salen de la tabla y saltan a
bytes cualesquiera. De ahí los sonidos equivocados y la melodía.

**RESUELTO (2026-09-02): el port usaba la secuencia equivocada.**
`Jam_GetTrackHandle(0x20000)` busca por identificador de conexión, y
`Cmd_ConnectName` lo forma como `grupo << 16 | nombre`. Es decir, 0x20000 es
`ConnectName 2, 0`, y **eso sólo lo declara `sysevent.jam`** (secuencia 1), en
su byte 28 — justo después de abrir sus dieciséis pistas hijas en un bucle:

    14: LoopS 16
    17: OpenTrack r0, 62      <- las 16 pistas de evento
    27: LoopE
    28: ConnectName 2, 0      <- el handle 0x20000

El port arrancaba su reproductor de eventos con `pikise.jam`. Ya lee la
secuencia 1 (`kEventSequence`), y la mitigación que rechazaba los eventos
distintos del 0 se ha retirado.

**Tres fallos más que salieron al abrir esa puerta**, todos medidos con el
barrido de las 281 acciones:

1. **La parada de una acción mataba la secuencia.** El manejador real
   (`sysevent.jam` byte 98) parte la palabra en ranura (nibble alto) y comando
   (12 bits bajos), y **comando 0 significa «cierra esta ranura»**; 0xFFFF
   cierra el evento entero. El port enviaba 0x0FFF como parada, que se leía
   como un comando legítimo, abría una pista y saltaba fuera de su tabla de
   despacho. Como los sonidos se paran continuamente, esto tumbaba el
   secuenciador de eventos una y otra vez. El comando 0 nunca es una acción
   real: la entrada 0 de `ACTION_STATUS` queda por debajo del desplazamiento de
   todos los tipos de evento.

2. **Una nota de duración cero hacía girar el intérprete.** En
   `Jam_SeqmainNote` una nota en formato completo **siempre cede el paso**, y
   duración 0 significa «esperar indefinidamente» (`waitTimer = -1`), no «no
   esperar». El port sólo cedía si la duración era distinta de cero, así que un
   manejador de efecto que hace bucle sobre una nota así giraba hasta agotar el
   límite de operaciones y mataba la secuencia. Cuatro comandos hacían esto.

3. **Un opcode no implementado era fatal.** Dos manejadores empiezan con
   `OscFull` (0xF2), que el port no implementa. Ahora el `default` del
   despacho consume los operandos según la tabla `Arglist` y sigue, igual que
   ya hacía la familia 0xB0.

**Resultado medido, sobre las 281 acciones de evento del juego:**

| | antes | después |
|---|---|---|
| acciones sin sonido | 100 | **5** |
| acciones que matan la secuencia | — | **0** |

Las 22 secuencias de música emiten exactamente los mismos eventos que antes, y
el barrido de efectos sigue en 95 sonidos / 0 muertes. `pc_jam_test` vigila
ahora las tres cosas.

### Tres correcciones más de audio (2026-09-02, tarde)

**1. `duración 0` no es «esperar para siempre», es «esperar a que la nota
termine de sonar» — REGRESIÓN MÍA, CORREGIDA.** El arreglo anterior de la nota
de duración cero puso una espera infinita. El original hace otra cosa
(`jammain_2.c` ~2955):

    if (track->waitTimer == -1) {
        if (CheckNoteStop(track, 0)) track->waitTimer = 0;  // la voz 0 ya calló
        else goto timed;                                     // sigue esperando
    }

Es decir, un efecto de un disparo se cronometra solo: suena, espera a que la
muestra acabe, y sólo entonces sigue y avisa al juego de que ha terminado. Con
la espera infinita la pista se quedaba aparcada para siempre, **sin soltar su
ranura de evento**, y a los pocos sonidos dejaban de oírse cosas. Eso es lo que
dejó mudos el silbato y las bombas.

Para hacerlo bien hace falta que el mezclador avise: `PCJamPlayer::noteFinished`
y `reap_finished_voices` en `pc_audio.cpp`, que barre las voces de cada
reproductor antes de cada tic y notifica las que ya no suenan. Medido con el
final de muestra simulado: de **254 acciones aparcadas a 10**, y esas diez
sostienen a propósito hasta que se las cierra.

**2. La envolvente `SimpleADSR` no se aplicaba — causaba la estática de la
nave.** La nave (evento tipo 7, acción 4, comando 0x092) es un arpegio de tres
notas en bucle sobre el **programa 240**, que es un oscilador sintetizado, no
una muestra. Su manejador empieza con `SimpleADSR 1, 2, 10, 1000, 10`, que
`Osc_Setup_ADSR` (jamosc.c) convierte en dos plantillas de envolvente:

    ADS_TABLE = { 0, 0, 0x7FFF,  0, 0, 0x7FFF,  0, 0, 0,  14, 0, 0 }
    REL_TABLE = { 0, 10, 0,  15, 1, 0 }

parcheando los cinco argumentos en las posiciones 1, 4, 7, 8 (ataque) y 1
(caída). Con esos valores la nota decae a ~3% en diez tics: son destellos
suaves. El port ignoraba el opcode, así que cada nota sonaba a volumen máximo
durante toda su duración — un zumbido continuo. Ahora `PCJamEvent` lleva la
envolvente de su pista y el camino de osciladores la usa.

**Fallo grave que introduje al hacerlo, y ya corregido: un puntero colgante en
el hilo de audio.** `SampleVoice::oscillators` es un puntero crudo. Para los
instrumentos del banco eso es seguro porque el banco es inmutable y sobrevive a
toda voz —lo dice el propio comentario de `PCInstrumentSelection`—, pero yo le
pasé un puntero a un vector **dentro del reproductor JAM**, que se destruye al
cerrar la pista o al reiniciar el secuenciador. El mezclador se quedaba leyendo
memoria liberada: comportamiento indefinido, y voces de oscilador en bucle que
no paraban nunca. Es lo que hacía que la estática se quedase toda la partida.
Ahora la envolvente es un `shared_ptr` inmutable y la voz se queda con una
referencia mientras la use (`SampleVoice::oscillatorsOwned`).

**Lección, que es la misma de siempre en este proyecto:** antes de dar al hilo
de audio un puntero a algo, hay que preguntarse quién es el dueño y cuánto vive.
El banco de instrumentos podía; una pista del secuenciador no.

**El periodo del oscilador estaba dos octavas bajo — CORREGIDO, y era la
estática.** `Play_1shot_Osc` fija `basePitch = 16736.016 / JAC_DAC_RATE`. Esa
constante no es arbitraria: con una tabla de **64** muestras, la tecla 60 sale a
16736,016 / 64 = **261,5 Hz**, el do central, que es exactamente el ancla
musical que cabe esperar. El port usaba una tabla de 256, así que toda nota de
oscilador sonaba dos octavas por debajo.

Con eso, el zumbido de reposo del cohete (`SE_UFO_IDLING`, evento tipo 7 acción
4) caía a **49 Hz** en notas de 25 ms — apenas 1,2 ciclos por nota, repetidas 40
veces por segundo. Eso no es un tono, son chasquidos: exactamente lo que se oye
como estática. Con 64 muestras la misma nota sale a 196 Hz y da ~5 ciclos.

| tabla | tecla 60 | tecla 55 (el cohete) |
|---|---|---|
| 256 (antes) | 65 Hz | 49 Hz |
| **64 (ahora)** | **261,5 Hz** | **196 Hz** |

**El aviso de «acción terminada» — IMPLEMENTADO.** La pista por acción ejecuta
`SyncCPU` con el bit 0x8000 sobre su comando (`sysevent.jam` byte 231), y
`TrackReceive` lo traduce a `MML_StopEventAction`, que libera la ranura. El port
lo ignoraba, así que las dieciséis ranuras se llenaban y los sonidos posteriores
empezaban a desplazar a los vivos. Ahora `PCJamPlayer` deduce el par
(evento, ranura) del árbol de pistas —la pista de la acción es hija de la pista
del evento, que es hija de la raíz— y lo entrega por un gancho que limpia
`sEvents[].actions[]`. Medido: **231 de 281 acciones avisan**; las 50 restantes
son las sostenidas, que sólo terminan cuando se las cierra.

### La estática del cohete: era ruido blanco literal — RESUELTO (2026-09-02)

La sonda de eventos lo resolvió en una ejecución. Junto al cohete el juego pide
sin parar la acción 8 del tipo 7, que es `SE_UFO_LIGHT`, la luz que parpadea:

    [PC Audio] evento 0 tipo 7 accion 8 -> enviado (comando 0x0DA, ranura 1)

Ese comando emite dos notas de **banco 1, programa 247**. El programa 247 es
`0xF7`, es decir el **oscilador 7** — y en la tabla de formas de onda inventada
del port, los osciladores 6 y 7 eran **generadores de ruido blanco**:

    case 6: case 7:
        noise = noise * 1664525u + 1013904223u;
        sample = (s16)(noise >> 16) / 32768.0f;

Dos voces de ruido, **en bucle**, sin note-off, con el volumen subiendo despacio
(0,075 a los 300 tics) y re-disparadas cada vez que parpadea la luz. Eso es la
estática, y explica que creciera durante la partida.

**Y aun así volvió.** El pulso estrecho con el que sustituí el ruido es casi
igual de áspero, y la luz del cohete son **dos voces desafinadas a 46 Hz en
bucle** (tecla 30): eso zumba con cualquier forma de onda debajo. Tres intentos,
tres veces «estática» en la prueba en juego.

**Decisión: los programas de oscilador quedan mudos por defecto.** Todo lo demás
de este port se deriva del original; estas formas de onda no pueden derivarse, y
adivinarlas ha producido tres veces algo peor que el silencio. `PIKMIN_OSC=1`
vuelve a encender la aproximación para quien quiera experimentar. Cuesta 21 de
las 281 acciones de evento, el zumbido y la luz del cohete entre ellas.

Los osciladores 6 y 7 ya no son ruido, por si se reactivan. **Nada en la decompilación dice que
ninguno de los dieciséis sea ruido** —las formas reales viven en el microcódigo
del DSP, que no está aquí—, y el ruido es la única conjetura que no se puede
distinguir de un defecto: es literalmente lo que significa «estática». Si hay
que equivocarse en el timbre, que sea con un tono.

Nota para quien retome esto: el zumbido de reposo (`SE_UFO_IDLING`, acción 4,
oscilador 0) es otra cosa y ya estaba corregido por el periodo de 64 muestras.
Eran dos fallos distintos sonando en el mismo sitio.

### La sonda de eventos, mejorada

El tope plano de 64 líneas lo llenaba la luz del cohete antes de que ocurriera
lo que se investigaba. Ahora imprime **una línea por cada combinación
(tipo, acción, resultado)**, así que una ejecución da la cobertura completa sin
inundar nada.

### La estática del cohete, tercera vuelta: NO era el oscilador (2026-09-02)

Con los osciladores ya mudos por defecto, la traza lo confirma:

    [PC Audio]   nota de evento: banco 0 programa 247 tecla 30 vol 0.000 ... -> SIN VOZ

y **la estática seguía sonando**. Así que el ruido blanco del oscilador 7 no
era la causa —o no la única—, y las tres rondas anteriores persiguieron algo
que no era.

Descartado también, comprobando muestra por muestra, que sea alguno de los
sonidos de muestra del cohete tocando mal: `SE_UFO_RADER` es `banco 0 programa
24 tecla 60`, onda 335, **raíz 60 → factor de tono 1,000x**, 4,4 s a 16 kHz, sin
bucle. Las demás del entorno (programas 0, 7, 20) también tienen raíz 60 y
factores entre 0,56x y 1,26x. Ninguna se reproduce a velocidad disparatada.

**Sonda nueva, y esta vez la que hacía falta desde el principio.**
`pc_audio_report_levels` imprime ahora, junto a los picos por bus, **las tres
voces más fuertes del intervalo con nombre**:

    [PC Audio]   voz fuerte: pico 21430  banco 0 programa 24 tecla 60  bus 2 evento 0  EN BUCLE

Es decir: en vez de deducir qué suena, el mezclador lo dice. `SampleVoice` lleva
su pico y su etiqueta (banco/programa/tecla), que se ponen al asignar la voz.

**Lección de método, y van varias en este hilo:** llevo tres rondas acotando por
eliminación desde el lado de la secuencia, y el dato que zanjaba el asunto —qué
voz está sonando fuerte— no costaba más que las sondas que sí puse. Cuando el
síntoma es "se oye algo raro", la primera sonda debe ser la que nombra lo que
se oye.

### El "Whoo!" al 2026-09-03: el audio es fiel; el problema es *cuándo* se pide

Medición final, comparando el tiempo que cada acción retiene la ranura contra la
duración real de su muestra (programa 230, 16 kHz):

| acción | sonido | muestra | ranura retenida |
|---|---|---|---|
| 24 | BREAKUP | 90 ms | 85 ms |
| 25 | CALLED | 89 ms | 110 ms |
| 26 | FIND | 106 ms | 134 ms |
| 23 | **YATTA** | 470 ms | 467 ms |
| 22 | LAND | 637 ms | 628–642 ms |

**Coinciden.** El port retiene cada ranura exactamente lo que dura su sonido, ni
más ni menos. La maquinaria de grupos, prioridades y liberación es fiel.

**Por qué se pierde el "Whoo!", entonces.** Las seis voces de Pikmin comparten
grupo 1 con prioridad 0 y bandera 0x20: **una sola a la vez por evento**, y la
nueva se descarta. Al lanzar un amarillo a una bomba, el aterrizaje (`LAND`,
637 ms) ocupa la ranura, y el `FIND` y el `YATTA` llegan dentro de esa ventana y
se caen. En el log se ve literalmente:

    accion 22 -> enviado
    accion 26 -> DESCARTADA (prioridad)
    accion 23 -> DESCARTADA (prioridad)
    accion 22 -> TERMINADA tras 642 ms

El original haría lo mismo: se comprobó que su manejador de voces (byte 2124 de
`sysevent.jam`) también toca una nota de duración 0, que espera a que la voz
calle antes de devolver.

**Dónde está la diferencia, entonces:** en *cuándo* el juego pide el `YATTA`.
`ActPickItem::exec()` marca `PikiEmotion::Victorious`, pero esa emoción se
consume al **volver a la formación** (`aiAction.cpp:395/419/427`), segundos
después de coger la bomba — para entonces el aterrizaje hace rato que acabó. En
el port llega mientras el aterrizaje aún suena.

**O sea: el hilo se sale del audio.** Lo que queda por mirar es la máquina de
estados del Pikmin: por qué la transición a `PIKISTATE_Emotion` ocurre antes de
lo que debería.

**Ese terreno está planificado aparte, en `PLAN_MAQUINA_ESTADOS_PIKI.md`.** Ese
documento se abre en frío: lleva el mapa del código, lo ya descartado con
medición, cuatro pasos en orden —cada uno terminando en un dato, no en una
opinión— y lo que no hay que tocar. Empezar por ahí, no por aquí.

Descartado y comprobado por el camino, para no repetirlo:

- El banco de 64 pistas del secuenciador **no se agota**.
- `MAX_SOUND_EVENTS` y el radio de reutilización de 200 unidades son código de
  la decompilación, no invención del port.
- `MML_StopEventAll` (SyncCPU con 0x9000) es una reconciliación periódica que el
  port **no implementa**. Es red de seguridad, no la ruta principal. Pendiente.

### Grupos y prioridades de evento: la mitad que faltaba (2026-09-02)

La traza sin deduplicar lo destapó. `tipo 1 accion 23` —`SE_PIKI_ATTACK_VOICE`—
se enviaba **decenas de veces por segundo**, todas a la misma ranura, cada una
reiniciando la anterior, hasta dejar `eventos libres 0`.

Causa: el port elegía ranura con un "la primera libre" inventado.
`Jac_PlayEventAction` del original (pikiinter.c) usa las **otras tres columnas
de `ACTION_STATUS`**, que el port nunca transcribió:

- `flags & 0x10` — limita cuántas acciones del mismo **grupo** suenan a la vez;
  el tope es el nibble bajo.
- `flags & 0x20` — una petición que no gana la ranura por prioridad se
  **descarta**, en vez de robarla.
- `prio` y `group`, más una marca de tiempo por ranura para desempatar por
  antigüedad.

Con eso, `SE_PIKI_ATTACK_VOICE` tiene `flags 0x32`: máximo **2** simultáneas del
grupo 3, y las demás se tiran. El port las tocaba todas.

Las tres columnas están ya transcritas en `pc_event_commands.h` (282 filas,
generadas del decomp) y `Jac_PlayEventAction` sigue ahora el algoritmo original
paso a paso: conteo por grupo, robo por prioridad más baja y desempate por
antigüedad, y descarte cuando corresponde.

**Consecuencia que conviene tener presente:** `SEF_PIKI_YATTA` (23) y
`SEF_PIKI_FIND` (26) comparten **grupo 1**, misma prioridad y bandera 0x20. Es
decir, mientras una suena la otra se descarta — por diseño del juego. Por eso
importa que el aviso de "acción terminada" libere la ranura a tiempo: sin él, el
"Whoo!" de la bomba se perdía detrás del "¡eh!" de ir a buscarla.

### El "Whoo!" de los amarillos con la bomba: identificado (2026-09-02)

Búsqueda online confirmada: el sonido existe y es un "¡Whoo!" característico,
suficientemente conocido como para ser un meme en la comunidad Pikmin. No es
una impresión del usuario.

**Es `SEF_PIKI_YATTA`, acción 23 del evento tipo 6 — no la 26.** El disparador
está en `aiPick.cpp`, `ActPickItem::exec()`:

    if (mPiki->isHolding()) {
        mPiki->mActionState = 0;
        mPiki->mEmotion     = PikiEmotion::Victorious;
        return ACTOUT_Success;
    }

y `PikiEmotion::Victorious` (pikiState.cpp ~3276) toca `SEF_PIKI_YATTA` con la
animación `Jump_B1`. La emoción se consume al transitar a `PIKISTATE_Emotion`,
que ocurre al volver a la formación (`aiAction.cpp`).

Cadena en el port, verificada de punta a punta:

    tipo 6, accion 23  ->  comando 0x0B3
                       ->  nota banco 0, programa 230, tecla 19
                       ->  wsys 0, muestra 315: 7.526 muestras a 16 kHz (470 ms)

Y la traza en juego la muestra llegando al mezclador:

    evento 8 tipo 6 accion 23 -> enviado (comando 0x0B3, ranura 2, volumen 0.689)
      nota de evento: banco 0 programa 230 tecla 19 vol 1.000 -> voz asignada

**Y la prueba en juego confirmó que el juego SÍ la pide** al coger la bomba:
`evento 10 tipo 6 accion 23 -> enviado ... volumen 0.711`. La ruta
`Victorious` → `PIKISTATE_Emotion` funciona. Lo que la tapaba era la falta de
grupos y prioridades, arriba.

Nota: mi hipótesis anterior (`SEF_PIKI_FIND`, acción 26) era el sonido de
*decidir* ir a por algo, no el de *conseguirlo*. Los dos suenan y son
distintos.

**`PIKMIN_AUDIO_TRACE_ALL=1`** desactiva la deduplicación de la traza de
eventos, para poder ver la secuencia exacta en el instante en que se coge una
bomba en vez de sólo la primera aparición de cada acción.

### Registro de recursos: expulsión por heap (2026-09-01)

Al cambiar de guardado y volver a entrar en un nivel, el escenario no se
dibujaba: `memStat` daba `mapMgr → shape` en 0 KB y `GameFlow` leía 83 KB en vez
de 5.000. No era un fallo de render — el mapa no se cargaba.

Causa: el juego expulsa recursos del registro por **rango de direcciones**
(`StdSystem::invalidateObjs`), porque en GameCube las arenas eran contiguas. El
port sustituyó ese asignador por `malloc` (`sysNew.cpp`, `piki_pc_alloc`), así
que nada cae dentro de los límites de una `AyuStack` y la expulsión no hacía
nada, en silencio. `loadShape` seguía encontrando en caché una forma cuya
memoria había muerto con el heap.

Arreglo: `GfxobjInfo::mOwnerHeap` guarda el heap activo al registrar, y
`StdSystem::resetHeap` expulsa por ese índice antes de liberar. Afectaba a todo
recurso registrado (texturas, animaciones, formas), no sólo al mapa.

**Patrón a vigilar en el resto del port:** cuando una pieza de sistema se
sustituye, hay que buscar qué código dependía de la propiedad perdida. Van dos
casos hoy: la contigüidad de las arenas (aquí) y la persistencia del estado GX
entre display lists (el replay de 60 Hz, más abajo).

### 60 Hz: dos fallos encontrados y corregidos (2026-09-01)

**1. El ajuste encendía además un experimento de replay que corrompía el 3D.**
`System::run` (system.cpp ~310) usaba `pc_settings_get_fps_mode() == 1` para
activar la captura de display lists y `pc_gfx_replay_captured_frame()`. Ese
replay reejecuta las listas capturadas con el descriptor de vértices que haya
en ese momento, no con el vigente al capturarlas: se recorrían 9 bytes por
vértice donde el dato tiene 11, los índices salían de campos vecinos (64768
sobre un array de posiciones) y la geometría se estiraba por toda la pantalla.
Sólo el 3D; el HUD 2D no pasa por ahí.

Ese replay **no daba 60 FPS** — repinta el mismo frame. Ahora vive detrás de
`PIKMIN_REPLAY_TEST=1`, apagado por defecto, y `fpsMode` sólo controla
`setFrameClamp(1)`. Si alguna vez se retoma la interpolación de frames, el
defecto a arreglar es que el replay restaure el descriptor por paquete.

**2. Las cinemáticas iban al doble de velocidad.** `cinePlayer.cpp:477` hacía
`mCurrentPlaybackTime += 1.0f` por tick, y ese tiempo se cuenta en frames de
cine autorados a 30 Hz. Ahora avanza `gsys->getFrameTime() * 30.0f`, que da
exactamente 1,0 por tick a 30 Hz y conserva el timing original.

Cómo se encontró el primero, porque el método importa: cinco sondas descartaron
causas benignas (PNMTXIDX fuera de rango, desincronizado del parser, caché del
descriptor) antes de que un `backtrace()` en el primer vértice imposible
nombrara la ruta culpable en un intento. Cuando la contradicción entre el
código y la pantalla no se resuelve leyendo, hay que preguntarle al programa.

Aviso de método: el motivo de haber tardado tanto fue un `grep -v pc_settings`
al buscar quién leía `pc_settings_get_fps_mode`, que filtró justamente las
líneas buscadas y llevó a concluir, en falso, que el ajuste no tocaba el logo.

### Interruptor de 60 Hz — cableado el 2026-09-01

La opción «60 FPS (experimental)» del menú F1 ya existía y se persistía en
`pikmin_settings.conf`, pero `pc_settings_get_fps_mode()` no lo leía nadie: el
ajuste no hacía nada. Ahora alimenta el clamp de gameplay en
`newPikiGame.cpp:2852`. Apagado por defecto.

**El escalado de tiempo se cuida solo.** `mDeltaTime` (`system.cpp:382`) se mide
del reloj real y sólo está topado a 1/30, así que a 60 Hz pasa a ~1/60 sin
tocar nada. Todo lo que use `getFrameTime()` escala bien.

**Contadores por frame en bruto — auditoría hecha, dos afectan al juego:**

- `naviState.cpp:431` `mEscapeAttemptCounter` — escapar al quedar enterrado.
  Cuenta eventos de entrada, no frames, pero el sondeo al doble de ritmo
  registra más sacudidas del stick: **escapar se vuelve más fácil**.
- `aiCrowd.cpp:398` `mNearSlotCounter` — tope 6, incrementado por frame en una
  banda de distancia. Alcanza el tope en la mitad de tiempo real; afecta a
  cuándo los Pikmin recolocan su formación.
- `aiBreakWall.cpp:209` `mFailAttackCounter` e `itemAI.cpp:97` `mSAICtx.mCounter`
  se revisaron y no son por frame: cuentan intentos y acciones de estado.

Ninguno es fatal, y por eso el ajuste se queda como experimental en vez de
bloquearse. **Lo que falta es comparar en juego contra la referencia a 30 Hz:**
altura del salto, alcance del lanzamiento de Pikmin y comportamiento de los
enemigos. La integración de Euler no es invariante de escala y el RNG se
consume al doble de velocidad, así que las diferencias, si las hay, saldrán
ahí.

## 6. Variables de diagnóstico disponibles

- `PIKMIN_AUDIO_STATS=1` — picos por bus, ganancias, línea `DROPPED` con las
  cuatro formas de perder un sonido, volcado de ids de sistemas de onda, y la
  traza de opcodes JAM al morir una secuencia (topada a 8 informes).
- `PIKMIN_AUDIO_STATS=1` traza además cada acción de evento de gameplay y las
  tres voces más fuertes de cada intervalo.
- `PIKMIN_AUDIO_TRACE_ALL=1` quita la deduplicación de esa traza.
- `PIKMIN_OSC=1` — enciende la aproximación de los programas de oscilador
  (0xF0+), muda por defecto: su forma de onda no está en la decompilación.
- `PIKMIN_BATCH=0` — desactiva la agrupación de primitivas; A/B para cualquier
  defecto visual del que se sospeche del batcher.
- `PIKMIN_REPLAY_TEST=1` — enciende el experimento de replay de frames. **Rompe
  el 3D**; ver el apartado de 60 Hz.

- `PIKMIN_TICK_STATS=1` — coste de CPU por tick, desglosado (lo de arriba).
- `PIKMIN_PERF_STATS=1` — tiempos de GPU por consultas de temporizador.
- `PIKMIN_TEV_SPECIALIZE=0` — vuelve al über-shader.
- `PIKMIN_TEV_MAX_STAGES=N`, `PIKMIN_GL_CHECK=1`, `PIKMIN_DUMP_SHADERS=1`,
  `PIKMIN_NO_UNIFORM_CACHE=1`.

## 7. Documentación relacionada

- `REFERENCIAS_EXTERNAS.md` → proyectos de fuera ya evaluados (Aurora) con la
  conclusión y el motivo, para no reevaluarlos desde cero.

- `ROADMAP.md` → PERF-NATIVE-002 y la sección «Medición de CPU por tick».
- `AI_HANDOFF.md` → bloque «Estado al 2026-09-01».
