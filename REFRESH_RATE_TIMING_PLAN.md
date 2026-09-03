# Plan de temporización independiente del refresco

> **Estado 2026-09-01:** la última activación falló in-game con imagen en blanco
> y negro y corrupción gráfica; el usuario la revirtió al estado funcional de
> 30 FPS. No volver a presentar llamando una segunda vez a `renderall()`. Leer
> `60FPS_FAILED_ATTEMPTS.md` antes de implementar este plan. La arquitectura
> preferida ahora es captura/replay inmutable en la frontera GX -> OpenGL.

## Objetivo

El port debe conservar exactamente la cadencia lógica original de Pikmin:

- Secciones `setFrameClamp(1)`: un tick lógico cada `1/60 s`.
- Secciones `setFrameClamp(2)`: un tick lógico cada `1/30 s`.
- El monitor, VSync, compositor, carga gráfica y opción de FPS no pueden
  acelerar ni ralentizar simulación, animaciones, cinemáticas, reloj del día,
  IA, físicas, partículas, input o audio.
- 30 segundos de juego deben representar 30 segundos reales cuando el equipo
  puede mantener el objetivo. Un retraso puntual no debe producir un acelerón
  ni una espiral de actualizaciones.

El resultado final no estará visualmente limitado a 30/60 FPS: presentará a la
frecuencia nativa elegida por el usuario. Para conseguirlo sin alterar físicas,
la simulación autoritativa mantiene el paso original y el render interpola
entre estados. La corrección del reloj lógico se implementa primero porque es
la base necesaria, no porque 30 FPS sea el objetivo final.

## Estado y riesgos actuales

- `System::updateSysClock()` obtiene un delta real variable, lo limita a
  `1/30 s` y lo expone globalmente como `mDeltaTime`. No produce todavía un
  paso fijo determinado por `mFrameRate`.
- `pc_window_swap_buffers()` usa un limitador software basado en una VI fija de
  60 Hz, pero reinicia la referencia después de dormir. Esto acumula deriva.
- Con VSync desactivado se elimina el limitador de presentación; el bucle y los
  sistemas heredados que avanzan una vez por iteración pueden ejecutarse más
  rápido de lo previsto.
- `sTargetRefreshRate` detecta o recibe la frecuencia del monitor, pero no
  participa en la cadencia actual.
- Actualización y render viven dentro de `app->idle()`. Renderizar a la tasa del
  monitor requerirá separar explícitamente tick lógico y presentación; no debe
  simularse llamando más veces a `idle()`.
- Hay código heredado mixto: parte usa `getFrameTime()` y parte avanza por
  llamada. Un delta variable no puede hacer correctos ambos grupos a la vez.

## Invariantes de diseño

1. Un único reloj monotónico (`steady_clock`) es la autoridad temporal PC.
2. El paso lógico es fijo: `1 / (60 / frameClamp)` segundos.
3. `mDeltaTime` durante un tick contiene ese paso fijo, nunca el intervalo del
   monitor ni el coste del frame.
4. VSync solo regula presentación. Nunca decide cuántos ticks lógicos ocurren.
5. Cambiar de 60 a 30 Hz reinicia/acota el acumulador sin ejecutar ticks
   atrasados con el paso anterior.
6. Las pausas, carga, cambio de foco y depuración descartan deuda excesiva.
7. El catch-up tiene un máximo pequeño y medido. Si se supera, se pierde tiempo
   visual antes que ejecutar una espiral de simulación.
8. El audio conserva su propio reloj de muestras; solo recibe eventos lógicos,
   no debe avanzar según número de presents.
9. Ninguna solución puede ser un multiplicador particular para 120/144 Hz.
10. Una presentación intermedia no puede llamar a `animate()`, `updateAnim()`,
    avanzar partículas ni incrementar ningún contador de animación. Sólo puede
    consultar los snapshots anterior/actual y construir una pose temporal.

## Ruta de implementación

## Progreso 2026-08-30

- [x] Fase 0: telemetría `[Timing]` por segundo y auditoría del flujo principal.
- [x] Fase 1: `PcFrameScheduler` aislado con reloj inyectable y CTest para la
  matriz 50–240 Hz, jitter, stall, suspensión y cambio de clamp.
