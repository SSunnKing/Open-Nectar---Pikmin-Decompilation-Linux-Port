# Pikmin Native Linux PC Port — Handoff operativo

Última actualización: 2026-09-01

## Configuración de referencia alcanzada (2026-09-01)

**1080p nativo (`renderScale = 1`), 60 FPS en menús, 30 FPS en gameplay, sin
fallos gráficos.** Validado in-game por el usuario sobre GTX 1050 4 GB con
Ubuntu y Mesa 26.0. Este es el estado que debe conservarse; véase la
configuración de referencia al principio de `ROADMAP.md`.

Los 60/30 no son una limitación del port: son los `setFrameClamp(1)` de
títulos, selección de datos y mapa, y el `setFrameClamp(2)` de `newPikiGame`,
es decir el comportamiento original del juego. **El objetivo del usuario ya no
incluye 60 FPS en gameplay**, lo que deja TIME-002 fuera del camino crítico y
retira la presión de la ruta de captura/reproducción.

Lo que lo hizo posible fue PERF-NATIVE-003, la especialización de pipelines
TEV: el fragment shader interpretaba la configuración TEV por píxel y hundía la
ocupancia. Ahora se genera un programa por configuración (15 distintas en todo
el recorrido). GPU de escena ~3,7-5,9 ms a 1080p nativo frente a ~55 ms de
título y ~32-34 ms de gameplay que costaba antes **a media resolución**.

Lección de método, por si se repite un fallo parecido: cuatro intentos se
perdieron razonando sobre el código para explicar colores erróneos, cuando
había un `GL_INVALID_OPERATION` señalando el problema desde el principio. Lo
resolvió comprobar el error **en cada escritura de uniform** y hacer que el
código nombrase el uniform dueño de la localización rechazada. Un
`glUniform1i` sobre un `vec4` sólo puede ocurrir si la localización no
pertenece al programa enlazado. Instrumentar antes que teorizar.

Red de seguridad: `pc_port/` no está en git. Los snapshots de fuentes
(`tools/snapshot.sh`) son el mecanismo real de vuelta atrás; el de la
configuración de referencia es `snapshots/20260901-185758-estable-60-30.tar.zst`.

Diagnóstico disponible: `PIKMIN_TEV_SPECIALIZE=0` (vuelve al über-shader),
`PIKMIN_TEV_MAX_STAGES=N`, `PIKMIN_GL_CHECK=1`, `PIKMIN_DUMP_SHADERS=1`,
`PIKMIN_NO_UNIFORM_CACHE=1`, `PIKMIN_PERF_STATS=1` (GPU),
`PIKMIN_TICK_STATS=1` (CPU por tick).

`PIKMIN_TICK_STATS=1` mide el coste de CPU de un tick repartido en `update`
(lógica), `renderall` (construcción de listas GX) y `doneRender` (envío y
presentación), y contrasta el total contra el presupuesto de 60 Hz (16,6 ms).
Las tres partes se ejecutarían el doble de veces en un gameplay a 60 Hz, así
que ese total es la respuesta a si 60 FPS en juego es viable. Da media, p50,
p95, p99, peor caso y el porcentaje de ticks que ya no caben: el p99 importa
tanto como la media, porque un tick medio de 9 ms con picos de 24 ms no es un
tick de 60 Hz sino uno que tartamudea. `waitRetrace` queda fuera de la medida a
propósito: es la espera, no el trabajo.

## Estado al 2026-09-01: dónde retomar

Medido y cerrado: el coste de CPU por tick. La lógica del juego (`update`) es
**0,01 ms** — descartada como obstáculo para los 60 FPS. Todo el coste está en
`renderall`: 13 ms de mediana, 33 ms en p95, con **8.300 draws por frame de 9
vértices cada uno**. La GPU, en 3,7–5,9 ms, está ociosa.

Siguiente tarea, ya planificada en detalle en ROADMAP → «Medición de CPU por
tick» y PERF-NATIVE-002: **agrupar primitivas consecutivas en lotes**. Empezar
por la sonda de longitud de racha antes de escribir nada: toda la ganancia
depende de que el juego emita rachas con el mismo estado, y comprobarlo es
barato. Los puntos de vaciado del lote son el riesgo real del trabajo.

Snapshot de esta build medida:
`snapshots/20260901-191759-medicion-cpu-2b.tar.zst`.

## ALERTA 60 FPS — último intento fallido y retirado

La última activación de presents intermedios volvió a ejecutar `renderall()`
con los runtimes de cámara y pose habilitados. En la prueba in-game la imagen
quedó en blanco y negro y aparecieron bugs gráficos. El usuario hizo rollback
y recuperó el juego funcional a 30 FPS. **No reactivar ni reconstruir ese
parche.** El registro completo de los tres intentos, sus síntomas, causas y la
arquitectura recomendada está en `60FPS_FAILED_ATTEMPTS.md`.

La próxima ruta recomendada es capturar una lista inmutable de comandos en la
frontera GX -> OpenGL durante el único render autoritativo y reproducirla desde
el backend, sin volver a entrar en `renderall/draw/refresh`. Antes de interpolar
nada debe probarse que reproducir dos veces el mismo paquete es visualmente
idéntico y no modifica el estado del juego. Inspeccionar siempre el código
actual: el rollback puede haber retirado también parte de la integración del
scheduler que las notas históricas inferiores describen.

## Estado histórico anterior (no asumir que sigue activo tras el rollback)

Avance TIME-002 (2026-08-31): existe ya una base offline de snapshots visuales
en `pc_port/timing/pc_visual_snapshot.*`. Usa identidad estable
`(owner persistente, dominio, slot semántico)`, copia matrices y conserva
previous/current/presentation; soporta altas, bajas, teleports y sincronización
de transiciones. `pc_visual_snapshot_test` cubre esas reglas y la build Release
completa más CTest 8/8 pasan. No está conectada todavía a `BaseShape` y no se
habilitaron presents extra. Ya se extrajo, sin cambiar comportamiento, la
frontera `updateFixed()` / `renderAuthoritativeFrame()`, todavía con una llamada
de cada por tick. La auditoría demostró que `BaseShape*` no es identidad válida:
los Pikmin comparten modelo y sobrescriben el mismo buffer por criatura. La
clave deberá ser `(owner visual persistente, índice)`, recibida desde el actor.
El render actual aún avanza poses, materiales y fades; no volver a llamar
`draw()`, `animate()` o `updateAnim()` para fabricar frames.

Unidad posterior TIME-002 (2026-08-31): `PcRenderPhase` distingue tick
autoritativo y presentación mediante un serial que sólo avanza con lógica y un
alpha acotado para render. `System::run` entra en autoritativo antes de
input/audio. Ya están protegidos en fase presentación el avance de
`BaseShape::updateAnim`, las animaciones PVW de color/textura/TEV y los
timers/fades globales de `PlugPikiApp::draw`. La fase presentación permanece
sin uso hasta cubrir el resto de refresh/partículas y pasar owner por actor.
Build completa y CTest 9/9 correctos; no se ejecutó el juego.

