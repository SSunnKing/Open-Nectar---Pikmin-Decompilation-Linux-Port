# Referencias externas evaluadas

Proyectos y recursos de fuera que se han valorado para este port, con la
conclusión y **la fecha y el motivo**, para no tener que reevaluarlos desde cero
cada vez que alguien los encuentre.

---

## Aurora — capa de compatibilidad GameCube/Wii

<https://github.com/encounter/aurora> · MIT · C++

**Qué es.** Una capa de compatibilidad GameCube/Wii **a nivel de código fuente,
pensada explícitamente para proyectos de decompilación** como éste. Nació para
Metaforce (Metroid Prime) y ya sostiene ports terminados (Dusklight, entre
otros).

**Qué ofrece:**

- Capa de aplicación sobre **SDL3** (Windows, Linux, macOS, iOS, tvOS, Android)
- **GX sobre WebGPU** (Dawn) → D3D12, Vulkan, Metal
- Capas **PAD** (SDL_Gamepad), **DVD** (imágenes de disco) y **CARD** (guardado)
- Dear ImGui y RmlUi integrados
- Packs de texturas compatibles con Dolphin, escalado de resolución, giroscopio
  y ratón, caché de pipelines transferible

**Qué NO ofrece: audio.** Comprobado en el árbol del repositorio
(`lib/`: `card`, `dawn`, `dolphin`, `gfx`, `gx`, `rmlui`, `webgpu`). No hay
módulo de DSP, AX, MusyX ni JAudio. Toda la reimplementación de audio de este
port se quedaría exactamente como está.

### Evaluación (2026-09-03): interesante, pero NO adoptar todavía

Cobertura frente a lo que este port tiene escrito a mano:

| área del port | líneas | ¿la cubre Aurora? |
|---|---|---|
| `pc_port/gl/` (GX → OpenGL) | 5.561 | **Sí** |
| `pc_port/dolphin_stubs/` (PAD, DVD, CARD…) | 7.489 | Parcialmente |
| `pc_port/audio/` (JAudio completo) | 6.139 | **No. Nada.** |
| `pc_port/timing/`, `pc_port/settings/` | 3.059 | No |

**Motivos para no adoptarla ahora:**

1. **El apartado gráfico está en su mejor momento**: 60 FPS reales a 1080p sin
   fallos visuales. Cambiar el backend significa tirar PERF-NATIVE-002 (el
   batching de primitivas, que fue lo que dio los 60 FPS) y la especialización
   TEV, y volver a empezar. Es arreglar algo que no está roto.
2. **Todo lo que está abierto es de audio**, y Aurora no lo toca.
3. **Salto de SDL2 a SDL3.** La capa de audio de este port está construida sobre
   SDL2; habría que migrarla entera, y es justo la parte que Aurora no cubre.
4. **Dawn es una dependencia de compilación pesada.** En ejecución, WebGPU sobre
   Vulkan en una GTX 1050 iría bien; el problema es construirlo.
5. **Choca con la forma de trabajar del proyecto**: avanzar paso a paso con
   vuelta atrás fácil por snapshots. Sustituir el backend de render es un cambio
   grande y de golpe, con todo en el aire hasta el final.

**Cuándo sí valdría la pena reconsiderarla:**

- Si aparece un **muro gráfico** que el backend a mano no pueda expresar, o un
  techo de rendimiento que no se pueda romper.
- Si se quieren **builds de Windows o macOS**: ahí se paga sola.
- Si mantener el backend GL propio se vuelve una carga (la corrupción del replay
  de display lists ya costó cinco hipótesis).

**Qué hacer mientras tanto, que es gratis:** usarla como **referencia de
lectura**, no como dependencia. Cuando aparezca un fallo concreto de traducción
GX, mirar cómo lo resuelve Aurora sale mucho más barato que adoptarla — ellos ya
han pasado por esos casos. Es la misma jugada que funcionó con `src/jaudio/`:
la respuesta suele estar escrita en algún sitio, y leerla gana a deducirla.

### Aurora frente al plan de Windows (2026-09-03)

Aurora **sí** ofrece Windows (SDL3 + D3D12/Vulkan), así que parece la opción
obvia para `PLAN_WINDOWS.md`. Contrastado con la auditoría de ese plan, **no lo
es**, y por un motivo que conviene tener escrito:

**La parte que Aurora cubre es justo la que ya funciona en Windows.** El
renderizador del port carga GL con `SDL_GL_GetProcAddress`, no usa GLU en la
configuración de build actual y no tiene nada de Win32: según la auditoría
necesita **cero trabajo** para Windows. Lo que cuesta días es otra cosa:

| trabajo del plan de Windows | esfuerzo | ¿lo cubre Aurora? |
|---|---|---|
| Launcher Win32 (fase 3) | 3-5 días | **No** |
| CMake multiplataforma, MinGW/vcpkg | 1 día | No |
| Shims de compilador (MSVC) | dentro de 2-3 días | No |
| Empaquetado, CI, validación | 2-4 días | No |
| Renderizador | **0 días** | Sí |

Es decir: de los 8-13 días estimados, Aurora se lleva ~0 — y **añade** trabajo
(migrar de SDL2 a SDL3, del que depende toda la capa de audio, más construir
Dawn en las dos plataformas).

**El único argumento real a su favor**, y merece quedar anotado: el riesgo nº 1
del plan de Windows —GPUs con drivers genéricos, donde el OpenGL de serie es
1.1 y el juego no arranca—. El backend D3D12 de Aurora esquivaría esa clase de
problema por completo. Si ese riesgo se materializa en pruebas reales y molesta
a usuarios, es el momento de reabrir la evaluación.
