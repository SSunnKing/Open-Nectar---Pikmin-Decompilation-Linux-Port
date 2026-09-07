# Plan: temporización de la máquina de estados de los Pikmin

Documento de planificación escrito el 2026-09-03. Está pensado para abrirse en
un chat vacío, sin depender del historial: contiene el problema, lo que ya está
descartado por medición, el mapa del código y el orden de trabajo propuesto.

El caso de entrada es un sonido que no se oye, pero **el hilo no es de audio**.
Se llegó hasta aquí depurando audio y el audio quedó descartado con datos; lo
que queda es *cuándo* la lógica del juego pide las cosas.

---

## 0. DESCARTADO POR MEDICIÓN (2026-09-04): no es la máquina de estados

**Este plan partía de una hipótesis falsa. La ruta y la temporización del juego
son correctas; el fallo está aguas abajo, en el mezclador.**

Traza en juego, dos capturas independientes del mismo lanzamiento a una bomba:

    reloj=3589294  evento 6 tipo 6 accion 22 -> enviado          (LAND)
      nota de evento: banco 0 programa 230 tecla 18 -> voz asignada
    reloj=3589925  accion 22 -> TERMINADA tras 631 ms
    peak  bgm=7493  se= 9992                                     <- el LAND SUENA

    [PC Piki] reloj=3591711 emocion 6 -> PIKISTATE_Emotion por doJoinParty
    reloj=3591711  evento 6 tipo 6 accion 23 -> enviado          (YATTA)
      nota de evento: banco 0 programa 230 tecla 19 -> voz asignada
    reloj=3592185  accion 23 -> TERMINADA tras 474 ms
    peak  bgm=5805  se=    0                                     <- el YATTA NO

Lo que esto cierra, y no hay que volver a investigar:

- **La emoción llega tarde y de sobra, no pronto.** El `YATTA` se pide 1,8 s
  después de que el `LAND` haya liberado la ranura. La hipótesis de la sección 3
  —que el `YATTA` caía dentro de la ventana de 637 ms del aterrizaje— es falsa.
- **No se descarta: se envía.** Gana ranura, y el saltito `Jump_B1` se ve en
  pantalla, así que `PikiEmotionState::init` corre entero.
- **Se le asigna voz.** No es un fallo de resolución de instrumento ni de
  muestra.
- **La rama que consume la emoción es `doJoinParty`** (`aiAction.cpp:395`), que
  es la que el plan daba por correcta. El paso 1 queda respondido.
- **La muestra está intacta.** Decodificada offline con los propios cargadores
  del port: onda 315, 7526 muestras a 16 kHz, pico 31911, media 2462. La onda
  314 (`LAND`, que sí suena) da pico 32233 y media 2637. No se distinguen.

O sea: voz asignada, muestra buena, ranura libre, y el bus SE da **cero** en la
ventana entera. El fallo está entre `play_wave_info` y el mezclador.

Sospecha principal a comprobar en la siguiente ejecución: el volumen real de la
nota es `region.volume * instrumentVolume * (velocidad/127)²`, y la traza
imprimía el volumen *de pista*, no la velocidad. Una velocidad baja deja la voz
activa y muda. La traza ya imprime `vel`, `izq`/`der`, `paso` y `pcm`.

Herramientas añadidas en esta sesión (todas bajo `PIKMIN_AUDIO_STATS=1`):

- `PIKMIN_AUDIO_TRACE_TYPE=6` — filtra la traza por tipo de evento JACEVENT. Sin
  él, la ambientación del UFO mete ~240 líneas por segundo y entierra todo. Los
  fallos (tipo 0) salen siempre, con filtro o sin él.
- `reloj=` en cada línea, y al descartar, **quién ocupa la ranura y desde hace
  cuántos ms**. Sin marca de tiempo el orden de las líneas no distingue "llegó
  30 ms tarde" de "llegó medio segundo tarde", que era justo la pregunta.
- `PERDIDO (sin handle de evento)` en `SeContext::playSound`: un sonido que no
  consigue handle no llegaba a `Jac_PlayEventAction` y desaparecía **sin ninguna
  línea**, indistinguible de que el juego no lo pidiera.
- La traza de nota imprime ahora los parámetros finales de la voz: `vel`, `paso`,
  `base`, `pistaTono`, `izq`, `der`, `pcm`, `fin`.

Lo de abajo se conserva porque el mapa de código y la lista de callejones
cerrados siguen siendo válidos. La hipótesis de la sección 3 y el orden de
trabajo de la sección 4 **no**.

---

## 1. El caso concreto