Unidad posterior TIME-002 (2026-08-31): las 37 llamadas a
`BaseShape::updateAnim()` entregan ahora la identidad persistente del actor,
criatura, objeto, mapa, efecto o actor cinematográfico. `BaseShape` conserva
ese owner host-only junto al buffer compartido. `PcVisualRuntime` captura las
matrices en `drawshape/drawculled`, después de overrides, y su test cubre dos
actores con varias matrices y orden invertido. El runtime está desactivado por
defecto para no introducir coste por hueso antes de habilitar presents y aún no
reemplaza matrices del juego. Build completa y CTest 10/10 correctos; no se
ejecutó el juego.

Unidad posterior TIME-002 (2026-08-31): `PcVisualShapeOverride` aplica una pose
interpolada sólo en Presentation, exige todos los slots y restaura por RAII el
buffer autoritativo; el test incluye ausencia del último slot para demostrar
que no hay escritura parcial. `AnimContext`, `Animator`, `PaniAnimator` y los
coordinadores de Pikmin/pellet/nave tampoco avanzan frames o eventos durante
Presentation. Todo permanece apagado en ejecución normal. Build completa y
CTest 10/10 correctos; pendientes partículas, cámara y demás escrituras de
refresh antes de presentar frames reales.

Unidad posterior TIME-002 (2026-08-31): `PcCameraSnapshotStore` modela la
cámara por position/focus/up, FOV, aspect, near/far y blur. Interpola por
identidad estable, normaliza up, no mezcla gameplay con movie camera y admite
cortes sin blend. Rechaza estados no finitos o proyecciones inválidas.
`PcCameraRuntime` ya captura desde `DGXGraphics::setCamera`, reconstruye
look-at/ejes/frustum para Presentation y restaura la cámara completa al final
del frame. Permanece desactivado por defecto junto con los presents extra.
Build completa y CTest 11/11 correctos; no se ejecutó el juego.

Unidad posterior TIME-002 (2026-08-31): auditadas las rutas de partículas.
Billboards, hijos y partículas simples sólo leen estado al dibujar, pero
`drawPtclOriented` guardaba una normal de continuidad y podía terminar la vida
de una partícula degenerada. Esas escrituras se conservan únicamente en
Authoritative; `particleManager::update`, `particleGenerator::update` y
`simplePtclManager::update` rechazan Presentation. Build completa correcta;
los presents y runtimes visual/cámara siguen apagados hasta completar la
auditoría de refresh.

Unidad posterior TIME-002 (2026-08-31): `BaseGameSection::draw()` contenía
lógica no visual: avanzaba el fade de sección y banner y ejecutaba
`ModeState::postUpdate()`. Ahora sólo Authoritative hace esas operaciones;
Presentation puede redibujar sus valores actuales. También se impide repetir
`SeSystem::update()` desde `GameCoreSection::draw()` y los timers/transiciones
de `LifeGauge::refresh()`. Quedan más mutaciones históricas dentro de rutas
`refresh`, por lo que aún no deben habilitarse presents reales.

Unidad posterior TIME-002 (2026-08-31): protegidos los estados físicos y de
pose derivados desde refresh. `CollInfo::updateInfo` y
`CreaturePlatMgr::update` no reescriben colisiones/plataformas en Presentation;
los setters centrales de culling de `Creature` y opciones de `Teki` tampoco
filtran decisiones de la cámara interpolada hacia la IA. Las luces de la nave
no avanzan ni emiten sonido. `BaseShape::updateAnim` ya no reconstruye el
buffer compartido en Presentation: el snapshot completo se aplica al dibujar.
También quedan bloqueados weighted matrices, look-at manual de Navi/Pikmin y
deformaciones de Snake/King/Spider/Slime. Runtime y presents siguen apagados.

Avance de temporización (2026-08-30): se añadió `PcFrameScheduler` offline y
se integró el fixed-step autoritativo. `System::run` ejecuta 60/30 ticks según
`frameClamp`, con delta fijo, catch-up acotado, descarte de suspensión y logs
`[Timing]`; VSync Off ya no acelera el juego. El test determinista cubre
50–240 Hz, 59.94, jitter, stalls y cambios de clamp. El limitador de swap usa
deadlines absolutos. Esto completa las fases 0–2 como integración intermedia,
Un intento posterior de separar `PlugPikiApp::updateFixed()`/`renderFrame()` y
repetir draws entre ticks produjo corrupción grave de UI/estado GX en la prueba
del usuario y fue retirado íntegramente. No repetir `draw()` como presentación
extra: primero hay que aislar sus mutaciones y diseñar snapshots visuales.

El segundo intento, limitado a 60 Hz y con bloqueo de soft reset, también fue
retirado tras la prueba del usuario: emparejar matrices por orden de subida no
es válido porque GX reutiliza slots y el orden de objetos cambia con culling.
Produjo geometría gigante y dejó a Olimar visualmente inmóvil. El código vuelve
al render estable monolítico. La siguiente implementación debe guardar estado
anterior/actual por identidad de Camera/Creature/Shape antes de dibujar; queda
prohibido inferir identidad desde la secuencia de comandos GX.

La misma prueba confirmó que el ejecutable sí alcanzaba más FPS, pero las
animaciones iban aceleradas y perdían su timing. Por tanto, la presentación
extra estaba ejecutando también alguna ruta que avanza pose/animación. En la
próxima separación, `animate()`, `updateAnim()`, partículas y contadores sólo
pueden ejecutarse en ticks lógicos; el render intermedio será estrictamente de
lectura sobre snapshots.

Prioridad nueva del usuario (2026-08-30): dedicar el trabajo activo a una
auditoría y optimización integral del rendimiento para que el port se comporte
como software nativo, sin rebajar fidelidad ni ejecutar el juego desde Codex.
Se añadieron PERF-NATIVE-001..006 al roadmap. La primera pasada ya incorpora
timer queries GPU asíncronas, vértices de streaming un 31 % menores y NoteON
sin copias/asignaciones del catálogo IBNK. El port futuro a Windows x64 se
registró como PLATFORM-WIN-001 y queda expresamente aplazado; la primera ruta
recomendada será MinGW-w64, no MSVC.

Última corrección de input/estabilidad (2026-08-30):

- El ratón directo se sumaba sobre X/Z fijos del mundo, por eso Q hacía que
  arriba/abajo se convirtieran en laterales o se invirtieran según el yaw. En
  `Navi::makeVelocity` SDL X/Y se proyecta ahora sobre los ejes horizontales
  normalizados `mViewXAxis` y `-mViewZAxis`; el signo negativo convierte Y-down
  de SDL en el eje vertical esperado del cursor. El delta se consume una vez.
  `pc_window.cpp` ya no descarta movimientos de hasta dos counts.
- Una partida cayó justo después de inicializar `2-3.gen`; otra superó los
  mismos generadores. La causa estructural encontrada en el intervalo siguiente
  era `BPikiInf::doRestore`: recibía un `PikiHeadItem`, lo convertía a `Piki` y
  escribía en campos de animación fuera de su layout. Ahora restaura
  `mSeedColor` y `mFlowerStage`, simétricos a `doStore`, y valida ambos valores
  serializados. Es consistente con que sólo fallen saves con brotes enterrados.
- La desaparición posterior de entidades no procedía de esa corrección. La
  causa era la recuperación previa de `init.gen`: mezclaba el caché persistente
  con datos iniciales y eliminaba «duplicados» comparando nombre, objeto y
  posición con tolerancia. Dos generadores legítimos cercanos podían colisionar.
  La heurística se eliminó. Un caché validado es ahora autoridad exclusiva,
  incluso si `mHasInitialised` discrepa; si falta o es inválido se descarta
  antes de reconstruir desde `init.gen`.
