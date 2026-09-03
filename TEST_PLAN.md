# Pikmin PC Port — Pruebas manuales

Estas pruebas las ejecuta el usuario. Codex prepara las builds, analiza los
resultados y no inicia el juego. Para evitar mezclar fallos, se debe detener el
recorrido en el primer error grave o crash y guardar todo el log de terminal.

## Información que debe acompañar cada prueba

- ID de la prueba y build utilizada (`build` o `build-sanitize`).
- Resultado: `PASA`, `FALLA` o `BLOQUEADA`.
- Último paso completado y primer síntoma incorrecto.
- Log completo desde el arranque hasta cerrar o crashear.
- Captura si el problema es visual.

## RUN-001 — Arranque y título

1. Ejecutar `./build/bin/pikmin` desde la raíz del proyecto.
2. Esperar hasta que el título esté completamente visible.
3. Pulsar Enter una vez y comprobar la transición del título.

Comprobar: ventana única, fondo y título visibles, entrada aceptada una sola
vez, ausencia de pantalla negra y de errores de shader en el log.

## RUN-002 — Selector de archivo e inicio

1. Continuar desde RUN-001.
2. Abrir el selector de archivo.
3. Recorrer los slots y comenzar una partida.

Comprobar: paneles, texto, cursor y fondos visibles; selección coherente; sin
pantallas negras ni cierres al cambiar de estado.

## RUN-003 — Cinemática inicial completa

1. Iniciar una partida nueva y no pulsar teclas durante la cinemática.
2. Observar nave, Olimar, planeta, estrellas, partículas y movimiento de cámara.
3. Esperar a que el juego entregue el control.

Comprobar: modelos presentes durante toda su animación, materiales iluminados,
transparencias sin rectángulos negros/blancos y transición a gameplay sin crash.

## RUN-004 — Cinemática inicial saltada

1. Iniciar otra partida nueva.
2. Pulsar Enter una sola vez durante la cinemática.
3. Esperar a que aparezca gameplay.

Comprobar: salto único y limpio, mismo estado jugable que RUN-003, sin recursos
colgantes, pantalla negra ni crash. Si el salto aún no está disponible, marcar
la prueba como `BLOQUEADA`.

## RUN-005 — Primer nivel y cebolla

1. Avanzar desde el aterrizaje hasta la cebolla.
2. Completar su animación y obtener el primer Pikmin.
3. Caminar y mover la cámara alrededor de Olimar, nave y cebolla.

Comprobar: cámara estable sobre el terreno; modelos, objetos y vegetación
presentes; iluminación no plana; cebolla y Pikmin funcionales; sin crash.

## RUN-006 — Flor y primera pastilla

1. Lanzar el Pikmin contra la flor que contiene la pastilla.
2. Romper la flor y observar la aparición de la pastilla.
3. Ordenar su transporte hasta la cebolla y esperar a que termine.

Comprobar: la pastilla no desaparece al romperse la flor, mantiene modelo y
colisión, puede transportarse y genera el resultado esperado.

## RUN-007 — Recarga y cambio de estado

Resultado parcial más reciente (26-08-2026): **PASA hasta el segundo nivel**.
El usuario completó el primer nivel, guardó, cargó la partida, continuó hasta
el segundo nivel y venció varios enemigos. No valida todavía una segunda
recarga, niveles posteriores ni la completabilidad de extremo a extremo.

1. Cerrar normalmente y volver a abrir el juego.
2. Cargar el archivo creado.
3. Cuando el progreso lo permita, completar un día o cambiar de nivel.
4. Repetir la carga una segunda vez.

Comprobar: guardado persistente, recursos recreados correctamente, memoria y
estado no heredados de la sesión anterior, sin degradación ni crash.

## RUN-008 — Rendimiento y cadencia

1. Usar siempre la build normal `build`, no la build sanitizer.
2. Permanecer al menos diez segundos en título, cinematográfica y gameplay.
3. Guardar las líneas `[PC Port] FPS` de cada tramo.
4. Comparar movimiento de Olimar, silbato, cámara de despertar, cebolla y
   Pikmin con una referencia de gameplay real.

