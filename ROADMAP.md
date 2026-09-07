# Pikmin PC Port — Hoja de ruta

Última actualización: 2026-09-04

> ## Configuración de referencia validada (2026-09-01)
>
> **Este es el objetivo alcanzado y el estado que debe conservarse.** Validado
> in-game por el usuario sobre GTX 1050 4 GB / Ubuntu / Mesa 26.0.
>
> - **1920x1080, `renderScale = 1`** (resolución interna nativa, sin reescalado).
> - **60 FPS en menús, título y selección de mapa**, y **30 FPS en gameplay**.
>   No son cifras impuestas por el port: son los `setFrameClamp(1)` y
>   `setFrameClamp(2)` del propio juego. `fpsMode = 0`.
> - **Especialización TEV activada por defecto** (`PERF-NATIVE-003`). Sin ella
>   estas cifras no se alcanzan a 1080p.
> - Sin fallos gráficos.
>
> GPU medida con `PIKMIN_PERF_STATS=1`: escena ~3,7-5,9 ms y blit ~1,0 ms a
> 1080p nativo, frente a ~55 ms de título y ~32-34 ms de gameplay que costaba
> antes **a media resolución**. El presupuesto de 16,6 ms queda holgado.
>
> Escapes si algo se rompe en el futuro: `PIKMIN_TEV_SPECIALIZE=0` vuelve al
> über-shader (correcto pero muy lento), `PIKMIN_TEV_MAX_STAGES=N` acota la
> especialización por complejidad de material, `PIKMIN_GL_CHECK=1` nombra la
> escritura de uniform que OpenGL rechace y `PIKMIN_DUMP_SHADERS=1` vuelca los
> shaders generados y lee de GL los samplers de cada programa.
>
> ### Cómo volver a esta build
>
> El versionado de este proyecto son **copias locales**, no git. El repositorio
> `.git` que hay conserva la procedencia de la decompilación original y no
> estorba, pero no cubre el trabajo del port: `pc_port/` entero está sin
> rastrear. La red de seguridad son los snapshots de fuentes:
>
> ```sh
> tools/snapshot.sh list                    # ver los disponibles
> tools/snapshot.sh restore snapshots/20260901-185758-estable-60-30.tar.zst
> cmake --build build -j"$(nproc)"
> ```
>
> `20260901-185758-estable-60-30.tar.zst` es exactamente esta configuración de
> referencia, verificada byte a byte contra el árbol en el momento de crearla.
> `restore` guarda antes el estado actual como `pre-restore`, de modo que
> restaurar nunca puede ser lo que pierda trabajo. Sólo toca fuentes: la
> carpeta `build/`, los assets y las partidas guardadas quedan intactos.
>
> Además, todo trabajo experimental posterior debe ir **detrás de un
> interruptor apagado por defecto**, para que revertir sea normalmente cambiar
> un ajuste y no restaurar archivos.
>
> **No perseguir 60 FPS en gameplay.** El objetivo fijado por el usuario es el
> comportamiento original del juego: 30 Hz de lógica en gameplay. Eso deja
> TIME-002 fuera del camino crítico.

> **Si el juego va lento, empieza por `RENDIMIENTO.md`** y por
> `tools/check-rendimiento.sh`, que comprueba las causas conocidas en un
> segundo e imprime el comando que corrige cada una.

> **Alerta de rendimiento (2026-09-04):** la build había perdido rendimiento
> sin cambios en el renderizador. La causa fue de compilación, no de código:
> al separar las fuentes decompiladas en `pikmin_legacy`, `-O3`,
> `-march=native` e IPO quedaron aplicadas sólo a `pikmin_pc`, y `build/`
> estaba además configurado como `Debug` (`-O0`). Corregido en
> PERF-NATIVE-007. Antes de medir nada, confirmar el tipo de build y que
> `build/CMakeFiles/pikmin_legacy.dir/flags.make` contiene `-O3`.

> **Alerta TIME-002:** la activación más reciente de 60 FPS falló in-game con
> imagen en blanco y negro y corrupción gráfica, y fue revertida por el usuario.
> No repetir un segundo recorrido de `renderall()`. Véase
> `60FPS_FAILED_ATTEMPTS.md` antes de tocar temporización o presentación.

Este documento es el registro principal del progreso del port. Cada cambio debe
actualizar su casilla y añadir una entrada breve al historial con la prueba que
confirma el resultado.

## Estados

- `[ ]` Pendiente.
- `[~]` En curso o corregido parcialmente.
- `[x]` Terminado y verificado.
- `[!]` Bloqueado o con una regresión conocida.

## Regla para marcar una tarea como terminada

Una tarea solo pasa a `[x]` cuando:

1. El código compila en una build limpia.
2. Se ha probado el escenario afectado.
3. No aparecen errores nuevos de ASan/UBSan cuando la prueba sea aplicable.
4. Se registra abajo qué se probó y cuál fue el resultado.

---

## Fase 0 — Recuperar una base verificable

- [x] **BUILD-001 — Recuperar la compilación de `pc_gfx.cpp`.**
  - Declarar/cargar de forma coherente las funciones OpenGL de diagnóstico.
  - Unificar los sufijos `_fn` y `_ptr`.
  - Criterio: `cmake --build build -j4` termina correctamente.

- [x] **BUILD-002 — Recuperar la build con sanitizers.**
  - Compilar con AddressSanitizer y UndefinedBehaviorSanitizer.
  - Criterio: el ejecutable sanitizer se construye sin errores.

- [x] **TEST-001 — Definir recorridos de prueba repetibles.**
  - Arranque y título.
  - Selector de archivo.
  - Cinemática inicial completa y saltada.
  - Primer nivel hasta la cebolla y la primera pastilla.
  - Cambio de día o recarga de nivel.
  - Procedimiento y plantilla de resultados: [`TEST_PLAN.md`](TEST_PLAN.md).

---

## Fase 1 — Decodificación GX y materiales

Esta fase tiene prioridad máxima. No se deben compensar sus errores ajustando
manualmente el brillo de modelos concretos.

- [~] **GX-TEV-001 — Corregir A/B/C/D del combiner de color.**
  - A: bits 12–15; B: 8–11; C: 4–7; D: 0–3.
  - Criterio: los valores obtenidos del display list coinciden con llamadas
    directas equivalentes a `GXSetTevColorIn`.

- [~] **GX-TEV-002 — Corregir el combiner de alpha.**
  - Respetar los bits 0–3 usados por swap mode.
  - A: bits 13–15; B: 10–12; C: 7–9; D: 4–6.
  - Criterio: partículas y materiales transparentes reciben los cuatro
    operandos correctos.

- [~] **GX-TEV-003 — Implementar la ecuación TEV real.**
  - Usar `d + a * (1 - c) + b * c` para suma y su equivalente para resta.
  - Aplicar bias, scale y clamp con la semántica de GX.
  - Criterio: no se necesita la fórmula compensatoria `d + b*c + a`.

- [~] **GX-TREF-001 — Decodificar completamente TEV order/TREF.**
  - Extraer texture map, texture coordinate, texture enable y raster channel
    para las dos etapas de cada registro.
  - Criterio: las etapas sin textura no aparecen como `TEXMAP0` y COLOR1 se
    selecciona cuando corresponde.

- [~] **GX-TEX-001 — Separar `GXTexMapID` de `GXTexCoordID`.**
  - El shader no debe elegir UV usando el índice del mapa.
  - Criterio: una etapa puede usar mapa 0 con coordenada 1 correctamente.

- [~] **GX-TEX-002 — Ampliar el soporte de coordenadas y matrices de textura.**
  - Soportar los slots que realmente usan los assets, hasta el máximo de GX.
  - Incluir fuentes POS, NRM y TEX0–TEX7 necesarias.
  - La tubería TEV del título demostró uso de TEXCOORD2. El vértice PC conserva
    ahora TEX0–TEX7 y cada generador puede seleccionar POS, NRM o una fuente
    TEX0–TEX7 con matriz independiente.
  - Corregido además que `uTcMode/uTcMtx` se declaraban pero nunca se enlazaban,
    por lo que la generación configurada no llegaba al shader.
  - La primera versión transportó ocho varyings dinámicos hasta fragmento: no
    cambió el suelo y hundió el título de ~17-18 a ~10 FPS por coste de
    interpolación/selección. Se retiró esa forma; el shader transporta ahora
    explícitamente los cuatro slots demostrados/adyacentes (0-3), mientras el
    vértice conserva los ocho para ampliar sólo cuando un asset lo demuestre.
  - Build Release correcta; pendiente medir que la mitigación recupera FPS y
    validar UI, cinemáticas y gameplay antes de cerrar la tarea.
  - La medición confirmó recuperación completa: ~17-18 FPS, render CPU
    ~15,5 ms y wait/swap ~40 ms. TEXCOORD2 no alteró el suelo; no es su causa.

- [x] **GX-TEX-003 — Implementar texturas de intensidad e indexadas.**
  - I4/I8 deben replicar intensidad en RGB y alpha.
  - Implementar C4, C8 y C14X2 con paletas IA8, RGB565 y RGB5A3.
  - Criterio: UI y partículas indexadas conservan color y máscara alpha.

- [~] **GX-TEV-004 — Implementar Konst por etapa.**
  - Aplicar `GXSetTevKColorSel` y `GXSetTevKAlphaSel`.
  - Evitar que todas las etapas usen siempre `uKonst0`.

- [~] **GX-TEV-005 — Implementar operaciones comparativas TEV.**
  - `GX_TEV_COMP_R8`, `GR16`, `BGR24`, `RGB8` y variantes EQ/GT.

- [~] **GX-TEV-006 — Corregir swap tables y swap selectors.**
  - Cada selector debe elegir R, G, B o A del mismo valor de entrada.

- [ ] **GX-FOG-001 — Implementar fog básico.**
  - Empezar por `GX_FOG_NONE` y `GX_FOG_LINEAR`.

---

## Fase 2 — Iluminación GX

- [~] **GX-XF-001 — Decodificar correctamente `GXSetChanCtrl`.**
  - `matSrc`: bit 0; enable: bit 1; luces 0–3: bits 2–5.
  - `ambSrc`: bit 6; diffuse: bits 7–8; attenuation: bits 9–10.
  - Luces 4–7: bits 11–14.

- [ ] **GX-LIGHT-001 — Respetar `GXDiffuseFn`.**
  - Diferenciar NONE, SIGN y CLAMP.

- [ ] **GX-LIGHT-002 — Corregir atenuación angular y por distancia.**
  - Aplicar coeficientes A y K con la semántica de GX.
  - Revisar luces direccionales frente a luces posicionales.

- [~] **GX-LIGHT-003 — Verificar el canal especular COLOR1.**
  - Hacerlo después de GX-XF-001 y GX-TEV-001/002/003.
  - Criterio: la nave conserva detalle y no queda blanca o saturada.

- [ ] **GX-LIGHT-004 — Verificar modelos de gameplay.**
  - Olimar, nave, cebolla, Pikmin, enemigos y objetos.
  - Criterio: los modelos responden a luces y no usan colores planos.

---

## Fase 3 — Partículas, transparencia y efectos

- [~] **FX-001 — Corregir cuadrados blancos en partículas.**
  - Verificar primero TEV alpha, TREF y textura activa.
  - Después revisar blend y alpha compare.
  - **Causa encontrada 2026-09-04 (pendiente de validar in-game).** No era ni
    la textura, ni el alpha, ni el blend: los tres se comprobaron y son
    correctos. `DGXGraphics::setBlendMode` leía **`GX_CC_C2`** en los modos de
    mezcla 2 y 3, es decir el registro TEV 2. El color de entorno de la
    partícula lo aporta `setPrimEnv(prim, env)`, que escribe `GX_TEVREG0` y
    `GX_TEVREG1` — los que leen `GX_CC_C0` y `GX_CC_C1`. **Nadie escribe nunca
    `GX_TEVREG2` en el camino de partículas**, así que ese término tomaba lo
    que hubiera dejado el dibujo anterior: el blanco opaco que `P2DPicture`
    guarda ahí para la interfaz 2D. De ahí el cuadrado blanco.
  - Corregido `GX_CC_C2` → `GX_CC_C1` en los modos 2 y 3.
  - Comprobaciones que descartaron el resto, y que no hace falta repetir:
    `hokori4.bti` es I8 y el port lo decodifica con alfa = intensidad (correcto
    en GX); `pc_gfx_set_blend_mode` ignora el *logic op* cuando el modo es
    `GX_BM_BLEND`, como debe; `uTevReg0/1/2` se suben desde
    `sTevRegisters[GX_TEVREG0/1/2]`, sin desfase.
  - Los datos de los `.pcr` confirman qué registro se pretendía. Leídos con el
    lector offline `tools/leer_pcr.py`: `sd_rakk1` (modo 2) lleva prim
    (111,106,78) y env (63,50,15) — interpolar entre ambos da una nube de
    tierra de marrón oscuro a claro; `sd_rakk2` (modo 3) lleva prim (87,87,63)
    y env (15,15,0) — base casi negra con motas de tierra. Con un registro sin
    escribir, ambos salen blancos. Además `blendFactor = 0x54`
    (SRCALPHA/INVSRCALPHA) y `zMode = 0x0B` (LEQUAL, sin escritura de Z), que
    es exactamente lo que corresponde a una partícula.
  - Afecta a todo lo que use `sd_rakk1`/`sd_rakk2`: desenterrar un Pikmin,
    aterrizajes de Olimar y enemigos, y `Mizu`.
  - Si tras validar siguiera mal, la hipótesis alternativa es la simétrica:
    que sea `setPrimEnv` quien deba escribir `GX_TEVREG2` en vez de
    `GX_TEVREG1`. Snapshot previo:
    `snapshots/20260904-003310-antes-fix-particulas-blancas.tar.zst`.

- [ ] **FX-002 — Revisar la luz/halo que sigue a Olimar.**
  - Determinar si es una partícula, flare, billboard o material aditivo.

- [ ] **FX-003 — Verificar sombras y texturas con fondos negros.**
  - Revisar alpha, formatos IA/RGBA/CMPR y blend mode.

- [ ] **FX-004 — Verificar efectos de nave y cebolla.**
  - Propulsión, humo, destellos, rayos y absorción de Pikmin/pastillas.

---

## Fase 4 — Robustez de gameplay y recursos

- [~] **ROUTE-001 — Validar grupos e índices de waypoints.**
  - `RouteMgr::getWayPoint` ya valida el índice.
  - Pendiente: adaptar todos los consumidores al posible `nullptr`.

- [ ] **ROUTE-002 — Endurecer cebolla, nave, Pikmin y transporte.**
  - Evitar desreferencias directas después de búsquedas de waypoint.
  - Definir una recuperación segura cuando una ruta no exista.

- [ ] **ENDIAN-001 — Corregir IDs de partes corporales de enemigos.**
  - Canonicalizar `TekiParameters::mParaIDs` en la frontera de lectura/escritura.
  - No modificar globalmente `ID32`.

- [ ] **STREAM-001 — Validar conteos, tamaños y offsets serializados.**
  - Comprobar tamaño restante antes de reservar arrays o avanzar streams.
  - Añadir límites razonables a cantidades provenientes de assets.

- [ ] **INDEX-001 — Auditar índices de managers restantes.**
  - Items, plantas, pellets, enemigos, bosses y work objects.

- [ ] **STUB-001 — Convertir stubs PC alcanzables en errores identificables.**
  - No activar indiscriminadamente stubs del SDK GameCube que no se compilan.
  - Registrar la primera llamada a funciones relevantes no implementadas.

---

## Fase 5 — Memoria y estabilidad prolongada

- [x] **MEM-001 — Hacer `invalidateObjs` seguro en 64 bits.**

- [x] **MEM-002 — Corregir `DataChunk` para usar `delete[]`.**

- [x] **MEM-003 — Inicializar a cero las reservas del `new` global en PC.**

- [~] **MEM-004 — Definir ciclos de vida equivalentes a los AyuHeap.**
  - El asignador PC evita corrupción entre hilos, pero los `resetHeap()` no
    liberan las reservas normales hechas en el heap C.
  - Al salir informa asignaciones/bytes vivos, picos, total y liberaciones de
    punteros ajenos al heap C para poder medir sesiones reales.
  - Medir crecimiento al cambiar repetidamente de día y nivel.
  - La sesión larga del 29-08 informó 141.316 reservas y 1.602.698.616 bytes
    vivos, pero el ejecutable aún importaba los `new/delete` de libstdc++: esa
    cifra no era global. Los reemplazos son ahora símbolos fuertes y la salida
    incluye `frees`; repetir la sesión con esta build.

### Rendimiento (prioridad inmediata)

- [x] **PERF-001 — Retirar instrumentación del camino crítico gráfico.**
  - Eliminados el hash y registro a disco de cada primitiva, los volcados de
    draw calls, las consultas síncronas `glGetError` y los cronómetros de cada
    subsistema por fotograma.
  - Los uniformes TEV solo se envían para las etapas activas.
  - Eliminado el antiguo limitador duplicado; la presentación usa vsync más un
    pacing lógico que descuenta el tiempo ya consumido por el frame.
  - Criterio: medir FPS en título, cinemática y gameplay con la build Release.

- [x] **PERF-002 — Reducir cambios de estado y transferencias por draw call.**
  - Cachear uniformes, texturas y estados OpenGL que no hayan cambiado.
  - Sustituir las recreaciones frecuentes de buffers por streaming/orphaning
    medido, sin agrupar primitivas con estados GX distintos.
  - Criterio: perfil confirma que el coste por draw deja de dominar el frame.

- [~] **PERF-004 — Agrupar micro-tiras dentro de display lists.**
  - El scope por actor aisló ~8.964 draws en `titles/logo.mod` y ~3.100 en
    `opening.mod`; 13.063 de 13.074 primitivas son triangle strips diminutas.
  - El parser combina ahora sólo tiras consecutivas dentro de la misma display
    list, mediante triángulos degenerados y corrección de paridad. Vacía el
    lote antes de cualquier comando BP, XF, CP, indexed load o nested DL, por
    lo que nunca cruza un cambio de estado GX.
  - `[PERF]` diferencia draws OpenGL resultantes de primitivas GX fuente para
    medir cobertura real. Build Release correcta; falta validar imagen y FPS.
  - Validación in-game: imagen sin regresión, 13.063 primitivas se reducen a
    617 draws; `opening.mod` baja de ~3.100 a 4 y `logo.mod` de ~8.964 a 399.
    Render CPU mejora de ~15,5-17 ms a ~7 ms. El FPS permanece ~17 porque
    wait/swap ronda 52 ms; el cuello restante está fuera del envío CPU.
  - El log siguiente reveló `vsync=0` pero `pc_window_swap_buffers()` seguía
    aplicando siempre el pacing software. Ahora sólo duerme cuando VSync está
    activado; con VSync Off la presentación queda libre mientras el delta/VI
    lógico de la simulación permanece independiente. Pendiente medir in-game.