- `GeneratorCache` valida transaccionalmente sus cinco descriptores, usa lecturas
  acotadas y escrituras staged sin registros parciales; protege además conteos
  de brotes y estados de piezas. `pc_generator_cache_validation_test` cubre
  huecos, solapamientos, tamaños, conteos y estados corruptos.
- Build completa y CTest 5/5 correctos. Pendiente validación in-game por el
  usuario del ratón, del save afectado y de varias revisitas/niveles.
- Los timer queries del último log ya resolvieron el siguiente objetivo de
  rendimiento: título 1080p emplea ~55,74 ms GPU en escena y ~0,50 ms en blit;
  gameplay ~32,5-34 ms de escena y <0,85 ms de blit. Priorizar especialización
  del shader TEV/raster y coste por píxel; no rediseñar el blit.

Unidad de distribución pública iniciada (2026-08-30):

- Existe un launcher nativo independiente en `pc_port/launcher/`. Acepta por
  ahora ISO/GCM sin comprimir, inspecciona el ID del disco y exige Pikmin USA
  Rev. 1 (`GPIE01`, revisión 1). Extrae el árbol FST con comprobaciones de
  offsets y rutas a un staging único antes de renombrarlo a `assets`.
- El primer uso ya es una ventana SDL propia titulada
  `Pikmin PC Port Installer`: campo+selector para ISO/GCM, campo+selector para
  destino, botón `Instalar`, barra/archivo actual y mensajes modales. Zenity o
  KDialog sólo abren los selectores del sistema. Copia ambos ejecutables junto
  a `assets`; la copia instalada no vuelve a solicitar el disco.
- `--install-dir` conserva un flujo headless reproducible; `--data-dir` es alias
  compatible y `--extract-only` no ejecuta el juego.
- `pikmin-launcher` instala por defecto en
  `$XDG_DATA_HOME/pikmin-native` o `~/.local/share/pikmin-native`, cambia allí
  el cwd y ejecuta el binario `pikmin` situado a su lado. Esto evita cambiar las
  rutas heredadas `assets/dataDir` del port.
- Selección gráfica mediante Zenity o KDialog; CLI disponible con `--rom`,
  `--install-dir`/`--data-dir` y `--extract-only`. RVZ/WIA/GCZ deben
  convertirse externamente a ISO con `dolphin-tool`.
- CMake construye e instala ambos binarios juntos. `gamecube_image_test` crea
  una ISO mínima sintética y verifica identidad/FST/extracción. CTest ejecuta
  siempre extractor y envolventes; con assets locales añade JAM y mixer. Última
  ejecución local: 4/4. No se ejecutó el juego.
- `--extract-only` fue validado además contra la ISO local real: extrajo 3.496
  archivos/641 MiB y encontró el árbol requerido. El directorio temporal se
  eliminó y las copias locales originales permanecen intactas.
- Preparación GitHub: README del port, `LEGAL.md`, `CONTRIBUTING.md`, CI Linux
  y reglas `.gitignore` para ROMs/assets/configuración/backups. Las copias
  locales siguen intactas y ahora aparecen como ignoradas.
- Pendiente antes de una release pública: prueba manual real del launcher,
  empaquetado relocatable/AppImage y revisión final del conjunto a versionar.
- El directorio hermano `pikmin-github` es una copia de staging para la futura
  subida. Se genera sólo con `git ls-files --cached --others
  --exclude-standard`, por lo que refleja el worktree público actual sin
  `.git`, ROM, assets extraídos, builds, configuración ni backups.

El usuario pidió estabilizar primero los avances recientes del port. La build
global vuelve a estar verde en Release y se integraron estructuralmente el menú
F1, los controles configurables, el atajo F2 y el pacing lógico. El pitido largo
del selector sigue pendiente y se retomará después. No ejecutar el juego: sólo
el usuario realiza las pruebas in-game.

Última unidad de integración (2026-08-29):

- Rendimiento/título: el usuario confirmó 9.974 rutas rápidas de 13.074 draws,
  pero el título sigue en ~17,5-18,2 FPS a 1080p/1x. La ruta TEV rápida no era
  el cuello principal; quedan ~13.000 micro-draws por frame. Bajo
  `PIKMIN_PERF_STATS`, `[PERF FAST]` informa ahora primitivas y vértices medios
  por ruta para diseñar batching con límites de estado GX demostrables.
- La captura muestra el logotipo floral y la UI sobre negro: `opening.cin` sí
  se ejecuta, pero su escenario/actor de fondo no produce imagen. Los 5/7
  stages suman unas 3.100 llamadas; `[PERF TEVN]` agrupa la firma de su etapa
  final para aislar la traducción TEV múltiple antes de cambiar materiales.
  No sustituir el fondo por una imagen estática ni aplicar un parche por actor.
- El log posterior confirmó 13.063 triangle strips de 13.074 draws, con 5,04
  vértices medios en la ruta raster. Antes de implementar batching se corrigió
  un defecto TEV directamente relacionado con el negro: el shader escribía el
  resultado RGB como `vec4` y sobrescribía el alpha aunque color y alpha fueran
  a registros diferentes. Ahora actualiza `.rgb` y `.a` independientemente.
  La prueba recuperó la vegetación del título, confirmando la causa. El suelo
  permanece negro y ciertas hojas salen demasiado claras. Se conectó después
  el registro programable TEVPREV (BP E0/E1) al shader; antes cada draw lo
  iniciaba incorrectamente a cero. La nueva captura no cambió suelo/hojas, por
  lo que se conserva como corrección semántica pero no como solución. Con
  `PIKMIN_PERF_STATS`, la build imprime una sola vez `[PERF TEVPIPE5]` y
  `[PERF TEVPIPE7]` con todas las etapas y registros; pedir esas líneas.
- La traza TEVPIPE5 demostró que el material usa TEXCOORD2. El backend sólo
  llevaba TEX0/TEX1 y `resolveTex` reutilizaba TEX0 para cualquier índice mayor;
  además nunca obtenía las locations de `uTcMode/uTcMtx`. Transportar ocho
  varyings hasta fragmento no cambió el suelo y bajó a ~10 FPS. La revisión
  actual conserva TEX0-7 en CPU pero sólo interpola explícitamente TEX0-3,
  evitando arrays dinámicos; recuperó ~17-18 FPS. No restaurar
  la variante de ocho varyings sin especialización por material.
- El PIPE7 anterior era del `leaf.mod` auxiliar, no necesariamente del suelo.
  `CinematicPlayer::refresh` abre ahora scopes de diagnóstico con el nombre del
  actor. Pedir `[PERF ACTOR]` y `[PERF TEVPIPE5/7 opening.mod]` para separar
  `opening.mod` de `titles/logo.mod`. Sólo se activa con `PIKMIN_PERF_STATS`.
- El scope confirmó ~3.100 draws para `opening.mod` (los 5/7-stage) y ~8.964
  para `logo.mod` (raster simple). Se añadió batching conservador de triangle
  strips consecutivas por display list, con degenerados/paridad y flush ante
  cualquier comando de estado. `[PERF]` muestra draws resultantes y `source`.
  Build Release correcta; validar visualmente antes de conservar/cerrar.
