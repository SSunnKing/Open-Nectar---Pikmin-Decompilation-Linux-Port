# Portabilidad: cualquier PC Linux y, más adelante, Windows

Este documento explica por qué el paquete actual solo funciona en el equipo
donde se compiló, qué hay que corregir para que funcione en cualquier PC con
Linux, y qué trabajo queda para una versión de Windows.

Fecha del diagnóstico: 2026-09-03.

---

## 1. Diagnóstico: por qué falla hoy en otros equipos

El paquete generado por `packaging/arch-linux/build-portable.sh` hereda tres
problemas del entorno de compilación local.

### 1.1 Compilado solo para la CPU de esta máquina (crítico)

- `CMakeLists.txt` activa `PIKMIN_NATIVE_OPTIMIZE` por defecto, lo que añade
  `-march=native` al compilar.
- `build-portable.sh` **no** desactiva esa opción al generar el paquete.
- Resultado verificado sobre el binario publicado: ~16.000 instrucciones
  AVX2/FMA/BMI2 (`ymm`, `vfmadd132ss`, `vpermd`, `shlx`...).

Cualquier CPU sin AVX2 aborta al instante con `Illegal instruction
(core dumped)`. Esto incluye Intel anteriores a 2013 (Haswell), AMD
anteriores a 2015 (Excavator/Ryzen) y, muy importante, **CPU modernas de
bajo consumo sin AVX2 como los Intel N100/N95/N97 (Alder Lake-N)**, muy
habituales en mini-PCs y portátiles económicos actuales.

Comprobación rápida:

```sh
objdump -d pikmin | grep -c ymm    # debe dar 0 en un build genérico
```

### 1.2 Requiere glibc 2.43 (crítico)

El ejecutable `pikmin` enlaza símbolos de **GLIBC_2.43** porque se compiló
en un Ubuntu muy reciente. En cualquier distro con glibc anterior ni siquiera
arranca:

```
./pikmin: /lib/x86_64-linux-gnu/libc.so.6: version `GLIBC_2.43' not found
```

Referencia de glibc por distro:

| Distro              | glibc |
|---------------------|-------|
| Debian 12           | 2.36  |
| Ubuntu 22.04 LTS    | 2.35  |
| Ubuntu 24.04 LTS    | 2.39  |
| Fedora 40           | 2.39  |
| Este equipo         | 2.43  |

Comprobación rápida:

```sh
objdump -T pikmin | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1
```

### 1.3 Dependencias dinámicas y diálogos externos

- `pikmin` y `pikmin-launcher` enlazan dinámicamente SDL2, PulseAudio/ALSA,
  X11/Wayland, OpenGL... Si falta alguna: `error while loading shared
  libraries: libSDL2-2.0.so.0`.
- El instalador gráfico necesita `zenity` o `kdialog`
  (`pc_port/launcher/launcher_main.cpp`). Si no están, el launcher escribe
  el error en stderr y sale; al hacer doble clic desde un gestor de archivos
  **el usuario no ve nada** y concluye que "no funciona".

---

## 2. Objetivo A: que funcione en cualquier PC Linux

### 2.1 Build genérico de CPU (obligatorio)

En `packaging/arch-linux/build-portable.sh`, añadir a la llamada de cmake:

```sh
-DPIKMIN_NATIVE_OPTIMIZE=OFF
```

Con eso el compilador genera código para el x86-64 base (SSE2), que ejecuta
el 100 % de los PCs de 64 bits. El coste de rendimiento es asumible para un
juego de GameCube; quien compile desde el código fuente en su propia máquina
puede seguir activando la optimización nativa.

Opcional, más adelante: publicar dos variantes (`x86-64` genérico y
`x86-64-v3` para CPUs con AVX2) y que el launcher o el LEEME indiquen cuál
elegir. No es prioritario.

### 2.2 Independizarse de la glibc del equipo de compilación (obligatorio)

**Resuelto** con el paquete autocontenido (ver 2.7): al incluir la propia
glibc en `lib/` y arrancar mediante su `ld-linux`, el paquete ya no depende
de la glibc de la distro del usuario ni de la del equipo que compiló.

Alternativas documentadas por si algún día se cambia de estrategia:

1. **Contenedor de build**: compilar el release dentro de un contenedor con
   Ubuntu 22.04 o Debian 12 (glibc hacia adelante no es compatible, hacia
   atrás sí; el runner `ubuntu-22.04` de GitHub Actions ya da glibc 2.35).

2. **AppImage**: empaqueta el binario junto con SDL2 y el resto de librerías
   (excepto glibc y drivers de GPU). Herramienta habitual: `linuxdeploy`.

3. Enlazar estáticamente libc/libstdc++ (`-static-libstdc++ -static-libgcc`
   ayuda, pero glibc estático completo da problemas con NSS y con el
   `dlopen` que SDL2 hace de sus backends de audio; no recomendado).

### 2.3 Dependencias y experiencia de instalación (obligatorio)

- Documentar y, si es posible, **empaquetar** SDL2: o bien AppImage (2.2.2),
  o bien enlazar SDL2 estáticamente (su licencia zlib lo permite), o al menos
  detectar su ausencia y mostrar un diálogo explicando qué instalar.
- El launcher no debe morir en silencio: si no hay `zenity`/`kdialog`, además
  del mensaje por stderr debería intentar abrir un x-terminal o, como mínimo,
  el LEEME debe indicar ejecutarlo desde terminal. A medio plazo, integrar
  los selectores de archivo en la propia ventana SDL del launcher elimina la
  dependencia por completo.
- Añadir al paquete un `ejecutar.sh` mínimo que llame a `./pikmin-launcher`
  desde terminal, para que los errores sean visibles.

### 2.4 Verificación automática en CI (obligatorio)

Extender el job de Linux de `.github/workflows/linux.yml` para que cada
release falle si se cuela algo no portable:

```sh
# 1. Sin instrucciones por encima de SSE2 en el build genérico
! objdump -d pikmin | grep -qE '\b(ymm|zmm)[0-9]*\b'