- [~] **PERF-003 — Reducir coste por píxel a resoluciones altas.**
  - El render target se dimensiona desde el viewport de salida exacto, sin
    cuantizar antes el aspect ratio mediante una base de 480 líneas. A escala
    1.0, 1920x1080 ya produce 1920x1080 en vez de 1919x1080.
  - La presentación omite el clear completo del backbuffer cuando el blit cubre
    toda la ventana; conserva el clear cuando existen bandas negras.
  - Pendiente medir in-game 1080p/1440p/4K y usar GPU timing antes de decidir
    entre shaders TEV especializados o render directo sin blit intermedio.
  - Medición del título a 1080p/1x: ~13.074 draws, 71.054 vértices, 606 DL,
    0 fast; CPU render 16,66 ms y wait/swap 44,93 ms, resultando ~16,2 FPS.
  - Añadida ruta rápida exacta para la etapa común `GX_MODULATE`
    (textura por raster, color y alpha), que antes caía en el evaluador genérico.
    Pendiente repetir `[PERF]` para conocer cobertura y ganancia.
  - La repetición sólo alcanzó 15 fast de 13.074 draws y ~16,6 FPS. Se añadió,
    únicamente bajo `PIKMIN_PERF_STATS`, un histograma sin asignaciones que
    informa etapas TEV y los cuatro patrones de una etapa más frecuentes.
  - El histograma aisló 9.959 draws de raster puro codificados como A=RASC,
    B=C=D=0 (y equivalente alpha). La ruta directa reconoce ahora tanto esta
    forma como RASC en D; matemáticamente son idénticas bajo add/bias0/scale1.
  - La validación posterior alcanzó 9.974 fast de 13.074 draws, pero sólo
    ~17,5-18,2 FPS: render CPU permanece en ~17-18 ms y wait/swap en ~36-40 ms.
    Esto descarta el coste del shader de una etapa como cuello principal y
    apunta a la cantidad de micro-draws. El diagnóstico informa ahora tipo de
    primitiva y vértices medios por cada ruta rápida antes de diseñar batching.
  - La captura del título confirma además que `opening.cin` dibuja el logotipo
    floral y la UI, pero el actor/escenario de fondo queda negro. Se añadió un
    histograma de la etapa final de materiales TEV múltiples (`[PERF TEVN]`)
    para separar ese defecto de corrección gráfica del trabajo de batching.
  - El nuevo log muestra que 13.063 de 13.074 draws son triangle strips y que
    la ruta raster dominante tiene sólo 5,04 vértices por llamada. Existe por
    tanto margen claro para agrupar tiras consecutivas, insertando degenerados
    y respetando cada cambio de estado GX/display-list.

- [~] **GX-TEV-007 — Separar las escrituras de salida RGB y alpha.**
  - El shader genérico asignaba el resultado de color como un `vec4` completo;
    cuando `colorOutReg != alphaOutReg`, destruía el alpha previo del registro.
  - Los materiales de fondo de 5/7 etapas terminan leyendo `A1`, por lo que esa
    corrupción podía anular su `CPREV`. RGB y alpha se escriben ahora sólo en
    sus componentes respectivos, como permite el hardware GX.
  - La prueba visual recuperó la vegetación completa alrededor del logotipo;
    permanecen negro el suelo/fondo y algunas hojas inferiores demasiado claras.
  - Se corrigió además el valor inicial de `TEVPREV`: el shader lo forzaba a
    cero en cada draw aunque BP E0/E1 forman un registro programable. Ahora
    `CPREV/APREV` reciben el valor real antes de la primera etapa.
  - La captura posterior no mostró cambios adicionales: vegetación recuperada,
    pero suelo negro y dos hojas inferiores grises. Se añadió una única traza
    `[PERF TEVPIPE5/7]` de las etapas completas y valores PREV/REG0-2 para
    localizar dónde se pierde `CPREV`/`A1` sin generar logs por draw.
  - Build Release correcta; pendiente analizar esa traza del usuario.
  - La primera TEVPIPE7 correspondía a `pikis/happas/leaf.mod`, cargado antes
    del escenario, así que no era evidencia del suelo. El diagnóstico etiqueta
    ahora scopes por actor cinematográfico y publica `[PERF ACTOR]` junto con
    `[PERF TEVPIPE5/7 <actor>]`, sin cambiar el contenido de los materiales.
  - Los scopes confirman que `opening.mod` contiene los ~2.792 draws de cinco
    etapas y ~308 de siete; `logo.mod` concentra ~8.964 draws raster simples.
    Falta obtener la línea `TEVPIPE7 cinemas/opening/opening.mod`, ausente del
    fragmento enviado, para diagnosticar específicamente el suelo.
  - La TEVPIPE7 de `opening.mod` termina en `CPREV * (1 - A1)` mientras A1 vale
    1 en la captura, produciendo negro exacto. Antes de corregir nada se añadió
    `[PERF MAT7]`: nombre/flags, registros S10, conteos y frames de animación
    para determinar si A1 base es correcto o no avanza su animación.
  - El primer fragmento de log no incluía MAT7 porque quedó entre mensajes de
    arranque. Ahora se repite cada 120 aplicaciones del mismo material, además
    de la primera, manteniendo el volumen acotado.
  - La traza repetida confirma que el material TEV7 tiene `anim=0` en los tres
    registros y colores base blancos opacos constantes. No es una animación
    bloqueada. Además, tras el batching TEV7 representa una sola de las cuatro
    llamadas de `opening.mod`; no asumir que esa tubería sea por sí sola todo
    el fondo ausente ni forzar A1 hasta identificar su geometría/material.
  - El diagnóstico multi-TEV informa ahora `[PERF TEVGEOM5/7]` con vértices,
    caja proyectada NDC y rangos UV activos. Permite identificar estructuralmente
    qué región cubre cada tubería sin sustituir materiales ni alterar píxeles.
  - La primera TEVGEOM5 recibida sólo ocupa NDC X `-0.052..0.024`, Y
    `-0.049..0.047`: no puede ser el fondo completo. La traza enumera ahora
    hasta cuatro llamadas por conteo de etapas (`#0..#3`) en vez de registrar
    únicamente la primera.
  - Las cuatro llamadas confirmaron además que los materiales consumen
    TEXCOORD2/3 mientras sus atributos directos son cero. Se encontró la causa
    general: el generador PC sólo resolvía `GX_TG_TEX0..7` y trataba las fuentes
    encadenadas `GX_TG_TEXCOORD0..6` como UV directo. El vertex shader evalúa
    ahora TEXCOORD0–3 en orden y permite que cada texgen use la salida de uno
    anterior, como GX. Pendiente validar que restaura las capas del fondo.
  - Causa raíz posterior: `GXTevColorArg` estaba numerado incorrectamente como
    `C0,C1,C2,A0,A1,A2`. El hardware y los datos PVW usan el orden intercalado
    `C0,A0,C1,A1,C2,A2`. Por ello el selector 6 de la etapa final se interpretó
    como A1, anulando el color, cuando realmente selecciona C2, preparado por
    la etapa anterior. Corregidos tanto `GXEnum.h` como `resolveC()` GLSL. Esta
    es una corrección global de la API GX, no una excepción para el título.

- [x] **TIME-001 — Restaurar la cadencia lógica independiente del rendimiento.**
  - `mFrameRate` vuelve a representar retraces por actualización también en PC:
    intervalo 1 para 60 Hz e intervalo 2 para los modos originales de 30 Hz.
  - `mDeltaTime` sigue midiendo tiempo real para los sistemas dependientes del
    tiempo, mientras el límite protege código heredado dependiente de frames.
  - Criterio: movimiento, animación, reloj del día y cinemáticas duran lo mismo
    aunque el equipo pueda renderizar por encima del objetivo.

- [~] **TIME-002 — Fixed-step verificable e independencia total del refresco.**
  - Ruta detallada en `REFRESH_RATE_TIMING_PLAN.md`.
  - Sustituir el delta real variable de la simulación por ticks fijos de 60/30
    Hz, con reloj monotónico, deadlines absolutos y catch-up acotado.
  - VSync y Hz del monitor controlarán exclusivamente la presentación; VSync
    Off tampoco podrá acelerar sistemas que avanzan una vez por tick.
  - Añadir un planificador con reloj falso y matriz CTest de 50 a 240 Hz antes
    de abordar interpolación/render de alto refresco.
  - Resultado obligatorio: presents fluidos a los Hz nativos seleccionados,
    no un límite visual de 30/60 FPS; desacoplar update/render e interpolar el
    estado visual sin ejecutar más ticks de físicas, IA, input, audio o RNG.
  - **Estado real 2026-09-01:** la activación con snapshots/guardas y un segundo
    `renderall()` falló in-game (blanco y negro y corrupción gráfica) y fue
    revertida por el usuario. No repetir esa arquitectura. El próximo prototipo
    debe capturar/reproducir paquetes inmutables desde el backend GX -> OpenGL,
    sin reentrar al árbol de render. Ver `60FPS_FAILED_ATTEMPTS.md`.
  - Scheduler e integración autoritativa terminados: la matriz 50–240 Hz,
    jitter, stall, suspensión y cambio de clamp pasan CTest. Un intento de
    repetir `draw()` a Hz nativos corrompió UI/estado GX y fue retirado;
    separación segura, snapshots e interpolación siguen pendientes.
  - El segundo intento limitado a 60 Hz también fue retirado: interpolar
    matrices por índice de llamada GX mezcló objetos por reutilización de slots
    y culling, generó polígonos gigantes e inmovilizó visualmente a Olimar.
    El usuario confirmó que había más FPS, pero las animaciones estaban
    aceleradas y fuera de tiempo, señal de que avanzaban también durante los
    presents adicionales. Próxima ruta obligatoria: snapshots con identidad en
    Camera/Creature/Shape y cero avance de animación fuera del tick lógico.
  - Primera unidad segura de esa ruta terminada offline: `PcVisualSnapshotStore`
    identifica cada muestra mediante propietario persistente, dominio y slot,
    nunca por dirección del buffer ni orden GX. Mantiene previous/current,
    copia matrices, descarta claves desaparecidas y soporta discontinuidades y
    sincronización de escena. Su CTest valida orden cambiante, altas/bajas,
    teleports, alpha y datos inválidos. Aún no se conecta al render ni activa
    frames adicionales: falta separar actualización de pose y presentación.
  - Extraída sin cambiar comportamiento la frontera `updateFixed()` /
    `renderAuthoritativeFrame()`: sigue habiendo exactamente una de cada por
    tick. El nombre autoritativo impide tratar el render actual como read-only;
    todavía avanza poses, materiales y fades.
  - Corrección de diseño: `BaseShape*` no es owner suficiente. Los Pikmin de un
    tipo comparten `PikiShapeObject`/modelo y sobrescriben sus matrices por
    criatura. La clave deberá recibir la identidad persistente de `ViewPiki`,
    actor u objeto llamante, además del índice semántico.
  - Añadido `PcRenderPhase`: cada tick autoritativo incrementa un serial; un
    present conserva ese serial y sólo publica alpha acotado. El scheduler entra
    en fase autoritativa antes de input/audio/lógica.
  - Primeras escrituras protegidas: una presentación no avanza
    `AnimContext` desde `BaseShape::updateAnim`, frames PVW de color/textura/TEV
    ni timers/fades globales de `PlugPikiApp::draw`. La fase de presentación
    sigue desactivada hasta cubrir refresh, partículas e identidad por actor.
  - Las 37 evaluaciones `updateAnim()` pasan ya owner de instancia: actores,
    criaturas, modelos compartidos, mapa, efectos y cinemáticas dejan la
    identidad junto al buffer de pose transitorio.
  - `PcVisualRuntime` captura al inicio de `drawshape/drawculled`, después de
    overrides de pose. Su test valida múltiples matrices/actores con orden
    distinto. Está desactivado por defecto para no añadir asignaciones/copias al
    render actual y todavía no reemplaza matrices durante presentación.
  - Añadido override transaccional y acotado por RAII: una presentación sólo
    sustituye una pose si existen todos sus slots y restaura el buffer
    autoritativo al salir. El test cubre restauración completa y fallo en un
    slot final sin escritura parcial.
  - Protegidos los avances centrales `AnimContext`, `Animator`, `PaniAnimator`
    y coordinadores de Pikmin/pellet/nave: Presentation no cambia frames ni
    dispara eventos de animación. Pendientes partículas, cámara y otras
    escrituras de refresh antes de activar presents reales.
  - Añadido `PcCameraSnapshotStore`: interpola position/focus/up y parámetros
    de proyección por identidad de cámara, normaliza up, separa cámaras de
    gameplay/cinemática y soporta cortes sin blend. Valida valores finitos,
    FOV, aspect y clipping. Aún no se conecta al objeto `Camera` real.

- [~] **MEM-005 — Evitar búsqueda lineal global en cada `delete`.**
  - Sustituida la lista única por 4096 buckets fijos con cabeceras intrusivas;
    no reserva memoria durante tracking y mantiene separados los punteros de
    arena. Pendiente medir distribución/coste y picos en una sesión in-game.
  - `nm` confirma ahora que el ejecutable define las seis variantes ordinarias
    de `new/delete` en vez de importarlas.

- [ ] **MEM-006 — Ejecutar pruebas prolongadas con sanitizers.**
  - Varios reinicios, días, niveles y cinemáticas en una sola sesión.

---

## Fase 6 — Audio

- [x] **AUDIO-001 — Reproducir streams cinematográficos `.stx`.**

- [x] **AUDIO-001A — Sustituir la cola exclusiva por un mixer nativo.**
  - Callback SDL a 32 kHz, voces independientes para stream y salida AI/DMA.
  - Un stream ya no borra los bloques PCM pendientes y el DMA no corta el
    bloque anterior. Es la base necesaria para BGM + SFX simultáneos.
  - Validado manualmente el 26-08-2026: `opening.stx` continúa correctamente
    entre las dos partes de la cinemática inicial.

- [x] **AUDIO-001B — Endurecer el mixer para síntesis sostenida.**
  - Separar estado de control y estado consumido por el callback, sin hacer
    I/O, decodificación, reservas ni movimientos grandes bajo el lock de SDL.
  - Añadir ganancia maestra por buses (stream/BGM/SE), margen de mezcla y
    limitación o saturación medible para evitar clipping con muchas voces.
  - Sustituir el robo fijo de la voz 0 por prioridades y antigüedad; reservar
    la ruta AI/DMA solamente como compatibilidad y no como base del sintetizador
    nativo, porque sus direcciones `u32` no son seguras en x86_64.
  - Criterio: prueba nativa determinista con 64 voces, altas/bajas concurrentes
    y stream simultáneo, sin carrera, asignación en callback ni cola creciente.

- [~] **AUDIO-002 — Implementar BGM de menús y niveles.**
  - Los assets originales están presentes en `SndData/Seqs`, `Banks` y los
    archivos `.aw`.
  - [x] Catálogo nativo WSYS: 22 sistemas, 882 muestras y decodificación DSP
    ADPCM4/PCM8/PCM16 sin punteros GameCube de 32 bits.
  - [x] Catálogo BARC: las 22 secuencias originales de `pikiseq.arc` se
    resuelven y leen con límites comprobados.
  - [x] Mixer de 64 voces con remuestreo, bucle, panorama y caché de muestras.
  - [~] **AUDIO-002A — Catálogo IBNK y conexiones virtuales.** Analizar los 22
    bloques IBNK de `pikibank.bx` con offsets big-endian comprobados: INST,
    PERC, regiones de nota/velocidad, Vmap, osciladores, efectos aleatorios,
    sensores de nota/velocidad y tablas de envolvente.
    Resolver banco virtual y WSYS virtual a sus índices físicos; conservar las
    dependencias de escena de WBCT/SCNE que determinan qué ondas están activas.
  - [~] **AUDIO-002B — Voz instrumental nativa.** Convertir
    banco/programa/nota/velocidad en onda, afinación, ganancia, panorama,
    bucle y envolvente attack/release. Implementar note-on, note-off y release
    sobre el mixer sin depender del DSP o ARAM de GameCube. Las curvas IBNK se
    ejecutan a 1 kHz y modulan volumen, tono y panorama; los note-off usan sus
    tablas de release o la liberación PER2 correspondiente. Los modos 3/4 y
    efectos IBNK alimentan ahora envíos FX y Dolby propios del mixer.
  - [~] **AUDIO-002C — Núcleo JAM y reloj musical.** Interpretar primero el
    subconjunto realmente usado por las 22 secuencias: notas/gates, esperas,
    tempo/timebase, registros, programa/banco, parámetros, saltos/bucles,
    llamadas y apertura/cierre de tracks hijos. Ejecutarlo con un reloj de
    audio independiente de FPS y con límites de PC/stack/bucles por tick.
  - [~] **AUDIO-002D — BGM vertical de extremo a extremo.** Conectar BARC ->
    JAM -> IBNK -> WSYS -> mixer primero para una pista de menú, y después
    `Jac_SceneSetup` para título, selector, mapa y los cinco niveles.
  - [~] **AUDIO-002E — Controles musicales de Pikmin.** Implementar pausa,
    volumen, fades, cambio de escena y las capas/ports/ext-params que usa
    `piki_bgm.c`; después validar mezcla normal, batalla y jefe.
  - Portar el secuenciador por capas y sustituir su DSP físico por mezcla
    software; no activar en bloque el JAudio original ni sustituir la banda
    sonora por audios artificiales.
  - Criterio: las pistas usan los assets originales, mantienen tempo aunque
    varíe el framerate, liberan todas sus voces al cambiar de escena y no
    interrumpen un STX simultáneo.

- [~] **AUDIO-003 — Implementar efectos de sistema y gameplay.**
  - Reutilizar IBNK/JAM y el mismo asignador de voces para `Jac_PlaySystemSe`,
    Olimar, Pikmin, objetos, enemigos y UI; no crear tablas directas de ondas
    por llamada salvo como diagnóstico temporal.
  - Añadir handles generacionales para que stop/update nunca afecten una voz
    que ya haya sido robada y reutilizada.