- Validación: batching conserva la imagen, reduce 13.063 primitivas a 617 draws
  y CPU render a ~7 ms; FPS sigue ~17 por wait/swap ~52 ms. El fondo continúa
  negro. Inicialmente TEV7 pareció calcular `CPREV*(1-A1)` con A1=1. Se añadió una salida
  única `[PERF MAT7]` en `DGXGraphics::setMaterial` con nombre, colores S10 y
  metadatos de animación. Se repite cada 120 usos para que no se pierda entre
  el arranque. La línea recibida confirma los tres registros blancos opacos y
  `anim=0`: no hay una animación detenida. TEV7 es sólo una de cuatro llamadas
  agrupadas de `opening.mod`; no forzar A1 ni identificarla automáticamente
  con todo el fondo hasta aislar su geometría/material. Se añadió
  `[PERF TEVGEOM5/7]` (vértices, caja NDC y rangos UV) para hacerlo sin cambiar
  el render. La primera TEVGEOM5 cubre sólo NDC `(-.052,-.049)-(.024,.047)`,
  así que no es el fondo completo. El diagnóstico enumera ahora hasta cuatro
  llamadas por pipeline como `#0..#3`; pedir todas las líneas TEVGEOM de
  `opening.mod` en la siguiente ejecución.
  Las líneas completas mostraron TEXCOORD2/3 directos en cero. La causa
  estructural encontrada es que el shader sólo reconocía fuentes texgen
  `GX_TG_TEX0..7`: las fuentes encadenadas `GX_TG_TEXCOORD0..6` caían en UV
  directo cero. Ahora TEXCOORD0–3 se generan secuencialmente y pueden consumir
  la salida anterior, siguiendo GX. Esto no cambió el fondo.
  La causa raíz encontrada después es la numeración de `GXTevColorArg`: el port
  usaba `C0,C1,C2,A0,A1,A2`, pero GX/PVW codifican
  `C0,A0,C1,A1,C2,A2`. El supuesto A1 (selector 6) es realmente C2 y la etapa
  previa lo escribe expresamente. Se corrigieron el enum público y `resolveC`
  GLSL globalmente; build/prueba visual pendientes. No reintroducir la lectura
  antigua ni parchear el material.
- El mismo log carga la configuración con `vsync=0`, pero el swap seguía
  durmiendo siempre según el intervalo VI. `pc_window_swap_buffers()` respeta
  ahora `sVsyncEnabled`: VSync Off elimina el pacing software sin tocar el reloj
  lógico/delta. La prueba mantuvo ~30,5 ms en wait/swap; SDL ya usa intervalo 0,
  así que ese tiempo es probablemente trabajo GPU diferido/presentación, no el
  sleep retirado. Render CPU sigue ~7,3 ms.

- **Implementado** el reemplazo del stick virtual con decaimiento por deltas
  directos SDL según `MOUSE_INPUT_IMPLEMENTATION_GUIDE.md`:
  - `pc_window.cpp`: Estado de delta por frame (`sMouseCursorDeltaX/Y`) publicado
    una sola vez por `pc_window_poll()`, sin acumuladores ni factor 0.9f.
    Getters C `pc_window_get_mouse_cursor_delta_x/y()` y
    `pc_window_clear_mouse_cursor_delta()`.
  - `navi.cpp::makeVelocity()`: Rama explícita para
    `PC_CONTROL_MOUSE_CURSOR` que aplica `targetPos = mCursorPosition + delta`,
    sin normalizar, sin multiplicar por `DeltaTime`, con `kMouseCursorWorldScale`
    documentado (0.5f inicial). Ambos `mCursorPosition` y
    `mCursorTargetPosition` terminan iguales para evitar interpolación tras soltar
    el ratón.
  - Limpieza de deltas en transiciones: F2, F1, Tab/Escape, foco de ventana y
    entrada/salida de modo relativo SDL (consumiendo un
    `SDL_GetRelativeMouseState` inicial al entrar).
  - La prueba manual detectó Y invertida; se retiró la inversión adicional en
    `Navi::makeVelocity()`. Pendiente confirmar arriba/abajo con la nueva build.
  - Sensibilidad lineal (0.5 = mitad, 1.0 = base, 2.0 = doble) sin curvas de
    aceleración ocultas.
  - Classic, teclado, mando, F1 y F2 conservan su comportamiento original.
  - Build Release compila correctamente. Pendiente validación in-game por el
  usuario (tests 1-8 del plan).

- F2 alterna intencionadamente `PC_CONTROL_CLASSIC` y
  `PC_CONTROL_MOUSE_CURSOR`; se corrigió el nombre residual que bloqueaba la
  compilación sin retirar esta función.
- F1 tiene una sola fuente de verdad. Al abrir muestra el cursor y libera el
  ratón; al cerrar restaura captura relativa si está activo Mouse Cursor.
- Teclado, botones de mando, sensibilidad, zona muerta e inversiones de ambos
  sticks se cargan, aplican y guardan. Los bindings del menú ya alimentan el
  polling SDL real; el flujo avanzado ya no guarda `sConfig` antiguo mientras
  edita `sPending`.
- El aspect ratio participa en aplicar/revertir vídeo. Se corrigieron dos fallos
  de memoria del overlay: `va_start` inválido y `valueBuf[6]` sobre un array de
  seis elementos.
- El pacing lógico de la simulación permanece independiente del monitor; con
  VSync activado la presentación respeta el intervalo VI y con VSync apagado
  queda sin la espera software. Drawable 0x0 al minimizar ya no divide por cero.
- `zen::makePathName` usa basename y escritura acotada en lugar de una copia
  inversa potencialmente fuera de `PATH_MAX`.
- El overlay F1 se rediseñó con primitivas GX para acercarlo al menú de opciones
  original: bisel cromado, carcasa negra/azul, reflejos, cabecera elevada,
  selección naranja y columnas separadas para etiqueta/valor. Conserva toda
  la navegación existente y espera validación visual in-game.
- Los tres submenús ya no son rectángulos translúcidos sobre la lista principal:
  usan páginas internas opacas, cabecera, columnas, nueve filas visibles y un
  pie de ayuda dividido para no desbordar 640x480. Build Release correcta;
  pendiente nueva captura del usuario.
- Corregido un acceso fuera de rango visual: Controls/Gamepad/Advanced usaban
  `valueBuf[7..9]` aunque sólo existían siete valores. Ahora muestran `Open >`.
- Validación: build Release completa; envelope 63/63, JAM 0 fallos y mixer 0
  fallos, también en los tres binarios sanitizados existentes. La build global
  ASan/UBSan compiló todas las fuentes actuales hasta el enlace, pero el
  enlazador no terminó tras varios minutos y se interrumpió; repetirla antes de
  afirmar que el ejecutable sanitizado actual está verde. No se ejecutó el juego.

Unidad de memoria posterior:

- `piki_pc_free` ya no busca linealmente entre todas las reservas. El tracker
  usa 4096 buckets estáticos, no reserva durante tracking y al salir imprime
  `[PC Alloc] live=... bytes=... peak=... total=... unknown-frees=...`.
- `operator new` trata tamaño cero y overflow y lanza `std::bad_alloc` si no
  puede reservar. Los frees desconocidos se cuentan pero no se liberan porque
  pueden ser objetos de una arena `AyuHeap`.
