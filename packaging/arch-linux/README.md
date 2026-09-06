# Compilación portable para Arch Linux

Esta carpeta genera un paquete local con `pikmin` y `pikmin-launcher` juntos.
Esa disposición es obligatoria: el launcher busca el juego en su propio
directorio antes de extraer los recursos del disco original.

## Dependencias

En Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake pkgconf sdl2 mesa libglvnd zenity
```

`zenity` proporciona los selectores de ROM y carpeta del instalador. En un
escritorio KDE puede sustituirse por `kdialog`.

## Compilar

Desde la raíz del repositorio:

```sh
./packaging/arch-linux/build-portable.sh --clean
```

El resultado queda en:

```text
packaging/arch-linux/out/pikmin-native-linux-arch/
├── LEEME.txt
├── pikmin
└── pikmin-launcher
```

No se incluyen ROMs ni recursos propietarios. Ejecuta `pikmin-launcher` dentro
de esa carpeta y selecciona tu copia legal sin comprimir de Pikmin USA Rev. 1.

El script usa un directorio independiente, `build-arch-linux`, ejecuta CTest y
después usa las reglas de instalación de CMake. `--skip-tests` permite omitir
las pruebas y `--clean` reconstruye todo desde cero. La configuración para Arch
desactiva IPO/LTO: GCC 16 puede eliminar erróneamente el punto de entrada de
esta base de código durante el enlace. La compilación continúa siendo Release
con optimización `-O3`.
