# Pikmin Native — port nativo para Linux

Port nativo, experimental y comunitario de *Pikmin* (GameCube, 2001) para
Linux. Ejecuta el código del juego directamente en el sistema anfitrión y
traduce GX a OpenGL; no usa Dolphin como emulador.

El proyecto está en desarrollo. Se puede compilar y recorrer buena parte del
juego, pero todavía existen diferencias gráficas, de audio y rendimiento.
Consulta [ROADMAP.md](ROADMAP.md) antes de informar o abordar un fallo.

## Requisitos legales y del juego

Este repositorio y sus binarios **no contienen ROMs ni recursos de Nintendo**.
Para jugar necesitas volcar legalmente tu propio disco de **Pikmin USA Rev. 1
(GPIE01, revisión 1)** a un archivo ISO/GCM sin comprimir.

No subas ROMs, assets extraídos, claves ni material propietario a issues o pull
requests. Lee [LEGAL.md](LEGAL.md) para conocer el alcance de la licencia y las
marcas de terceros.

## Compilar en Linux

En Debian/Ubuntu y derivados:

```sh
sudo apt install build-essential cmake pkg-config libsdl2-dev libgl1-mesa-dev zenity
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

En Arch Linux hay un generador de carpeta portable que compila y coloca el
juego y el launcher en la disposición requerida. Consulta
[`packaging/arch-linux/README.md`](packaging/arch-linux/README.md).

El instalador es una ventana SDL propia. `zenity` proporciona los selectores
nativos de archivo y carpeta; en KDE también puede usarse `kdialog`. Al menos
uno de los dos debe estar disponible para la instalación interactiva; las
opciones CLI se conservan para automatización y CI.

## Instalación gráfica

Extrae el paquete publicado y abre `pikmin-launcher` con doble clic. No es
necesario abrir una terminal. El asistente:

1. Muestra `Pikmin PC Port Installer` con campos para la ROM y la instalación.
2. Cada campo tiene su botón `Seleccionar`; `Instalar` permanece inactivo hasta
   disponer de ambas ubicaciones.
3. Valida la ISO/GCM y muestra el progreso en la misma ventana.
4. Copia `pikmin` y `pikmin-launcher` a la carpeta elegida y coloca allí
   `assets/`, sin copiar ni modificar la ROM.
5. Confirma que la instalación terminó e inicia el juego.

En las siguientes ejecuciones se abre la copia de `pikmin-launcher` ubicada en
la carpeta instalada; detecta los assets y arranca el juego directamente. Para
instalaciones sin interfaz gráfica o automatizadas:

```sh
./build/bin/pikmin-launcher --rom /ruta/a/Pikmin.iso \
  --install-dir /ruta/de/instalacion
```

El extractor integrado acepta ISO y GCM sin comprimir. Convierte previamente
RVZ, WIA o GCZ a ISO con `dolphin-tool`; esos contenedores no se implementan en
el proyecto para evitar mantener varios descompresores.

Opciones útiles:

```text
--install-dir DIR instala el port en DIR
--data-dir DIR    alias compatible de --install-dir
--extract-only    instala los assets sin iniciar el juego
--help            muestra la ayuda
```

## Rendimiento

La configuración de referencia validada es **1920x1080 con resolución interna
nativa** (`renderScale = 1`, ajustable con `F1`). El juego funciona entonces a
**60 FPS en menús y 30 FPS en gameplay**, que son los ritmos del original: es
el propio juego el que pide 60 Hz en títulos y mapas y 30 Hz en la partida.

El traductor GX genera un programa OpenGL especializado por cada configuración
TEV en lugar de interpretarla por píxel. Si hiciera falta descartarlo para
depurar un problema gráfico, `PIKMIN_TEV_SPECIALIZE=0` vuelve al shader
genérico, correcto pero mucho más lento.

## Controles y ajustes

- `F1`: ajustes del port, vídeo, controles y mando.
- `F2`: alterna entre control clásico y puntero directo con ratón.
- Los cambios guardados pertenecen al usuario y no deben versionarse.

Consulta [CONTROL_SCHEMES.md](CONTROL_SCHEMES.md) y
[MOUSE_INPUT_IMPLEMENTATION_GUIDE.md](MOUSE_INPUT_IMPLEMENTATION_GUIDE.md) para
los detalles técnicos.

## Colaborar

Se aceptan correcciones, documentación, pruebas offline y mejoras estructurales.
Lee [CONTRIBUTING.md](CONTRIBUTING.md) antes de abrir un pull request. Las
pruebas in-game deben describir el recorrido, la versión del disco y el hardware
usado, sin adjuntar datos del juego.

Este trabajo parte de la decompilación de
[projectPiki/pikmin](https://github.com/projectPiki/pikmin). El trabajo original
de esa comunidad y su historial deben conservar su atribución.

## Licencia

El código del repositorio se ofrece bajo [CC0 1.0](LICENSE.MD) en la medida en
que cada contribuyente posea derechos para hacerlo. Esa licencia no concede
derechos sobre *Pikmin*, sus recursos, marcas ni ningún otro material propiedad
de Nintendo u otros terceros. Consulta [LEGAL.md](LEGAL.md).