- PC carga recursos desde el árbol extraído; `parseArchiveDirectory` ya no
  ejecuta la falsa DMA ARAM de 32 bits. `AramStream`, `copyRamToCache` y
  `copyCacheToRam` fallan explícitamente si una ruta futura intenta usarlos.
- `showError` no recorre en PC una pila GameCube de 32 bits inexistente; se
  corrigió también su selección invertida entre símbolo y dirección.
- Build Release completa y tres tests offline verdes. Pendiente: el usuario
  debe enviar la línea `[PC Alloc]` tras sesiones cada vez más largas para
  evaluar MEM-004/MEM-005. No se ejecutó el juego.

Validación real posterior (sesión larga del 29-08):

- El recorrido llegó a varios escenarios, F2 cambió correctamente de Mouse
  Cursor a Classic y el proceso salió limpio.
- El antiguo informe terminó en 141.316 reservas/1,60 GB vivos y cero frees,
  pero `nm` mostró que el binario importaba `new/delete` de libstdc++. Ahora
  son definiciones fuertes en `sysNew.cpp`, el binario las exporta y el informe
  añade `frees`. Repetir una sesión antes de decidir la estrategia MEM-004.
- Se protegió el cálculo de MB/s contra tiempo cero. Build Release y las tres
  pruebas offline están verdes; no se ejecutó el juego.
- El log contiene además 185 fallos `pikise.jam` `InvalidAddress` (resultado
  3), no reproducidos offline; cuatro cámaras `near >= far`; framebuffer
  1919x1080; y faltan `logo.dck` y `naka1a/f/s.dck` del árbol extraído.

Últimos cambios ya compilados:

- `pc_wave_bank.cpp` interpreta `Wave_ +0x14` como inicio de bucle decodificado
  y `+0x18` como final de reproducción. Antes `+0x18` se usaba erróneamente como
  inicio y generaba bucles diminutos de 65-118 muestras.
- `PCWaveInfo` conserva `loopSample` y `loopEndSample`; el mixer respeta ambos
  límites e interpola el final contra el comienzo del bucle.
- El intérprete distingue `GateON` de `NoteON`. Un gate reutiliza la voz activa
  en vez de reiniciar la muestra, como hace `src/jaudio/noteon.c`.
- Se auditaron las voces de `select.jam`: las candidatas largas son los
  programas 18:4 y 18:5, ondas 8/9 estéreo, 44.1 kHz, unas 213.000 muestras y
  bucle desde 36.784. La auditoría posterior confirmó que arrancan juntas con
  nota 60, reciben note-off cada 3.840 ticks (16,000 s) y se relanzan en ese
  mismo tick; no usan GateON ni quedan huérfanas. `pc_jam_test
  --render-select-long` exporta ambos canales como WAV mono independientes en
  `/tmp/pikmin-select-18-4-wave8.wav` y
  `/tmp/pikmin-select-18-5-wave9.wav`. No revertir `+0x14`, porque el usuario
  confirmó la mejora.
- `pc_jam_test` informa voces/opcodes del selector para facilitar el análisis.

Validación al entregar: build normal completa correcta, `pc_jam_test` con 0
fallos sintéticos y `pc_envelope_test` 63/63. La última corrección del fin de
bucle aún necesita prueba audible del usuario.

Se añadió después `pc_audio_mixer_test` con SDL dummy: valida 64 voces, rechazo
por prioridad, robo de voz, handles generacionales, parada completa y stream
STX simultáneo. Termina con 0 fallos y 0 clips, por lo que AUDIO-001B queda
cerrado offline. El catálogo actual ya interpreta también `PERC`: carga 217
instrumentos, 773 regiones, 218 osciladores y no deja percusión antigua sin
soporte. La siguiente unidad de AUDIO-006 debe añadir métricas de callback,
robo/rechazo/underrun y estrés prolongado; no volver a implementar `PERC`.

La unidad siguiente añadió esas métricas y 512 ciclos de control con STX
simultáneo; el test detecta también un underrun DMA provocado y termina sin
fallos ni clips. Los modos IBNK 3/4 dejaron de ignorarse: FX usa un envío de
reverberación estéreo y Dolby una matriz L-R, con buffers estáticos. JAM
propaga sus parámetros 2/4 y su herencia `EF` en altas y actualizaciones de
voz.

La pausa/reanudación ya distingue la pista hija 10 del jugador y el
secuenciador persistente de eventos; JAM y el mixer congelan su tiempo y sus
voces sin reiniciarlas. Menú, pausa, DVD, piezas, texto y demos aplican esa
política con los retardos de reanudación del original. También se portaron las
cuatro columnas relevantes de las 115 entradas `DEMO_STATUS` y sus 614 valores
temporales: las demos disparan audio/SFX, final, corte, parada y recuperación
en sus frames originales. `Jac_Freeze_Precall` corta todas las voces one-shot
del bus SE sin destruir BGM/stream; la prueba verifica también sus handles
obsoletos. Build normal y las tres pruebas offline pasan, igual que sus targets
ASan/UBSan con detección de leaks desactivada. Queda documentar la validación
audible manual separada antes de cerrar AUDIO-003/004/005/006.

Última unidad cerrada: los relojes BGM/jefe/SE/eventos ya no descartan el tiempo
que excede 250 ms y publican sus ticks en `PCAudioMetrics`; el test demuestra
la recuperación tras 350 ms. Se eliminó el único remapeo arbitrario restante
(`program % 30`), se completó la política/timeout de eventos y
`Jac_GameVolume` usa las tablas originales. El limitador informa frames
limitados por separado de clips reales y recupera ganancia suavemente. Un
estrés offline de 30 s con BGM+STX+SE terminó con 0 fallos, robos, rechazos y
clips, y sin voces residuales; para ello se corrigió la limpieza de releases
BGM al parar una pista. ASan/UBSan queda limpio también para el mixer tras
corregir el shift de PCM8 negativo.

La regresión global por `PC_CONTROL_MOUSE_OLIMAR` ya está corregida y la build
Release completa pasa. Pendiente sólo de prueba in-game:
audición de BGM/SFX/posicional, pausas/transiciones y sesión real prolongada.
Después se puede retomar el pitido del selector. No revertir WSYS `+0x14`.

Archivos centrales de esta frontera: `pc_wave_bank.h/.cpp`, `pc_audio.h/.cpp`,
`pc_jam.h/.cpp`, `pc_jam_test.cpp` y `ROADMAP.md`, todos bajo `pc_port/audio`
salvo el roadmap.

Siguiente acción recomendada: escuchar por separado los dos WAV renderizados y
compararlos con el pitido in-game. Si los archivos están limpios, instrumentar
pitch, panorama, volumen y envolvente efectivos de 18:4/18:5 en el mixer. No
aplicar remapeos manuales de instrumentos sin demostrar primero qué etapa
produce el defecto.

## 1. Objetivo y forma de trabajo

Port nativo de Pikmin 1 (GameCube) para Linux. SDL2 proporciona ventana,
entrada y audio; `pc_port/gl/pc_gfx.cpp` traduce la API GX a OpenGL.

Reglas acordadas:

- Codex inspecciona, modifica, compila y analiza logs.
- **El usuario ejecuta el juego** y envía logs, capturas y resultados.
- Buscar causas estructurales y revisar otros consumidores afectados; evitar
  parches por modelo u objeto.