- [~] **AUDIO-004 — Implementar eventos posicionales y actualización de cámara.**
  - Conectar crear/destruir/acción/posición, atenuación y panorama a la cámara;
    aplicar límites y prioridades por distancia sin trabajo costoso en callback.

- [~] **AUDIO-005 — Implementar transiciones y modos especiales.**
  - Jefes, fin de día, pausas, capas adaptativas, secuencias de demo no-STX y
    convivencia con los streams cinematográficos.

- [~] **AUDIO-006 — Validar mezcla, latencia y estabilidad prolongada.**
  - Música + varios SFX + stream; sin cortes, saturación, crecimiento de cola
    ni dependencia de la tasa de vídeo.
  - Añadir pruebas offline de parser/secuenciador con resultados deterministas,
    contadores de underrun y voces, y recorridos manuales separados para BGM,
    SFX, audio posicional, pausa/cambio de escena y sesión prolongada.

---

## Fase 7 — Compatibilidad y acabado

- [~] **SAVE-001 — Probar tarjeta virtual en múltiples sesiones.**
  - Crear, cargar, sobrescribir, borrar y recuperar un archivo.
  - Validado manualmente un ciclo guardar/cargar: la partida continuó desde el
    primer nivel hasta el segundo con gameplay y combate funcionales. Faltan
    sobrescritura, borrado, recuperación y varias sesiones consecutivas.
  - Corregida una corrupción al restaurar brotes enterrados: `BPikiInf` hacía
    un cast de `PikiHeadItem` a `Piki` y escribía en un layout ajeno. Ahora
    restaura simétricamente `mSeedColor`/`mFlowerStage` y sanea ambos campos al
    leer la tarjeta. Pendiente confirmar la partida que fallaba.
  - Corregida la política global de entidades persistentes: un caché válido es
    la única autoridad y `init.gen` sólo se usa cuando el caché falta o es
    inválido (incluida la primera visita). Se eliminó la
    fusión por nombre/objeto/proximidad, que podía confundir dos generadores
    legítimos y hacer desaparecer entidades.
  - La carga valida primero todo el directorio de cachés (estados, offsets,
    continuidad, componentes y conteos), usa streams acotados y descarta de
    forma atómica un bloque de generadores malformado antes de reconstruirlo.
    La escritura serializa cada registro en staging para impedir desbordamientos
    o registros parciales. Pendiente validación in-game de varios saves/niveles.

- [x] **INPUT-001 — Completar configuración de teclado y mando.**
  - El backend ya aplica los remapeos de teclado y botones de mando, incluidos
    bindings digitales opcionales para ambos sticks. Sensibilidad de ratón,
    zona muerta e inversión X/Y de ambos sticks se cargan, guardan y aplican.
  - F2 conserva el cambio inmediato entre control clásico y puntero por ratón.
  - Pendiente: validación manual con teclado, ratón y un mando real.
  - **Implementado**: Sustitución de la inercia del ratón por desplazamiento
    directo según `MOUSE_INPUT_IMPLEMENTATION_GUIDE.md`:
    - `pc_window.cpp`: Nuevos deltas de ratón (`sMouseCursorDeltaX/Y`) publicados
      una vez por frame sin acumulación ni decaimiento.
    - `pc_window.h`: Getters `pc_window_get_mouse_cursor_delta_x/y()` y
      `pc_window_clear_mouse_cursor_delta()`.
    - `navi.cpp::makeVelocity()`: Rama dedicada para `PC_CONTROL_MOUSE_CURSOR`
      que usa deltas directos, sin normalizar, sin multiplicar por DeltaTime,
      con límite radial y ambos `mCursorPosition`/`mCursorTargetPosition`
      igualados para evitar interpolación residual.
    - Limpieza de deltas en transiciones (F2, F1, Tab/Escape, foco).
    - Corregido el signo vertical definitivo: como SDL Y crece hacia abajo, se
      proyecta sobre `-mViewZAxis`; así arriba/abajo permanece estable al girar
      la cámara y no depende de ejes fijos del mundo.
    - Corregida la orientación al girar la cámara: los deltas de pantalla se
      proyectan sobre `mViewXAxis`/`mViewZAxis` horizontales normalizados en
      vez de sumarse a X/Z fijos del mundo. Se consume cada delta una sola vez
      y se retiró la zona muerta oculta de dos counts.

- [~] **SETTINGS-001 — Añadir menú de configuración PC con F1.**
  - Overlay accesible durante menús y gameplay, dibujado por la pila GX del
    juego (mismo font y estilo que el HUD), con navegación por teclado
    (Up/Down/Left/Right, A/Enter, B/Esc, F1 toggle) y parche de fondo + panel.
    El pad se suprime mientras está abierto para que el juego no reaccione a la
    misma entrada; se cierra con Esc/B o F1 sin alterar el estado de la partida.
  - La presentación usa ahora una carcasa por capas inspirada en el menú de
    opciones original: sombra, bisel cromado, interior negro/azul, reflejos,
    cabecera elevada y selección naranja. Etiquetas y valores ocupan columnas
    estables en vez de formar una única cadena centrada.
  - [x] Seleccionar resolución (tamaño de ventana), modo ventana/pantalla
    completa/sin bordes, frecuencia (Auto/60/120/144/165/240) y sincronización
    (VSync), con límite de FPS y escala interna.
  - [x] Resolución interna 3D ("3D Resolution"): escala del renderizado interno
    sobre la nativa 640x480 (Auto/native, 0.5x, 1x, 1.5x, 2x) que afecta a la
    calidad 3D en ventana, pantalla completa y borderless (multiplica la
    resolución interna del framebuffer; se reasigna en caliente en `begin_frame`).
  - [x] Aplicación en caliente con confirmación y reversión segura: los cambios
    de vídeo se aplican al instante y un diálogo permite conservarlos (A/Enter)
    o revertirlos (B/Esc), con auto-reversión a los ~8 s.
  - [x] Persistencia: configuración guardada en `pikmin_settings.conf`
    (resolución, modo, frecuencia, vsync y escala 3D) y aplicada al arrancar
    (`pc_settings_init`), incluso en fullscreen/borderless.
  - [~] Remapeo de controles, sensibilidad, zona muerta e inversión conectados
    al backend y persistidos. Pendiente validarlos in-game con periféricos
    reales y revisar la repetición de navegación analógica del menú.

- [ ] **VIDEO-001 — Evaluar reproducción de vídeo/THP pendiente.**

- [ ] **PLATFORM-001 — Probar X11 y Wayland.**

- [ ] **PLATFORM-002 — Probar distintas GPUs/controladores OpenGL.**

- [~] **RELEASE-001 — Crear build reproducible y documentación de instalación.**
  - Añadido `pikmin-launcher`, separado del juego, que selecciona una ISO/GCM,
    valida GPIE01 Rev. 1, extrae de forma segura su FST a un directorio XDG del
    usuario y arranca el binario desde allí. No copia ni modifica la ROM.
  - El extractor tiene un fixture ISO completamente sintético y prueba offline
    registrada en CTest; no necesita ni incorpora datos de Nintendo.
  - README público, guía de contribución, aviso legal, exclusiones de ROM/assets
    y workflow Linux añadidos. CMake instala launcher y juego juntos.
  - El primer arranque es ahora una ventana SDL `Pikmin PC Port Installer` con
    campos y botones independientes para ROM e instalación, botón `Instalar`,
    progreso integrado y mensajes modales. Copia ambos ejecutables junto a los
    assets; la copia instalada no vuelve a preguntar. CLI sigue para CI/headless.
  - Pendiente: probar manualmente el selector/extracción con una copia legítima,
    añadir paquete relocatable/AppImage y publicar una primera release firmada.

- [ ] **PLATFORM-WIN-001 — Port nativo futuro para Windows x64.**
  - Empezar por MinGW-w64 para conservar las extensiones GNU del código
    decompilado; evaluar MSVC sólo después de estabilizar esa ruta.
  - Sustituir XDG/POSIX del launcher por LocalAppData, `IFileOpenDialog`,
    `GetModuleFileNameW` y `CreateProcessW`, siempre con rutas Unicode.
  - Separar flags y macros de compilador, adaptar atributos/alineación y
    empaquetar `pikmin.exe`, launcher y SDL2 sin datos del juego.
  - Trabajo aplazado hasta completar rendimiento y fidelidad del port Linux.

---

## Fase 8 — Rendimiento de port nativo

Objetivo: sostener la cadencia original con margen en hardware razonable sin
reducir fidelidad, eliminar trabajo emulado redundante y basar cada cambio en
métricas comparables.

- [~] **PERF-NATIVE-001 — Auditoría integral y línea base reproducible.**
  - Separar CPU update, parser GX, envío OpenGL, GPU/presentación, audio e I/O.
  - Medir título, cinemática y gameplay a 720p/1080p/1440p/4K y escalas 3D.
  - Añadir GPU timer queries asíncronas; `wait/swap` no distingue actualmente
    trabajo GPU diferido, bloqueo del compositor y presentación.
  - Implementado un anillo de ocho consultas `GL_TIME_ELAPSED`: informa por
    separado `[PERF GPU] scene` y `blit`, y sólo recoge resultados disponibles,
    sin `glFinish` ni esperas que contaminen la medición. La línea base manual
    confirma en título 1080p `scene=55,74 ms` y `blit=0,50 ms`; a 1920x823 la
    escena baja a `27,37 ms`. En gameplay ronda `32,5-34 ms` de escena y
    `0,76-0,84 ms` de blit. El cuello de alta resolución está en el shader/
    raster de escena, no en el blit ni en la simulación.

- [x] **PERF-NATIVE-002 — Reducir overhead del traductor GX.** HECHO 2026-09-01.
  - Agrupación de primitivas en `pc_gfx_end`: los uniforms se escriben al abrir
    un lote y el dibujo se difiere; las primitivas que comparten clave de estado
    se acumulan y salen en un solo `glDrawArrays`. Strips, fans y quads pasan a
    `GL_TRIANGLES` para poder fundirse; el orden de mezcla se preserva.
  - Resultado en stage1, frames pesados (~25.800 primitivas): `renderall` p95 de
    66,5 a **16,4 ms**, draws/frame de 25.800 a **479**. Entra en 60 Hz.
  - `PIKMIN_BATCH=0` restaura el comportamiento anterior (A/B para defectos
    visuales). Detalle completo y puntos de vaciado en SIGUIENTE_SESION.md.
  - (Notas previas al trabajo:) Auditar parser de display lists, transformaciones CPU, cachés de estado,
    asignaciones, uniformes y límites actuales del batching.
  - No agrupar a través de cambios BP/XF/CP ni alterar el orden primitivo.
  - Primer recorte seguro: el vértice de streaming baja de 104 a 72 bytes
    (−31 %). TEX4–TEX7 se siguen consumiendo del display list, pero no se
    almacenan porque el shader actual sólo representa TEX0–TEX3.
  - **Medido 2026-09-01 (paso 2b). Éste es ahora el único obstáculo para los
    60 FPS en gameplay.** Ver el bloque «Medición de CPU por tick» más abajo.

#### Medición de CPU por tick (2026-09-01, pasos 2 y 2b)

Instrumentación: `PIKMIN_TICK_STATS=1`, implementada en
`pc_port/timing/pc_tick_profiler.*`, con puntos de medida en
`PlugPikiApp::idle()` y dentro de `pc_gfx_end()`. No altera comportamiento y no
lee el reloj cuando está apagada.

Resultado en gameplay (mediana; entre paréntesis el p95):

| Región | ms | Lectura |
|---|---|---|
| `update` (lógica del juego) | 0,01 | Irrelevante. Descartada como obstáculo. |
| `renderall` | 13 (33) | Todo el coste está aquí. |
| `doneRender` | 0,00 | El envío es asíncrono; la espera vive en `waitRetrace`. |
| — `gl:uniforms` | 3,4 (8,4) | ~400 ns por draw pese a la caché de valores. |
| — `gl:vbo` | 2,2 (6,6) | Un `glBufferSubData` por primitiva. |
| — `gl:draw` | 1,1 (2,7) | |
| — resto de `renderall` | ~7 (~15) | Sin instrumentar: recorrido de escena y ensamblado de vértices. |

| Contador | p50 | p95 |
|---|---|---|
| `gl:draws/frame` | 8.300 | 27.376 |
| `gl:verts/draw` | 9,3 | ~15 |

**Diagnóstico.** Ocho mil llamadas de dibujo por frame de nueve vértices cada
una: kilómetro y medio de sobrecarga de driver por cada triángulo y medio de
trabajo real. Contrastar con la GPU, medida en 3,7–5,9 ms a 1080p nativo: la
tarjeta está ociosa el 85 % del tiempo. El cuello de botella no es el hardware
ni la resolución, es la emisión.

Corolario incómodo: con p95 en 33,0 ms y el periodo de 30 Hz en 33,3, la build
estable actual **no va holgada a 30 FPS, va justa**. Optimizar esto no es sólo
para llegar a 60; también saca a los 30 actuales del límite.

**Plan (PERF-NATIVE-002, agrupación por lotes).** Orden previsto:

1. **Derisgo primero, y es barato.** Antes de escribir el batcher, medir cuántos
   draws *consecutivos* comparten estado. Toda la ganancia depende de que el
   juego emita rachas de primitivas con el mismo material; si las rachas son
   cortas, el diseño cambia. Sonda: calcular la clave de estado en `pc_gfx_end`
   y contar la longitud media de racha, informando por `PIKMIN_TICK_STATS`.
2. **Clave de estado.** Debe cubrir *todo* lo que el bloque de uniforms de
   `pc_gfx_end` lee (líneas ~2915–3320): programa TEV, matriz de proyección,
   `sCurrentPosMtxId` + `sVerticesPretransformed`, colores de material y canales,
   comparación alfa, registros TEV, konst, `numStages`, `fastPath`, modos y
   matrices de texgen, más las texturas ligadas y el estado GL (blend, depth,
   cull, scissor).
3. **Conversión a triángulos.** Strips, fans y quads pasan a `GL_TRIANGLES` al
   acumular, para que primitivas consecutivas puedan fundirse. Líneas y puntos
   llevan lotes propios.
4. **Sólo primitivas consecutivas, sin reordenar.** Esto preserva exactamente el
   orden de mezcla, que es lo que hace el cambio verificable visualmente.
5. **Puntos de vaciado.** El riesgo real del trabajo: hay que vaciar el lote
   pendiente antes de *cualquier* mutación de estado GL hecha fuera de
   `pc_gfx_end` (blend, depth, ligado de textura, viewport, scissor, ligado de
   framebuffer, `glClear`, lecturas de vuelta) y al terminar el frame. Un punto
   de vaciado olvidado da corrupción visual dependiente del contenido, que es
   justo la clase de fallo que costó cinco hipótesis en PERF-NATIVE-003.

**Estimación.** Si el subtotal GL se vuelve despreciable, `renderall` pasa de
13 → ~7 ms en el caso típico y de 33 → ~15 ms en el p95. El caso típico entra
holgado en los 16,6 ms y el peor caso entra ajustado: **los 60 FPS en gameplay
salen**, con el otro medio `renderall` aún sin optimizar como margen de reserva.

**Snapshot previo:** `snapshots/20260901-191759-medicion-cpu-2b.tar.zst`.

- [x] **PERF-NATIVE-003 — Especializar pipelines TEV.**
  - Sustituir ramas dinámicas del shader genérico por programas cacheados según
    firma material sólo cuando las métricas GPU demuestren beneficio.
  - Conservar un fallback genérico para combinaciones raras.
  - Implementado `pc_port/gl/pc_tev_shader.*`: genera el fragment shader de cada
    configuración TEV con los selectores resueltos en tiempo de generación. Sin
    bucle de etapas, sin cadenas de selección de operando, una sola lectura de
    textura por etapa y sólo los samplers que la configuración usa. El test de
    alpha estáticamente siempre-pasa no emite `discard`, lo que devuelve al
    driver el rechazo temprano de profundidad.
  - Los nombres de uniform generados son un subconjunto de los del über-shader,
    de modo que la ruta de subida existente sirve a ambos: los uniforms
    horneados resuelven a -1 y sus escrituras son ignoradas. Un test cubre esa
    invariante, de la que depende toda la integración.
  - Caché por clave con atajo de última configuración; über-shader conservado
    como fallback automático ante un fallo de compilación y como referencia A/B
    mediante `PIKMIN_UBERSHADER=1`. El log periódico informa del número de
    programas generados.
  - Los índices de atributo se fijan con `glBindAttribLocation` en todos los
    programas para que un único VAO siga siendo válido.
  - Corregido un fallo introducido por la propia especialización: la caché de
    valores de uniform de `pc_gfx.cpp` indexaba sólo por número de localización,
    lo que era correcto con un único programa. Con varios, la misma localización
    designa uniforms distintos y la caché se saltaba subidas necesarias dejando
    valores obsoletos. Ahora cada entrada lleva la generación en que se escribió
    y enlazar otro programa la incrementa, invalidando en O(1).
  - Añadidas sondas `gl_error_checkpoint` bajo `PIKMIN_GL_CHECK=1`. Atribuyeron
    el `GL_INVALID_OPERATION` a `query_program_locations`: al extraer esa
    función se perdió el `glUseProgram` previo a asignar los samplers, así que
    `uTex1`..`uTex7` no llegaban a escribirse en ningún programa y toda etapa
    multitextura leía de la unidad 0. Corregido.
  - Medida GPU a 1920x1080 nativo con `PIKMIN_PERF_STATS=1`: escena 3,7-5,9 ms
    y blit ~1,0 ms, frente a los ~55 ms del título y ~32-34 ms de gameplay a
    960x540 anteriores. El criterio de salida de ~14 ms queda cumplido con
    margen amplio en título y menús; falta la medida en gameplay real.
  - Medido in-game: 15 programas TEV en todo el recorrido hasta el nivel, y los
    menús pasan de ~17-18 FPS a 60 sostenidos. Confirma el diagnóstico.
  - **Causa raíz encontrada y corregida.** Al extraer `query_program_locations`
    a una función, el script de refactor eliminó el `glUseProgram` que precedía
    al bucle de samplers, dejando dos bucles: el original escribía
    `glUniform1i(uTexN_del_programa_nuevo, N)` mientras seguía enlazado el
    programa **anterior**. GL rechazaba la escritura (de ahí el
    `GL_INVALID_OPERATION` persistente) y, cuando esa localización correspondía
    a un uniform entero del programa anterior, la escritura tenía éxito y lo
    corrompía con el índice del sampler. La caché de valores consolidaba la
    corrupción al registrar ese valor como ya escrito. Ese primer bucle ahora
    sólo consulta; las escrituras ocurren una vez, con el programa enlazado.
  - Diagnóstico que lo localizó: comprobación de error por cada escritura de
    uniform (`PIKMIN_GL_CHECK=1`), que nombró el uniform dueño de la
    localización rechazada (`1i write ... holds 'uMaterialColor'`). Un
    `glUniform1i` sobre un `vec4` sólo ocurre si la localización no pertenece al
    programa enlazado.
  - **Validado in-game por el usuario (2026-09-01): imagen correcta y las
    cifras de la configuración de referencia.** Tarea cerrada.

