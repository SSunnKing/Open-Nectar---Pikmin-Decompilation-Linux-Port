# Nectar — port a Windows

Compilación cruzada desde Linux con MinGW-w64. No hace falta una máquina
Windows para producir el `.exe`.

## Por qué MinGW y no MSVC

Las ~197.000 líneas decompiladas dependen de comportamiento de GCC:
`-fpermissive`, la palabra clave `register`, conversiones implícitas y varias
extensiones. MSVC las rechazaría en masa y obligaría a modificar código
heredado, con el riesgo de perder fidelidad respecto al juego original. MinGW
es el mismo compilador que ya usa la build de Linux, así que ese código
compila sin tocarlo.

## Requisitos

```sh
sudo apt install g++-mingw-w64-x86-64 mingw-w64-tools
```

SDL2 para MinGW no está en los repositorios; se descarga a `third_party/`:

```sh
ver=2.32.10
curl -L -o /tmp/sdl2-mingw.tar.gz \
  "https://github.com/libsdl-org/SDL/releases/download/release-${ver}/SDL2-devel-${ver}-mingw.tar.gz"
mkdir -p third_party && tar -xzf /tmp/sdl2-mingw.tar.gz -C third_party \
  --wildcards "SDL2-${ver}/x86_64-w64-mingw32/*"
mv "third_party/SDL2-${ver}/x86_64-w64-mingw32" third_party/SDL2-mingw64
rm -rf "third_party/SDL2-${ver}" /tmp/sdl2-mingw.tar.gz
```

## Compilar

```sh
cmake -S . -B build-windows \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows -j"$(nproc)"
```

Produce `build-windows/bin/pikmin.exe` y `build-windows/bin/pikmin-launcher.exe`.
Junto a ellos hay que distribuir `SDL2.dll` (de `third_party/SDL2-mingw64/bin/`).
El toolchain enlaza `libgcc` y `libstdc++` de forma estática, así que no se
necesitan más DLL de MinGW.

## Las tres diferencias que resuelve el port

### 1. Las 162 ramas `#ifdef WIN32` del código heredado

El código decompilado conserva el backend Win32/OpenGL del Pikmin original.
Es **incompatible** con la traducción GX → OpenGL de este port: cambia el
número de joints (16 en vez de 12), el cálculo de matrices y la carga de
texturas. MinGW define `WIN32` automáticamente, así que el toolchain la retira
con `-UWIN32`.

`_WIN32` sí se conserva: sus dos únicos usos son correctos y necesarios en
Windows (endianness en `stream.cpp` y las convenciones de llamada de
`glext.h`).

### 2. La capa POSIX del launcher

Todo el POSIX del proyecto estaba en un único archivo, `launcher_main.cpp`.
Ahora vive detrás de [`launcher_platform.h`](pc_port/launcher/launcher_platform.h),
con dos implementaciones:

| | Linux | Windows |
|---|---|---|
| Ruta del ejecutable | `/proc/self/exe` | `GetModuleFileNameW` |
| Selector de archivo | zenity / kdialog | `GetOpenFileNameW` |
| Selector de carpeta | zenity / kdialog | `IFileOpenDialog` |
| Avisos | zenity / kdialog | `MessageBoxW` |
| Carpeta de datos | `XDG_DATA_HOME` | `FOLDERID_LocalAppData/Nectar` |
| Arranque del juego | `chdir` + `execl` | `SetCurrentDirectoryW` + `CreateProcessW` |

En Windows el instalador gráfico **no necesita instalar nada**: los diálogos
son parte del sistema. Desaparece la causa más común de fallo del instalador
en Linux.

### 3. Las cabeceras de OpenGL

En Windows, `<GL/gl.h>` necesita `APIENTRY` y `WINGDIAPI`, que normalmente
llegan incluyendo `windows.h`. Eso aquí no es viable: `windows.h` define una
macro `ERROR` que chocaría con la macro de registro del proyecto, usada en 371
archivos.

[`pc_port/gl/pc_opengl.h`](pc_port/gl/pc_opengl.h) define esas convenciones a
mano. Como tanto `gl.h` como `glext.h` condicionan su `#include <windows.h>` a
que `APIENTRY` no esté definida, se lo saltan por completo.

## Estado

- [x] Toolchain de compilación cruzada
- [x] SDL2 para MinGW
- [x] Capa de plataforma del launcher (POSIX + Win32)
- [x] Shim de cabeceras OpenGL
- [x] Ramas de Windows en CMake
- [x] Sin regresión en Linux: compila y pasa las 14 pruebas
- [ ] Primera compilación real con MinGW instalado
- [ ] Empaquetado `.zip` con DLL
- [ ] Prueba en Windows real

## Aviso

El paquete de Linux tiene un fallo sin diagnosticar: el juego aborta al
construir el menú del título (`tag <pall> is not found`) en ordenadores
distintos al de desarrollo. Si resulta ser un bug del código y no datos
corruptos, **se reproducirá igual en Windows**. Conviene cerrar ese
diagnóstico antes de dar por bueno el port.
