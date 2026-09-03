# Plan de port nativo a Windows

Documento de planificación detallado para llevar el port a Windows de forma
nativa. Estado del análisis: 2026-09-03, sobre el código actual del repo.

> **Solo planificación**: este documento no implementa nada; define el trabajo,
# el orden y los riesgos.

---

## 1. Auditoría de portabilidad: qué tenemos hoy

Antes de planificar, se ha auditado el código para saber exactamente qué
separa el proyecto de compilar en Windows. El resultado es mejor de lo
esperado.

### 1.1 Lo que YA es portable (no requiere trabajo)

| Componente | Verificación | Estado |
|---|---|---|
| Núcleo del juego (`pc_port/`, excepto launcher) | Cero includes POSIX (`unistd.h`, `sys/`, `dlfcn.h`, `pthread.h`) | Portable |
| Extractor de ISO (`launcher/gamecube_image.cpp`) | Solo C++ estándar (`<fstream>`, `<vector>`...) | Portable tal cual |
| Temporización | Todo vía `SDL_GetPerformanceCounter()` (p. ej. `pc_port/audio/pc_audio.cpp:665`) | Portable |
| Hilos | `std::thread` en `pc_port/dolphin_stubs/os_stubs.cpp:163` | Portable |
| Carga de funciones OpenGL | **`SDL_GL_GetProcAddress`** en `pc_port/gl/pc_gfx.cpp:99+` | Portable — esta era la mayor amenaza potencial y ya está resuelta |
| Endianness (GC es big-endian, x86 little) | Abstraída con macros en `pc_port/pc_types_c.h` | Lógica OK, falta shim de compilador (3.1) |
| Ventana, entrada, mando, audio | SDL2 lo abstrae todo (XInput y WASAPI funcionan solos) | Portable |
| Fuentes excluidas | CMake ya excluye `src/MSL_C/` y `src/Runtime/` (CodeWarrior) | Hecho |
| Empaquetado portable de ficheros | El modelo "carpeta con todo junto" es idiomático en Windows | Concepto reutilizable |

### 1.2 Lo que NO es portable (lista cerrada de trabajo real)

| # | Problema | Ficheros afectados | Gravedad |
|---|---|---|---|
| 1 | Builtins GNU: `__builtin_clz`, `__builtin_bswap16/32` | `pc_port/pc_types_c.h:105-117` | Solo MSVC (MinGW los acepta) |
| 2 | Atributos GNU: `__attribute__((aligned/unused/section))` | `pc_port/pc_types.h:133-135` | Solo MSVC |
| 3 | Colisión de `wingdi.h`: nuestro stub tapa el header real del SDK | `pc_port/wingdi.h`, incluido desde `src/sysCore/oglGraphics.cpp:11` | Alto en Windows |
| 4 | GLU legacy: `gluPerspective`, `gluBuild2DMipmaps` | `src/sysCore/oglGraphics.cpp:421`, `src/sysDolphin/texture.cpp:276` | Bajo (existe glu32, pero conviene quitarlo) |
| 5 | Launcher 100 % POSIX: `fork/exec/readlink/waitpid/unistd.h`, zenity/kdialog, rutas XDG, wrappers `sh`, bits de permiso | `pc_port/launcher/launcher_main.cpp` entero (548 líneas) | Reescritura del 60 % del fichero |
| 6 | CMake asume Unix: `target_link_libraries(... m ...)`, `/bin/chmod` post-build, pkg-config para SDL2 | `CMakeLists.txt:218,229,77` | Medio |
| 7 | Punto de entrada: en Windows, SDL2 exige `SDL_main`/WinMain o `SDL_MAIN_HANDLED` | `pc_port/pc_main.cpp:29` | Bajo pero obligatorio |
| 8 | Rutas con caracteres no-ASCII (usuarios con acentos/CJK): `std::filesystem::path` con strings estrechos rompe en Windows | launcher + cualquier ruta de usuario | Medio (bug de usuario real frecuente) |
| 9 | SDL2 y cabeceras en el sistema de build: en Windows no hay `pkg-config` ni `/usr/include` | build system | Decisión de tooling (3.3) |