- [ ] **PERF-NATIVE-004 — Modernizar streaming y presentación.**
  - Evaluar buffers persistentes/ring buffer, fences, render directo cuando no
    haga falta reescalado y blit óptimo cuando sí sea necesario.
  - Evitar sincronizaciones CPU/GPU y copias fullscreen redundantes.

- [ ] **PERF-NATIVE-005 — Optimizar CPU, recursos y audio fuera del renderer.**
  - Perfilar managers, colisiones, IA, partículas, carga, asignador y callback
    de audio; corregir únicamente hotspots medidos.
  - NoteON ya no copia los vectores anidados de osciladores/efectos ni crea un
    `shared_ptr` por voz: usa vistas estables al banco de instrumentos inmutable.

- [ ] **PERF-NATIVE-006 — Builds públicas portables y optimizadas.**
  - Separar una build portable de `-march=native`, conservar LTO configurable
    y comparar GCC/Clang sin sacrificar diagnósticos ni reproducibilidad.
  - Separados `-O3`, ISA local e IPO: Release/RelWithDebInfo usan optimización
    alta, `PIKMIN_NATIVE_OPTIMIZE` sólo controla `-march=native` e
    `PIKMIN_ENABLE_IPO` usa la detección portable de CMake para GCC/Clang.
  - Los scripts de empaquetado portable ya no apagan `PIKMIN_ENABLE_IPO`: el
    LTO no afecta al juego de instrucciones y su ausencia sólo restaba
    rendimiento donde más falta hace.

- [x] **PERF-NATIVE-007 — Recuperar la optimización perdida en la separación
  de objetivos.** HECHO 2026-09-04.
  - Al extraer las fuentes decompiladas a `pikmin_legacy`, `-O3`,
    `-march=native` e IPO se quedaron sólo en `pikmin_pc`. El grueso del
    trabajo por frame vive en `pikmin_legacy`, así que el juego perdió ISA
    local y LTO sin ningún síntoma visible salvo el tiempo de frame.
  - Las tres opciones se aplican ahora a ambos objetivos
    (`PIKMIN_OPTIMIZED_TARGETS`).
  - Un `CMAKE_BUILD_TYPE` vacío ya no cae en `-O0`: por defecto
    `RelWithDebInfo`, y configurar `Debug` avisa de que el rendimiento medido
    no será representativo.
  - Retiradas del camino caliente las sondas de vértices salvajes que habían
    quedado sin condicionar; comparten ya el interruptor `PIKMIN_WILD_VERTS=1`.
  - **Regla que deja este fallo:** cualquier reorganización de objetivos de
    CMake debe comprobarse leyendo `flags.make` de cada objetivo, no el
    `CMakeLists.txt`. Un objetivo nuevo nace sin las opciones del anterior y
    nada lo señala.

---

## Correcciones estructurales ya verificadas

- [x] Carga de modelos de puerta y llave en Linux.
- [x] `ERROR()` activo en la configuración PC.
- [x] Direcciones de `AyuStack` e invalidación gráfica de 64 bits.
- [x] Caché de matrices de animación adaptada a punteros de 64 bits.
- [x] IDs de generadores, rutas, colisiones y pellets adaptados localmente.
- [x] `PolyObjectMgr` y `GeneratorCache` sin truncamiento principal de punteros.
- [x] Condiciones persistentes sin temporales `stack_new` colgantes.
- [x] Representación de las tres patas de la cebolla segura en 64 bits.
- [x] Modelos skinned usando la ruta portable de matrices ponderadas.
- [x] Decodificación de texturas CMPR.
- [x] Tarjeta virtual persistente básica.
- [x] Salida normal al cerrar la ventana SDL.

---

## Historial de cambios

Añadir las entradas nuevas arriba de las antiguas.

### 2026-09-01 — TIME-002 — Activación de 60 FPS fallida y revertida

- Se activó un present intermedio que volvía a recorrer `renderall()` con fases
  Authoritative/Presentation, snapshots de owner/cámara y guardas para varias
  mutaciones de animación, partículas, UI, audio, colisión y pose.
- La build y 11/11 tests offline pasaban, pero la prueba decisiva in-game dejó
  la imagen en blanco y negro y produjo bugs gráficos. El usuario hizo rollback
  y recuperó el funcionamiento a 30 FPS.
- Resultado: **fallido; no constituye una implementación parcial válida.** Las
  pruebas unitarias no cubrían equivalencia del estado GX/TEV ni del resultado
  visual de una segunda pasada completa.
- Decisión: queda prohibido fabricar presents volviendo a llamar `renderall`,
  `draw` o `refresh`. La siguiente ruta será captura/replay de paquetes de
  render inmutables en el backend GX -> OpenGL, conservando el render estable
  de 30 FPS como fallback. Registro completo en `60FPS_FAILED_ATTEMPTS.md`.
- Antes de continuar hay que inspeccionar el árbol actual: el rollback puede
  haber restaurado también el bucle temporal previo, pese a las notas
  históricas que documentan el scheduler.

### 2026-08-30 — RELEASE-001 — Instalador gráfico y destino elegible

- `pikmin-launcher`, abierto con doble clic, muestra una ventana SDL propia con
  el título `Pikmin PC Port Installer`, dos campos de ubicación, botones de
  selección y un botón `Instalar`.
- Zenity/KDialog sólo proporcionan los selectores del sistema. El porcentaje,
  archivo actual, barra de progreso y errores viven en la ventana del launcher.
- Tras una extracción transaccional, se copian `pikmin` y `pikmin-launcher` a
  la carpeta elegida y se preservan sus permisos ejecutables. La ROM no se
  copia ni modifica. La copia instalada detecta `assets/.pikmin-assets` y
  arranca directamente en usos posteriores.
- `--install-dir` permite el mismo flujo headless; `--data-dir` queda como alias
  compatible y `--extract-only` sigue evitando iniciar el juego.
- Validación: build completa; `--help` y CTest offline. No se abrió la GUI ni
  se ejecutó el juego desde Codex.

### 2026-08-30 — RELEASE-001 — Primer flujo legal de instalación

- Añadidos `pc_port/launcher/gamecube_image.*` y `launcher_main.cpp`: lector FST
  con límites, nombres seguros, extracción temporal y validación GPIE01 Rev. 1.
- El ejecutable `build/bin/pikmin-launcher` usa Zenity/KDialog o terminal,
  instala en `${XDG_DATA_HOME:-~/.local/share}/pikmin-native` y encuentra
  `pikmin` como binario hermano.
- `.gitignore` bloquea ROMs, contenedores comprimidos, assets, configuración y
  copias locales; `assets/README.md` es la única excepción versionable.
- Añadidos `README.MD`, `LEGAL.md`, `CONTRIBUTING.md` y CI Linux sin datos del
  juego. La CI incluye una comprobación explícita de contenido propietario.
- Pruebas offline: `gamecube_image_test` y `pc_envelope_test`, 2/2 correctas.
- Validación con la copia local real mediante `--extract-only`: 3.496 archivos,
  641 MiB, marcador y `dataDir/parms/gamePrms.bin` correctos. El staging de
  prueba se eliminó después; no se ejecutó el juego.
- `cmake --install` coloca únicamente `bin/pikmin` y
  `bin/pikmin-launcher` juntos, como exige su resolución relocatable.
- Preparado un staging público independiente en el directorio hermano
  `pikmin-github`, construido desde el manifiesto de archivos versionados y no
  ignorados. No contiene `.git`, ROM, assets, builds ni estado del usuario.
- No se ejecutó el juego ni se borraron las ROMs/assets locales existentes.

### 2026-08-29 — PERF-003 — Primer recorte de ancho de banda a alta resolución

- Centralizado el cálculo del área final de presentación y usado también para
  dimensionar el framebuffer interno, eliminando el error 1919x1080.
- Si no hay letterbox/pillarbox, se evita limpiar millones de píxeles que el
  blit iba a sobrescribir inmediatamente; las bandas siguen limpiándose.
- Build Release completa. No se ejecutó el juego; falta comparación manual de
  FPS/GPU en varias resoluciones.
- La primera muestra real aisló ~13.000 microdibujos y ninguna ruta rápida en
  el título. El fast path reconoce ahora `GX_MODULATE` como textura × raster,
  sin cambiar su ecuación ni saltarse alpha compare. Build Release completa.

### 2026-08-29 — SETTINGS-001 — Rediseño visual del overlay F1

- Sustituido el panel azul sencillo por una composición GX escalable que imita
  la silueta del menú original: sombra suave, borde metálico, carcasa negra,
  cavidad azul profunda, reflejo especular y cápsula de título sobresaliente.
- Las opciones normales usan cian claro; la opción activa recibe banda oscura,
  texto naranja y marcador lateral. Etiqueta y valor se alinean en columnas.
- No se añadieron texturas ni assets externos y no cambió la navegación.
- Build Release completa. Pendiente validación visual in-game del usuario; no
  se ejecutó el juego.
- Tras la primera captura se rediseñaron también Controles de teclado, Mando y
  Ajustes avanzados: cada uno tapa completamente la página padre, usa cabecera
  y superficie propias, filas etiqueta/valor, selección coherente y ayuda en
  dos líneas que cabe en el lienzo lógico. Las listas muestran nueve entradas
  por página para reservar un pie legible. Build Release completa.
- La captura siguiente reveló que las tres entradas de submenú leían fuera de
  `valueBuf[7]` y mostraban glifos aleatorios. Ahora usan valores explícitos
  `Open >`; eliminado el acceso fuera de rango.

### 2026-08-29 — MEM-004/MEM-005 — Métrica global corregida tras sesión larga

- El registro real recorrió título, selector, cinemáticas y varios escenarios,
  cambió F2 de Mouse Cursor a Classic y terminó limpiamente.
- La línea final de 1,60 GB no registró liberaciones. La inspección de símbolos
  demostró que los `new/delete` inline no reemplazaban globalmente a libstdc++;
  se movieron a definiciones fuertes y se añadió el contador `frees`.
- El informe de carga ya evita dividir por cero y no imprimirá `NaN/inf`.
- Build Release completa. JAM: 0 fallos; envelope: 63/63; mixer: 0 fallos. No
  se ejecutó el juego.
- Abiertos: 185 fallos JAM `InvalidAddress`, cuatro cámaras `near >= far`,
  framebuffer 1919x1080 y cuatro DCK ausentes (`logo` y `naka1a/f/s`).

### 2026-08-29 — MEM-004/MEM-005 — Frontera ARAM y asignador nativo

- El tracker de `operator new/delete` PC deja de recorrer una lista global:
  usa 4096 buckets fijos, sin reservas recursivas, y conserva la decisión de no
  liberar como C heap los punteros procedentes de las arenas del juego.
- `operator new` cumple el contrato C++ para tamaño cero, overflow y agotamiento
  lanzando `std::bad_alloc`. Al salir se imprime `[PC Alloc]` con vivos, bytes,
  picos, total y frees desconocidos; servirá para medir reinicios/días/niveles.
- La carga PC ya usaba directamente el árbol extraído y nunca consumía las
  entradas ARAM. `parseArchiveDirectory` omite ahora explícitamente en PC una
  DMA que el stub no implementa y que truncaba punteros host; `AramStream` y las
  copias ARAM abortan con diagnóstico si una futura ruta vuelve a alcanzarlas.
- Eliminado en PC el recorrido de stack GameCube de 32 bits en `showError`; el
  código original además tenía invertida la selección nombre/dirección. Los
  mensajes `QUIT/CONT` usan `uintptr_t` sin truncar el token puntero.
- La ruta PC de `copyCacheToTexture` ya no declara/castea una dirección host a
  `u32` antes del `#if`. `Texture::offsetGLtoGX`, actualmente no referenciada,
  devuelve un valor definido después de su trap en vez de caer sin `return`.
- Validación offline: reconstrucción Release completa correcta; envelope 63/63,
  JAM 0 fallos y mixer 0 fallos. No se ejecutó el juego. Pendiente obtener las
  métricas `[PC Alloc]` de una sesión real y repetir el enlace global sanitizer.

### 2026-08-29 — INPUT-001/SETTINGS-001/TIME-001 — Integración de controles y ajustes

- Recuperada la build global sustituyendo la referencia obsoleta
  `PC_CONTROL_MOUSE_OLIMAR` por `PC_CONTROL_MOUSE_CURSOR`. F2 sigue alternando
  deliberadamente control clásico/puntero y configura el ratón relativo.
- Eliminado el segundo estado del menú F1: `pc_settings` es ahora la fuente de
  verdad y comunica al backend cuándo debe mostrar cursor o restaurar captura.
- La carga y el guardado aplican conjuntamente modo de control, sensibilidad,
  teclado, botones de mando, zona muerta e inversión X/Y de stick/C-stick. El
  polling deja de usar botones y umbral fijos; todos los bindings mostrados por
  la interfaz tienen un consumidor real.
- Corregidos la persistencia de `controlMode`, el flujo `pending -> aplicar ->
  guardar`, la reversión de aspect ratio y el `va_start` inválido del texto con
  contorno. El menú reservaba seis buffers para siete filas y escribía fuera de
  límites; ahora el array tiene el tamaño correcto.
- El pacing vuelve a usar siempre la base VI de 60 Hz, independientemente de la
  frecuencia física seleccionada y de la preferencia de presentación. Intervalo
  1 equivale a 60 Hz e intervalo 2 a 30 Hz incluso con monitor de 144/240 Hz.
- El framebuffer evita dividir por cero cuando SDL informa tamaño drawable cero
  al minimizar. `zen::makePathName` deja de invertir/copiar nombres sin límite y
  construye la ruta con `snprintf` y basename acotado.
- Validación offline: build Release completa; `pc_envelope_test` 63/63,
  `pc_jam_test` 0 fallos y `pc_audio_mixer_test` 0 fallos. La compilación
  ASan/UBSan alcanzó el enlace sin errores de fuentes, pero su enlace quedó sin
  finalizar tras varios minutos y se interrumpió; los tres tests sanitizados
  existentes sí pasan sin hallazgos. No se ejecutó el juego.
- Pendiente manual: comprobar F1, F2, captura/restauración del cursor, guardar y
  reiniciar, todos los remapeos, zona muerta/inversiones y modos de vídeo.

### 2026-08-29 — AUDIO-002E/004/006 — Relojes, limitador y estrés prolongado

- Eliminado el límite de 250 ms que descartaba tiempo de los cuatro relojes
  JAM tras un frame largo. La telemetría distingue ticks BGM, jefe, SE y
  eventos; el test espera al menos 28 ticks tras una única pausa de 350 ms,
  demostrando recuperación independiente de la cadencia de vídeo.
- Eliminado el fallback no utilizado `program % 30`: una resolución inválida
  ya no puede convertirse silenciosamente en otro instrumento.
- La política de eventos rechaza tipos nulos/fuera de catálogo y nuevas altas
  durante demos de fin de día o con flag de gameplay 2. Las acciones de un
  evento sin actualización durante 100 frames se detienen como en JAudio.
- `Jac_GameVolume` usa ahora las tablas originales no lineales para SE, stream
  y demo BGM, además del nivel lineal de BGM.
- El limitador maestro conserva ataque inmediato y añade recuperación suave de
  100 ms. `limitedFrames` mide su intervención por separado; `clips` sólo
  cuenta desbordamientos reales posteriores. El estrés simultáneo reveló y
  corrigió voces BGM en release que sobrevivían a `stop_sequence_track`.
- Nuevo modo `pc_audio_mixer_test --stress-seconds N`. Ejecución offline de 30
  segundos con `select.jam`, `opening.stx` y tráfico SE: 22.198 iteraciones,
  828 callbacks/847.872 frames, pico de 46 voces, 0 robos, 0 rechazos, 0 clips,
  limpieza final completa y 7.226/12.114/12.114 ticks BGM/SE/eventos.
- Build y pruebas de audio normales correctas; mixer 0 fallos, JAM 0 fallos y
  envolventes 63/63. ASan/UBSan también pasa tras sustituir un shift negativo
  indefinido del PCM8 por la multiplicación equivalente. La build global está
  bloqueada fuera de audio por `pc_window.cpp`: `PC_CONTROL_MOUSE_OLIMAR` no
  existe actualmente. No se ejecutó el juego.
- Pendiente exclusivamente in-game: calidad audible de BGM/SFX/posicional,
  pausas y transiciones, y una sesión real prolongada. El pitido del selector
  continúa aparcado para la investigación específica posterior; se conserva
  sin cambios el inicio de bucle WSYS `+0x14`.

### 2026-08-29 — AUDIO-002E/003/005 — Pausa selectiva y demos temporizadas

- El intérprete JAM puede pausar y reanudar recursivamente una pista hija sin
  avanzar sus esperas, parámetros ni note-off. El mixer congela también cursor
  y envolvente de las voces que pertenecen a esa pista o al secuenciador de
  eventos. Una prueba sintética pausa ocho ticks una hija activa y confirma
  que la nota sólo termina después de reanudarla.
- Menú, pausa global, pausa DVD, diálogos de pieza/texto y demos aplican ahora
  la política separada del original: pista 10 de jugador y eventos persistentes
  se suspenden donde corresponde; la reanudación de eventos conserva los
  retardos de tres o seis frames.
- Portadas las tablas completas de 115 entradas `DEMO_STATUS`: modos de fade,
  flags de gameplay, configuración de audio y 614 valores frame/evento. Las
  secuencias no-STX arrancan en su evento `-6` y se respetan final, corte,
  parada y recuperación (`-1` a `-5`); `-7` sólo precargaba bancos de forma
  asíncrona y es innecesario con la carga nativa síncrona.