Al lanzar un Pikmin amarillo sobre una bomba, el juego original suelta un
"¡Whoo!" característico. En el port no se oye.

El sonido es `SEF_PIKI_YATTA`, y la cadena es:

    ActPickItem::exec()            aiPick.cpp:85
      -> mPiki->mEmotion = PikiEmotion::Victorious
    ActAction::exec()              aiAction.cpp:395 / 419 / 427
      -> mFSM->transit(piki, PIKISTATE_Emotion)
    PikiEmotionState::init()       pikiState.cpp:3182
      -> case PikiEmotion::Victorious: playPikiSound(SEF_PIKI_YATTA)

## 2. Lo que ya está descartado, con medición

No repetir nada de esto. Todo se comprobó durante la sesión del 2026-09-02/03.

**El audio reproduce el sonido cuando se lo piden.** La traza en juego muestra
`accion 23 -> enviado`, la nota resuelve (`programa 230 tecla 19`, muestra 315,
470 ms) y recibe voz.

**La maquinaria de ranuras es fiel al original.** El tiempo que cada acción
retiene su ranura coincide con la duración de su muestra:

| acción | sonido | muestra | ranura retenida |
|---|---|---|---|
| 24 | BREAKUP | 90 ms | 85 ms |
| 25 | CALLED | 89 ms | 110 ms |
| 26 | FIND | 106 ms | 134 ms |
| 23 | **YATTA** | 470 ms | 467 ms |
| 22 | LAND | 637 ms | 628–642 ms |

**El descarte es la regla del juego, no un fallo.** Las seis voces de Pikmin
comparten grupo 1, prioridad 0 y bandera 0x20 en `ACTION_STATUS`: sólo suena una
a la vez por evento y la nueva se descarta en vez de interrumpir. El manejador
del original (`sysevent.jam` byte 2124) también toca una nota de duración 0, que
espera a que la voz calle antes de devolver.

**Otros callejones cerrados:** el banco de 64 pistas del secuenciador no se
agota; `MAX_SOUND_EVENTS` y el radio de reutilización de 200 unidades son código
de la decompilación; las ranuras sí se liberan en juego.

## 3. La hipótesis

En el original la emoción `Victorious` se consume al **volver a la formación**
(`aiAction.cpp`, rama `doJoinParty`, línea 395), segundos después de coger la
bomba. Para entonces el sonido de aterrizaje (`LAND`, 637 ms) hace rato que
terminó y la ranura está libre.

En el port el `YATTA` llega **mientras el aterrizaje aún suena** y se descarta.
En el log se ve literalmente:

    accion 22 -> enviado
    accion 26 -> DESCARTADA (prioridad)
    accion 23 -> DESCARTADA (prioridad)
    accion 22 -> TERMINADA tras 642 ms

Es decir: los tres sonidos del mismo lanzamiento —aterrizar, decidir, conseguir—
caen dentro de una ventana de 637 ms, cuando deberían estar repartidos.

**Hay dos ramas que consumen la emoción y llevan a sitios distintos:**

- `aiAction.cpp:395` — rama `doJoinParty`: el Pikmin vuelve a la formación.
- `aiAction.cpp:427` — rama `else`: `actOnSituaton()` y emoción **inmediata**.

Saber cuál de las dos se toma en el port es la primera pregunta del plan.

## 4. Orden de trabajo propuesto

Cada paso termina en un dato, no en una opinión. Es la lección que este proyecto
ya ha pagado varias veces: instrumentar antes que teorizar.

### Paso 1 — ¿Por qué rama sale?

Sonda en `aiAction.cpp` que imprima, al consumir una emoción, cuál de las tres
llamadas a `transit(PIKISTATE_Emotion)` se ejecutó, con la emoción y el modo del
Pikmin. Una línea por transición, topada.

**Decide el resto del plan:** si sale por la rama 427 (inmediata) cuando el
original saldría por la 395 (al volver a la formación), la causa está en
`doJoinParty` y el paso 2 se centra ahí. Si sale por la misma rama que el
original, el problema es de *duración*, no de ruta, y salta al paso 3.

### Paso 2 — La condición `doJoinParty`

`aiAction.cpp` alrededor de la línea 370 decide `doJoinParty` comparando
`qdist2(mPiki->mNavi, mPiki)` contra `mPostWorkJoinPartyRange`. Comprobar:

- Que el parámetro se lee bien del `.bin` correspondiente (comparar el valor en
  ejecución contra el del archivo).