**Conclusión de la auditoría**: el trabajo NO está en el motor del juego
(que ya es portable), sino en tres frentes: **launcher** (reescritura
parcial), **build system** (CMake + obtención de SDL2) y **shims de
compilador** (solo si elegimos MSVC).

---

## 2. Estrategia recomendada: dos etapas

### Etapa A — MinGW-w64 (cruzado desde Linux): "Windows funciona"

El objetivo es tener un `.exe` jugable lo antes posible con el mínimo
cambio de código, validando el juego en Windows real.

**Por qué primero MinGW**:
- Acepta todas las extensiones GNU existentes (`__builtin_*`,
  `__attribute__`): los puntos 1 y 2 de la tabla **desaparecen**.
- Se compila cruzado **desde el mismo Linux de siempre**, sin mantener
  todavía una máquina Windows ni aprender vcpkg.
- El CI actual es Linux: añadir un job de cross-compile es barato.
- Produce binarios Windows sin dependencias de runtime si se enlaza
  estáticamente libstdc++/libgcc (`-static-libgcc -static-libstdc++`) y se
  distribuye `SDL2.dll`.

**Limitaciones aceptadas**: depuración peor que MSVC, sin análisis estático
de Microsoft, y algunos colaboradores Windows preferirán MSVC (eso lo
resuelve la Etapa B).

### Etapa B — MSVC (nativo): "Windows bien mantenido"

Una vez el juego funciona en Windows, se añade MSVC como toolchain de
referencia para desarrolladores Windows: mejor depurador, tooling de
rendimiento (PIX, Visual Studio Profiler) y es lo que espera la mayoría de
la comunidad. Aquí sí hay que resolver los shims de compilador (puntos 1-2)
y limpiar las advertencias específicas de MSVC.

> Orden deliberado: MinGW primero porque desbloquea "se puede jugar en
> Windows" en días; MSVC es inversión de mantenimiento a medio plazo.

---

## 3. Plan de trabajo detallado

### Fase 0 — Preparativos (0.5 día, sin código de juego)

1. **Renombrar el stub `pc_port/wingdi.h` → `pc_port/pc_wingdi_stub.h`**.
   En Windows, `<wingdi.h>` es un header real del SDK (declara `wglCreateContext`,
   tipos GDI...). Nuestro stub (solo `BOOL`/`TRUE`/`FALSE`,
   `pc_port/wingdi.h`) lo ocultaría si `pc_port/` está en el include path.
   - En `src/sysCore/oglGraphics.cpp:11`, cambiar a:
     ```cpp
     #ifdef _WIN32
     #  include <wingdi.h>   // el real del SDK
     #else
     #  include "pc_wingdi_stub.h"
     #endif
     ```
   - Verificar que en la build Linux actual nada más usa ese header.
2. **Decidir el nombre de producto para Windows** (`pikmin.exe` choca con
   búsquedas y con el ejecutable de la GameCube en documentación; valorar
   `pikmin-native.exe` en los paquetes, manteniendo `pikmin` internamente).
3. Crear rama `windows-port` y una etiqueta de referencia del estado Linux
   estable actual, para poder comparar comportamiento.

### Fase 1 — CMake multiplataforma (1 día)

Archivo: `CMakeLists.txt`. Cambios concretos:

1. **Librería matemática**: `m` no existe en MSVC.
   ```cmake
   if(NOT WIN32)
       target_link_libraries(pikmin_pc PRIVATE m)
   endif()
   ```
2. **`/bin/chmod` post-build** (`CMakeLists.txt:228-230`): envolver en
   `if(UNIX)`.
3. **SDL2** (`CMakeLists.txt:75-81`): hoy usa `find_package(SDL2)` +
   fallback a pkg-config. En Windows:
   - Etapa A (MinGW): `find_package(SDL2)` encuentra `sdl2-config` de MinGW
     o el paquete `mingw-w64-SDL2`; mantener el fallback.
   - Etapa B (MSVC): vcpkg con toolchain file
     (`-DCMAKE_TOOLCHAIN_FILE=.../vcpkg.cmake`) y `find_package(SDL2 CONFIG REQUIRED)`.
   - Unificar en un `cmake/FindOrBundleSDL2.cmake` si crece.