# 2. glibc y libstdc++ dentro del límite
test "$(objdump -T pikmin | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1 | sed 's/GLIBC_//')" \
     = "$(printf '%s\n' 2.35 "$(objdump -T pikmin | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1 | sed 's/GLIBC_//')" | sort -V | head -1)"

# 3. Sin dependencias inesperadas (lista blanca)
ldd pikmin | grep 'not found' && exit 1
```

Y un job de **prueba de humo en contenedor limpio** (p. ej. `debian:12` o
`ubuntu:22.04` sin nada instalado salvo lo documentado) que ejecute
`pikmin-launcher --help` y los tests. Eso reproduce exactamente lo que verá
otro equipo.

### 2.5 Validación manual antes de publicar

Probar el paquete en al menos:

- [ ] Una máquina/VM con Ubuntu 22.04 o 24.04 limpio (sin instalar nada más
      que lo que digan las instrucciones).
- [ ] Una máquina/VM con Debian 12 o Fedora reciente.
- [ ] Una CPU sin AVX2 (o forzar `qemu-x86_64` sin AVX2 / `GLIBC_TUNABLES`)
      para confirmar que no hay `Illegal instruction`.
- [ ] GPU AMD, NVIDIA e Intel (Mesa y, en NVIDIA, también driver propietario).
- [ ] Sesión Wayland y sesión X11.

### 2.6 Checklist resumen Linux

| # | Tarea | Estado |
|---|-------|--------|
| 1 | `-DPIKMIN_NATIVE_OPTIMIZE=OFF` en build-portable.sh | Hecho (2026-09-03) |
| 2 | Build de release portable sin Docker: `packaging/linux/package-standalone.sh` | Hecho: paquete autocontenido de 30 MB con glibc, SDL2 y 42 librerías incluidas |
| 3 | Launcher funciona sin zenity/kdialog | Hecho: modo texto por terminal y relanzamiento automático al hacer doble clic |
| 4 | CI: verificación de portabilidad + test en Debian 12 limpio | Hecho: `verify-portable.sh` + job `smoke-clean-distro` en `linux.yml` |
| 5 | Validación manual en 2-3 distros físicas | Pendiente |

### 2.7 Solución implementada: paquete autocontenido

**Sin Docker ni contenedores.** El script `packaging/linux/package-standalone.sh`:

1. Compila con `-march=native` desactivado (x86-64 base)
2. Copia las librerías del sistema (glibc, SDL2, X11, PulseAudio...) a `lib/`
3. Copia los avisos de licencia de cada librería a `lib/licenses/`
   (obligatorio al redistribuir glibc/LGPL y compañía)
4. Genera scripts wrapper que ejecutan el binario real con la glibc incluida
5. Funciona en **cualquier Linux x86-64** con kernel moderno y driver OpenGL

**Estructura del paquete** (~30 MB):
```
pikmin-native-linux/
  pikmin            <- script wrapper
  pikmin-launcher   <- script wrapper
  pikmin.real       <- binario real
  pikmin-launcher.real
  lib/              <- librerías incluidas
  lib/licenses/     <- avisos de licencia de terceros
  LEEME.txt