- Preservar el worktree: contiene muchos cambios del usuario y otras IAs.
- Cada corrección o resultado debe actualizar `ROADMAP.md`.

Documentos:

- `ROADMAP.md`: tareas, estado e historial.
- `TEST_PLAN.md`: recorridos manuales repetibles.
- `PORT_AUDIT.md`: auditoría histórica de riesgos.
- Este archivo: contexto para continuar en conversaciones nuevas.

## 2. Rutas, build y ejecución

Raíz del checkout:

```text
<directorio-del-repositorio>/pikmin
```

Build normal optimizada (`Release -O3`):

```bash
cd pikmin
cmake --build build -j4
```

Build ASan/UBSan:

```bash
cmake --build build-sanitize -j4
```

El usuario ejecuta normalmente:

```bash
./build/bin/pikmin-launcher
```

El binario `pikmin` permanece disponible para el flujo local antiguo desde una
raíz que ya contenga `assets/dataDir`; el paquete público debe exponer primero
el launcher.

No ejecutar el juego desde Codex. No borrar diagnósticos antiguos sin permiso;
llegó a existir `/tmp/opencode/tevreg.log` con unos 557 MiB.

## 3. Estado funcional actual

Funciona o ha progresado de forma importante:

- Una sola ventana SDL, cierre normal y entrada básica.
- Título, selector de archivo y transición a partida.
- Partículas indexadas del selector corregidas; el usuario lo confirmó.
- Cinemática inicial con modelos, cámara, estrellas y audio `.stx`.
- Transición de película a gameplay sin los crashes históricos.
- Primer nivel poblado: nave, Olimar, cebolla, Pikmin, plantas y objetos.
- Cámara de gameplay y eventos de la cebolla avanzan sin los crashes anteriores.
- Tarjeta virtual persistente básica.
- Validación manual del 26-08-2026: el usuario completó el primer nivel, guardó
  y cargó correctamente, llegó al segundo nivel y venció varios enemigos. Esto
  valida progresión inicial, persistencia y combate básico tras recarga; no
  demuestra aún que el juego sea completable de principio a fin.
- Rendimiento: comenzó sobre 5 FPS y el último log llegó a gameplay cercano a
  30 FPS; la película alcanza 60 FPS en tramos ligeros.
- El usuario considera por ahora correcta la velocidad.

Cadencia vigente:

- `setFrameClamp(1)`: objetivo lógico 60 Hz.
- `setFrameClamp(2)`: objetivo lógico 30 Hz.
- El pacing usa una base VI de 60 Hz independiente de monitores 60/120/144 Hz
  y descuenta el tiempo consumido por render/vsync.
- Películas y gameplay usan escala temporal `1.0`.
- Se probó `0.5` en gameplay y se revirtió por ser demasiado lenta frente a
  vídeo real de referencia.

## 4. Problemas aún pendientes

No asumir que están resueltos sin una prueba reciente:

1. Fondo del título ausente/negro. Las letras recuperaron color; probablemente
   es escena/recurso de título, separado del selector.
2. Iluminación/materiales aún no son fieles a GX en todos los modelos. Nave,
   cebolla y Olimar mejoraron, pero quedan partes blancas/planas o incorrectas.
3. Cabeza/visor de Olimar y Pikmin requieren verificación final.
4. Halo/luz grande que sigue a Olimar (`FX-002`).
5. Nave estrellada con piezas/articulación extraña: distinguir skinning,
   animación y materiales.
6. Primera pastilla: históricamente desaparecía al romper la flor. Revisar
   aparición, colisión, transporte y absorción.
7. El primer nivel, un guardado/recarga y la llegada con combate al segundo
   nivel ya están validados. Faltan niveles posteriores, múltiples finales de
   día, jefes, piezas/eventos especiales y los finales del juego.
8. Audio: streams de película funcionan. El 26-08-2026 el backend pasó de
   `SDL_QueueAudio` exclusivo a callback/mixer con voces separadas para STX y
   AI/DMA. Además existe un parser nativo de `pikibank.bx` (22 WSYS, 882
   ondas), decodificador DSP ADPCM4/PCM8/PCM16, mixer de 64 voces con caché y
   parser BARC validado para las 22 secuencias de `pikiseq.arc` (276.032 bytes).
   Está en `pc_port/audio/pc_wave_bank.*`, `pc_sequence_archive.*` y
   `pc_audio.*`. Aún falta interpretar JAM y resolver IBNK, por lo que los BGM
   y SFX no se oyen todavía.
   El trabajo posterior añadió un primer parser IBNK seguro: reconoce los 22
   slots (19 presentes), 216 instrumentos y 762 regiones INST/PER2, además de
   la tabla banco virtual -> físico. Una única percusión con formato antiguo
   `PERC` se detecta pero permanece sin interpretar. Faltan sensores/efectos
   aleatorios y cerrar el formato `PERC`. Los 203 osciladores, curvas
   attack/release y la resolución WBCT/SCNE de wave IDs ya se analizan.
   La validación cruzada resuelve 748/762 regiones; 14 referencias no figuran
   en ningún control de escena y se rechazan sin remapeo especulativo.
   Existe un primer `pc_audio_play_note` que resuelve banco/programa/nota/
   velocidad y calcula afinación/volumen. `pc_audio_release_wave` aporta ya un
   note-off lineal por frames, seguro frente a handles reciclados, pero aún no
   se ejecutan las curvas IBNK ni está conectado a JAM o a eventos del juego.
   El mixer dispone ahora de buses stream/BGM/SE/DMA, prioridades, antigüedad,
   handles generacionales, contador de clipping y consulta del número de voces
   activas; falta prueba de estrés y limitación final, por lo que AUDIO-001B
   continúa en curso.

### Semántica comprobada de las envolventes JAudio

`Bank_OscToOfs` se evalúa en `DSPCHCB_NormalUpdate`. El DSP usa 560 muestras
por frame y 7 subframes, por lo que cada actualización representa 80 muestras.
El contador inicial es `time * (JAC_DAC_RATE / 80) / 600` y cada actualización
resta `osc.rate`: con `rate=1`, `time=600` equivale a un segundo; en general la
duración es `time / (600 * rate)` segundos. En PC conviene convertirla una sola
vez a frames de salida, no ejecutar la máquina de estados por cada muestra.

- Estado 1 inicia ataque; 2 continúa la tabla; 3 mantiene el valor (`0x0E`).
- Estado 4 solicita release: usa la tabla de release (estado 5) o un release
  forzado temporizado (estado 8) cuando `releaseParam` es distinto de cero.
- Estado 6 selecciona la tabla de release forzada y pasa a 7; estado 0 terminó.
- Cada entrada es `(curve, time, target)`. `0x0D` salta al índice `target`,
  `0x0E` mantiene y `0x0F` termina. Los saltos necesitan un límite explícito.
- Curva 0 es lineal; 1 aplica cuadrado conservando signo; 2 aplica raíz
  conservando signo. El resultado final es `curveValue * width + vertex`.
- `mode` 0 multiplica volumen, 1 afinación, 2/3 parámetros de panorama.

Los estados ya se aplican a volumen y afinación por voz sin asignaciones dentro
del callback. La semántica temporal no debe sustituirse por aproximaciones.

### Próxima tarea segura y acotada