- [x] Fase 2: `mDeltaTime` fijo 1/60 o 1/30; `idle()` sólo en ticks debidos;
  catch-up máximo cuatro y deuda de pausas descartada. El swap usa deadlines
  absolutos.
- [ ] Fases 3–4: dos intentos de repetir render entre ticks fueron retirados.
  El segundo demostró que las matrices GX no poseen identidad persistente: los
  slots se reutilizan y el orden cambia por culling, produciendo geometría
  gigante y actores inmóviles. La prueba del usuario confirmó además que sí se
  presentaban más frames, pero las animaciones quedaban aceleradas y fuera de
  tiempo: alguna actualización de pose seguía ocurriendo por presentación. La
  próxima ruta debe capturar snapshots en los objetos/actores, no emparejar
  llamadas del backend ni repetir `draw()`.
- [~] Base de snapshots por identidad añadida el 2026-08-31. El componente
  offline `PcVisualSnapshotStore` conserva generaciones anterior/actual y crea
  valores de presentación por `(owner persistente, dominio, slot semántico)`.
  Copia los datos para no depender de buffers temporales, elimina claves
  ausentes, permite marcar teleports/transiciones y sincronizar ambos estados.
  `pc_visual_snapshot_test` cubre orden variable, altas/bajas, discontinuidad,
  sincronización, alpha acotado y entradas inválidas. Todavía no está conectado
  a `BaseShape` ni habilita presents adicionales; esa integración requiere
  separar primero el render sin volver a ejecutar actualización de pose.

### Fase 0 — Congelar referencias y añadir observabilidad

- Registrar por segundo: Hz detectados, `frameClamp`, paso lógico, ticks,
  presents, tiempo acumulado, ticks descartados, catch-up máximo y tiempo de
  `swap`.
- Separar nombres: `realDelta`, `fixedDelta`, `presentDelta`, `alpha` y FPS
  renderizados. Evitar que una cifra llamada FPS o delta mezcle conceptos.
- Documentar qué secciones solicitan clamp 1 y 2 y dónde cambia el valor.
- Obtener del usuario referencias temporales reproducibles: 30 s de reloj del
  día, cinemática inicial, animación de Olimar, movimiento y menú/título.

**Salida:** logs suficientes para demostrar una relación 1:1 entre segundos
reales y tiempo lógico antes y después de cada cambio.

### Fase 1 — Extraer un planificador probado offline

- Crear un componente PC sin SDL/OpenGL, por ejemplo `PcFrameScheduler`, que
  reciba tiempos monotónicos inyectables y devuelva:
  `logicalTicks`, `fixedDelta`, `renderDue`, `interpolationAlpha` y deadline.
- Usar aritmética de duración de alta precisión, no milisegundos enteros.
- Mantener deadlines absolutos (`next += period`) para no acumular deriva.
- Limitar el delta real tras suspensión/foco y el número de ticks de catch-up.
- Añadir una prueba CTest con reloj falso para 50, 59.94, 60, 75, 90, 120,
  144, 165 y 240 Hz, VSync on/off, jitter y frames lentos.

**Criterio:** tras 10 minutos simulados, los ticks deben ser exactamente los
esperados con un error temporal inferior a un tick y sin dependencia del Hz.

### Fase 2 — Corregir primero la simulación autoritativa

- Hacer que cada iteración lógica publique `mDeltaTime = 1/60` o `1/30` según
  `mFrameRate`.
- Como paso de integración temporal, se permite mantener render y actualización
  juntos a la cadencia lógica únicamente hasta que las pruebas del scheduler
  pasen. Ese estado intermedio no cumple el objetivo final ni debe publicarse
  como soporte de alto refresco terminado.
- VSync Off podrá eliminar sincronización con el monitor, pero no el deadline
  lógico. Si se desea benchmark sin límite, deberá ser una opción de desarrollo
  explícita que tampoco altere el paso lógico.
- Restablecer el planificador al cambiar sección, ventana, modo de pantalla o al
  volver de una pausa larga.

**Criterio:** 30/60 ticks por segundo en todos los refrescos, sin aceleración
con VSync Off y sin deriva acumulada.

### Fase 3 — Auditar consumidores dependientes de frame