4. **OpenGL**: `find_package(OpenGL REQUIRED)` funciona en Windows
   (resuelve `opengl32.lib`), sin cambios de enlace. Las funciones > 1.1 ya
   se cargan con `SDL_GL_GetProcAddress` (auditado), así que no hace falta
   GLEW/GLAD.
5. **GLU** (punto 4 de la tabla): dos opciones:
   - (a) Enlazar `glu32` en Windows (`if(WIN32) target_link_libraries(pikmin_pc PRIVATE glu32)`): cero cambios de código, funciona en MSVC y MinGW.
   - (b) Eliminar GLU: `gluPerspective` son 6 líneas de matriz propia y
     `gluBuild2DMipmaps` → `glGenerateMipmap` (ya disponible vía loader).
     Recomendado a medio plazo; para la Etapa A basta (a).
6. **Icono y metadatos del .exe** (recurso `.rc`): título, versión e icono.
   Opcional en Etapa A, recomendado en la B. CMake compila `.rc` solo con
   `enable_language(RC)` implícito en MSVC/MinGW.
7. **Instalación**: `install(TARGETS ...)` ya es portable; añadir
   `if(WIN32)` para instalar `SDL2.dll` junto al ejecutable.
8. **Opción de toolchain**: crear `cmake/mingw-w64-x86_64.cmake`:
   ```cmake
   set(CMAKE_SYSTEM_NAME Windows)
   set(CMAKE_SYSTEM_PROCESSOR x86_64)
   set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
   set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
   set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
   set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
   set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
   set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
   set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
   ```

### Fase 2 — Núcleo del juego en Windows (2-4 días, mayoría pruebas)

1. **Punto de entrada** (`pc_port/pc_main.cpp:29`): SDL2 en Windows redefine
   `main` como `SDL_main` para gestionar `WinMain`. Debe funcionar tal cual
   enlazando `SDL2main` (MinGW) o `SDL2::SDL2main` (MSVC/vcpkg). Si aparece
   "undefined reference to WinMain", añadir al inicio de `pc_main.cpp`:
   ```cpp
   #ifdef _WIN32
   #  define SDL_MAIN_HANDLED   // ya gestionamos la consola nosotros
   #endif
   #include <SDL.h>
   ```
   y llamar a `SDL_SetMainReady()` antes de `SDL_Init`. Decisión: empezar
   enlazando SDL2main (más estándar) y solo usar `SDL_MAIN_HANDLED` si
   molesta la consola auxiliar.
2. **Consola**: en MSVC, el subsistema por defecto para `main` es consola;
   para ocultarla en releases usar `WIN32` en `add_executable` + SDL2main.
   Recomendación: mantener consola en builds de desarrollo (los logs del
   juego van a stdout) y ocultarla en release con un `.log` a fichero.