- Build completa correcta. `pc_audio_mixer_test` termina con 0 fallos y 0
  clips, `pc_jam_test` con 0 fallos y `pc_envelope_test` con 63/63. No se ha
  ejecutado el juego. `Jac_Freeze_Precall` libera ahora estructuralmente todas
  las voces one-shot del bus SE sin cortar BGM/stream, y la prueba del mixer
  comprueba además que un handle SE invalidado no afecta a la BGM conservada.
  Queda la validación audible manual antes de cerrar AUDIO-003/004/005.

### 2026-08-29 — AUDIO-002B/002C/006 — FX/Dolby y telemetría del mixer

- Añadida telemetría atómica consultable y reiniciable: callbacks, frames
  mezclados, pico de voces, robos, rechazos, underruns DMA y clipping. No se
  reserva memoria desde el callback.
- `pc_audio_mixer_test` ejecuta ahora 512 ciclos adicionales de alta,
  actualización de volumen/pan/pitch y baja mientras conviven una voz BGM y
  `opening.stx`. También provoca un underrun DMA controlado para verificar su
  contador. Resultado observado: 13 callbacks, 13.312 frames, pico 64, un
  robo, un rechazo, un underrun detectado y cero clips/fallos.
- Portada la semántica de osciladores/efectos IBNK 3 (`fxmix`) y 4 (`dolby`)
  documentada por `DoEffectOsc`/`UpdateEffecterParam`: el mixer dispone de un
  envío de reverberación estéreo y una matriz surround L-R, ambos con buffers
  estáticos. La señal seca permanece intacta.
- Los parámetros JAM 2/4, incluidos movimientos temporizados y herencia `EF`,
  se publican en NoteOn/GateUpdate/VoiceUpdate y actualizan esos envíos durante
  toda la vida de la voz. Build completa, mixer test, `pc_jam_test` y
  `pc_envelope_test` correctos.
- Pendiente antes de cerrar AUDIO-003/004/005: auditar pausa/reanudación de los
  secuenciadores persistentes durante demos y las transiciones que todavía se
  representan mediante stubs vacíos. La comprobación audible continúa siendo
  exclusivamente in-game por el usuario.

### 2026-08-29 — AUDIO-001B/006 — Prueba offline determinista del mixer

- Añadido `pc_audio_mixer_test`, que usa el backend SDL dummy y los assets
  originales sin abrir el juego. Llena los 64 slots con voces BGM, comprueba
  que una voz de menor prioridad se rechaza y que una SE de mayor prioridad
  roba el slot más antiguo de la clase inferior.
- La prueba conserva los 64 handles anteriores y los detiene después del robo:
  el handle generacional obsoleto no afecta a la voz nueva. Después verifica
  parada completa y convivencia de una voz BGM con `opening.stx` durante la
  ejecución concurrente del callback SDL.
- Resultado: `pc_audio_mixer_test` 0 fallos y 0 clips; `pc_jam_test` 0 fallos;
  `pc_envelope_test` 63/63. AUDIO-001B queda cerrado por prueba offline.
- La auditoría confirmó también que el parser actual ya admite el objeto
  antiguo `PERC` con la estructura común de `Perc_`: catálogo real 19/22 IBNK,
  217 instrumentos, 773 regiones, 218 osciladores y 0 percusiones antiguas sin
  interpretar. La descripción anterior del handoff estaba desactualizada.
- AUDIO-006 sigue abierto: añadir contadores de callbacks, robos, rechazos y
  underrun, y someter el mixer a una prueba prolongada con altas/bajas y
  actualizaciones repetidas mientras conviven BGM, SE y stream.

### 2026-08-29 — AUDIO-002C — Aislamiento offline de voces largas del selector

- `pc_jam_test` empareja ahora altas, GateON y bajas de los programas 18:4 y
  18:5 de `select.jam`, con pista, slot, tick y duración musical; una opción
  `--render-select-long` exporta además sus ondas decodificadas por separado.
- Ambas voces forman una pareja sincronizada: 18:4 usa la onda 8 y 18:5 la 9,
  siempre con nota 60. Se inician juntas, reciben note-off tras 3.840 ticks
  (16,000 s) y vuelven a iniciarse en el mismo tick. No reciben GateON y no
  quedan huérfanas durante la ventana auditada.
- Las ondas son PCM decodificado mono a 44,1 kHz de 213.411/213.430 muestras
  (4,839/4,840 s), con loop común 36.784..213.328. Los WAV renderizados son
  válidos y presentan señal dinámica, sin offset DC significativo; son dos
  canales distintos de material estéreo, no una onda constante sintetizada.
- Validación offline: `pc_jam_test` continúa con 0 fallos. No se remapeó ni se
  silenció ningún instrumento y se conserva el inicio de bucle WSYS `+0x14`.
  Siguiente diagnóstico: comparar audiblemente los dos WAV aislados con el
  pitido in-game; si están limpios, revisar pitch/pan/envolvente de esta pareja
  en la ruta mixer, no su selección IBNK.

### 2026-08-29 — AUDIO-002A/002C — Límites WSYS y notas ligadas JAM

- La corrección previa de `Wave_ +0x14` mejoró audiblemente la música según la
  prueba del usuario, aunque permanece al menos un pitido largo incorrecto.
- El mixer conserva ahora por separado inicio y fin de bucle WSYS (`+0x14` y
  `+0x18`) y vuelve al inicio al alcanzar el fin declarado, en lugar de incluir
  la cola física de la muestra. La interpolación del último sample también se
  hace contra el inicio del bucle.
- Portada la distinción original entre `NoteON` y `GateON`: una nota ligada
  reutiliza la voz activa sin reiniciar la muestra. Antes cada gate podía crear
  un nuevo ataque y convertir instrumentos sostenidos en tonos atascados.
- Build completa correcta; `pc_jam_test` sin fallos y `pc_envelope_test` 63/63.
- Pendiente in-game: volver a escuchar selector de slot, título, mapa y nivel.
  Si el pitido persiste, aislar primero las voces 18:4/18:5 de `select.jam`; no
  deshacer el inicio de bucle `+0x14`, cuya mejora ya fue confirmada.

### 2026-08-29 — AUDIO-002A/002B — Punto de bucle WSYS corregido

- La prueba manual confirmó que añadir los filtros de pista no cambió el fallo
  audible. La auditoría se amplió hasta identificar cada banco, programa, onda
  y metadato de loop usado por `select.jam`.
- Corregida la interpretación de `Wave_`: `+0x14` es la dirección de muestra
  a la que vuelve el DSP y `+0x18` es el final de reproducción. El port usaba
  erróneamente `+0x18` como inicio del loop.
- El error hacía que las ondas sostenidas del selector repitieran únicamente
  sus últimas 65–118 muestras, convirtiéndolas en pitidos periódicos largos.
  Ahora repiten sus tramos originales, que en los casos auditados abarcan
  aproximadamente 8.600–21.300 muestras. La corrección es común a cualquier
  BGM o efecto que utilice ondas WSYS con loop.
- Validación offline: build completa, `pc_jam_test` 0 fallos y
  `pc_envelope_test` 63/63. Pendiente: comprobación audible in-game.

### 2026-08-29 — AUDIO-002C/002D — Filtros de pista JAM

- La prueba manual posterior al bloque AFC2/IBNK confirmó que el fallo audible
  seguía igual; por tanto esas correcciones se conservan, pero no se consideran
  la causa principal del selector ni de las músicas incompletas.
- La auditoría de opcodes descubrió que el port descartaba `F1` (IIR cutoff) y
  `EF` (modos de cálculo panorámico). `select.jam` usa respectivamente 21 y 27
  de esos comandos en 20.000 ticks; mapa, niveles y jefes también los usan.
- `F1` se propaga ahora desde cada pista hasta sus voces nuevas y sostenidas.
  El mixer aplica un filtro paso bajo estable, con bypass en 127, para retirar
  los armónicos que JAudio filtraba y que antes quedaban como pitidos agudos.
  `EF` conserva además la elección original entre panorama local, del padre o
  interpolado, en vez de promediar siempre.
- Validación offline: build completa, las 22 secuencias terminan la auditoría
  sin opcodes no soportados, `pc_jam_test` 0 fallos y `pc_envelope_test` 63/63.
  Pendiente: comparación audible del selector antes/después.

### 2026-08-29 — AUDIO-002A/002B/002C — Voces sostenidas y formatos IBNK/WSYS

- Las voces JAM activas reciben ahora continuamente los parámetros de volumen,
  panorama y pitch de su pista. Antes se congelaban en el valor del `note-on`,
  aunque JAudio actualiza esos parámetros durante toda la vida de la nota; esto
  podía dejar notas largas fuera de mezcla o con afinación obsoleta.
- Añadida decodificación AFC2 (formato WSYS 1, bloques de 5 bytes/16 muestras),
  que faltaba por completo junto a AFC4, PCM8 y PCM16.
- El parser IBNK conserva y aplica los dos efectos aleatorios y los dos sensores
  de nota/velocidad de cada INST. Los osciladores de modo 2 ya modulan panorama;
  los modos 3/4 (FX mix/Dolby) mantienen señal seca hasta disponer de buses de
  reverberación/surround equivalentes.
- Al cerrar una pista se envían `note-off` para todas sus voces, igual que
  `Jaq_CloseTrack`, evitando notas huérfanas tras cerrar pistas hijas.
- Validación offline: build completa OK, `pc_jam_test` con 0 fallos y las 22
  secuencias interpretadas; `pc_envelope_test` 63/63. Pendiente: comprobación
  audible in-game en selector de slot, título, mapa, nivel y jefe.

### 2026-08-29 — SETTINGS-001 — Menú PC de configuración con F1 (vídeo)

- Añadido un overlay de configuración abierto con F1 (título, selector y
  gameplay), dibujado por la pila GX del juego (mismo font `bigFont.bti` y
  estilo que el HUD) con dim de fondo y panel. Navegación por teclado
  (Up/Down/Left/Right, A/Enter, B/Esc, F1 toggle); el pad se suprime mientras
  está abierto para que el juego no reaccione a la misma entrada.
- Ajustes de vídeo aplicables en caliente con diálogo de confirmación/reversión
  (A conserva, B revierte, auto-reversión a ~8 s): modo ventana/pantalla
  completa/sin bordes, resolución (tamaño de ventana), frecuencia
  (Auto/60/120/144/165/240), VSync y escala de render interno 3D.
- Nueva resolución interna 3D ("3D Resolution"): `pc_gfx_set_render_scale()`
  multiplica la resolución interna del framebuffer sobre la nativa 640x480
  (Auto/native, 0.5x, 1x, 1.5x, 2x) y se reasigna en caliente en `begin_frame`;
  funciona igual en ventana, pantalla completa y borderless.
- Persistencia en `pikmin_settings.conf` (ancho/alto, modo, frecuencia, vsync,
  escala 3D) cargada/aplicada al arrancar desde `pc_settings_init()`; el modo
  pantalla completa y la escala se restablecen también al iniciar.
- Corrección de un bug de detección de bordes del teclado: la toma de estado se
  hacía antes de comprobar el flanco (comparaba estado actual contra sí mismo),
  de modo que F1 nunca se detectaba; ahora se hace la instantánea tras procesar.
- Validación: builds Release y ASan/UBSan OK, `pc_jam_test` 0 fallos y
  `pc_envelope_test` 63/63. En el juego el menú se abre con F1, el cambio de
  resolución 3D se aprecia en pantalla completa y borderless.
- Pendiente dentro de SETTINGS-001: remapeo de controles de teclado/mando,
  sensibilidad, zonas muertas, ejes invertidos y navegación con mando.

### 2026-08-28 — AUDIO-002E/003/005 — Envolventes, voces y aterrizaje

- Corregido el reloj inicial de las envolventes IBNK: el primer tramo se
  calculaba a 32 kHz pero después avanzaba a 1 kHz, alargándolo 32 veces. Era
  una causa directa de tonos largos y capas que parecían quedarse pilladas.
- Los `note off` usan ahora la curva de liberación original del instrumento en
  vez de cortar siempre la muestra. La percusión `PER2` incorpora además su
  envolvente fija y el tiempo de liberación específico de cada tecla.
- Restauradas las reglas de `piki_player.c`: bloqueo de voces Pikmin durante
  demos, estado `FlyReady`, variaciones antirrepetición al arrancarlos, caída
  de Olimar, `ContainerOK` y filtros de sonidos impropios fuera del gameplay.
- El murmullo del grupo vuelve a esperar tras caminar/formar, entra y sale con
  fade y deja de reenviar el comando Pikmin cada frame. Las capas de SE ya
  aceptan volumen externo por pista sin duplicar la ganancia de BGM.
- El aterrizaje ejecuta de nuevo sus cuatro eventos temporizados originales en
  los frames 3, 4, 150 y 190; antes el port omitía toda `demo40`.
- Añadida sincronización de tempo para pistas JAM hijas y duración por registro
  en notas extendidas, siguiendo `Jam_SeqmainNote`.
- El target aplica modo `775` tras cada enlazado; una reconstrucción LTO ya no
  puede volver a dejar `build/bin/pikmin` sin permiso de ejecución.
- Validación: build completa, `pc_jam_test` con 0 fallos, `pc_envelope_test`
  63/63 y ambas pruebas bajo ASan/UBSan sin errores (LSan no puede ejecutarse
  bajo el `ptrace` del entorno). Pendiente: comprobación audible in-game del
  selector, aterrizaje, voces Pikmin/Olimar y música de jefe.

### 2026-08-28 — AUDIO-004/005 — Acciones directas, percusión y jefe

- La tabla de eventos de `pikise.jam` mezcla descriptores indirectos de cuatro
  bytes con direcciones directas de bytecode. La ruta PC trataba ambas como
  indirectas, por lo que muchas acciones válidas terminaban sin ninguna nota.
- Corregida la distinción mediante los bits de herencia del descriptor. La
  cobertura posicional aumenta de 621 a 1.156 notas, todas resolubles. Quedan
  verificadas específicamente beber néctar (Piki 28), golpear piedra (Build 6)
  y romper piedra (Build 7).
- La percusión `PER2` exige ahora la tecla exacta, igual que la tabla de 128
  entradas original, y aplica volumen/afinación según `Play_1shot_Perc`. Antes
  podía escoger el golpe siguiente y alterar notablemente su tono o duración.
- `Jac_EnterBossMode` comprueba que la segunda secuencia siga activa y recupera
  `boss2.jam`/`boss3.jam` si una transición o demo la retiró antes del crossfade.
- Pendiente in-game: confirmar que desaparecen los pitidos sostenidos, que
  beber/romper ya suena y que el jefe entra con música propia.

### 2026-08-28 — AUDIO-003/004/005 — Rutas persistentes y fuentes DSP

- Corregidos los comandos JAM envueltos `D1/D2` que detenían la instancia
  persistente de efectos después de la primera orden de Pikmin. Olimar y
  Pikmin cubren ahora offline sus 48 combinaciones de puerto/comando.
- Restaurada la apertura de subsecuencias posicionales al terminar la
  interrupción `E3`, validando descriptor y dirección antes de crear la pista.
  Una entrada vacía se ignora localmente y ya no apaga todos los eventos.
- Separada la acción válida `0` del valor de parada. Las 281 acciones de
  enemigos, objetos, cebollas, construcciones, Pikmin y nave se recorren sin
  error: 621 notas, de las que 621 tienen ahora una fuente nativa resoluble.
- Implementadas las fuentes sintetizadas `F0-FF` que el DSP de GameCube genera
  sin una entrada IBNK/WSYS. Eran 180 notas que se descartaban como instrumentos
  ausentes, incluidas variantes usadas por Olimar, Pikmin y enemigos.
- Los secuenciadores persistentes de SE y eventos se reconstruyen de forma
  segura si encuentran una secuencia dañada, evitando que un solo efecto deje
  muda toda la sesión.
- Las voces de capas BGM y jefe silenciadas siguen avanzando en fase. Ya no se
  destruyen durante el mute, de modo que combate y jefe pueden reaparecer con
  crossfade sincronizado en vez de convertir la música en silencio permanente.
- Acotados a 16 bits los desplazamientos lógicos/aritméticos JAM; UBSan detectó
  una cantidad de desplazamiento mayor que el ancho nativo y la repetición
  sanitizada ya termina sin comportamiento indefinido.
- Validación automática: build completa correcta, `pc_jam_test` normal y
  sanitizado sin fallos y `pc_envelope_test` 63/63. Pendiente únicamente la
  comprobación audible in-game de efectos y transiciones de combate/jefe.

### 2026-08-28 — AUDIO-002E/003/004/005 — Cierre técnico previo a prueba in-game

- Separada la identidad de capa BGM de la lógica especial de acciones para que
  activar los eventos posicionales no reinterprete pistas musicales ni añada
  notas ajenas. Las voces sostenidas reciben ahora volumen de pista, capa y
  evento en tiempo real sin duplicar la atenuación.
- Añadidos mezcla mono/estéreo, limitador de pico compartido por ambos canales,
  retirada de voces de capas silenciadas y handles inicializados a `-1`; se
  evita así clipping duro, voces fantasma y consumo innecesario del mixer.
- Completados los opcodes JAM usados por las secuencias restantes, incluidos
  panorama/IIR, saltos indexados por tabla y comprobación de bancos de ondas.
  Las siete variantes válidas de `demobgm.jam` producen notas offline.
- Corregida la ruta de cinemáticas: cada ID usa su configuración original de
  stream o BGM 1-10, los casos sin audio ya no reproducen un stream incorrecto
  y el BGM no-STX comienza en su fotograma programado.
- Corregida la escena de resultados: final y desafío usan sus secuencias
  propias; el cierre normal del día no intenta reproducir `d_end2.jam`, cuyo
  banco 6 no existe en el catálogo IBNK/WSYS distribuido.
- Validación: build completa correcta, ejecutable `775`, `pc_jam_test` con 0
  fallos sintéticos (incluye eventos, 32 notas de SE, silbato 1/0/1 y 1956
  notas de demo), `pc_envelope_test` 63/63 y `git diff --check` limpio.
- Siguiente frontera: prueba in-game de menú, silbato corto/repetido, varios
  enemigos/objetos, batalla, jefe, pausa, fin de día y una cinemática no-STX.

### 2026-08-27 — SAVE-MERGE — Recuperación permanente sin duplicados

- **Superado el 2026-08-30 por SAVE-AUTHORITY.** La fusión de `init.gen` con
  generadores RAM por nombre/clase/proximidad resultó no ser una identidad
  estable: podía suprimir generadores distintos colocados cerca. Ya no se usa.