- Clasificar usos de `getFrameTime()`, contadores `++`, animadores, partículas,
  cinemáticas, input, reloj del día y temporizadores de IA.
- No convertir masivamente constantes `30.0f`/`60.0f`: muchas son velocidades,
  ángulos o parámetros y no representan frecuencia.
- Verificar transiciones `setFrameClamp(1) <-> setFrameClamp(2)` y que ningún
  subsistema aplique además un factor compensatorio histórico.
- Añadir pruebas offline para reloj del mundo y cualquier animador/timer que se
  pueda aislar sin assets.

**Criterio:** comportamiento idéntico al original a cadencia nativa y ninguna
compensación duplicada.

### Fase 4 — Presentación obligatoria a la frecuencia nativa

- Dividir de forma explícita `updateFixed()` y `render(alpha)` en el nivel más
  alto posible. No llamar dos veces a lógica, input por flanco, audio o RNG para
  producir un frame extra.
- Conservar estados visuales anterior/actual e interpolar únicamente datos de
  render seguros: cámara y transformaciones. No interpolar estado autoritativo,
  colisiones, IA, partículas o datos guardados.
- Cubrir cámara, actores, modelos animados, partículas y UI con una estrategia
  explícita. Repetir el último estado solo se admite como fallback transitorio
  documentado: el objetivo de cierre es movimiento visual fluido a Hz nativos.
- Sincronizar presentación mediante VSync/adaptive VSync cuando sea fiable y
  ofrecer limitador software como fallback. La frecuencia detectada decide
  cuándo presentar, nunca cuánto simular.

**Criterio:** 75/90/120/144/165/240 presents reales y uniformes, medidos en el
backend, con 30/60 ticks autoritativos constantes y sin vibraciones, dobles
inputs, físicas distintas ni divergencia de partida.

### Fase 5 — Robustez y validación del usuario

- Probar ventana, borderless y fullscreen; VSync on/off; 50–240 Hz; Alt-Tab,
  mover ventana entre monitores y cambio de refresco en caliente.
- Probar título, cinemática, partida nueva, carga, gameplay, pausa, F1/F2 y
  transiciones de nivel.
- Comparar duración real, reloj del día, distancia recorrida y frames/ticks de
  una misma secuencia.
- Ejecutar sesiones largas y una build sanitizer offline; el usuario realiza
  todas las pruebas in-game.

## Pruebas offline mínimas

1. `30 Hz`: 300 s -> 9000 ticks.
2. `60 Hz`: 300 s -> 18000 ticks.
3. Presentación a cada Hz de la matriz sin cambiar esos conteos.
4. Jitter alterno de +/-2 ms sin deriva.
5. Bloqueo de 250 ms con catch-up acotado.
6. Pausa de 5 s tratada como suspensión, sin 150/300 ticks repentinos.
7. Cambio 60 -> 30 -> 60 sin tick doble ni delta incorrecto.
8. VSync Off sin aumento de ticks lógicos.
9. Deadline absoluto sin deriva tras 10 minutos de reloj simulado.

## Orden de archivos para el siguiente chat

1. `AI_HANDOFF.md`, `ROADMAP.md` y este documento completos.
2. `src/sysDolphin/system.cpp` y `include/system.h`.
3. `src/sysDolphin/dgxGraphics.cpp`.
4. `pc_port/pc_window.cpp`, `pc_port/pc_window.h` y `vi_stubs.cpp`.
5. Puntos de entrada de `app->idle()` y cambios de `setFrameClamp`.
6. Configuración de VSync/refresco en `pc_port/settings/pc_settings.cpp`.

## Restricciones

- No ejecutar el juego; las pruebas in-game corresponden al usuario.
- No cambiar velocidad mediante escalas arbitrarias por escena o monitor.
- No asumir que más FPS de presentación permiten más ticks de simulación.
- La interpolación es obligatoria para cerrar la tarea, pero se integra después
  de probar el fixed-step que protege física y timing.
- Conservar el worktree y usar `apply_patch` para editar.

## Referencia contrastada: Dusklight

Se auditó el repositorio oficial `TwilitRealm/dusklight` en el commit
`e2bf07805ecb61332031ac140451098338f544c0` (2026-08-30). Su solución confirma
la arquitectura que debe seguir este port y, sobre todo, descarta emparejar
matrices por el orden de llamadas GX:

