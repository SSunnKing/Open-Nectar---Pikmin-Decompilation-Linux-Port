# 60 FPS por INTERPOLACIÓN: intentos fallidos

Última actualización: 2026-09-07

> **Aviso (2026-09-07).** Este documento trata de una ruta concreta: fabricar
> frames intermedios interpolando entre ticks lógicos. Esa ruta falló tres
> veces y sigue desaconsejada.
>
> **No confundir con los FPS altos que sí funcionan.** Los 60 (y los 120) se
> consiguen subiendo la tasa de ticks lógicos vía `fpsMode` -> `setFrameClamp`,
> lo cual es correcto porque el juego integra por `getFrameTime()`. No hay
> interpolación activa en ninguna parte: `pc_render_begin_presentation()` no se
> llama desde el juego. Detalle en `AI_HANDOFF.md`, primera sección.

## Estado verificado

Los intentos realizados hasta ahora **no resolvieron los 60 FPS**. La última
activación produjo imagen en blanco y negro y otros fallos gráficos durante la
prueba in-game. El usuario hizo rollback y el juego volvió a funcionar a
30 FPS. Las pruebas offline que pasaban sólo verificaban piezas aisladas de
temporización y snapshots; no demostraban que un segundo render fuese válido.

Antes de continuar, inspeccionar el código actual. El rollback puede haber
restaurado partes del bucle anteriores al fixed-step, así que ninguna nota
histórica debe interpretarse como prueba de que esa integración siga activa.

## Qué se probó y por qué falló

### 1. Repetir el árbol de dibujo entre ticks lógicos

Se separaron conceptualmente actualización y render y se intentó llamar otra
vez a las rutas de dibujo para generar presents adicionales. La prueba produjo
corrupción grave de UI y estado GX. Fue retirado.

Conclusión: en este juego `draw`, `refresh`, `animate` y rutas relacionadas no
son funciones puras. También actualizan estado, cachés, animaciones, fades,
partículas y configuración GX heredada.

### 2. Interpolar matrices según el orden de comandos GX

Se intentó asociar matrices anteriores y actuales por slot/orden de subida GX.
La prueba alcanzó más FPS, pero produjo polígonos gigantes, dejó a Olimar
visualmente inmóvil y aceleró animaciones.

Conclusión: el orden no identifica un objeto. Los slots se reutilizan, el
culling cambia el orden y varios actores comparten modelos y buffers de pose.
No se puede usar índice de llamada, slot GX, dirección de `BaseShape` ni
puntero de un buffer compartido como identidad visual persistente.

### 3. Segundo render completo con snapshots y guardas de Presentation

Se construyeron y probaron offline estas bases:

- scheduler de ticks lógicos y decisión de presentación;
- fases `Authoritative` y `Presentation`;
- snapshots de cámara semántica;
- snapshots de matrices con owner persistente y override transaccional RAII;
- guardas para animación, partículas, fades, audio, life gauges, colisiones,
  plataformas, culling, luces y varias correcciones manuales de pose.

Después se activaron los runtimes visual/cámara y un present intermedio que
volvía a recorrer `renderall()` con estado interpolado. Aunque la suite offline
pasaba, la prueba real dejó el juego en blanco y negro y con bugs gráficos. El
usuario hizo rollback.

Conclusión: proteger mutaciones conocidas no convierte el árbol de render en
idempotente. Quedaron dependencias implícitas de estado GX/TEV, materiales,
texturas, display lists, cachés dinámicos, orden y escrituras de render no
inventariadas. Los tests de stores y política no validaban equivalencia visual
ni equivalencia del estado completo del renderer.

## Prohibiciones para el próximo intento

- No generar un frame adicional volviendo a llamar `renderall()`, `draw()`,
  `refresh()`, `animate()` o `updateAnim()` dentro del mismo tick lógico.
- No reaplicar el parche de activación anterior ni asumir que “faltaba una
  guarda” sin una prueba de equivalencia completa del renderer.
- No inferir identidad desde el orden de comandos, slots GX o punteros de
  modelos/buffers compartidos.
- No considerar un contador de 60 FPS ni una suite offline como validación
  in-game. Deben verificarse color, geometría, UI, animación y velocidad real.
- No mezclar la cámara interpolada con decisiones de gameplay o culling de IA.
- No avanzar audio, input, RNG, partículas, eventos ni animaciones para crear
  frames visuales.

## Ruta recomendada: capturar y reproducir paquetes de render inmutables

La siguiente implementación no debe reentrar al código de juego para producir
el segundo frame. El tick autoritativo ejecutará el render original una sola
vez. En la frontera GX -> backend OpenGL se capturará una lista ordenada e
inmutable de comandos/paquetes ya resueltos, incluyendo como mínimo:

- estado completo GX/TEV, depth, blend, alpha compare, cull y viewport;
- texturas, samplers, materiales y uniformes resueltos;
- vértices/índices o referencias cuya vida esté garantizada;
- matrices de cámara/modelo;
- clave estable de owner, submalla y material cuando sea interpolable.

El backend podrá reproducir ese paquete una o más veces sin llamar de nuevo al
árbol `renderall()`. Se conservarán los paquetes anterior y actual. Al inicio,
UI, partículas, transparencias problemáticas y elementos sin pareja usarán el
estado actual sin interpolar. Sólo cámara y transformaciones con clave estable
se interpolarán; altas, bajas, teleports y cortes harán snap al estado actual.

La propiedad que debe demostrarse antes de interpolar es: **reproducir dos
veces el mismo paquete produce el mismo color y el mismo estado final, sin
modificar ningún estado del juego**. Si esto falla, se corrige la captura del
backend; no se añaden más guardas dispersas al gameplay.

## Secuencia segura de implementación

1. Congelar y conservar la ruta estable de 30 FPS como fallback por defecto.
2. Añadir captura pasiva detrás de una opción experimental desactivada. Comparar
   hash y contenido del stream capturado sin cambiar la salida.
3. Reproducir una vez el paquete actual en lugar de su emisión inmediata y
   demostrar equivalencia visual/estado a 30 FPS.
4. Reproducir el mismo paquete dos veces, todavía sin interpolación. Verificar
   primero que no reaparecen blanco y negro, corrupción, audio doble ni cambios
   de velocidad.
5. Interpolar únicamente cámara. Después, transformaciones de owners estables.
   Mantener UI/partículas en el paquete actual hasta tratarlas explícitamente.
6. Validar in-game título, menús, gameplay, transparencias, día/noche,
   cinemáticas, cambios de área, pausas y soft reset. Cualquier fallo debe poder
   volver de inmediato a la ruta original mediante una sola opción.

## Alternativas, en orden de preferencia

1. **Captura/replay en el backend:** recomendada; evita reentrar en código de
   juego con efectos laterales.
2. **Refactor del renderer a función pura:** posible pero mucho más invasivo.
   Requiere sacar todas las mutaciones de `draw/refresh` y establecer un estado
   GX completo al comienzo de cada pasada antes de intentar un segundo render.
3. **Simulación completa a 60 Hz con subsistemas heredados a 30 Hz:** riesgo
   alto para físicas, IA, RNG, audio y scripts; no es la primera opción.
4. **Interpolación de frame final/motion vectors:** no reentra al renderer,
   pero puede introducir ghosting, latencia y tratamiento especial de UI.

