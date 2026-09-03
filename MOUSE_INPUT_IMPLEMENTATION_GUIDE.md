# Guía de implementación — Ratón directo para el cursor de gameplay

## Objetivo

Mejorar la respuesta del modo `PC_CONTROL_MOUSE_CURSOR` para que el cursor se
detenga al detener físicamente el ratón y su sensibilidad controle distancia,
no inercia. El control clásico de GameCube, teclado, mando, F1 y F2 deben
conservar su comportamiento.

No ejecutar el juego desde la IA. Se permite inspeccionar, editar, compilar y
ejecutar pruebas offline. El usuario hará la validación in-game.

## Causa confirmada

La ruta actual está en `pc_port/pc_window.cpp`, dentro de `pc_window_poll()`.
En modo relativo:

1. `SDL_GetRelativeMouseState()` obtiene el desplazamiento del frame.
2. Este se suma a los estáticos `accumX` y `accumY`.
3. Si no hay desplazamiento, ambos valores se multiplican por `0.9f`.
4. Se publican como `sVirtualCursorX/Y` en el rango `[-127, 127]`.

Después, `Navi::makeCStick()` en `src/plugPikiKando/navi.cpp` crea un vector a
partir de esos valores, lo normaliza y lo multiplica por
`mCursorMoveSpeed`. Un residuo pequeño de `accumX/Y` sigue generando velocidad
completa hasta que el cast a `s8` llega finalmente a cero. Ésta es la sensación
de que el puntero se desliza.

La solución no consiste en cambiar `0.9f` por otro número: hay que dejar de
modelar el ratón como un stick con velocidad persistente.

## Diseño recomendado

### 1. Publicar deltas, no un stick acumulado

En `pc_port/pc_window.cpp`, añadir estado de frame con precisión flotante:

```cpp
static float sMouseCursorDeltaX = 0.0f;
static float sMouseCursorDeltaY = 0.0f;
```

Al comienzo de cada `pc_window_poll()` deben ponerse a cero. En modo Mouse
Cursor relativo, asignar el movimiento recibido una sola vez:

```cpp
sMouseCursorDeltaX = static_cast<float>(mouseX) * sMouseSensitivity;
sMouseCursorDeltaY = static_cast<float>(mouseY) * sMouseSensitivity;
```

No usar acumuladores, amortiguación ni zona muerta. Un ratón ya entrega deltas
discretos; una zona muerta elimina movimientos de precisión.

Decidir y documentar una sola convención de signos. La recomendada es:

- X positiva: derecha en pantalla.
- Y positiva: abajo en pantalla, igual que SDL.

La conversión a las coordenadas de gameplay debe hacerse en un único sitio,
preferiblemente `Navi::makeCStick()`.

### 2. Añadir getters explícitos

Declarar en `pc_port/pc_window.h`:

```cpp
extern "C" float pc_window_get_mouse_cursor_delta_x(void);
extern "C" float pc_window_get_mouse_cursor_delta_y(void);
extern "C" void pc_window_clear_mouse_cursor_delta(void);
```

Implementarlos en `pc_window.cpp`. No reutilizar los getters `s8` de
`sVirtualCursorX/Y`: pierden precisión y su nombre expresa semántica de stick,
no desplazamiento.

Los deltas deben ser consumidos una sola vez por actualización lógica. Si
`pc_window_poll()` y `Navi::makeCStick()` tienen una relación estricta 1:1,
basta con reiniciarlos al siguiente poll. Si no se puede demostrar esa relación,
usar `pc_window_clear_mouse_cursor_delta()` inmediatamente después de leerlos.

### 3. Aplicar desplazamiento directo en `Navi::makeCStick()`

Separar explícitamente las dos rutas:

- Classic: conservar literalmente la lógica actual de `stickVec`.
- Mouse Cursor: no normalizar el delta y no multiplicarlo por `getFrameTime()`.

Esquema:

```cpp
if (pc_window_get_control_mode() == PC_CONTROL_MOUSE_CURSOR) {
    Vector3f mouseDelta(
        pc_window_get_mouse_cursor_delta_x(),
        0.0f,
        pc_window_get_mouse_cursor_delta_y()
    );

    // Aplicar aquí la misma orientación cámara/mundo que corresponda a los
    // ejes del cursor original. Verificar signos con una prueba manual.
    mouseDelta = mouseDelta * kMouseCursorWorldScale;
    targetPos = mCursorPosition + mouseDelta;
} else {
    // Ruta original sin cambios.
}
```

`kMouseCursorWorldScale` debe ser una constante central y documentada, no un
remapeo por nivel. Empezar con un valor conservador y ajustar únicamente con
la prueba del usuario. La sensibilidad configurada ya se aplica en la entrada,
por lo que esta constante sólo convierte counts SDL a unidades de mundo.

No multiplicar el delta por `DeltaTime`: SDL ya acumula la distancia física
desde el sondeo anterior. Multiplicarlo de nuevo haría que la distancia variase
con el framerate.

### 4. Conservar el límite radial

Después de sumar el delta, conservar la restricción original de
`mCursorMaxRadius`:

```cpp
if (targetPos.length() > NAVI_PARM(mCursorMaxRadius)) {
    targetPos.normalise();
    targetPos = targetPos * NAVI_PARM(mCursorMaxRadius);
}
```

Para ratón es preferible proyectar la posición final al círculo, en vez de
eliminar la componente radial de una velocidad normalizada. Así el cursor queda
pegado al borde pero puede recorrerlo tangencialmente de forma inmediata.