- La simulación conserva un periodo fijo y la presentación se ejecuta por
  separado. Cuando la interpolación está activa, el reloj mantiene la
  simulación un tick por detrás para disponer de dos estados completos.
- Cada tick registra matrices finales en dos mapas `previous/current`. La clave
  es una identidad estable: normalmente la dirección persistente de la matriz,
  del modelo, de la partícula o una clave derivada explícitamente del objeto.
  Nunca es el índice de la llamada de render.
- En cada presentación se construye un mapa de reemplazos interpolados. Los
  puntos centrales del motor (`J3DModel`, concatenación de matrices y carga de
  matrices) consultan ese reemplazo sin modificar el estado autoritativo.
- La cámara se captura como datos semánticos (eye, center, up, bank, fov,
  aspect, near y far), se interpola antes de dibujar y se restaura después.
- Elementos que no quedan cubiertos por matrices persistentes —partículas,
  cuerdas, cadenas, recortes de cámara y UI— usan callbacks o rutas específicas.
  Existe además una petición de `presentation sync` para pausa, mapas, capturas
  y transiciones donde interpolar produciría un frame incoherente.

### Aplicación segura a Pikmin

1. Separar una única actualización autoritativa de 30 Hz de una presentación
   de 60 Hz, dejando input, audio, RNG, mensajes y cambios de sección dentro
   del tick lógico.
2. Añadir un registrador `previous/current/replacements` probado offline. Las
   claves deben proceder de objetos persistentes; las matrices temporales de
   pila y el orden de `GXLoad*` quedan expresamente prohibidos.
3. Integrar primero `BaseShape`: sus matrices de animación se consumen desde
   `getAnimMatrix()`, pero el buffer se reasigna secuencialmente cada frame.
   La auditoría del 2026-08-31 demostró además que `BaseShape` tampoco identifica
   una instancia: todos los Pikmin de un tipo comparten `PikiShapeObject` y el
   mismo modelo se sobrescribe al dibujar cada criatura. La clave debe ser
   `(owner visual persistente, índice de matriz)`, pasando el owner desde
   `ViewPiki`/actor/objeto; quedan prohibidos tanto `BaseShape*` aislado como la
   dirección del bloque devuelto por `Graphics::getMatrices()`.
4. Interpolar la cámara por sus campos semánticos y reconstruir sus matrices
   sólo durante presentación, con copia/restauración alrededor del dibujo.
5. Cubrir transformaciones de `Creature::mWorldMtx` y los renderizadores que no
   pasan por `BaseShape` con claves de objeto explícitas. UI y efectos quedan
   inicialmente sincronizados al tick hasta disponer de una ruta específica.
6. Activar 60 Hz únicamente tras pruebas de altas/bajas de claves, cambio de
   escena, primera muestra y teleport; ante cualquier discontinuidad se usa el
   estado actual sin interpolar.

### Frontera extraída el 2026-08-31

`PlugPikiApp::idle()` conserva exactamente su secuencia previa, pero update y
render están ahora expresados como `updateFixed()` y
`renderAuthoritativeFrame()`. La segunda función no es todavía una presentación
de sólo lectura y su nombre lo hace explícito. La auditoría encontró al menos:

- `BaseShape::updateAnim()` llama a `AnimContext::animate()` desde refresh.
- Varias rutas llaman a `ShapeDynMaterials::animate()` durante el dibujo.
- `PlugPikiApp::draw()` modifica alpha/timers de carga y fade global.

Hasta mover o proteger todas esas escrituras, `renderAuthoritativeFrame()` debe
ejecutarse una sola vez por tick y no puede usarse para fabricar 60 presents.

El contexto `PcRenderPhase` ya distingue ambos modos y asigna un serial sólo a
ticks autoritativos; las presentaciones conservan el serial y publican alpha
acotado. `System::run` entra explícitamente en autoritativo antes de input,
audio y lógica. En presentación, `BaseShape::updateAnim()` ya no avanza el
`AnimContext`, las animaciones PVW de color/textura/TEV reutilizan su frame
actual y `PlugPikiApp::draw()` no avanza timers ni fades. El modo de
presentación todavía no se invoca: quedan más rutas de refresh/partículas y la
identidad por actor antes de hacerlo visible.