```

El launcher detecta automáticamente si está en un paquete standalone
(presencia de `.real` + `lib/`) y copia todo el árbol al directorio de
instalación elegido por el usuario. Los wrappers resuelven su directorio
real aunque se invoquen mediante PATH o enlaces simbólicos.

**Para generar el paquete:**
```bash
./packaging/linux/package-standalone.sh
```

No requiere Docker, solo `cmake`, un compilador C++ y las dependencias de
desarrollo habituales (`libsdl2-dev`, `libgl1-mesa-dev`).

**Protección frente a reinstalaciones**: si el launcher ya instalado se
vuelve a ejecutar sobre su propio directorio, detecta que origen y destino
son el mismo (`fs::equivalent`) y no toca `lib/` ni los binarios.

---

## 3. Objetivo B: Windows (fase posterior)

Buena noticia del diagnóstico: **el núcleo del juego ya es casi portable**.
`pc_port/` solo usa SDL2 + OpenGL; la gestión de endianness está resuelta
(`pc_types_c.h`, `pc_gfx.cpp`, audio). No hay llamadas POSIX en el motor.

### 3.1 Lo que hay que reescribir

| Componente | Problema en Windows | Solución propuesta |
|------------|--------------------|--------------------|
| `launcher_main.cpp` | Usa `fork`, `execvp`, `waitpid`, `readlink("/proc/self/exe")`, `unistd.h`, `sys/wait.h` | Reescribir con `CreateProcessW`, `GetModuleFileNameW` y `WaitForSingleObject`, tras `#ifdef _WIN32` o en un `launcher_windows.cpp` separado |
| Selectores de archivo | Dependen de `zenity`/`kdialog` | Usar `IFileOpenDialog` (COM) o la librería tinyfiledialogs (un solo .c, licencia zlib) |
| `pc_port/wingdi.h` | **Colisión de nombre**: Windows SDK tiene su propio `wingdi.h` | Renombrar a `pc_wingdi_stub.h` y solo incluirlo en builds no-Windows |
| Rutas | Separador `\`, sin `$HOME` ni XDG | Usar `%APPDATA%\PikminNative` vía `SHGetKnownFolderPath` / SDL_GetPrefPath |
| Permisos de ejecución | `fs::permissions` con bits POSIX no aplica | Envolver en `#ifndef _WIN32` |
| Audio/vídeo | Nada grave: SDL2 lo abstrae | Verificar WASAPI y Direct3D/OpenGL en hardware real |

### 3.2 Toolchain: dos caminos

1. **MSVC + vcpkg** (recomendado a largo plazo): mejor tooling de depuración,
   es lo que esperan la mayoría de colaboradores Windows. Requiere limpiar
   posibles extensiones GNU del código (el proyecto se compiló con GCC).
2. **MinGW-w64 cruzado desde Linux**: permite generar el build de Windows
   desde el mismo CI Linux sin mantener un runner Windows al principio.
   `x86_64-w64-mingw32-g++` + SDL2 para MinGW. Menos pulido pero muy barato
   de empezar.

En ambos casos: **enlazar SDL2 estáticamente o distribuir `SDL2.dll` junto al
.exe** (en Windows no hay gestor de paquetes garantizado; el usuario espera
un zip que funciona al descomprimir).

### 3.3 Distribución Windows

- Carpeta portable en `.zip` con `pikmin.exe`, `pikmin-launcher.exe` y
  `SDL2.dll` (equivalente al paquete Linux actual).
- Instalador opcional con Inno Setup o NSIS.
- Firmado de código: posponer; Windows SmartScreen avisará al principio,
  documentarlo en el LEEME.
- El launcher de Windows aplicará las mismas reglas legales: nunca distribuir
  la ROM ni assets; extracción local desde la ISO del usuario (el código de
  `gamecube_image.cpp` ya es portable).

### 3.4 Checklist resumen Windows

| # | Tarea | Depende de |
|---|-------|-----------|
| 1 | Renombrar `wingdi.h` y aislar el stub | Nada, hacer ya |
| 2 | Abstraer rutas de datos (XDG vs %APPDATA%) con SDL_GetPrefPath | Nada |
| 3 | Portar launcher a Win32 (CreateProcess, diálogos nativos) | 2 |
| 4 | Toolchain MinGW cruzado en CI (build + tests) | 1 |
| 5 | Probar en Windows 10 y 11 reales, GPUs AMD/NVIDIA/Intel | 3, 4 |
| 6 | Evaluar MSVC + vcpkg como toolchain principal | 4 |
| 7 | Empaquetado zip + LEEME (SmartScreen, SDL2.dll) | 5 |

---

## 4. Orden recomendado de trabajo

1. **Ahora (bloquea cualquier release Linux)**: checklist 2.6, puntos 1-5.
   Con un día de trabajo el paquete deja de ser "solo mi PC".
2. **Antes de anunciar el proyecto**: validación manual 2.5.
3. **Después del primer release Linux público**: empezar Windows por los
   puntos 1-3 del checklist 3.4 (son independientes del resto y baratos).
4. **Más adelante**: MSVC, instalador Windows, variantes x86-64-v3 para
   Linux, y evaluar Flatpak como canal adicional.

## 5. Notas

- Todo lo anterior asume x86-64. ARM64 Linux (Raspberry Pi 5, etc.) sería
  viable por SDL2/OpenGL, pero conviene dejarlo fuera hasta que Linux x86-64
  y Windows estén estables.
- Ningún paso de este plan implica distribuir datos de Nintendo: los paquetes
  siguen conteniendo solo ejecutables, y la extracción de assets se hace
  siempre en el equipo del usuario desde su propia ISO.
