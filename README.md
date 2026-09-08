# Open Nectar — Pikmin Native PC Port

<img width="2172" height="476" alt="opennectarlogo (1)" src="https://github.com/user-attachments/assets/71283101-1be5-4ca4-9b16-488320343cc8" />

Native, experimental, and open-source port of *Pikmin* (GameCube, 2001) for **Linux and Windows**. Runs the game code directly on the host system and translates GX to OpenGL; does not use Dolphin or any emulator.

This project builds upon the decompilation by [projectPiki/pikmin](https://github.com/projectPiki/pikmin) and adds a native PC port layer.

## Project Status

**Functional:**
- Native builds for Linux x86-64 and Windows x86-64, from the same source
- Most of the game playable from start to finish
- 30, 60 or 120 FPS gameplay, selectable in-game
- Full audio: the game's original JAudio engine, with a software DSP
- TEV specialization for optimal performance
- Controller, keyboard and mouse support

**In development:**
- Some minor graphical differences
- Ports to other operating systems

## Play directly (without compiling)

Grab the package for your system from [Releases](../../releases).

### Linux

```sh
tar -xzf nectar-linux.tar.gz
cd nectar-linux
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

Everything else — glibc, SDL2, audio libraries — travels inside the package, so it runs on any x86-64 distribution without installing anything further.

### Windows

**What you need**

- 64-bit Windows
- Graphics drivers with OpenGL 3.3 or newer. Intel, NVIDIA and AMD drivers all
  provide it. Windows' generic "Basic Display Adapter" driver does not, and the
  game will not start with it — install your GPU vendor's driver, not the one
  Windows Update supplies
- About 1 GB free where you install it: the extracted assets take roughly
  650 MB, plus the executables

**Installing**

1. Extract `nectar-windows.zip` anywhere. There is no installer to run and
   nothing is written outside the folder you choose.
2. Run `nectar-launcher.exe`.
3. It asks for your Pikmin ISO or GCM, and then for a folder to install into.
   Any folder works.
4. It verifies the image, extracts the assets and starts the game.

Installation takes a minute or two, most of it verifying that the disc image is
intact and that every extracted file came out right. That check catches damaged
copies and failing drives, which are the usual reason a game installs fine and
then misbehaves later.

**Playing afterwards**

Go to the folder you installed into and run `nectar-launcher.exe` again. It sees
the game is already installed and starts it straight away.

You can also run `nectar.exe` directly, but only from inside that folder: the
game looks for its `assets` folder relative to the current directory.

**Installing without dialogs**

From `cmd` or PowerShell:

```
nectar-launcher.exe --rom C:\path\to\Pikmin.iso --install-dir C:\Games\OpenNectar
```

Add `--extract-only` to install without launching the game afterwards. This
works over Remote Desktop and on machines with no desktop session.

**The console window is intentional**

The game opens a console window alongside it, printing what it is doing. It is
not an error. This is a young port and those messages are the only thing that
explains a failure, so they are left visible on purpose. To keep them:

```
cd C:\Games\OpenNectar
nectar.exe > log.txt 2>&1
```

**Where your files live**

Everything stays in the installation folder:

- `save\card0\` — your save files, as ordinary files on disk
- `pikmin_settings.conf` — the F1 menu settings
- `assets\` — the extracted game data

To move the installation elsewhere, copy the folder. To remove it, delete it.

**If something goes wrong**

| Symptom | Cause |
|---|---|
| Closes instantly, no window | `SDL2.dll` is missing from the folder, or Windows blocked it |
| "Could not initialize window/OpenGL" | Graphics drivers too old, or the generic Windows display driver |
| Starts but finds no data | Run it from the installation folder, not from elsewhere |
| The image is rejected | It must be Pikmin USA Rev. 1 (GPIE01), uncompressed. Convert RVZ/WIA/GCZ to ISO with `dolphin-tool` |

### Both platforms

The launcher asks for your **Pikmin USA Rev. 1 (GPIE01)** ISO/GCM, extracts the assets it needs and starts the game. The ROM is never copied or modified.

### Launcher options

```sh
nectar-launcher --rom /path/to/Pikmin.iso --install-dir /path/to/installation
nectar-launcher --extract-only    # extract assets only, don't run
nectar-launcher --help
```

## Build from source

### Requirements

- CMake 3.16+
- C++17 compiler (GCC 10+ or Clang 12+)
- SDL2
- OpenGL
- zenity (optional on Linux, for the graphical dialog)

### Linux

```sh
# Debian/Ubuntu
sudo apt install build-essential cmake pkg-config libsdl2-dev libgl1-mesa-dev zenity

# Arch Linux
sudo pacman -S base-devel cmake sdl2 mesa zenity

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

### Windows (cross-compiled from Linux)

The Windows executable is built with MinGW-w64, cross-compiled from Linux.

SDL2 for MinGW is expected in `third_party/SDL2-mingw64`. It is not committed to
the repository; download `SDL2-devel-<version>-mingw.tar.gz` from the
[SDL releases](https://github.com/libsdl-org/SDL/releases) and extract its
`x86_64-w64-mingw32` directory there, so that
`third_party/SDL2-mingw64/lib/libSDL2.dll.a` exists.

```sh
sudo apt install g++-mingw-w64-x86-64

cmake -S . -B build-windows \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw64.cmake \
      -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows -j"$(nproc)"
```

The result is `build-windows/bin/nectar.exe`, which needs `SDL2.dll` beside it.

### Run after building

```sh
./build/bin/nectar-launcher --rom /path/to/Pikmin.iso --install-dir ./my-installation
```

Or, for development directly against a checked-out asset tree in `./assets/`:

```sh
./build/bin/nectar
```

## Project structure

```
.
├── src/           # Decompiled game code from original
├── include/       # Game headers
├── pc_port/       # Native port layer (audio, video, input)
├── cmake/         # Toolchain files
├── config/        # Build configuration per region
├── packaging/     # Packaging scripts
├── third_party/   # Third-party code and its licences
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
5. **Audio**: the game's original JAudio engine runs unchanged; only the boundary where the GameCube's DSP chip used to sit is replaced by a software renderer

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
- Mouse wheel: picks the Pikmin colour to throw, or zooms the camera (see below)
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
  - `120 FPS (experimental)`: 120 FPS gameplay, on a display that can show it
- **Chain Pikmin actions**: When active, Pikmin automatically look for more work after completing a task. Example: a Pikmin thrown at a flower destroys it and then automatically carries the pellet to the Onion. Disabled by default to maintain fidelity to the original

**Mods:**
- **Mouse wheel**: chooses what the wheel does, one or the other:
  - `Pikmin Colour`: cycles which colour to throw next, through the colours you actually have with you. With one colour it does nothing, with two it alternates, with three it cycles. If the chosen colour is out of reach, the captain still grabs the nearest Pikmin
  - `Camera Zoom`: pulls the camera between 0.45× and 2.50× of its normal distance
- **Pikmin limit**: how many Pikmin may be on the field at once, from 50 up to 999. The original is 100. The Onion, the field and the matrix pool are all sized from this number, so it applies when a stage loads rather than mid-day. Tested to 945 Pikmin on screen; high values do cost frame rate, and how much depends on your machine
- **Day length**: 5 to 30 minutes of play per in-game day, 10 being the original. This stretches the game's own clock, so nothing moves faster or slower — sunset simply arrives sooner or later

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
- **60 and 120 FPS**: The original ran gameplay at 30
- **Mouse control**: Precision impossible on GameCube, including wheel shortcuts
- **Improved Pikmin AI**: Chain tasks automatically (optional)
- **Superior performance**: TEV specialization generates optimal shaders per material
- **No emulation**: Native x86-64 code, no Dolphin overhead
- **Instant saves**: Save files are accessible on disk
- **Portable**: The release packages run without installing dependencies

## Performance

The validated configuration is **1920x1080 with native internal resolution** (`renderScale = 1`) on a GTX 1050 4GB with Mesa 26.0.

The game integrates by elapsed time, so raising the frame rate does not speed up gameplay. 120 FPS needs a display that can present it; on a 60 Hz panel with VSync the game still shows 60.

## Debug and environment variables

```sh
PIKMIN_TEV_SPECIALIZE=0      # Disable specialization (slower)
PIKMIN_TEV_MAX_STAGES=N      # Limit shader complexity
PIKMIN_GL_CHECK=1            # Check OpenGL errors
PIKMIN_DUMP_SHADERS=1        # Dump generated shaders
PIKMIN_PERF_STATS=1          # GPU statistics
PIKMIN_TICK_STATS=1          # CPU statistics per tick
PIKMIN_AUDIO_STATS=1         # Audio mixer statistics
PIKMIN_WHEEL_TRACE=1         # Trace mouse wheel colour selection
```

The game binary also accepts `--audio-self-test`, which walks scenes, stages,
boss transitions, muting, sound effects and cinematic streams without opening a
window, and reports whether each produced audio.

## AI disclosure

This project was developed with the assistance of AI tools. The generated code was reviewed, tested and integrated by me. I mention it because I'd rather be upfront about how this was built.

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
- Software DSP and sound bank loader: [NextOs-Ports/pikmin-nextos](https://github.com/NextOs-Ports/pikmin-nextos), adapted here from SDL3 to SDL2. Licence and attribution in [third_party/nextos-audio/](third_party/nextos-audio/)
- Native PC port: Open Nectar Community

## Links

- [LEGAL.md](LEGAL.md) - Legal notice
- [CONTRIBUTING.md](CONTRIBUTING.md) - Contribution guide