El evaluador puro de `Bank_OscToOfs` está corregido, validado offline e
integrado en las voces creadas por `pc_audio_play_note`. Cada selección copia
sus osciladores antes de bloquear SDL; la voz mantiene propiedad inmutable y
el callback sólo avanza estados preconstruidos. Los modos 0 y 1 ya modulan
volumen y afinación, y note-off selecciona las tablas release IBNK. Las voces
sin osciladores mantienen `pc_audio_release_wave` como fallback lineal. Faltan
los modos 2/3 de panorama y validación audible desde una secuencia real.

La primera ruta BGM JAM ya está conectada a `Jac_SceneSetup` y requiere ahora
RUN-012 del usuario. El auditor offline mantiene activas las 22 entradas durante
20.000 ticks y las pistas principales resuelven sus notas contra IBNK/WSYS.
No interpretar esto como validación audible ni como implementación completa de
JAudio: faltan fidelidad de parámetros/capas y SFX.

Después del resultado manual, orden recomendado:

1. Analizar el log y el resultado audible de RUN-012; corregir primero cualquier
   nota no resuelta, parada JAM, tempo o transición de escena.
2. Aplicar volumen/panorama y movimientos temporizados JAM, incluidos los modos
   de oscilador 2/3 y capas adaptativas usadas por `piki_bgm.c`.
3. Conectar `pikise.jam`/`sysevent.jam` y los IDs originales de SFX por grupos,
   con prioridades y pruebas separadas de UI, Olimar, Pikmin y gameplay.

Invariantes: no modificar stream, DMA, decodificación WSYS ni selección IBNK;
no añadir módulos JAudio originales a CMake; no restaurar backups; no ejecutar
el juego. Si la implementación exige tocar más de dos archivos de código o
aproximadamente 150 líneas, detenerse y dividirla.

Verificación mínima: build normal, build sanitizer, smoke test offline del
evaluador y `git diff --check` limitado a los archivos tocados. Sólo el usuario
puede validar audio audible. Actualizar ROADMAP/AI_HANDOFF con la diferencia
entre compilado, validado offline y probado manualmente.
   La apertura consta de `DEMOID_OpeningIntroPt1` y `Pt2`: el stream de la
   primera lleva semántica JAudio `0x20` y debe sobrevivir al corte entre ambas.
   Esa continuidad ya está implementada en `audio_stubs.cpp`.
   Un intento posterior de compilar directamente módulos JAudio originales se
   dejó en `pc_port/dolphin_stubs/`, pero esos archivos están deliberadamente
   fuera de `PC_PORT_SOURCES`: mezclarlos con `audio_stubs.cpp` causa símbolos
   duplicados y el DSP físico sigue incompleto. No volver a activarlos en bloque;
   portar primero una salida DSP software y conectar subsistemas por etapas.
   `src/jaudio/pikiseq.c` sí está incluido deliberadamente: sólo contiene la
   cabecera BARC estática y no activa ninguna parte del DSP.
9. Robustez 64 bits: quedan casts/truncamientos y consumidores de rutas
   potencialmente inseguros; consultar fases 4 y 5 del roadmap.

## 5. Correcciones gráficas estructurales aplicadas

Principalmente en `pc_port/gl/pc_gfx.cpp` y stubs relacionados:

- Decodificación BP de entradas TEV color/alpha, TREF, mapa, coordenada, enable
  y raster channel.
- Separación `GXTexMapID`/`GXTexCoordID`.
- Konst por etapa, compare TEV y swap tables/selectors.
- Registros BP E0–E7 corregidos; antes el último par podía salir del array.
- Bits XF de `GXSetChanCtrl` y canales color/alpha separados.
- COLOR1/especular separado, sin sumar diffuse dos veces.
- Ambient por defecto corregido de blanco a negro cuando corresponde.
- Normales de display lists transformadas con la misma PNMTXIDX que posiciones.
- PE blend, alpha compare y canales sin iluminación.
- Texturas I4/I8/IA, RGB565, RGB5A3, RGBA8 y CMPR.
- CI C4/C8/C14X2 y TLUT IA8/RGB565/RGB5A3.
- Modelos skinned con ruta portable de matrices ponderadas.

No corregir modelos cambiando colores concretos. Revisar primero TEV, XF,
iluminación, textura, matriz, alpha y blend comunes.

## 6. Rendimiento: trabajo aplicado

- `PIKMIN_PERF_STATS=1` usa ahora un anillo de ocho timer queries OpenGL para
  medir GPU sin sincronización. `[PERF GPU] scene` cubre los draws al FBO y
  `[PERF GPU] blit` la copia al backbuffer. Si no hay resultado disponible se
  conserva pendiente en vez de bloquear. Pedir estas cifras antes de elegir
  entre especializar shaders, buffer persistente o cambiar presentación.
- `Vertex` conserva sólo TEX0-TEX3, que son los atributos/varyings realmente
  expuestos por el shader. El parser sigue avanzando sobre TEX4-TEX7 para no
  desalinear display lists. Tamaño: 104 -> 72 bytes (−31 %), unos 3 MiB/frame
  menos con la carga observada del título.
- La auditoría descartó `glReadPixels` de `Texture::grabBuffer`: está en la rama
  `!PIKI_USE_DGX` y no forma parte del ejecutable Linux actual.
- La selección IBNK y las voces guardan vistas al catálogo inmutable; NoteON
  ya no copia vectores anidados de efectos/osciladores ni reserva un
  `shared_ptr` nuevo. El callback continúa sin asignaciones.
- CMake separa ahora O3, arquitectura e IPO/LTO. Release y RelWithDebInfo usan
  O3; `PIKMIN_NATIVE_OPTIMIZE` sólo añade `-march=native`, mientras
  `PIKMIN_ENABLE_IPO` emplea `CheckIPOSupported`. La CI pública mantiene
  `PIKMIN_NATIVE_OPTIMIZE=OFF`, por lo que sus binarios no dependen de la CPU
  concreta del runner.
- Validación offline RelWithDebInfo posterior: build completa y CTest 4/4
  (extractor, envelope, JAM y mixer). CMake sólo registra los dos tests que
  requieren contenido cuando el banco local existe, por lo que CI pública
  sigue funcionando sin datos propietarios.
- Primera unidad específica de alta resolución: el FBO usa el viewport final
  exacto (1920x1080 deja de convertirse en 1919x1080) y `pc_gfx_present` no
  limpia el backbuffer completo cuando el blit lo cubre entero. Build Release
  correcta; pendiente medición in-game a 1080p/1440p/4K.
- Medición posterior del título 1080p/1x: ~16,2 FPS, 13.074 draws, 71.054
  vértices, 606 DL y 0 fast; render CPU 16,66 ms, wait/swap 44,93 ms. Se añadió
  una ruta rápida exacta para `GX_MODULATE` (textura × raster), patrón común de
  UI/materiales simples. Build Release correcta; repetir `PIKMIN_PERF_STATS=1`
  para comprobar cuántos draws pasan ahora a fast y el cambio de FPS.
- Segunda medición: sólo 15/13.074 fast y ~16,6 FPS, por lo que modulate no es
  dominante. El modo de métricas imprime ahora `[PERF TEV]` y hasta cuatro
  líneas `[PERF TEV1]` con entradas/orden/operaciones dominantes. Pedir esas
  líneas antes de añadir más fast paths o batching.