La identidad por actor ya se propaga en las 37 llamadas reales a
`BaseShape::updateAnim()`: Pikmin/Navi, enemigos, bosses, items, plantas, mapa,
efectos, piezas y actores cinematográficos pasan su instancia persistente. El
modelo compartido guarda ese owner junto al buffer transitorio. La captura se
realiza en `drawshape()`/`drawculled()`, después de ajustes de pose y antes de
consumir matrices. `PcVisualRuntime` adapta bloques 4x4 al almacén y su test
verifica dos actores, dos matrices y orden invertido. Queda desactivado por
defecto para no pagar mapas/copias por hueso antes de habilitar la presentación;
el test lo activa expresamente. Aún no se reemplazan matrices del juego.

La sustitución está preparada mediante `PcVisualShapeOverride`: sólo funciona
en fase Presentation, resuelve la pose completa de forma transaccional, guarda
el buffer autoritativo y lo restaura por RAII al terminar `drawshape` o
`drawculled`. Una clave ausente —incluso la última— deja toda la pose original
intacta. El runtime continúa apagado por defecto.

También se cerró otra familia de mutaciones: `AnimContext::animate`,
`Animator::animate`, `PaniAnimator::animate` y los coordinadores de Pikmin,
pellets y nave no avanzan frames ni disparan eventos durante Presentation. En
Authoritative su comportamiento permanece igual.

La base semántica de cámara está ahora aislada en `PcCameraSnapshotStore`.
Captura por identidad de `Camera`: position, focus, up, FOV, aspect, near/far y
blur. Interpola campos —normalizando up—, no matrices GX; un cambio a otra
cámara usa directamente su estado actual y un corte marcado suprime el blend.
Rechaza NaN, FOV/aspect inválidos y rangos de clipping incoherentes. Su CTest
cubre gameplay/movie camera, orden variable, corte y validación.
`PcCameraRuntime` ya se conecta a `DGXGraphics::setCamera`: captura en
Authoritative y, durante Presentation, aplica el estado semántico, reconstruye
look-at/ejes/frustum y restaura el objeto `Camera` completo al final del frame.
Se mantiene desactivado por defecto; la proyección GX continúa reconstruyéndose
por la ruta normal de cada sección.

La auditoría de partículas encontró una excepción concreta al render de sólo
lectura: `particleGenerator::drawPtclOriented()` persistía la normal calculada
y marcaba partículas degeneradas como terminadas. Esas escrituras sólo se
permiten ahora en Authoritative. Las entradas `particleManager::update()`,
`particleGenerator::update()` y `simplePtclManager::update()` retornan sin
mutar durante Presentation. Las demás rutas de dibujo de partículas revisadas
—billboard, hijos y simples— construyen estado GX/local sin avanzar edad,
posición, color ni RNG. Queda pendiente auditar otras escrituras de refresh
antes de activar una presentación adicional real.

La siguiente pasada detectó lógica de gameplay/UI alojada directamente en
render: `BaseGameSection::draw()` avanzaba los fades de sección/banner y llamaba
a `ModeState::postUpdate()`, `GameCoreSection::draw()` repetía
`SeSystem::update()`, y `LifeGauge::refresh()` avanzaba transiciones, suavizado
de vida y temporizadores. Todas esas operaciones quedan limitadas ahora a
Authoritative; Presentation sólo consume sus valores actuales para dibujar.
La auditoría estática aún enumera otras rutas `refresh` con escrituras (por
ejemplo plataformas, colisiones, luces y algunos menús), así que esto no es
todavía autorización para repetir el árbol completo de draw.

La pasada posterior cerró precisamente plataformas, colisiones, culling y pose
compartida. `CollInfo::updateInfo()` y `CreaturePlatMgr::update()` sólo mutan en
Authoritative. Los setters de `CF_UseAICulling` y `mTekiOptions` ignoran
Presentation, evitando que una cámara interpolada cambie la IA siguiente. Las
luces de la nave tampoco avanzan frame ni disparan audio desde un draw extra.