3. **Rutas de datos**: el juego usa rutas relativas al cwd (`dataDir/...`),
   y el launcher hace `chdir` antes de `exec`. En Windows mantener el mismo
   modelo (`SetCurrentDirectoryW` antes de `CreateProcessW`), así el núcleo
   no necesita tocar rutas. Verificar que ningún sitio construye rutas con
   separador `/` a mano para escritura (Windows acepta `/` en la mayoría de
   APIs, pero no en todas: `CreateDirectory` lo tolera; el diálogo de
   archivos devuelve `\`).
4. **Unicode**: envolver la entrada de rutas del usuario:
   `std::filesystem::path` construido desde `std::string` estrecho en
   Windows interpreta ANSI, no UTF-8. Regla para todo el launcher Windows:
   convertir argumentos `wchar_t` → UTF-8 una vez (`WideCharToMultiByte`)
   y construir los `fs::path` con `fs::u8path()` (C++17) / `std::u8string`
   (C++20). Sin esto, usuarios tipo `C:\Users\José\...` fallan.
5. **Endianness/tipos** (Etapa A, MinGW): sin cambios. (Etapa B: ver 3.4.)
6. **`register` keyword**: MSVC en C++17 lo acepta con aviso C5033; si se
   sube el estándar a C++20 en el futuro hay que quitarlo de
   `include/Dolphin/OS/OSFastCast.h` (son macros inline triviales).
7. **Probar el renderer en Windows**: SDL_GL_GetProcAddress funciona igual,
   pero validar:
   - Contexto 3.3 compatibility con drivers NVIDIA/AMD/Intel (OK esperado).
   - Fallback 2.1 (`pc_window.cpp:276`) en GPUs antiguos.
   - Ojo: el "OpenGL" de serie de Windows sin drivers es 1.1 → el juego
     debe detectar el fallo de contexto y mostrar error comprensible
     ("instala los drivers de tu GPU"), no un crash. Añadir comprobación
     explícita tras `SDL_GL_CreateContext`.

### Fase 3 — Launcher para Windows (3-5 días, el grueso del trabajo)

El launcher actual (`pc_port/launcher/launcher_main.cpp`, 548 líneas) es la
pieza más POSIX del proyecto: `fork/execvp/waitpid/readlink/unistd.h`,
zenity/kdialog para diálogos, rutas XDG y permisos de fichero Unix.

**Diseño propuesto**: dividir el launcher en dos capas.

```
launcher_main.cpp        <- flujo común (parseo de args, orquestación)
launcher_platform.h      <- interfaz de plataforma (nueva)
launcher_posix.cpp       <- implementación actual (extraer lo POSIX)
launcher_win32.cpp       <- implementación Windows (nueva)
gamecube_image.cpp       <- sin cambios (ya es portable)
installer_ui.cpp         <- revisar: usa SDL; portable salvo diálogos
```

Interfaz de plataforma (funciones que hoy son POSIX inline):

| Función actual | POSIX | Windows |
|---|---|---|
| `executablePath()` | `readlink("/proc/self/exe")` | `GetModuleFileNameW(nullptr, ...)` |
| `defaultDataRoot()` | `$XDG_DATA_HOME` / `~/.local/share` | `SDL_GetPrefPath("PikminNative","Pikmin")` (→ `%APPDATA%`) o `%LOCALAPPDATA%` |
| `runDialog(zenity/kdialog)` | `fork` + `execvp` + pipe | No aplica: diálogos nativos (abajo) |
| `respawnInTerminal()` | `fork` + terminales X11 | No aplica (doble clic ya muestra consola opcional; errores vía `MessageBoxW`) |
| `launchGame()` | `chdir` + `execl` | `SetCurrentDirectoryW` + `CreateProcessW` con `CREATE_NO_WINDOW` opcional |
| Permisos +x | `fs::permissions` | No-op (`#ifndef _WIN32`) |
| Selectores de fichero | zenity/kdialog | `IFileOpenDialog` (COM) con filtro `*.iso;*.gcm` |
| `installExecutables` (copia) | `fs::copy_file` (portable) | Igual, pero copiar también `SDL2.dll` |

**Diálogos nativos**: usar `IFileOpenDialog`/`IFileSaveDialog` (COM,
Vista+) y `MessageBoxW` para errores. Es ~100 líneas con plantillas COM
verbosas pero estándar; alternativa: `tinyfiledialogs` (un `.c` único,
licencia zlib) si se prefiere velocidad sobre pureza Win32. Recomendación:
`IFileOpenDialog` directo (sin nueva dependencia).

**Extracción de ISO**: `gamecube_image.cpp` ya es C++ estándar puro; solo
hay que asegurar que abre en binario (`std::ios::binary` — ya lo hace) y
que las rutas llegan en UTF-8/UTF-16 correctamente (punto 3.2.4).

**Instalación**: mismo flujo que Linux (extraer a carpeta elegida, copiar
ejecutables). En Windows el "paquete standalone" es simplemente una carpeta
con `pikmin.exe`, `pikmin-launcher.exe`, `SDL2.dll`: no hace falta el truco
de `lib/` + `ld-linux` (Windows carga las DLLs del directorio del .exe).
El launcher Windows copiará esos 3 ficheros.

**Modo texto**: `attachConsole`/`AllocConsole` para mostrar consola cuando
se lanza con doble clic y hay error; o registrar salida a `pikmin.log` y
abrir `MessageBoxW` con el error + botón "abrir log".

### Fase 4 — MSVC (Etapa B, 2-3 días)

1. **Shims de compilador** — crear `pc_port/pc_compiler.h`:
   ```cpp
   #ifdef _MSC_VER
   #  include <intrin.h>
   #  include <stdlib.h>
   #  define PC_ALIGN(n)        __declspec(align(n))
   #  define PC_UNUSED
   #  define PC_BSWAP16(v)      _byteswap_ushort(v)
   #  define PC_BSWAP32(v)      _byteswap_ulong(v)
   static inline u32 pc_clz32(u32 v){ unsigned long i; _BitScanReverse(&i, v); return 31 - i; }
   #else
   #  define PC_ALIGN(n)        __attribute__((aligned(n)))
   #  define PC_UNUSED          __attribute__((unused))
   #  define PC_BSWAP16(v)      __builtin_bswap16(v)
   #  define PC_BSWAP32(v)      __builtin_bswap32(v)
   #  define pc_clz32(v)        __builtin_clz(v)
   #endif
   ```
   y actualizar `pc_types.h:133-135` y `pc_types_c.h:105-117` para usarlas.
   Ojo con `ATTRIBUTE_SECTION` (solo se usa en código ya excluido o se puede
   eliminar; verificar).
2. **Advertencias**: MSVC avisará de conversiones de narrowing y de los
   `register`; compilar con `/W3` (no `/WX` al principio) e ir limpiando.
3. **vcpkg manifest**: `vcpkg.json` con `sdl2`, para que
   `cmake --preset windows-msvc` lo resuelva todo.
4. **CRT estático**: compilar release con `/MT` (o vcpkg triplet
   `x64-windows-static`) para no obligar al usuario a instalar el
   Visual C++ Redistributable. Alternativa: documentar vcredist en el LEEME.
5. **Obtener GLU**: el SDK de Windows incluye `glu32.lib` — sin trabajo.

### Fase 5 — Empaquetado y distribución Windows (1-2 días)

1. **Script `packaging/windows/package-windows.sh`** (cross) o `.ps1`
   (MSVC): toma los binarios y genera:
   ```
   pikmin-native-windows/
     pikmin-launcher.exe
     pikmin.exe
     SDL2.dll
     LEEME.txt
     licenses/            <- avisos (SDL2 zlib, etc.)
   ```
   - Cross MinGW: copiar `SDL2.dll` de `/usr/x86_64-w64-mingw32/...`.
   - Comprimir en `.zip` (no `.tar.gz`: los usuarios Windows esperan zip).
2. **Firmado/SmartScreen**: sin firma, Windows Defender SmartScreen
   mostrará "editor desconocido". A corto plazo: documentarlo en LEEME con
   captura ("Más información → Ejecutar de todas formas"). A largo plazo:
   certificado OV (~100 €/año) o distribución vía GitHub Releases con
   reputación acumulada. **No** es bloqueo.
3. **Instalador opcional**: Inno Setup (script de 30 líneas) o NSIS para
   quien prefiera "Siguiente → Siguiente". No prioritario: la carpeta
   portable zip es suficiente para una alpha.
4. **Persistencia de partidas**: confirmar que el sistema de guardado
   escribe bajo `%APPDATA%` (vía `SDL_GetPrefPath`) y no junto al .exe
   (que puede estar en `C:\Program Files`, sin permisos de escritura).

### Fase 6 — CI/CD (1 día)

Extender `.github/workflows/` con un job Windows. Dos opciones:

**Opción A (Etapa A, recomendada al principio)** — cross desde el runner
Linux actual:
```yaml
build-windows-mingw:
  runs-on: ubuntu-22.04
  steps:
    - uses: actions/checkout@v4
    - run: sudo apt-get install -y mingw-w64 cmake pkg-config
    # SDL2 para MinGW: descargar el tarball oficial de desarrollo
    # (libsdl.org release *-mingw.tar.gz) y apuntar CMAKE_PREFIX_PATH.
    - run: cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake ...
    - run: cmake --build build-win -j2
    - run: packaging/windows/package-windows.sh build-win
    - uses: actions/upload-artifact@v4
      with: { name: pikmin-native-windows, path: packaging/windows/out/pikmin-native-windows }
```

**Opción B (Etapa B)** — runner Windows con MSVC:
```yaml
build-windows-msvc:
  runs-on: windows-latest
  steps:
    - uses: actions/checkout@v4
    - run: vcpkg install sdl2 --triplet x64-windows-static
    - run: cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    - run: cmake --build build --config Release
    # Los tests corren nativos en el runner: ctest --test-dir build -C Release
```

Ventaja clave de B: los 14 tests se ejecutan en Windows real. En la Opción
A los tests no corren (binarios Windows en runner Linux) — se puede
compensar parcialmente con Wine en CI, pero es frágil; mejor aceptar que la
Etapa A valida con pruebas manuales hasta que llegue la B.

**Test de humo Windows en CI**: `pikmin-launcher.exe --help` +
verificación de que el zip no contiene datos del juego (mismo check que
Linux).

### Fase 7 — Validación manual (continua)

| Prueba | Dónde |
|---|---|
| Instalación completa desde ISO real, modo gráfico | Windows 11, Windows 10 |
| Usuario con ruta no-ASCII (`C:\Users\José María`) | VM Windows |
| GPU NVIDIA (driver propietario) | hardware/VM con passthrough |
| GPU AMD | hardware |
| GPU Intel integrada | hardware |
| Mando XInput (Xbox) y teclado | hardware |
| Audio (WASAPI) y silencio correcto | hardware |
| Guardar/cargar partida entre sesiones | hardware |
| SmartScreen: documentar el aviso | Windows 11 limpio |
| Sin drivers OpenGL (GPU genérica): error claro, no crash | VM sin guest additions |

---

## 3.5. Verificación de la auditoría (2026-09-03)

La auditoría del apartado 1 se ha contrastado contra el código. **Es correcta**:
los cinco puntos comprobables de la tabla 1.1 coinciden, con los números de
línea exactos (ausencia de POSIX fuera del launcher, `SDL_GL_GetProcAddress` en
`pc_gfx.cpp:99`, `std::thread` en `os_stubs.cpp:163`, builtins en
`pc_types_c.h:105-117`, atributos en `pc_types.h:133-135`). Las líneas citadas
de `CMakeLists.txt` (218, 228-230, 75-81) y `pc_main.cpp:29` también.

**Dos correcciones**, ambas de la tabla 1.2:

- **Punto 3 (colisión de `wingdi.h`): no es un problema real hoy.** El único
  fichero que hace `#include <wingdi.h>` es `src/sysCore/oglGraphics.cpp`, y
  `src/sysCore/` está **excluido de la build** (`CMakeLists.txt:174`) — cosa que
  el propio apartado 6 de este plan confirma que seguirá así. La Fase 0 dedica
  su primera tarea a editar un fichero que no se compila. **Acción**: bajarlo a
  "renombrar el stub por higiene, sin urgencia", o eliminarlo del plan y dejar
  una nota de que el problema reaparecerá sólo si algún día se integra
  `sysCore/`.

- **Punto 4 (GLU): tampoco es un problema real hoy.** `gluPerspective` está en
  ese mismo fichero excluido. `gluBuild2DMipmaps`
  (`src/sysDolphin/texture.cpp:276`) **sí** está en un fichero compilado, pero
  dentro de la rama `#else` de un `#if PIKI_USE_DGX` (líneas 212/247/282), y la
  build define `PIKI_USE_DGX=1`. Comprobado además en el binario: `nm -uC` no
  muestra ningún símbolo `glu`. **Acción**: quitar la decisión "glu32 vs
  reescribir" del camino crítico; sólo vuelve si alguien compila con
  `PIKI_USE_DGX=0`.

**Un dato menor**: el launcher tiene 562 líneas, no 548.

**Un riesgo confirmado y localizado.** El punto 5 de riesgos y la Fase 5.4
aciertan: el guardado usa una **ruta relativa**. Es
`pc_port/dolphin_stubs/card_stubs.cpp:23`:

    fs::path root(s32 channel) { return fs::path("save") / (channel == 0 ? "card0" : "card1"); }

y `SDL_GetPrefPath` no se usa en ninguna parte del port (0 apariciones). En
Linux, con instalación en carpeta portable, funciona; en Windows bajo
`C:\Program Files` fallaría al escribir. Conviene subirlo de "verificar en la
Fase 2" a tarea explícita.

**Confirmado también**: el `register` de `OSFastCast.h:52+` existe y es como
describe el punto 3.2.6.

Con esas dos correcciones, la estimación de 8-13 días **se acorta ligeramente**:
la Fase 0 pierde su tarea principal y la Fase 1 pierde el punto 5.

## 4. Estimación y orden

| Fase | Contenido | Esfuerzo | Desbloquea |
|---|---|---|---|
| 0 | Preparativos (wingdi, rama) | 0.5 día | Todo |
| 1 | CMake multiplataforma + toolchain MinGW | 1 día | A |
| 2 | Núcleo compila y arranca en Windows | 2-4 días | A |
| 3 | Launcher Win32 | 3-5 días | A jugable |
| 5 | Empaquetado zip + CI MinGW | 1-2 días | Release alpha Win |
| 6A | CI cross MinGW | 0.5 día | Calidad continua |
| — | **Hito: alpha Windows jugable** | **~8-13 días** | |
| 4 | MSVC + shims + vcpkg | 2-3 días | B |
| 6B | CI windows-latest + tests nativos | 0.5 día | B |
| 5b | Inno Setup, firma | 1-2 días | Release pulida |

Total estimado hasta alpha jugable en Windows: **8-13 días de trabajo**
(asumiendo familiaridad básica con Win32; la fase 3 es la más incierta).

## 5. Riesgos y mitigaciones

1. **Renderer en GPUs Intel antiguas / drivers genéricos de Windows**:
   sin ICD, OpenGL es 1.1 y el juego no arranca. Mitigación: mensaje de
   error claro al crear el contexto; documentar "instala drivers de GPU";
   a muy largo plazo, evaluar ANGLE (OpenGL ES sobre DirectX 11) como
   fallback — fuera del alcance de este plan.
2. **Unicode**: es el bug silencioso más probable. Mitigación: tests
   unitarios del conversor UTF-8↔UTF-16 y prueba con usuario `José`.
3. **Falsos positivos de antivirus**: ejecutables MinGW sin firmar a veces
   son marcados heurísticamente. Mitigación: builds limpios, sin
   ofuscación, publicar checksums, y reportar falsos positivos a los
   vendors si ocurre.
4. **Divergencia Linux/Windows**: cada cambio futuro debe compilar en
   ambos. Mitigación: CI cruzado desde el día 1 (Fase 6A) para que ningún
   PR rompa Windows.
5. **`pc_generator_cache_validation` y saves**: el sistema de guardado no
   se ha auditado a fondo (no hay I/O directo en `pc_port/save/`, lo hace
   otra capa); en la Fase 2 verificar con una partida real que lee/escribe
   igual que en Linux.

## 6. Qué NO haremos (alcance)

- Sin Windows ARM64 (x86-64 solo; los dispositivos ARM emulan x86).
- Sin Microsoft Store ni UWP/MSIX en la alpha.
- Sin DirectX: OpenGL + SDL_GetProcAddress ya funciona.
- Sin port del antiguo código Win32 del decomp (`src/sysCore/oglGraphics.cpp`
  usa wglGetProcAddress pero está excluido de la build; se mantiene
  excluido).
- Sin cambios al extractor de ISO ni al formato de instalación: la misma
  ISO GPIE01 Rev. 1 y el mismo árbol `assets/` que en Linux.
