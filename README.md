# Open Nectar — Pikmin Decompilation Linux Port

<img width="2172" height="476" alt="opennectarlogo (1)" src="https://github.com/user-attachments/assets/71283101-1be5-4ca4-9b16-488320343cc8" />

Native, experimental, and open-source port of *Pikmin* (GameCube, 2001) for Linux. Runs the game code directly on the host system and translates GX to OpenGL; does not use Dolphin or any emulator.

This project builds upon the decompilation by [projectPiki/pikmin](https://github.com/projectPiki/pikmin) and adds a native PC port layer.

## Project Status

**Functional:**
- Complete compilation on Linux x86-64
- Most of the game playable from start to finish
- 60 FPS in menus/titles, 30 FPS in gameplay (original behavior)
- TEV specialization for optimal performance
- Controller and mouse support

**In development:**
- Some minor graphical differences
- Occasional audio issues
- Ports to other operating systems

## Play directly (without compiling)

Download `nectar-portable.tar.gz` from [Releases](../../releases) and extract:

```sh
tar -xzf nectar-portable.tar.gz
cd nectar-portable
```

Install only the OpenGL dependencies:

```sh
# Debian/Ubuntu
sudo apt install libopengl0 libglvnd0 libgbm1 libgl1-mesa-dri

# Arch Linux
sudo pacman -S libglvnd mesa

# Fedora
sudo dnf install libglvnd mesa-dri-drivers
```

Run the launcher:

```sh
./nectar-launcher
```

The launcher will ask for your **Pikmin USA Rev. 1 (GPIE01)** ISO/GCM. It will extract the necessary assets and run the game. The ROM is not modified or redistributed.

### Launcher options

```sh
./nectar-launcher --rom /path/to/Pikmin.iso --install-dir /path/to/installation
./nectar-launcher --extract-only    # extract assets only, don't run
./nectar-launcher --help
```

## Build from source

### Requirements

- CMake 3.16+
- C++17 compiler (GCC 10+ or Clang 12+)
- SDL2
- OpenGL
- zenity (optional, for graphical dialog)

### Debian/Ubuntu

```sh
sudo apt install build-essential cmake pkg-config libsdl2-dev libgl1-mesa-dev zenity

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

### Arch Linux

```sh
sudo pacman -S base-devel cmake sdl2 mesa zenity

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

### Run after building

```sh
./build/bin/pikmin-launcher --rom /path/to/Pikmin.iso --install-dir ./my-installation
```

Or for direct development (requires assets in `./assets/`):

```sh
./build/bin/pikmin_pc
```

## Project structure

```
.
├── src/           # Decompiled game code from original
├── include/       # Game headers
├── pc_port/       # Native port layer (audio, video, input)
├── config/        # Build configuration per region
├── packaging/     # Packaging scripts
├── tools/         # Development utilities
└── CMakeLists.txt
```

- `src/` and `include/` contain the decompiled code from the original game
- `pc_port/` is the native layer that translates GX→OpenGL, handles audio/input
- Port modifications go in `pc_port/`, not in `src/`

## Technical architecture

The port works as follows:

1. **Game code** (`src/`) is compiled as a static library
2. **Native layer** (`pc_port/`) implements GameCube APIs (GX, AI, PAD, etc.)
3. **GX→OpenGL translation**: GX display lists are translated to OpenGL shaders
4. **TEV specialization**: Optimized shaders are generated per material configuration


## Controls and port features

This port includes significant improvements over the original GameCube game, designed to take advantage of keyboard, mouse, and PC capabilities.

### Keyboard and mouse

**Keyboard:**
| Action | Key |
|--------|-----|
| Movement (left stick) | W A S D |
| Pikmin formation (C-stick) | T F G H (left/down/right) |
| D-Pad | Arrow keys |
| A (Confirm/Attack) | J or Space |
| B (Cancel/Whistle) | K |
| X (Dismiss) | L |
| Y (Group) | I |
| Z (Change camera) | U |
| L (Rotate camera) | Q / E |
| Start (Pause) | Enter |
| Settings menu | F1 |
| Switch control mode | F2 |
| Screenshot | F12 |

**Mouse:**
- In pointer mode, the mouse directly controls the game cursor
- Left click: Olimar throws Pikmin
- Right click: Whistle
- Mouse offers precision impossible with an analog stick

### F1 Menu — Port settings

Press **F1** at any time to open the configuration menu:

**Video:**
- **Resolution**: Any monitor resolution, including ultrawide
- **Display mode**: Windowed, fullscreen, or borderless
- **Render scale**: Internal resolution independent of output (improves performance on slower GPUs)
- **VSync**: Vertical synchronization on/off
- **Refresh rate**: Force a specific refresh rate

**Gameplay:**
- **FPS mode**: 
  - `30 FPS (stable)`: Original game behavior
  - `60 FPS (experimental)`: 60 FPS gameplay
- **Chain Pikmin actions**: When active, Pikmin automatically look for more work after completing a task. Example: a Pikmin thrown at a flower destroys it and then automatically carries the pellet to the Onion. Disabled by default to maintain fidelity to the original

**Controls:**
- **Control scheme**: Classic (GameCube) or Mouse pointer
- **Mouse sensitivity**: 0.1x to 5.0x
- **Stick dead zone**: 0-127
- **Invert sticks**: Options for main stick and C-stick

### Control modes

**Classic Mode (GameCube):**
- WASD controls cursor and movement simultaneously
- Identical behavior to the original
- Recommended for controllers

**Mouse Pointer Mode:**
- Mouse controls the cursor with absolute precision
- WASD available for independent movement
- Ideal for strategy with quick selection

### Controller support

The port supports any SDL2-compatible controller:
- Xbox, PlayStation, Nintendo Switch Pro
- Generic controllers with automatic mapping
- Customizable configuration from the F1 menu
- Vibration supported where available

### Port value-added features

**Improvements over the original:**
- **Arbitrary resolution**: From 480p up to 4K and ultrawide, no hacks needed
- **Experimental 60 FPS**: The original was 30 FPS in gameplay
- **Mouse control**: Precision impossible on GameCube
- **Improved Pikmin AI**: Chain tasks automatically (optional)
- **Superior performance**: TEV specialization generates optimal shaders per material
- **No emulation**: Native x86-64 code, no Dolphin overhead
- **Instant saves**: Save files are accessible on disk
- **Portable**: The release package works on any Linux without installing dependencies

## Performance

The validated configuration is **1920x1080 with native internal resolution** (`renderScale = 1`):

- 60 FPS in menus and map selection
- 30 FPS in gameplay (this is the original game behavior)
- GTX 1050 4GB / Mesa 26.0 as reference

## Debug and environment variables

```sh
PIKMIN_TEV_SPECIALIZE=0      # Disable specialization (slower)
PIKMIN_TEV_MAX_STAGES=N      # Limit shader complexity
PIKMIN_GL_CHECK=1            # Check OpenGL errors
PIKMIN_DUMP_SHADERS=1        # Dump generated shaders
PIKMIN_PERF_STATS=1          # GPU statistics
PIKMIN_TICK_STATS=1          # CPU statistics per tick
```

## Legal requirements

This repository **does not contain ROMs or Nintendo resources**.

To play you need to legally dump your own disc of:
- **Pikmin USA Rev. 1 (GPIE01, revision 1)**

Do not upload ROMs, extracted assets, keys, or proprietary material to issues or pull requests. See [LEGAL.md](LEGAL.md).

## Contributing

Corrections, documentation, tests, and structural improvements are accepted. Before opening a PR:

1. Read [CONTRIBUTING.md](CONTRIBUTING.md)
2. Build in Release and run tests
3. Test in-game for changes that affect gameplay

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

## License

The code is offered under [CC0 1.0](LICENSE.MD). This license does not grant rights over *Pikmin*, its resources, trademarks, or any other material from Nintendo or other third parties.

## Credits

- Original decompilation: [projectPiki/pikmin](https://github.com/projectPiki/pikmin)
- Native Linux port: Open Nectar Community

## Links

- [LEGAL.md](LEGAL.md) - Legal notice
- [CONTRIBUTING.md](CONTRIBUTING.md) - Contribution guide