Comprobar: título/modos de intervalo 1 no superan su cadencia lógica de 60 Hz;
película y gameplay se mantienen cerca de 30 FPS cuando la carga lo permite;
la frecuencia del monitor no acelera la simulación. Gameplay y película deben
usar escala temporal 1.0. Una lectura habitual de gameplay en el objetivo es
`FPS: 29.9, DeltaTime: 33.3333ms`.

## RUN-009 — Audio cinematográfico tras introducir el mixer

Resultado más reciente (26-08-2026): **PASA**. El stream continúa después de
que el cohete llegue a la Tierra.

1. Iniciar una partida nueva con la build normal.
2. Escuchar la cinemática inicial completa.
3. Dejar que termine y llegue a gameplay.

Comprobar: `opening.stx` continúa al pasar del cohete en el espacio a la llegada
a la Tierra y se oye hasta el final, sin silencio, repetición, chasquidos ni
cierre al terminar. Esta prueba sólo valida
la regresión del nuevo mixer; BGM y SFX permanecerán bloqueados hasta conectar
el secuenciador JAudio.

## RUN-010 — Regresión de audio tras restaurar la configuración estable

1. Iniciar la build normal desde el directorio raíz del proyecto.
2. Crear o cargar partida y reproducir la apertura.
3. Dejar que el cohete llegue a la Tierra.

Comprobar: se vuelve a oír `opening.stx`, continúa entre las dos partes de la
apertura y no aparecen cierres ni bloqueos. Menús y gameplay seguirán sin BGM o
SFX hasta completar AUDIO-002/AUDIO-003; esta prueba sólo confirma la
restauración del comportamiento que ya estaba validado. Al arrancar deben
aparecer exactamente una vez los diagnósticos de catálogo con `22 WSYS, 882
waves` y `22 sequences`; su ausencia indica que la base nativa no cargó.

## RUN-011 — Voces instrumentales y liberación nativa

Estado: **LISTA PARA PRIMERA VALIDACIÓN**. AUDIO-002C ya conecta eventos JAM
con `pc_audio_play_note` y `pc_audio_release_wave`; RUN-012 acota primero la
comprobación de escenas BGM.

1. Entrar en la primera pantalla o nivel que use una secuencia JAM conectada.
2. Escuchar al menos treinta segundos y provocar varios cambios de pantalla o
   acciones que inicien y terminen notas.
3. Repetir el tramo prestando atención al final de notas sostenidas.

Comprobar: las notas empiezan afinadas, las sostenidas no se cortan antes de
tiempo y el note-off produce una caída limpia, sin chasquido. No deben quedar
voces colgadas tras abandonar la pantalla, ni silenciarse voces nuevas por un
handle antiguo. Registrar `pc_audio_get_active_voice_count()` y el contador de
clipping cuando se conecten ambos a un diagnóstico visible.

## RUN-012 — Primera reproducción BGM nativa JAM

1. Iniciar la build normal y permanecer al menos veinte segundos en el título.
2. Entrar al selector de archivo y permanecer otros veinte segundos.
3. Cargar una partida y comprobar el mapa y un nivel durante treinta segundos.
4. Guardar el log desde el arranque hasta cerrar normalmente.

Comprobar: aparecen líneas `Native JAM BGM started` con `jungle.jam`,
`select.jam`, `map.jam` y la pista correspondiente al nivel; se oye música en
cada escena, continúa a tempo estable y cambia sin dejar la pista anterior.
Registrar cualquier línea `First unresolved JAM note` o `JAM sequence stopped`.
Esta prueba no valida todavía efectos de sistema/gameplay ni fidelidad completa
de volumen, panorama o capas adaptativas.

## Plantilla de resultado

```text
Prueba: RUN-___
Build: build / build-sanitize
Resultado: PASA / FALLA / BLOQUEADA
Último paso correcto:
Primer síntoma incorrecto:
Log adjunto: sí / no
Captura adjunta: sí / no
Observaciones:
```