`BaseShape::updateAnim()` ahora sólo asigna el owner y retorna en Presentation:
no reserva/reconstruye el buffer compartido ni toca el frame cache. La pose
completa, incluida cualquier corrección manual, procede de
`PcVisualShapeOverride`. Por el mismo motivo se bloquean en Presentation
`calcWeightedMatrices`, el look-at de cabeza de Navi/Pikmin y las deformaciones
de cuerpo de Snake, King, Spider y Slime. Así, las lecturas de joints posteriores
no heredan matrices recalculadas con la cámara presentada. Aún hay que revisar
salidas derivadas de joints, efectos/UI y transiciones de escena antes de
activar el segundo present.

Esta ruta intentaba evitar las dos causas ya observadas —reutilización de slots
GX entre objetos distintos y matrices/cámara incompatibles—, pero la prueba
posterior demostró que no bastaba: el segundo `renderall()` corrompió también
estado gráfico no capturado. Se conserva arriba sólo como registro histórico.

## Prompt para continuar

```text
Quiero continuar el port nativo Linux de Pikmin 1 situado en:
/home/sunking/Documentos/antigravity/pikmin

Lee primero y completamente:
- AI_HANDOFF.md
- ROADMAP.md
- REFRESH_RATE_TIMING_PLAN.md
- 60FPS_FAILED_ATTEMPTS.md

Objetivo prioritario: hacer que el port renderice realmente a la frecuencia
nativa seleccionada —75/90/120/144/165/240 Hz— a velocidad normal y sin bugs
de físicas. Para ello debe conservar ticks autoritativos fijos de 60 Hz para
setFrameClamp(1) y 30 Hz para setFrameClamp(2), desacoplar update y render, e
interpolar el estado visual entre ticks. No aceptes como resultado final un
port visualmente limitado a 30/60 FPS.

Empieza inspeccionando el estado real posterior al rollback; no supongas que
las integraciones históricas del scheduler o los runtimes siguen presentes.
Conserva la ruta funcional de 30 FPS como fallback. Diseña primero captura y
replay de paquetes inmutables en la frontera GX -> OpenGL: el código del juego
debe recorrer `renderall()` una sola vez por tick autoritativo. Demuestra replay
visualmente idéntico del mismo paquete antes de añadir interpolación. Después
interpola gradualmente cámara y transformaciones con owner estable.

Reglas:
- No ejecutes el juego; yo hago las pruebas in-game.
- Puedes inspeccionar, modificar, compilar y ejecutar pruebas offline.
- Conserva el worktree existente, que contiene muchos cambios.
- Usa apply_patch para editar.
- Actualiza ROADMAP.md, AI_HANDOFF.md y el plan con los avances.
- Busca una solución estructural; no uses escalas o excepciones por monitor.
- VSync solo debe afectar a presentación, nunca al reloj lógico.
- No alteres audio, input o RNG más veces para fabricar frames adicionales.
- Está prohibido fabricar frames llamando otra vez a `renderall()`, `draw()`,
  `refresh()`, `animate()` o `updateAnim()` dentro del mismo tick lógico.
- No reapliques `pc_decide_presentation` ni el parche de activación fallido.
- No uses orden/slot GX ni punteros de buffers compartidos como identidad.

Antes de cambiar código, reporta brevemente la arquitectura temporal encontrada
y los invariantes que vas a preservar. Al terminar, compila y ejecuta CTest,
pero no abras el juego.
```

**Corrección de estado (2026-09-01):**
La prueba in-game de la activación descrita anteriormente falló: al habilitar
los presents intermedios mediante otro recorrido completo de `renderall()`, el
juego quedó en blanco y negro y mostró corrupción gráfica. El usuario hizo
rollback y volvió a 30 FPS funcionales. Por tanto, los 11/11 tests offline sólo
validaron componentes aislados; no validaron el segundo render ni equivalencia
GX/TEV. No se debe reponer `pc_decide_presentation` ni esa integración a partir
de esta nota histórica.

La próxima implementación debe capturar durante el único render autoritativo
un paquete completo e inmutable en la frontera GX -> OpenGL y reproducirlo sin
invocar código de juego. Primero debe demostrar replay idéntico sin
interpolación; después podrá interpolar cámara y owners estables gradualmente.
Los detalles y prohibiciones están en `60FPS_FAILED_ATTEMPTS.md`.