- Cuidado con `qdist2`: **el nombre engaña y hay dos funciones distintas con
  él**. La que se usa aquí, `qdist2(Creature*, Creature*)`
  (`creature.cpp:1238`), devuelve la distancia 2D **real** —hace `sqrtf`—, no
  la distancia al cuadrado. La otra, `qdist2(f32,f32,f32,f32)`
  (`sysMath.cpp:497`), es una aproximación distinta. Si alguien "optimiza" la
  comparación quitando la raíz, el Pikmin volvería a la formación siempre o
  nunca. Comprobar que el port usa la primera.

### Paso 3 — Temporización a 30 y 60 Hz

Si la ruta es correcta, medir **cuántos milisegundos reales** pasan entre el
mensaje de aterrizaje y la transición a `PIKISTATE_Emotion`, y comparar con los
637 ms que dura el sonido de aterrizaje. Hacerlo con el ajuste de 60 FPS
encendido y apagado: si el retardo depende del modo, es un contador por frame.

Ya hay una auditoría previa de contadores por frame en `SIGUIENTE_SESION.md`
(sección de 60 Hz) con dos casos confirmados que afectan al juego
(`naviState.cpp:431` y `aiCrowd.cpp:398`). Ésa es la lista de la que partir.

### Paso 4 — Comparación con el original

Si los pasos anteriores no cierran, la referencia es la decompilación misma:
seguir `ActPickItem` → `AndAction::exec` → `ActAction::exec` y confirmar que el
port ejecuta la misma secuencia de acciones hijas en el mismo orden. Toda esta
lógica es código de la decompilación y debería ser idéntica; una diferencia ahí
sería un fallo de integración del port, no de diseño.

## 5. Qué NO hacer

- **No tocar la lógica de grupos y prioridades del audio.** Está verificada
  contra `pikiinter.c` y arregló un atasco real (las voces de ataque llegaban
  decenas por segundo). Cambiarla para "dejar pasar" el `YATTA` sería romper una
  cosa correcta para tapar otra.
- **No modificar el código de la decompilación por conveniencia.** Si la
  temporización difiere, la causa estará en lo que el port sustituyó (reloj,
  frecuencia de tick, física), no en la lógica de juego.
- **No dar por bueno un arreglo sin medir antes y después.** En este proyecto,
  contar eventos no basta: hay que comparar el flujo completo. Un cambio que
  "no altera el número de notas" ya cambió una vez el banco de toda la música.

## 6. Herramientas ya disponibles

- `PIKMIN_AUDIO_STATS=1` — traza cada acción de evento con su resultado
  (`enviado`, `DESCARTADA (prioridad)`, `DESCARTADA (grupo lleno)`,
  `TERMINADA tras N ms`), volumen, paneo y ranura; más las tres voces más
  fuertes de cada intervalo con nombre de banco/programa/tecla.
- `PIKMIN_AUDIO_TRACE_ALL=1` — quita la deduplicación de esa traza, para ver la
  secuencia exacta de un instante.
- `PIKMIN_TICK_STATS=1` — coste de CPU por tick, desglosado.
- Desensamblador de secuencias JAM en el scratchpad de la sesión anterior;
  reconstruye la tabla `Arglist` y lee `pikise.jam` y `sysevent.jam` como código.
  Si hace falta rehacerlo, la tabla está en `pc_port/audio/pc_event_commands.h`.

## 7. Reglas de trabajo del proyecto

- El usuario ejecuta y prueba el juego; el agente no. Las mediciones llegan
  porque él lanza el binario y pega el log. **Diseñar las sondas para que una
  sola ejecución dé la respuesta**, porque cada iteración le cuesta una partida.
- Antes de cada cambio grande: `tools/snapshot.sh save <nombre>`. El usuario no
  usa git; los snapshots son el versionado real.
- Compilar y probar: `cmake --build build -j$(nproc)` y `ctest --test-dir build`
  (14/14 deben pasar).
- Nada de ROMs ni assets en el repositorio (ver LEGAL.md).

## 8. Referencias externas

`REFERENCIAS_EXTERNAS.md` recoge los proyectos de fuera ya evaluados, con la
conclusión y el motivo. El único hasta ahora es **Aurora**, una capa de
compatibilidad GX para decompilaciones: útil de leer cuando aparezca un fallo de
traducción gráfica, descartada como dependencia por ahora y con las condiciones
bajo las que reconsiderarla escritas allí. **No cubre audio.**

## 9. Pendiente relacionado, por si sale al paso

`MML_StopEventAll` (pikiinter.c:563), que el port no implementa: cada pista de
evento publica su máscara de hijas vivas cada ~1000 tics y el juego libera las
ranuras que no estén en ella. Es una red de seguridad frente a ranuras colgadas,
no la ruta principal, pero si aparecen ranuras que no se liberan, es esto.