Mantener actualizados:

- `mCursorPosition`
- `mCursorTargetPosition`
- `mCursorNaviDist`

Revisar el bloque anterior de `Navi::update()` que aproxima
`mCursorPosition` a `mCursorTargetPosition`. La ruta de ratón no debe crear dos
posiciones divergentes que reintroduzcan interpolación tras soltar el ratón.
Para esta ruta, ambas posiciones deben terminar iguales tras aplicar el delta.

### 5. Magnitud para la orientación de Olimar

La lógica posterior usa `cursorStickMag` para decidir la orientación. No debe
seguir leyendo `sVirtualCursorX/Y` si éstos dejan de representar el ratón.

Calcular una magnitud de actividad a partir del delta del frame, normalizada a
un rango razonable `[0, 1]`, o conservar un booleano `mouseMovedThisFrame`.
Esta magnitud sólo debe afectar decisiones de orientación; no debe volver a
integrarse como movimiento.

Al no haber movimiento, el valor debe ser exactamente cero. No añadir una cola
de decaimiento.

### 6. Limpiar deltas en transiciones

Vaciar los deltas pendientes cuando:

- F2 cambia a Classic o Mouse Cursor.
- Se abre o cierra F1.
- Se activa o desactiva el modo relativo con Tab/Escape.
- La ventana pierde o recupera foco.
- SDL activa el relative mode.

Al entrar en Mouse Cursor, llamar una vez a `SDL_GetRelativeMouseState()` y
descartar el resultado puede evitar consumir movimiento acumulado durante la
transición.

No recentrar `mCursorPosition`: el cursor debe conservar su ubicación actual.

### 7. Ruta absoluta

La ruta no relativa actual convierte la posición de ventana en un stick radial.
No mezclarla silenciosamente con el nuevo diseño.

Opciones válidas:

1. Mantenerla temporalmente como está y aplicar el cambio sólo al relative
   mode, que es el usado automáticamente por Mouse Cursor.
2. Posteriormente convertir posición de ventana a posición objetivo absoluta.

La primera opción reduce el riesgo de esta unidad. Documentar que Tab conserva
por ahora una semántica distinta.

## Sensibilidad

`mouseSensitivity` debe ser ganancia lineal:

- 0.5 recorre aproximadamente la mitad.
- 1.0 usa la distancia base.
- 2.0 recorre aproximadamente el doble.

No debe modificar amortiguación, aceleración ni tiempo de retorno. Mantener el
rango del menú existente salvo que una prueba demuestre que no permite ajuste
fino suficiente.

No usar curvas de aceleración inicialmente. Si el usuario pide precisión y
giros rápidos simultáneos, añadir después una opción separada y desactivada por
defecto, nunca incorporarla de forma oculta a sensibilidad.

## Pruebas offline recomendadas

Extraer la conversión de delta a una función pura si resulta práctico. Probar:

1. Delta cero produce desplazamiento cero.
2. `(1, 0)` y `(-1, 0)` son simétricos.
3. La sensibilidad escala linealmente.
4. Dos deltas consecutivos suman la misma distancia que uno equivalente.
5. El resultado se limita a `mCursorMaxRadius`.
6. En el borde, un delta tangencial sigue moviendo el cursor.
7. Cambiar de modo limpia el delta sin cambiar la posición.
8. Classic no entra en ninguna función nueva de ratón.

Compilar Release y los tests offline existentes. No afirmar que la sensación
está validada hasta recibir prueba in-game.

## Prueba manual que debe hacer el usuario

1. Mover lentamente un píxel/count y comprobar precisión.
2. Hacer un movimiento rápido y detenerse: el cursor debe parar en el mismo
   frame lógico, sin cola visible.
3. Trazar círculos y diagonales.
4. Recorrer el límite radial alrededor de Olimar.
5. Comparar sensibilidad 0.5, 1.0 y 2.0.
6. Comparar gameplay a 30 y 60 Hz si ambos escenarios son alcanzables.
7. Abrir/cerrar F1, usar F2 y recuperar foco sin saltos.
8. Confirmar que teclado y mando clásico no cambiaron.

## Criterios de aceptación

- Cero desplazamiento físico implica cero movimiento nuevo del cursor.
- No existe acumulación ni decaimiento del input de ratón.
- La distancia no depende perceptiblemente del framerate.
- Sensibilidad es lineal y predecible.
- El límite radial, terreno y orientación siguen funcionando.
- Classic, mando, teclado, F1 y F2 no sufren regresiones.
- Build Release y pruebas offline verdes.
- Validación final in-game realizada por el usuario.

## Cambios que deben evitarse

- No limitarse a cambiar `accum *= 0.9f`.
- No añadir otro filtro exponencial sin una opción explícita.
- No normalizar el delta del ratón: destruye su magnitud.
- No multiplicar desplazamiento SDL por `DeltaTime`.
- No tocar `mCursorMoveSpeed` global para compensar el ratón; afectaría Classic.
- No aplicar constantes distintas por nivel, cámara u objeto.
- No ejecutar el juego desde la IA.

## Documentación al terminar

Actualizar `ROADMAP.md` y `AI_HANDOFF.md` indicando:

- Archivos y API añadida.
- Convención de ejes.
- Escala base elegida y su justificación.
- Pruebas offline ejecutadas.
- Estado de la validación manual.