- Las piezas de nave ya recogidas o todavía guardadas en la caché no pueden
  reaparecer desde `init.gen`. Los duplicados heredados se descartan al cargar y
  tampoco vuelven a escribirse en la siguiente caché.
- Añadida defensa final en `PlayerState::getUfoParts`: una misma ID nunca puede
  incrementar dos veces los contadores, aunque una entidad duplicada antigua
  llegue físicamente hasta la nave.
- Build completa correcta; `pc_jam_test` sin fallos y `pc_envelope_test` 63/63.
  Pendiente una última validación in-game de la partida afectada.

### 2026-08-27 — SAVE-RECOVERY — Recuperar generadores perdidos en partidas existentes

- La partida puede conservar `mHasInitialised` para el Bosque de la Esperanza
  aunque una versión anterior haya perdido su caché viva. En ese estado el
  juego omite correctamente `init.gen`, pero ya no tiene de dónde restaurar los
  enemigos y muros de una sola aparición.
- `GeneratorCache::preload` informa ahora si restauró generadores. Si el mundo
  figura como visitado pero no recupera ninguno, se vuelve a cargar `init.gen`
  como reparación de la partida; una caché válida sigue teniendo prioridad y
  no se duplican objetos en visitas normales.
- Desactivado temporalmente en runtime el último secuenciador JAM de efectos
  posicionales para volver a la frontera de audio que el usuario confirmó como
  estable. El intérprete y sus pruebas se conservan para reactivarlo sólo tras
  validar por separado esta restauración.
- Build completa correcta; `pc_jam_test` sin fallos y `pc_envelope_test` 63/63.
  Pendiente confirmar in-game enemigos, muros de bombas y salida de Kabekui.

### 2026-08-27 — TEKI-KABEKUI — Salida segura de enemigos enterrados

- Corregido el crash durante la transición de enterrado a visible: el efecto
  de tierra ya no desreferencia obligatoriamente la esfera de colisión `cent`
  antes de que esté reconstruida y usa la posición del enemigo como respaldo.
- Corregidas las asociaciones de animación de `KabekuiA` y `KabekuiC`, que
  estaban usando el modelo de `KabekuiB`, y la inicialización vuelve a registrar
  una vez cada una de las tres variantes.
- Compilación completa correcta; `pc_jam_test` sin fallos sintéticos y
  `pc_envelope_test` con 63/63 comprobaciones. Pendiente confirmar in-game la
  aparición del modelo junto con la restauración de enemigos y muros.

### 2026-08-27 — SAVE-GENCACHE — Restaurar enemigos y paredes

- Identificada la pérdida al reconstruir las cachés vivas desde la tarjeta: el
  mínimo desplazamiento se inicializaba a `-1`, pero la comparación sólo
  aceptaba valores todavía menores. Como todos los offsets válidos son
  positivos, ninguna caché se reincorporaba a `mAliveCacheList`.
- Corregida la selección para aceptar primero el sentinel y ordenar después por
  desplazamiento. Se añade un error explícito si una lista no vacía no produce
  candidato, en vez de descartar silenciosamente los generadores persistentes.
- Pendiente in-game: cargar la partida del día 3 y confirmar enemigos y paredes
  al entrar en el segundo mundo.

### 2026-08-27 — AUDIO-004 — Efectos posicionales de criaturas y objetos

- Conectado el segundo reproductor persistente de `pikise.jam` que alimenta
  los 16 eventos de gameplay usados por enemigos, Pikmin, piezas, cebollas,
  construcciones y la nave.
- Importada la traducción exacta de los 282 estados de acción a comandos JAM.
  El intérprete completa ahora la selección por tabla y abre la subsecuencia
  real de cada efecto, además de cerrar y reutilizar sus 16 ranuras.
- Corregida la semántica del registro de condición de JAudio y añadidas las
  operaciones extendidas `A9/AB` que necesitan estas subsecuencias.
- Los efectos reciben atenuación por distancia y panorama desde la posición ya
  transformada a cámara por `SeSystem`, respetando las escalas originales de
  cada tipo de evento.
- Validación offline: una acción real de enemigo produce nota, los sonidos de
  sistema mantienen 32 notas y el silbato conserva la secuencia `1/0/1`.
  `pc_jam_test`: 0 fallos; `pc_envelope_test`: 63/63; build completa correcta.
- Pendiente in-game: confirmar variedad, volumen, dirección y parada de sonidos
  de enemigos/Pikmin/objetos; el silbato sigue pendiente de la prueba indicada.

### 2026-08-27 — AUDIO-002E/003/004/005 — Capas adaptativas y mezcla completa

- Portadas las máscaras originales de 16 capas de `piki_bgm.c`, incluida su
  ganancia normal/intensa y transiciones a 60 Hz independientes del vídeo.
- Cada voz BGM conserva pista y capa, por lo que fades y cambios de mezcla
  afectan también notas sostenidas. Menú, pausa, batalla cercana, atardecer y
  cuenta atrás vuelven a modificar la música como en JAudio.
- Añadido un segundo intérprete JAM simultáneo para `boss2.jam`/`boss3.jam` y
  crossfade de entrada/salida de jefe sin reiniciar la música del nivel.
- Reactivado el reproductor posicional de eventos. Sus voces conservan el
  evento de origen para aplicar atenuación durante toda su vida, y las cuatro
  tablas de handles JAM se inicializan a `-1` para no detener voces ajenas.
- Recuperados ambiente por cantidad de Pikmin y jingles de descubrimiento de
  piezas/textos, con los fades y efectos de sistema originales.
- Pendiente in-game: validar el balance y temporización audible; no se marcan
  estas fases como terminadas hasta recorrer menús, nivel, batalla, jefe y
  cinemáticas no-STX.

### 2026-08-27 — SAVE-RUN — Recuperación de posición de piezas al revisitar

- Analizado el core del crash al entrar en el segundo mundo desde el día 3:
  `GeneratorCache::loadUfoParts` restauraba una pieza con una posición finita
  pero corrupta del orden de `10^14`; `Pellet::doLoad` la enviaba a
  `RouteMgr::getSafePosition`, que abortaba correctamente.
- La carga valida ahora coordenadas guardadas y de aparición antes de consultar
  rutas. Si la posición actual es inválida recupera la posición original de la
  pieza; si ambas están dañadas usa un origen seguro en vez de abortar.
- No se modifica ni elimina el archivo guardado existente. Compilación completa
  y pruebas de audio offline correctas; pendiente confirmar la entrada al mundo
  con esa misma partida.

### 2026-08-27 — AUDIO-003 — Corte inmediato del silbato y gate JAM

- `Jac_StopOrimaSe` replica ahora la liberación directa de las voces de la
  pista 10 que hacía el DSP original; una pulsación corta del silbato deja de
  reproducir la muestra completa al soltar el botón.
- El intérprete conserva estado y temporizadores por voz, aplica el campo
  `gate` de las notas temporizadas e implementa `FlushAll`/`FlushRelease`.
  Esto también evita sostener instrumentos musicales más tiempo del indicado.
- Compilación completa correcta; `pc_jam_test` queda en 0 fallos sintéticos y
  `pc_envelope_test` en 63/63.
- Nuevo fallo observado pendiente de traza: una partida guardada en el día 3
  carga hasta el mapa, pero falla al volver a entrar en el segundo mundo. El
  archivo de tarjeta existe y tiene el tamaño completo; falta capturar la parte
  final del log de esta nueva reproducción para distinguir carga de caché de
  generadores, creación del mapa o cinemática de aterrizaje.

### 2026-08-27 — AUDIO-003 — Silbato reutilizable y pistas de jugador

- Implementado el opcode JAM `0xFB` (`Printf` de depuración). Al aparecer en
  la ruta del silbato ya no detiene por error todo el secuenciador de efectos.
- Añadida una regresión iniciar/detener/reiniciar para `JACORIMA_Gather`: genera
  `1/0/1` note-on, conserva resultado JAM correcto y permite volver a silbar.
- Conectados los stubs de pasos de Olimar (pista 8) y formación/C-stick
  (pista 7, magnitud, panorama y activación/parada) con los puertos originales
  de `pikise.jam`.
- Compilación completa correcta; `pc_jam_test` termina con 0 fallos sintéticos
  y `pc_envelope_test` con 63/63 comprobaciones.
- Pendiente in-game: confirmar que soltar el botón corta el silbato, que puede
  repetirse y que pasos/formación usan las muestras y cadencia esperadas.

### 2026-08-27 — RUN-AUDIO — Observaciones pendientes de audio in-game

- Las músicas contienen sonidos extraños que no parecen pertenecer a las
  composiciones originales; también podrían estar entrando fuera de tiempo.
- Siguen faltando numerosos efectos de sonido durante menús y gameplay.
- El silbato no respeta cuánto tiempo se mantiene pulsado: una pulsación corta
  reproduce el efecto completo en vez de detenerlo al soltar el botón.
- Tras reproducirse una vez, el silbato no vuelve a sonar en los intentos
  posteriores.
- Esta entrada sólo documenta la prueba realizada; no se modificó código.

### 2026-08-27 — AUDIO-003 — Reloj de efectos independiente

- `pikise.jam` ya no avanza dentro del bucle de ticks de la BGM. Mantiene su
  propio acumulador basado en tiempo real, tempo y timebase, por lo que los
  efectos pueden dispararse en menús/transiciones sin música y dejan de heredar
  el tempo de la canción activa.
- Compilación completa y pruebas offline validadas: `pc_jam_test` sin fallos
  sintéticos (la ruta de puerto de SE genera 32 notas) y `pc_envelope_test` con
  63 comprobaciones correctas.
- Pendiente de verificación in-game: sonidos de menú sin BGM, sincronía de
  acciones dentro de un nivel y finalización correcta de efectos sostenidos.

### 2026-08-27 — SETTINGS-001 — Menú PC de vídeo y controles planificado

- Registrado un overlay F1 pendiente para resolución, modo de pantalla,
  frecuencia, escala de render, límite de FPS y configuración completa de
  teclado/mando.
- La implementación se dividirá en configuración persistente, acciones de
  entrada desacopladas de SDL, aplicación segura de vídeo y finalmente UI.
- El audio sigue abierto: BGM/JAM y parte de los SFX ya son audibles, pero
  faltan capas adaptativas, catálogo completo de efectos, posicionamiento,
  transiciones especiales y validación prolongada.

### 2026-08-27 — AUDIO-002C/002D — Primera ruta BGM JAM nativa

- Añadido un intérprete JAM portable y acotado con pistas hijas, notas,
  note-off, esperas, registros, parámetros, call/return, saltos condicionales,
  bucles, tempo y timebase. Los límites detienen direcciones, pila u operaciones
  inválidas sin ejecutar el motor JAudio/DSP original.
- El auditor recorre 20.000 ticks de las 22 entradas BARC. Las pistas musicales
  principales permanecen activas sin opcode no soportado y sus eventos se
  cruzan offline con IBNK/WSYS.
- `Jac_SceneSetup` inicia ahora los IDs BGM originales y el reloj usa tiempo
  real acumulado. Note-on/off llega al mixer nativo y el control WSYS activo es
  correctamente el índice 0, como en `WaveScene_Set(bgm, 0)`.
- Pendiente RUN-012: primera comprobación audible ingame. Los parámetros
  dinámicos, panorama/capas y los efectos de sistema/gameplay siguen parciales.

### 2026-08-27 — AUDIO-002B — Evaluador de envolventes validado offline

- Reemplazado el borrador del evaluador por una unidad aislada que reutiliza
  las estructuras IBNK, conserva tamaños explícitos de tabla y no retiene
  punteros ni reserva memoria por paso.
- La duración se convierte a frames con `time * sampleRate / (600 * rate)`.
  `releaseParam` tiene precedencia sobre la tabla normal y la liberación
  forzada reproduce los dos triples reales `{0,5,0}` y `{0x0F,0,0}`.
- Índices o tablas inválidos, curvas no soportadas, frecuencias/rates inválidos
  y el límite de operaciones terminan de forma determinista con un código de
  resultado consultable.
- Prueba offline determinista: 63 comprobaciones para curvas, temporización,
  saltos, hold/end, releases y entradas inválidas; build normal y sanitizer
  completadas. No se integró en `SampleVoice` ni se probó dentro del juego.
- Integración posterior en esta misma unidad: `pc_audio_play_note` copia los
  osciladores seleccionados antes del bloqueo SDL y `SampleVoice` evalúa por
  frame estéreo los modos 0 (volumen) y 1 (afinación). El note-off usa las
  tablas IBNK y las voces sin osciladores conservan el fallback lineal.
- `AUDIO-002B` continúa en curso hasta implementar los modos de panorama 2/3,
  probar la ruta completa desde JAM y validarla manualmente.

### 2026-08-26 — GAME/SAVE — Progresión manual hasta el segundo nivel

- El usuario completó manualmente el primer nivel, guardó y cargó la partida,
  continuó hasta el segundo nivel y venció varios enemigos.
- Quedan validadas la progresión inicial, una recarga persistente y el combate
  básico después de cargar. No se extrapola este resultado a niveles tardíos,
  jefes, final de día prolongado ni finales; la completabilidad total continúa
  pendiente.

### 2026-08-26 — AUDIO-002B — Infraestructura de note-off/release

- Añadido `pc_audio_release_wave`: inicia una liberación lineal expresada en
  frames de salida y conserva `pc_audio_stop_wave` como parada inmediata.
- Añadido un contador consultable de voces activas para diagnosticar notas
  colgadas y verificar la liberación al conectar el secuenciador.
- Documentada desde el código original la máquina `Bank_OscToOfs`: estados,
  comandos 0x0D/0x0E/0x0F, curvas y escala temporal. El DSP avanza cada 80
  muestras; con `rate=1`, `time` está expresado en unidades de 1/600 de segundo
  y la duración general se divide por `osc.rate`.
- La liberación se aplica dentro del callback una vez por frame estéreo, sin
  reservar memoria ni depender del framerate del juego. Los handles
  generacionales evitan liberar una voz reciclada por error.
- Build normal completado al 100 %. No se ejecutó el juego. La siguiente pieza
  de AUDIO-002B es interpretar las curvas attack/release IBNK ya catalogadas.
- El handoff deja esta pieza dividida en evaluador puro con smoke tests y una
  integración posterior en `SampleVoice`; JAM queda explícitamente fuera del
  siguiente cambio para reducir el riesgo de regresión.

### 2026-08-26 — AUDIO-002A/002B — Resolución de ondas y note-on instrumental

- El catálogo WSYS analiza ahora IDs virtuales y controles WBCT, SCNE, C-DF y
  C-EX, incluidas dependencias y referencias externas. Puede resolver el
  `waveId` de un Vmap a la onda y archivo `.aw` activos para una escena.
- IBNK conserva 203 osciladores y sus tablas attack/release acotadas. Se
  corrigió la semántica de los multiplicadores 0x08/0x0C usando `oneshot.c`:
  el primero afecta volumen y el segundo afinación, pese a los nombres dudosos
  de la estructura decompilada.
- Añadido un primer `pc_audio_play_note`: banco virtual, programa, nota y
  velocidad seleccionan región IBNK y onda WSYS; calcula volumen cuadrático de
  velocidad y afinación relativa a la nota base antes de crear una voz BGM/SE.
  Aún no ejecuta las curvas ni ofrece release, por lo que AUDIO-002B sigue en
  curso y esta API todavía no está conectada a eventos del juego.
- Validación cruzada offline: 748 de las 762 regiones apuntan a IDs presentes
  en algún control WBCT. Las 14 restantes son referencias serializadas que no
  aparecen en ninguna escena del WSYS; se mantienen como no resolubles en vez
  de remapearlas por tanteo. Los IDs conocidos de WSYS 0 y 2 se resuelven.
- Pruebas: smoke tests de IBNK/WSYS, build normal y build ASan/UBSan al 100 %.
  No se ejecutó el juego y no se afirma audio nuevo audible.

### 2026-08-26 — AUDIO-001B/002A — Mixer priorizado y primer catálogo IBNK

- Añadidos buses separados para stream, BGM, SE y AI/DMA, volumen por bus,
  contador de clipping y selección de voces por prioridad y antigüedad. Los
  handles incluyen generación para que un stop atrasado no mate una voz que
  ya haya reutilizado el mismo slot.
- Añadido `pc_instrument_bank.*`, un parser nativo big-endian que conserva los
  22 slots físicos del BX y valida offsets antes de recorrer BANK, INST, PER2,
  regiones de nota/velocidad y Vmap. Registra también la traducción de ID de
  banco virtual a slot físico.
- La prueba offline recorre correctamente 19 bancos presentes, 216 instrumentos
  y 762 regiones. Detecta una única percusión antigua `PERC` y conserva su slot
  como no soportado hasta conocer su estructura; no se interpreta por tanteo.
- Pruebas realizadas: smoke test offline del parser, build normal y build
  ASan/UBSan al 100 %. No se ejecutó el juego.
- AUDIO-001B sigue en curso hasta probar concurrencia/carga y política de
  limitación. AUDIO-002A sigue en curso hasta parsear osciladores/envolventes y
  WBCT/SCNE para resolver wave IDs según la escena.

### 2026-08-26 — AUDIO-PLAN-001 — Validar y desglosar la ruta de audio nativo

- Revisados el encabezado BX real, `bankloader.c`, `bankread.c`, `bankdrv.c`,
  `waveread.c`, `connect.c`, `noteon.c`, `jammain_2.c`, `piki_bgm.c` y
  `piki_scene.c` frente al backend PC activo.
- Confirmado que `pikibank.bx` contiene 22 WSYS y 22 IBNK. El flujo original
  resuelve banco y WSYS virtuales, dependencias de escena, instrumentos y
  regiones nota/velocidad antes de producir una voz; interpretar JAM sin esas
  capas no puede seleccionar de forma fiable la muestra correcta.
- AUDIO-002 se divide en catálogo IBNK/conexiones, voz instrumental, núcleo
  JAM/reloj, primera BGM vertical y controles adaptativos. Se añade AUDIO-001B
  para preparar el mixer antes de someterlo a síntesis sostenida.
- Verificación realizada: inspección estructural de fuentes y assets y build
  normal al 100 %. No se marca funcionalidad audible nueva como terminada;
  cualquier resultado en juego continúa reservado a prueba manual del usuario.

### 2026-08-26 — AUDIO-001A-regresión — Restaurar backend estable tras experimento JAudio

- El intento de reactivar JAudio había añadido simultáneamente al ejecutable
  15 módulos originales/incompletos y `audio_stubs.cpp`.