- El histograma posterior mostró 9.959/13.074 draws de una etapa con raster en
  A y ceros en B/C/D, sin textura. Se amplió el fast path raster para esa forma
  algebraicamente equivalente. Pendiente repetir métricas; se esperan cerca de
  9.974 draws fast si no hay otra condición divergente.

- Eliminados hashes, deduplicación lineal, volcados y escrituras por primitiva.
- Eliminados cronómetros por subsistema y `glGetError()` por draw.
- Solo se envían etapas TEV activas.
- Caché tipada de uniforms escalares, vectores y matrices.
- Caché de depth, blend, logic op, culling y máscaras.
- Samplers y atributos de vértice se configuran una vez.
- Solo se enlazan mapas usados y únicamente cuando cambian.
- `GXInitTexObj`/CI no vuelve a decodificar/subir una textura idéntica.
- VBO streaming de 16 MiB: orphan una vez por frame, primitivas añadidas con
  `glBufferSubData`, crecimiento automático si hace falta.
- Build normal confirmada en `Release -O3`.

Evolución aproximada observada:

- Inicio: ~5 FPS.
- Tras retirar instrumentación: título/gameplay ~7–14, película ~30–60.
- Tras caché de texturas/estado: gameplay ~14–19, película pesada ~40–50.
- Tras uniforms/VBO: gameplay llegó aproximadamente a sus 30 FPS nativos.

La siguiente optimización debe partir de métricas: draw calls, vértices, coste
CPU del parser y tiempo GPU. Después decidir batching, buffer persistente o
especialización de shaders. No degradar fidelidad visual para ganar FPS.

## 7. Temporización: decisión actual

Archivos:

- `src/sysDolphin/system.cpp`: reloj, `mDeltaTime`, FPS y escala.
- `src/sysDolphin/dgxGraphics.cpp`: aplica `mSystemFrameRate`.
- `pc_port/pc_window.cpp`: pacing SDL independiente del monitor.
- `src/plugPikiColin/introGame.cpp`: película 30 Hz, escala 1.0.
- `src/plugPikiColin/newPikiGame.cpp`: gameplay 30 Hz, escala 1.0.

La película del cohete es referencia correcta. La escala experimental 0.5 se
revirtió. Si reaparece velocidad incorrecta, comparar movimiento, animación,
cámara, reloj del día y efectos por separado antes de tocar tiempo global.

## 8. Arquitectura y archivos sensibles

Ruta principal:

```text
Shape -> DGXGraphics::setMaterial -> GX state -> GXCallDisplayList
-> pc_gfx_call_display_list -> pc_gfx_end -> OpenGL
```

Cinemáticas 3D y gameplay comparten materiales/modelos. Una diferencia puede
venir de luces, animación, cámara o reloj, no necesariamente de otro renderer.

Archivos clave:

- `pc_port/gl/pc_gfx.cpp`: shaders, TEV/XF/BP, texturas, VBO y draw.
- `pc_port/dolphin_stubs/gx_stubs.cpp`: frontera GX/backend.
- `pc_port/dolphin_stubs/vi_stubs.cpp`: VI/presentación.
- `pc_port/pc_window.cpp`: SDL, input, swap y pacing.
- `src/sysDolphin/dgxGraphics.cpp`: materiales, luces y presentación.
- `src/sysDolphin/system.cpp`: bucle, reloj y recursos.
- `src/plugPikiColin/cinePlayer.cpp`: actores cinematográficos.
- `src/plugPikiColin/introGame.cpp`: sección inicial.
- `src/plugPikiColin/newPikiGame.cpp`: gameplay.
- `src/plugPikiKando/navi.cpp`, `itemMgr.cpp`, `goalItem.cpp`: gameplay.

La base es 64 bits pero conserva estructuras de GameCube. No convertir punteros
a `u32` salvo que sea deliberadamente un token/dirección emulada.

## 9. Estado de builds en este handoff

- `cmake --build build -j4`: pasa.
- `cmake --build build-sanitize -j4`: la última ejecución recompiló las fuentes
  hasta el enlace sin errores, pero el enlace no finalizó tras varios minutos y
  fue interrumpido; el resultado sanitizado actual queda por reconfirmar.
- Avisos conocidos: `register` y casts heredados puntero/`u32`.
- Codex no ejecutó el juego. La última validación del usuario dejó rendimiento
  y velocidad “bien de momento”.

## 10. Próxima secuencia recomendada

1. Audio prioritario: implementar resolución IBNK (programa/nota/velocidad a
   WSYS/onda), después el intérprete JAM y conectar `piki_bgm` por etapas.
2. Con BGM audible, conectar IDs de SFX y finalmente eventos posicionales.
3. Ejecutar RUN-005/RUN-006: Olimar, cebolla, Pikmin, flor y pastilla.
4. Gráficos: completar `GX-LIGHT-001/002/004`, luego `FX-002` y título.
5. Usar sanitizer para crashes/corrupción reproducibles.
6. Retomar rendimiento únicamente con métricas; el objetivo actual es mantener
   los 30 FPS nativos de gameplay.

En una conversación nueva: leer completos `AI_HANDOFF.md`, `ROADMAP.md` y
`TEST_PLAN.md`; inspeccionar el worktree antes de editar y no asumir que un
problema histórico sigue presente sin una prueba nueva.

## 11. Carpeta pública preparada

- La copia destinada a subida manual se genera en `../pikmin-open-source` sin
  directorio `.git`, ROM, assets extraídos, partidas, builds ni configuración
  local del usuario.
- `.gitignore` usa `/save/`: no volver a convertirlo en `save/`, porque eso
  excluye por error `pc_port/save/pc_generator_cache_validation.*` y rompe una
  compilación obtenida únicamente desde los archivos publicables.
- Antes de entregar la carpeta se configura y compila desde cero sin assets y
  se ejecuta CTest. El instalador real y el juego siguen siendo pruebas manuales
  del usuario y no deben ejecutarse desde Codex.

## 12. Prioridad siguiente: temporización independiente del refresco

- El plan completo está en `REFRESH_RATE_TIMING_PLAN.md`; debe leerse antes de
  modificar el bucle principal.
- TIME-001 mejoró la cadencia, pero no constituye una garantía completa:
  `System::updateSysClock()` todavía publica tiempo real variable limitado a
  `1/30`, y VSync Off deja libre el bucle. El código heredado incluye sistemas
  por delta y otros que avanzan una vez por llamada.
- El siguiente paso es un `PcFrameScheduler` aislado, con reloj inyectable y
  pruebas deterministas, seguido de ticks fijos `1/60` y `1/30`. El refresco y
  VSync solo deben gobernar la presentación.
- No empezar por renders interpolados. Primero demostrar mediante CTest que
  50/59.94/60/75/90/120/144/165/240 Hz producen exactamente los mismos ticks
  lógicos y no acumulan deriva.
- El usuario aclaró que 30 FPS visuales no son aceptables como resultado final.
  El port debe presentar a los Hz nativos a velocidad normal. Los ticks fijos
  protegen física/timing; después es obligatorio separar update/render e
  interpolar cámara y estado visual para obtener fluidez real de alto refresco.
- (2026-08-31) Se ha completado la integración de `PcFrameScheduler` en `System::run()`, con interpolación visual y cámara según estado del tick. El juego presenta ahora a los Hz del monitor sin acumular frames lógicos en renders intermedios. Las pruebas offline pasan; queda verificarlo in-game.