- La combinación no era válida: producía símbolos duplicados (`Jac_Start`,
  `Jac_PlaySystemSe`, `DSPInit`, etc.) y numerosas referencias DSP/ARAM sin
  implementar, por lo que la build no enlazaba de forma limpia.
- Restaurada en `CMakeLists.txt` la selección estable: mixer SDL/STX y stubs.
  Los módulos experimentales se conservan fuera del binario para estudiarlos
  y portar el DSP software por etapas, sin perder el trabajo realizado.
- Restaurado también el tipo `OSMessage` en la cola de `audiothread`; usar un
  `int` como destino en x86_64 podía sobrescribir memoria.
- Pruebas realizadas: build normal y ASan/UBSan al 100 %.
- Pendiente: RUN-010 para confirmar manualmente que vuelve el audio de apertura.

### 2026-08-26 — GFX-BLEND-002 — Deshabilitar efecto blur fullscreen en PC

- Archivo modificado: `src/plugPikiColin/mapMgr.cpp`.
- El efecto blur de `MapMgr::postrefresh()` dibuja un rectángulo fullscreen
  semi-transparente cada frame usando `BLEND_MultiTexture`. Requiere
  `GXCopyTex` para capturar el framebuffer en una textura, pero ese stub es
  vacío. Las texturas de blur quedan negras/vacías, resultando en un
  rectángulo oscuro translúcido sobre toda la escena.
- Las cinemáticas no se veían afectadas porque el blur se desactiva cuando
  `mMoviePlayer->mIsActive` es true.
- Se añadió `!PIKI_PC_PORT` al guardado `#if PIKI_USE_DGX` del bloque de
  blur para deshabilitarlo en el port.
- Pruebas realizadas: build normal y ASan/UBSan al 100 %.
- Pendiente: verificar que gameplay ya no tiene filtro oscuro.

### 2026-08-26 — GFX-BLEND-001 — Corregir blend mode en transición 3D→2D

- Archivo modificado: `src/sysDolphin/dgxGraphics.cpp`.
- `setOrthogonal()` no configuraba blend mode al pasar de rendering 3D a 2D.
  El último material 3D podía dejar `GL_BLEND` deshabilitado, causando que el
  rectángulo de fade overlay (negro semi-transparente) se dibujara completamente
  opaco.
- En cinemáticas el fade alcanza 1.0 y el overlay no se dibuja, por eso se
  veían bien. En gameplay, después de transiciones, `mCurrentFade` necesita
  blend para ser semi-transparente.
- Se añadió `GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA,
  GX_LO_NOOP)` en `setOrthogonal()` para asegurar estado de blend conocido
  para overlays 2D.
- Pruebas realizadas: build normal y ASan/UBSan al 100 %.
- Pendiente: verificar visualmente que gameplay ya no tiene filtro oscuro.

### 2026-08-26 — AUDIO-002-parte — Corregir verificación de destrucción de eventos JAudio

- Archivo modificado: `pc_port/dolphin_stubs/audio_stubs.cpp`.
- Los stubs `Jac_CheckFreeEvents` y `Jac_DestroyEvent` no mantenían un
  contador real. La verificación en `SeSystem::destroyEvent` comparaba el
  número de eventos libres antes y después de la destrucción; al ser siempre
  0, la condición `before == después` siempre era cierta y activaba el
  `ERROR("なんでだよ～！！")` → panic.
- Añadido contador estático `sFreeEvents` (máximo 16) que aumenta en
  `Jac_DestroyEvent`, disminuye en `Jac_CreateEvent` y se reinicia en
  `Jac_InitAllEvent`.
- Pruebas realizadas: build normal y ASan/UBSan al 100 %.
- Pendiente: verificar que la flor+píldora不再 produce crash (RUN-006).

### 2026-08-26 — Documentación y handoff consolidados

- Reescrito `AI_HANDOFF.md` para reflejar el estado actual y eliminar
  diagnósticos visuales obsoletos de sesiones anteriores.
- Documentados flujo de trabajo, builds, decisiones de temporización,
  optimizaciones, correcciones GX, riesgos 64 bits, fallos pendientes y próxima
  secuencia de pruebas.
- El usuario considera por ahora correctos rendimiento y velocidad; PERF-001,
  PERF-002 y TIME-001 quedan verificados y pasan a `[x]`.

### 2026-08-26 — PERF-002 — Caché de texturas y estado por draw

- Sustituido `glBufferData` por primitiva por un VBO de streaming de 16 MiB:
  se descarta una vez al comenzar el frame y cada draw añade sus vértices con
  `glBufferSubData`, evitando miles de recreaciones y sincronizaciones.
- Segunda medición: gameplay subió de 10–14 a 14–19 FPS y los tramos pesados
  de la cinemática de unos 30 a 40–50 FPS; escenas ligeras alcanzan 60 FPS.
- Añadida caché tipada de uniformes escalares, vectores y matrices: solo se
  envían al driver cuando su valor cambia.
- Depth, blend, logic op, culling y máscaras de color/alpha también omiten
  llamadas OpenGL cuando el estado GX solicitado ya está activo.
- El log de referencia mejoró de unos 5 FPS a 7–14 FPS en título/gameplay,
  30 FPS durante la mayor parte de la cinemática y hasta 60 FPS en escenas
  ligeras. Esto confirma que el coste varía con la cantidad de dibujo.
- `GXInitTexObj` y `GXInitTexObjCI` ya no decodifican ni suben otra vez una
  textura cuyo origen, formato, dimensiones, wrap y paleta no han cambiado.
- Cada draw solo enlaza los mapas usados por sus etapas TEV y omite enlaces que
  ya coinciden; los samplers quedan asignados una sola vez.
- La configuración de atributos del VBO y `glUseProgram` también pasa de cada
  draw a la inicialización del backend.
- Verificación del usuario: gameplay cercano a 30 FPS; rendimiento aceptado por
  ahora sin una regresión visual nueva atribuida a estas optimizaciones.

### 2026-08-26 — TIME-001 — Cadencia original restaurada en PC

- La prueba confirmó 29,9 FPS y 33,33 ms tanto en película como gameplay, pero
  movimiento, silbato, cámara de despertar, cebolla y Pikmin seguían rápidos.
  Esto descartó presentación e input y localizó el desfase en la simulación de
  `NewPikiGameSection`.
- Una comparación posterior con gameplay real descartó la escala experimental
  0,5 por resultar demasiado lenta. Películas y gameplay usan escala temporal
  1,0; la presentación permanece limitada a su cadencia nativa de 30 Hz.
- La ruta PC había anulado `waitPostRetrace`, por lo que `setFrameClamp(2)` ya
  no limitaba gameplay/cinemáticas a una actualización cada dos retraces.
- `DGXGraphics::waitRetrace` configura ahora el intervalo SDL desde
  `mSystemFrameRate`, conservando 60 Hz para intervalo 1 y 30 Hz para 2.
- Verificación del usuario: tras comparar con gameplay real se mantiene escala
  1,0 y la velocidad queda aceptada por ahora.

### 2026-08-26 — PERF-001 — Primera limpieza del camino crítico

- Eliminados diagnósticos por primitiva que calculaban firmas, hacían búsquedas
  lineales y escribían continuamente en `/tmp/opencode/draws.log`.
- Eliminados `glGetError` por draw y cargas de las 16 etapas TEV cuando solo
  están activas una o unas pocas.
- Eliminados cronómetros por subsistema y el limitador manual adicional de
  `VIWaitForRetrace`; se conserva una lectura resumida de FPS cada dos segundos.
- Build Release y ASan/UBSan completadas; medición posterior del usuario
  confirmó la mejora hasta gameplay cercano a sus 30 FPS nativos.

### 2026-08-25 — GX-TEX-003 — Alfa de intensidad y ruta CI/TLUT implementados

- Archivos modificados: `pc_port/gl/pc_gfx.cpp`, `pc_port/gl/pc_gfx.h` y
  `pc_port/dolphin_stubs/gx_stubs.cpp`.
- La falta de cambio visual tras las correcciones TEV confirmó que el cuello
  de botella dominante estaba en la entrada de texturas.
- I4 e I8 ahora replican intensidad en RGBA; antes forzaban alpha 255 y
  convertían máscaras monocromas en cuadrados opacos.
- `GXInitTexObjCI`, `GXInitTlutObj` y `GXLoadTlut` dejaron de ser stubs.
- Se decodifican C4, C8 y C14X2 usando TLUT IA8, RGB565 o RGB5A3, respetando
  el tiling de GameCube y difiriendo la carga si la paleta aún no está lista.
- Pruebas realizadas: build normal y ASan/UBSan al 100 %.
- Pendiente: validación visual de menú y selector; auditar XF por separado para
  la iluminación plana de los modelos.

### 2026-08-25 — GX-TEV-REG-001 — Registros TEV/Konst BP corregidos

- Archivos modificados: `pc_port/gl/pc_gfx.cpp` y `CMakeLists.txt`.
- El segundo log confirma `First GX draw ... GL status 0x0000`; la escritura
  inválida de uniforms queda verificada como resuelta.
- Los pares BP E0/E1–E6/E7 se estaban leyendo con campos equivocados y se
  guardaban en `pairIdx + 1`. El último par escribía fuera de
  `sTevRegisters[4]`, corrompiendo memoria.
- Ahora se decodifican correctamente palabras RA/BG: R y A desde RA, B y G
  desde BG; el índice del par coincide con PREV/REG0/REG1/REG2.
- El bit 23 separa escrituras Konst de 8 bits de registros TEV signed-11, por
  lo que K0–K3 dejan de sobrescribir PREV/REG0–REG2.
- Se retiró `PC_GFX_TRACE` de la compilación: generaba `tevreg.log` en cada
  actualización y alcanzó 557 MB durante la prueba.
- Pruebas realizadas: build normal y ASan/UBSan al 100 %.
- Pendiente: validación visual RUN-001/002/003/005.

### 2026-08-25 — GX-TEV-006 — Escritura inválida del uniform swap corregida

- Archivo modificado: `pc_port/gl/pc_gfx.cpp`.
- El log del usuario confirmó que el shader enlaza correctamente, pero el
  primer dibujo devuelve `GL_INVALID_OPERATION`.
- `uTevSwapSel` aparece en la reflexión OpenGL como `ivec2`; se estaba
  escribiendo con `glUniform4i`, por lo que el controlador rechazaba el estado.
- Se carga y utiliza ahora `glUniform2i` para ese uniform.
- Pruebas realizadas: build normal y ASan/UBSan al 100 %.
- Pendiente: confirmar en el siguiente log que `First GX draw` devuelve
  `GL status 0x0000`; después se reevaluará el resultado visual.

### 2026-08-25 — GX-TEV-004/005 y FX-001 — Konst, comparaciones y estado PE

- Archivo modificado: `pc_port/gl/pc_gfx.cpp`.
- La segunda prueba visual siguió mostrando el fondo ausente, partículas
  cuadradas y materiales saturados.
- Cada etapa recibe ahora su selección Konst real, incluidas constantes
  fraccionarias y componentes R/G/B/A de K0–K3.
- Se reconstruyen las operaciones comparativas TEV codificadas mediante los
  campos bias/scale y se evalúan R8, GR16, BGR24, RGB8 y A8 (GT/EQ).
- El lector de display lists procesa ahora el registro PE de blend y máscaras
  de escritura (`0x41`) y alpha compare (`0xF3`), ambos relevantes para que
  los billboards descarten o mezclen sus píxeles transparentes.
- Pruebas realizadas: build normal y ASan/UBSan al 100 %.
- Pendiente para marcar `[x]`: validación visual y log de compilación del
  shader en RUN-001/002/003/005.

### 2026-08-25 — GX-TREF-001/TEX-001/TEV-006 — Estado de textura y swap corregido

- Archivo modificado: `pc_port/gl/pc_gfx.cpp`.
- La prueba anterior mostró fondo de título ausente, partículas cuadradas y
  modelos con colores extremos; el núcleo TEV estaba leyendo estado parcial.
- TREF ahora conserva por etapa mapa, coordenada, activación de textura y canal
  raster para sus dos mitades.
- El shader selecciona UV por `GXTexCoordID`, no por `GXTexMapID`.
- Los selectores swap se leen de los bits 0–3 del combiner alpha y las tablas
  se leen de KSEL. El shader hace swizzle dentro de la misma entrada, en vez de
  mezclar incorrectamente componentes raster y textura.
- Pruebas realizadas: build normal y ASan/UBSan al 100 %.
- Pendiente para marcar `[x]`: repetir RUN-001/002/003/005 y comprobar fondo,
  partículas y materiales.

### 2026-08-25 — GX-TEV-001/002/003 — Núcleo TEV corregido, pendiente de prueba visual

- Archivo modificado: `pc_port/gl/pc_gfx.cpp`.
- Color A/B/C/D se decodifica desde los bits 12/8/4/0.
- Alpha A/B/C/D se decodifica desde los bits 13/10/7/4; los bits 0–3 ya no
  se confunden con operandos.
- Se añadieron comprobaciones `static_assert` para impedir regresiones en el
  orden de los operandos.
- El shader usa `d + a*(1-c) + b*c` (o resta), bias positivo/negativo, escala
  GX y distingue resultados con clamp activado y desactivado.
- Pruebas realizadas: build normal y ASan/UBSan al 100 %.
- Pendiente para marcar `[x]`: ejecutar RUN-001 y RUN-003/RUN-005, confirmar
  que el shader compila en el controlador OpenGL y comparar materiales.

### 2026-08-25 — TEST-001 — Recorridos manuales definidos

- Archivo añadido: `TEST_PLAN.md`.
- Se definieron recorridos separados para título, selector de archivo,
  cinemática, primer nivel y recarga/cambio de día.
- La ejecución se reserva al usuario; cada recorrido pide log completo,
  capturas y el punto exacto del primer fallo.
- Resultado: procedimiento reproducible listo para las siguientes fases.

### 2026-08-25 — BUILD-002 — Build ASan/UBSan recuperada

- Archivo ajustado: `include/sysNew.h`.
- Se eliminaron declaraciones PC duplicadas de `operator delete` con una
  especificación de excepción incompatible.
- Pruebas realizadas: `cmake --build build -j4` y
  `cmake --build build-sanitize -j4`.
- Resultado: ambas terminan con `[100%] Built target pikmin_pc`.
- Riesgo registrado: persisten conversiones heredadas entre punteros de 64
  bits y `u32`; se tratarán en las fases de robustez y memoria.

### 2026-08-25 — BUILD-001 — Build normal recuperada

- Archivos modificados: `pc_port/gl/pc_gfx.cpp`.
- Se declararon y cargaron de forma centralizada las funciones OpenGL usadas
  para comprobar shaders, programas y uniforms.
- Se unificó la nomenclatura con el sufijo `_ptr` y se eliminaron cargas
  duplicadas dentro de `pc_gfx_init`.
- Prueba realizada: `cmake --build build -j4`.
- Resultado: `[100%] Built target pikmin_pc`.

### 2026-08-25 — Creación de la hoja de ruta

- Se consolidaron los hallazgos de la auditoría actual.
- La build está bloqueada por símbolos OpenGL de diagnóstico sin declarar.
- Se identificó como causa raíz visual la combinación de decodificación TEV/XF
  incorrecta y una ecuación TEV compensatoria no equivalente al hardware.
- Verificación ejecutada: `cmake --build build -j4` falla en `pc_gfx.cpp`.

### 2026-08-25 — GX-TEX-003 / GX-XF-001 — Selector verificado y canales XF corregidos

- La prueba manual confirma que el selector de archivo ya no muestra las
  partículas indexadas como cuadrados; `GX-TEX-003` queda verificado.
- El fondo del título continúa negro, por lo que se ha aislado como un fallo
  distinto de CI/TLUT.
- Los controles de canal XF de los display lists confundían `matSrc` (bit 0)
  con `enable` (bit 1), omitían `ambSrc`, desplazaban `diffFn`/atenuación y
  solo conservaban las luces 0–3.
- Se corrigió la decodificación completa de los bits 0–14 y la traducción de
  `GX_AF_NONE`, `GX_AF_SPOT` y `GX_AF_SPEC`.
- Pruebas realizadas: build normal y ASan/UBSan al 100 %.
- Pendiente: prueba visual del título y de los modelos iluminados para marcar
  `GX-XF-001` como terminado.

### 2026-08-25 — GX-XF-001 — Fuente raster sin iluminación

- La prueba manual posterior no produjo cambios visibles en título ni modelos.
- El registro acotado de dibujos mostró que los materiales del título usan
  mayoritariamente canales sin iluminación y `matSrc = GX_SRC_REG`.
- El shader trataba todo canal desactivado como color de vértice, ignorando
  el color de material. Ahora `matSrc` selecciona el valor raster también
  cuando la iluminación está desactivada, como hace GX, para COLOR0 y COLOR1.
- Pruebas realizadas: build normal y ASan/UBSan al 100 %.
- Pendiente: confirmación visual antes de cerrar la tarea.

### 2026-08-25 — GX-XF-001 — Separación de controles COLOR/ALPHA

- La prueba confirmó que la fuente raster corregida recupera el color original
  del logotipo y mejora parcialmente a Olimar, pero satura el cohete y empeora
  la cebolla; el fondo del título sigue ausente.
- El backend almacenaba COLOR0/ALPHA0 y COLOR1/ALPHA1 en una sola estructura.
  Las llamadas de alpha sobrescribían la activación, fuentes, máscara y
  funciones de iluminación RGB configuradas justo antes por el juego.
- Se separaron ambos estados y se añadieron los registros XF ALPHA0/ALPHA1,
  que antes se ignoraban. La fuente alpha del shader usa ahora su control real.
- Pruebas realizadas: build normal y ASan/UBSan al 100 %.
- Pendiente: validación visual; después se implementará la ecuación exacta de
  iluminación/atenuación sin mezclarla con el problema del fondo del título.

### 2026-08-26 — GX-LIGHT-003 — Saturación del canal especular

- La prueba confirmó transparencias correctas y cuerpos no especulares
  mejorados. Permanecían blancos el cuerpo Pikmin, la cabeza de Olimar y la
  mayor parte del cohete/cebolla; ojos, hojas y algunas piezas eran correctos.
- Todos los elementos afectados comparten COLOR1 especular. El shader sumaba
  al mismo canal una iluminación difusa completa y el término especular,
  duplicando la luz y saturando el resultado.
- Para `GX_AF_SPEC`, COLOR1 usa ahora solo su ruta especular. Además el color
  ambiente inicial de los canales pasa de blanco a negro, que es el estado de
  `GXInit`; un ambiente COLOR1 blanco saturaba el material aun sin luces.
- Se añadió la cabecera `<chrono>` requerida por el diagnóstico acotado.
- Pruebas realizadas: build normal y ASan/UBSan al 100 %.
- Pendiente: validación visual de cohete, Olimar, cebolla y los tres Pikmin.

### 2026-08-26 — GX-LIGHT-003 — Especular validado parcialmente

- Las capturas confirman que el cohete cinematográfico y la cebolla recuperan
  sus colores, reflejos, volumen y materiales transparentes después de quitar
  la doble iluminación de COLOR1 y restaurar el ambiente negro.
- Olimar también mejora, aunque cabeza/visor y el halo asociado aún necesitan
  aislarse. La nave estrellada conserva una pose o deformación incorrecta.
- `GX-LIGHT-003` permanece en curso hasta verificar Pikmin y distinguir los
  defectos especulares restantes de matrices de animación y partículas.

### 2026-08-26 — GX-LIGHT-004 — Normales por matriz de vértice

- Las capturas muestran materiales del cohete y cebolla sustancialmente
  recuperados, pero iluminación desigual en modelos articulados.
- La ruta PC transformaba cada posición con su `PNMTXIDX`, pero dejaba todas
  las normales para una única matriz global posterior. Ahora cada normal se
  transforma con el mismo índice que su posición y el shader recibe identidad
  para los vértices ya transformados.
- También se inicializan normales y TEX1 cuando el atributo no está presente,
  evitando datos indeterminados.
- Pruebas realizadas: build normal y ASan/UBSan al 100 %.
- Pendiente: prueba visual de cabeza de Olimar, Pikmin y nave estrellada.

### 2026-08-26 — AUDIO-002 — Catálogos nativos y mixer de muestras

- Añadidos `pc_wave_bank.*` y `pc_sequence_archive.*`: analizan `pikibank.bx`
  y el BARC embebido de `pikiseq.arc` con lectura big-endian y comprobación de
  todos los offsets, sin convertirlos en punteros de 32 bits.
- El catálogo WSYS contiene 22 sistemas y 882 ondas. Se decodifican los tres
  formatos usados por el juego: DSP ADPCM4, PCM8 y PCM16.
- Las 22 secuencias originales son legibles; prueba aislada: 22/22 entradas,
  276.032 bytes totales.
- El backend SDL dispone de 64 voces mono simultáneas, remuestreo lineal,
  volumen/panorama, bucles y caché de PCM decodificado. Una muestra frecuente
  ya no se abre y decodifica desde disco en cada reproducción.
- `src/jaudio/pikiseq.c` sólo aporta la tabla BARC estática. El motor DSP/JAudio
  incompleto continúa fuera de la build.
- Pruebas: build normal y ASan/UBSan al 100 %; smoke tests de banco y BARC.
- Pendiente: resolver IBNK y ejecutar comandos JAM para producir BGM real.

### 2026-08-30 — PERF-NATIVE-001/002/005 — Primera pasada integral

- Confirmado que `Texture::grabBuffer` no llama a `glReadPixels` en este port:
  esa rama pertenece al backend antiguo excluido por `PIKI_USE_DGX`.
- Añadidas consultas GPU asíncronas para escena y blit. Sus resultados aparecen
  en `[PERF GPU]` con `PIKMIN_PERF_STATS=1`; el anillo evita bloquear CPU/GPU.
- Compactado el formato de streaming de 104 a 72 bytes por vértice. Con las
  ~98.000 entradas observadas en el título, el tráfico baja aproximadamente de
  9,7 a 6,7 MiB por frame, sin cambiar atributos visibles del shader actual.
- Eliminadas copias/asignaciones de osciladores y efectos por NoteON; las voces
  referencian el catálogo IBNK inmutable cargado durante la inicialización.
- Separadas las opciones de build: `-march=native` deja de arrastrar O3/LTO y
  el IPO se habilita mediante `CheckIPOSupported`, sin `-flto=auto` específico
  de GCC en builds Clang o paquetes portables.
- CMake registra JAM y mixer en CTest cuando existen assets locales y conserva
  CI pública sin datos del juego. Validación offline RelWithDebInfo: build
  completa y 4/4 tests correctos. No se ejecutó el juego.
- Pendiente: recoger tiempos GPU reales y decidir con ellos entre shader raster
  especializado, streaming persistente y eliminación/optimización del blit.

### 2026-08-30 — INPUT-001/SAVE-001 — Cámara relativa y carga de brotes

- El cursor por ratón trataba SDL X/Y como X/Z absolutos del mundo; al rotar la
  cámara con Q, sus direcciones aparentes rotaban y se invertían. Ahora cada
  delta se expresa en la base horizontal normalizada de la cámara, sin alterar
  la sensibilidad con su inclinación, y se consume una sola vez.
- Eliminada la zona muerta fija de dos counts del ratón relativo para conservar
  movimientos finos; sensibilidad y límite radial permanecen iguales.
- El log de una carga fallida termina tras inicializar `2-3.gen`, mientras una
  segunda carga supera exactamente esos mismos 17 spawns. En la fase siguiente
  se encontró una corrupción determinista dependiente del save:
  `BPikiInf::doRestore` convertía un `PikiHeadItem` a `Piki` y escribía campos
  del animador fuera del objeto. Ahora restaura los campos de brote que
  `doStore` guardó y valida color/etapa al leer.
- Validación offline: build completa y CTest 4/4. No se ejecutó el juego;
  quedan pendientes la rotación completa de cámara y la partida problemática.

### 2026-08-30 — INPUT-001/SAVE-AUTHORITY — Ratón y entidades persistentes

- Ratón: SDL Y positivo significa bajar en pantalla; el movimiento vertical se
  transforma ahora mediante `-mViewZAxis` horizontal. X continúa usando
  `mViewXAxis`, por lo que ambos ejes siguen a la cámara en cualquier yaw.
- Causa de las entidades ausentes: se estaban combinando dos autoridades
  incompatibles —caché persistente e `init.gen`— y después se descartaban
  supuestos duplicados mediante nombre, tipo y distancia menor de una unidad.
  Esa heurística no identifica de forma única un generador y podía eliminar
  contenido legítimo de cualquier nivel.
- Política estructural: sin caché se carga `init.gen`; con caché válido se carga
  exclusivamente el caché, incluso si el flag redundante `mHasInitialised` de
  una partida antigua discrepa. Se retiró completamente el emparejamiento
  espacial.
- La carga del directorio del caché es transaccional y valida tamaños usados y
  libres, IDs/estados de los cinco niveles, rangos, continuidad, suma de bloques
  y conteos mínimos antes de publicar ningún descriptor. Generadores, criaturas
  y piezas usan streams acotados. Un bloque de generadores malformado se elimina
  y compacta antes del fallback, evitando aplicarlo después sobre la reparación.
- Las escrituras de generadores, criaturas y piezas se construyen primero en un
  buffer temporal acotado: una falta de espacio ya no puede escribir fuera del
  heap ni dejar un registro parcial. Se acotaron además conteos de brotes y
  estados de piezas al leer la tarjeta.
- Nueva prueba `pc_generator_cache_validation_test`: acepta cachés contiguos de
  varios niveles y rechaza solapamientos, huecos, componentes incoherentes,
  conteos imposibles, estados inválidos y discrepancias used/free.
- Validación offline: build completa y CTest 5/5. No se ejecutó el juego.
  Pendiente confirmar el save afectado y recorrer más de un nivel.

### 2026-08-30 — RELEASE-001 — Preparación de la carpeta pública

- Corregida la regla `save/` de `.gitignore` para que solo excluya `/save/` y
  permita publicar las pruebas estructurales situadas en `pc_port/save/`.
- Preparada una copia independiente sin metadatos Git ni datos del juego en
  `../pikmin-open-source`, generada exclusivamente desde el manifiesto público.
- La copia pública se valida mediante configuración, build Release y CTest sin
  ROM ni assets propietarios; el juego no se ejecuta.
- Pendiente del usuario: probar el instalador gráfico con una ROM propia y
  subir manualmente el contenido de esa carpeta a su repositorio.

### 2026-08-30 — TIME-002 — Scheduler fijo e integración autoritativa

- Añadido `PcFrameScheduler`, independiente de SDL/OpenGL y alimentado por
  tiempo monotónico inyectable. Usa catch-up máximo de cuatro, descarta pausas
  de 500 ms y reinicia la deuda al cambiar `frameClamp`.
- `System::run` sólo ejecuta `app->idle()` para ticks debidos y
  `updateSysClock()` publica exactamente 1/60 o 1/30. VSync Off ya no puede
  acelerar lógica, input, audio o RNG.
- El pacing de swap conserva deadlines absolutos. `[Timing]` informa clamp,
  delta fijo, ticks, presents, FPS render, descartes, alpha y refresco.
- `pc_frame_scheduler_test` valida diez minutos y toda la matriz, además de
  jitter, bloqueo, suspensión y 60->30. No se ejecutó el juego.
- Pendiente: dividir `BaseApp::idle()` en update/render, presentar a Hz nativos
  e interpolar estado visual seguro.

### 2026-08-31 — TIME-002 — Base de snapshots visuales con identidad

- Añadido `pc_port/timing/pc_visual_snapshot.*`, independiente del juego y de
  GX. Las claves son `(owner, domain, slot)` y las matrices se copian, evitando
  la reutilización de slots/buffers que corrompió los intentos anteriores.
- El almacén conserva dos estados autoritativos, produce un conjunto de
  presentación interpolado y trata correctamente objetos nuevos, eliminados,
  teleports, cambios de escena y alpha fuera de rango.
- Añadido `pc_visual_snapshot_test`; build Release completa y CTest 8/8 pasan.
  No se ejecutó el juego y el componente aún no está integrado en `BaseShape`.
- Próximo paso: crear una frontera de render estrictamente de lectura y capturar
  `(owner visual persistente, joint/envelope index)` al terminar cada tick, sin
  llamar `animate()` ni `updateAnim()` durante presents intermedios.
- Extraída `PlugPikiApp::updateFixed()` y
  `renderAuthoritativeFrame()` conservando exactamente una ejecución y el orden
  anterior. La build completa y CTest 8/8 siguen correctos.
- La auditoría descartó usar `BaseShape*` como owner: modelos compartidos, como
  el de los Pikmin, reciben muchas poses de criaturas distintas en un frame.
  También confirmó escrituras de pose, materiales y fade dentro del render.
- Añadido `PcRenderPhase` y su CTest. `System::run` marca cada tick antes de
  input/audio; la presentación no incrementa el serial y limita alpha a 0..1.
- Protegido el avance de `AnimContext`, animaciones PVW y fades/timers globales
  durante una futura presentación. Build Release completa y CTest 9/9 pasan;
  no se ejecutó el juego ni se habilitaron presents adicionales.
- Propagado owner estable por las 37 llamadas a `BaseShape::updateAnim()` y
  añadido adaptador `PcVisualRuntime`, que captura matrices justo antes del
  draw. El runtime queda apagado por defecto hasta integrar presents, evitando
  coste por hueso en el camino actual. Nuevo CTest multi-actor/multi-matriz;
  build completa y CTest 10/10 correctos.
- Implementado `PcVisualShapeOverride`: reemplazo completo sólo durante
  Presentation, resolución transaccional y restauración RAII del buffer
  autoritativo. Protegidos además los animadores centrales y sus coordinadores
  para que una presentación no avance frames/eventos. Build completa y CTest
  10/10; runtime y presents continúan apagados.
- Añadido almacén semántico de cámara independiente de GX, con cambios de
  cámara, discontinuidad, normalización de up y validación de proyección. Nuevo
  CTest; build completa y CTest 11/11 correctos. Integrado además el adapter
  sobre `DGXGraphics::setCamera`: recompone look-at/ejes/frustum y restaura la
  cámara completa al terminar el frame; continúa apagado por defecto.
- Auditadas las partículas: sus tres entradas de update quedan bloqueadas en
  Presentation y `drawPtclOriented` ya no persiste normales ni mata partículas
  degeneradas en un draw extra. Billboard, hijos y simples son de sólo lectura.
  Build completa correcta; los presents adicionales continúan desactivados.
- Separadas más mutaciones de render: fades/banner y `ModeState::postUpdate`
  en `BaseGameSection`, actualización de audio posicional en `GameCoreSection`
  y transiciones/timers de `LifeGauge` sólo avanzan en Authoritative.
  Presentation conserva el último valor para dibujarlo; quedan otras rutas
  `refresh` por auditar antes de habilitar el árbol de presentación.
- Protegidos en Presentation los updates centrales de colisión y plataformas,
  flags de culling/opciones Teki y luces/sonidos de la nave. La cámara
  interpolada ya no puede contaminar estado físico o decisiones de IA.
- `BaseShape::updateAnim` deja intacto el buffer compartido durante Presentation
  y la pose procede del snapshot transaccional. También se bloquean weighted
  matrices, look-at manual de Navi/Pikmin y deformaciones de cuatro bosses.
  Build completa correcta; los runtimes y presents continúan desactivados.

## Plantilla para futuras entradas

```text
### AAAA-MM-DD — ID — Descripción breve

- Archivos modificados:
- Qué se corrigió:
- Prueba realizada:
- Resultado:
- Problemas pendientes relacionados:
```

### 2026-09-01 — PERF-NATIVE-003 — Especialización de pipelines TEV

- Archivos modificados: `pc_port/gl/pc_tev_shader.{h,cpp}` (nuevo),
  `pc_port/gl/pc_tev_shader_test.cpp` (nuevo), `pc_port/gl/pc_gfx.{h,cpp}`,
  `src/sysDolphin/system.cpp`, `pc_port/pc_window.cpp`, `CMakeLists.txt`.
- Qué se corrigió: el fragment shader interpretaba la configuración TEV por
  píxel (bucle de hasta 16 etapas, ramas de 8 vías por sampler, cadenas de
  selección por operando), lo que hundía la ocupancia. Ahora se genera un
  programa por configuración con todo resuelto en tiempo de generación, con
  caché por clave y el über-shader como fallback. Durante la integración se
  corrigieron además tres fallos propios: caché de uniforms indexada sólo por
  localización, ausencia de `glUseProgram` antes de asignar samplers, y un
  segundo bucle de samplers que escribía las localizaciones del programa nuevo
  con el anterior aún enlazado, corrompiendo sus uniforms enteros.
- Prueba realizada: build Release limpia y CTest 13/13; recorrido in-game del
  usuario por título, selección de datos, mapa y gameplay a 1920x1080 con
  `renderScale = 1`; medición GPU con `PIKMIN_PERF_STATS=1`.
- Resultado: 60 FPS en menús y 30 en gameplay sin fallos gráficos. Escena
  ~3,7-5,9 ms de GPU frente a ~55 ms previos a media resolución. 15
  configuraciones TEV distintas en todo el recorrido.
- Problemas pendientes relacionados: el lado CPU sigue sin tocar
  (`pc_gfx_end` sube ~100-200 uniforms y emite un `glDrawArrays` por primitiva
  GX); PERF-NATIVE-004 continúa abierto; 002 cerrado el 2026-09-01.

### 2026-09-04 — PERF-NATIVE-007 — Regresión de rendimiento: la mitad del juego se compilaba sin optimizar

- Síntoma: el juego había perdido rendimiento respecto a la build validada del
  2026-09-02, sin que ningún cambio del renderizador lo explicara.
- Causa raíz, en `CMakeLists.txt`. El 2026-09-03, al preparar el port a
  Windows, las fuentes decompiladas se separaron del ejecutable a una
  biblioteca propia (`add_library(pikmin_legacy ...)`). Las tres opciones de
  optimización —`-O3` explícito, `-march=native` e IPO/LTO— se quedaron
  atadas al objetivo `pikmin_pc`, que a partir de ese momento sólo contenía
  `pc_port/`. Es decir: **todo el juego** (`renderall()`, el recorrido de
  escena, matrices, colisiones, IA, `dgxGraphics`) perdió el ISA de la máquina
  y el LTO, y la frontera de biblioteca impidió además que el traductor GX
  siguiera alineándose dentro del código de juego. El reparto de fuentes entre
  los dos objetivos es tal que la parte que perdió las opciones es justamente
  donde vive el grueso del trabajo por frame.
- Segunda causa, acumulativa: `build/` estaba configurado como `Debug`
  (`CMAKE_CXX_FLAGS_DEBUG = -g`, sin ninguna `-O`), o sea `-O0` para todo,
  desde la sesión de depuración de audio del 2026-09-03. El binario de
  `build/bin/pikmin` pesaba 29 MB frente a los 6 MB de la build Release.
- Arreglo: las tres opciones se aplican ahora a `pikmin_pc` **y** a
  `pikmin_legacy` mediante la lista `PIKMIN_OPTIMIZED_TARGETS`. Verificado en
  `build/CMakeFiles/pikmin_legacy.dir/flags.make`: `-O3 -march=native
  -flto=auto` presentes.
- Prevención: un tipo de build vacío ya no significa `-O0`; se fija
  `RelWithDebInfo` por defecto y configurar `Debug` emite un aviso explícito de
  que el rendimiento no será representativo. Ésta es la clase de fallo que no
  se ve en pantalla: no hay defecto visual, sólo tiempo de frame.
- Sondas de depuración retiradas del camino caliente. Quedaban del trabajo de
  vértices salvajes del 2026-09-01, sin condicionar: tres escrituras por
  vértice para `sLastPos*` (el bucle más interno del renderizador) y el
  historial de descriptor de vértices, que desde el arreglo de `setupVtxDesc`
  se escribe unas quince veces por malla. Ambas comparten ahora el interruptor
  `PIKMIN_WILD_VERTS=1` con el informe que es lo único que las lee.
- Paquetes portables: `PIKMIN_ENABLE_IPO` pasa a `ON` en
  `packaging/linux/package-standalone.sh` y en
  `packaging/arch-linux/build-portable.sh`. El LTO no cambia el juego de
  instrucciones —eso lo controla `PIKMIN_NATIVE_OPTIMIZE`, que sigue en `OFF`
  y lo comprueba `verify-portable.sh`—, así que apagarlo sólo restaba
  rendimiento en los equipos modestos que son la razón de ser del paquete.
- Prueba realizada: build limpia RelWithDebInfo y CTest 15/15. Falta la
  medición in-game del usuario.
- Snapshot previo al cambio:
  `snapshots/20260904-001606-antes-rescate-rendimiento.tar.zst`.
