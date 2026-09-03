# Pikmin Linux port audit

Audit date: 2026-08-24

## Verified

- Clean native x86_64 build with GCC 15.
- Plasma Wayland creates one SDL2 window and one OpenGL compatibility context.
- The boot sequence reaches the main loop and continues through title/menu,
  stage, enemy, model, save-screen and cinematic resource loading.
- Keyboard/controller polling advances the game state.
- GameCube big-endian streams and the common uncompressed BTI formats are
  decoded on little-endian hosts.
- CMPR (GameCube's tiled DXT1 variant) is decoded by the Linux GX backend.
- AddressSanitizer and UndefinedBehaviorSanitizer build successfully.
- A 25-second title-to-`demo02` sanitizer run completes without ASan/UBSan
  diagnostics after exercising the file selector, particle creation, model
  loading, save data, STX audio and cinematic setup.
- Closing the SDL window now exits the game loop and shuts SDL down.
- GX model display lists are decoded on the host: primitive commands,
  big-endian indices, vertex arrays, UVs, colours and per-vertex position
  matrices now reach OpenGL. A controlled run produced an indexed 82-vertex
  3D draw and continued through the full resource-loading sequence.

## Fixed during the audit

- Mixed Win32 OpenGL and GX rendering paths (`PIKI_USE_DGX=0`) were replaced
  by one GX-to-OpenGL path. The old combination left `GXTexObj` uninitialised
  and did not expose bound textures to the shader.
- Implemented the missing `MTXTrans` entry point revealed by the correct GX
  build configuration.
- Connected culling and display-copy/clear state to the OpenGL backend.
- Matched GX's clockwise front-face convention in OpenGL. The previous
  counter-clockwise default culled most cinematic and menu-floor geometry.
- Made `NaviMgr::getNavi` reject invalid multiplayer indices and routed the
  unused second-whistle effect to Pikmin 1's sole Navi. This removes the
  ASan-confirmed heap over-read that stopped the transition into gameplay.
- Fixed two camera vibration polynomials that allocated two coefficients for
  degree two. Their constructors clear three coefficients, causing the next
  ASan-confirmed heap overflow during gameplay camera setup.
- Guarded the optional Navi antenna collision sphere in `Navi::demoDraw`.
  The post-opening transition can use a model without an `ante` sphere; the
  previous unconditional dereference caused a reproducible SIGSEGV.
- Initialised `Navi::mGoalItem` to null. The first gameplay movie checks this
  pointer before an Onion has been assigned; its indeterminate constructor
  value caused the next reproducible SIGSEGV in `GoalItem::setSpotActive`.
- Initialised `GameMovieInterface::mComplexMesgCount`. Its indeterminate value
  replayed unqueued `demo49` entries, exhausted the reusable movie-info list,
  and caused the next reproducible SIGSEGV in `CoreNode::del`.
- Hardened movie dispatch at the shared boundary: message counts are validated,
  queues are cleared before invoking re-entrant handlers, and an exhausted
  `MovieInfo` pool now rejects a request instead of dereferencing a null node.
- Kept `ID32` in its original target byte layout and localized the reversed
  comparison to generator version tags. A global ID32 conversion fixed the
  generator count but broke model, pellet and collision identifiers.
- Enabled the portable weighted-matrix accumulation path on Linux. The port
  previously selected a PowerPC paired-singles helper whose body is empty
  outside Metrowerks, collapsing every skinned model to a point.
- Implemented CMPR texture decompression.
- Removed an invalid `CoreNode` to `DirEntry` downcast found by UBSan.
- Replaced a non-returning `System::hasDebugInfo()` implementation.
- Added normal window-close handling and made SDL reinitialisation safe.
- Corrected RGBA gather-pipe byte order for little-endian hosts.
- Connected channel material colour/alpha to GLSL, so P2D fades no longer
  render nominally transparent layers as fully opaque panels.
- Made TEV texture enable/order explicit and tracked texture bindings per map.
- Disabled empty GameCube material-state display-list caching on PC; material
  blend, depth, channel and TEV state is now replayed through the direct path.
- Added a bounded GX model display-list parser with CPU matrix transforms.
- Implemented both GX alpha comparisons and alpha-test discard in GLSL.
- Replaced the no-card stub with a persistent 16 MiB filesystem-backed card
  under `save/card0`, including create/read/write/status/rename/delete/format.
- Replaced the affine matrix inverse and inverse-transpose stubs with real
  implementations used by scaled models and normal matrices.
- Fixed the Play/new-save crash caused by narrowing a 64-bit `CARDStat*` to
  `s32` before handing it to the asynchronous card worker. Related banner and
  comment pointer arithmetic is now 64-bit safe and bounds checked.
- Replaced mismatched variadic JAudio/movie stubs with their real ABI
  signatures and return types. The SDL audio device is now initialized
  idempotently, and native decoding/resampling support exists for the game's
  stereo ADPCM `.stx` streams.
- Corrected the particle PCR/DDF parser to read GameCube big-endian offsets,
  flags, integers and floats. The old native-endian offset advanced 7168 bytes
  instead of 28 and caused the Play/file-selector heap overflow and corrupted
  effects.
- Removed an invalid downcast of the particle-list sentinel, made signed ADPCM
  expansion defined on x86_64, and initialized all MoviePlayer control state.

## Known incomplete functionality

The port is not yet feature-complete and cannot honestly be classified as
bug-free. In particular:

- Normal-based lighting, texture matrices, fog and full multi-texture TEV
  combiners are still stubs or
  partial implementations. Indexed 3D geometry now renders, but complex
  materials will not be accurate until these states are translated.
- The original JAudio sequence/bank/DSP engine remains disabled
  (`PIKI_USE_JAUDIO=0`). SDL output and `.stx` ADPCM decoding are present. The
  output backend now mixes independent STX and AI/DMA voices through an SDL
  callback, but the high-level BGM routing and `.aw` sample-bank synthesizer
  are not yet complete. The checked-in `src/jaudio/dspproc.c` also contains
  unimplemented DSP operations, so merely adding the directory back to CMake
  cannot produce audio; those operations need a native software DSP.
- Movie/video playback and a number of decompilation-era optional/debug
  methods still contain `TRAP_UNIMPLEMENTED`.
- ARAM emulation is incomplete. Extracted PC assets bypass its most dangerous
  path, but remaining APIs still use GameCube 32-bit address-shaped arguments.
- The original codebase requires `-fpermissive` and suppresses many warnings.
  A strict warning audit exposes legacy narrowing, deprecated constructs and
  several 32-bit assumptions. These need gradual type-safe conversion rather
  than blanket mechanical edits.
- Fixed little-endian construction of `ayuID` parameter tags. Previously tags
  such as `a01` and `x01` never matched their big-endian file headers, leaving
  animation paths, pellet names, camera settings, creature physics and other
  manager parameters at placeholders such as `base dir` and `noname`.
- Rebuilt the animation frame-cache layout for native pointer sizes. The old
  GameCube packing reserved four bytes per cached-matrix pointer and overlaid
  that table with matrix storage; on 64-bit Linux it corrupted bone transforms,
  disassembled animated models and eventually treated float data as a pointer.
- Corrected fourcc conversion at the `.gen` serialization boundary. The
  PowerPC-oriented helper reversed IDs such as `item`, `pint` and `1one` on a
  little-endian host, so every stage generator was read but none of its object,
  area and spawn-type components could be constructed.
- Normalised the remaining generator-local `ID32` fields (names, versions and
  pellet IDs) without changing the global `ID32` representation used by model
  and collision data.
- Replaced 32-bit pointer arithmetic throughout `GeneratorCache`; cache regions
  for generators, creatures and UFO parts now retain full native addresses on
  64-bit hosts. Aligned stream address checks likewise use `uintptr_t`.
- Replaced the same 32-bit address arithmetic in `PolyObjectMgr`. Its shared
  item pool truncated every slot address on x86_64, crashing as soon as the
  first Onion attempted to create a fulcrum/rope object.
- Corrected the route-local text/fourcc conversion without changing global
  `ID32` layout. On little-endian hosts the gameplay route `test` had become
  `tset`, so the first Onion crashed while looking up its nearest waypoint.
- Corrected the equivalent conversion at the model-collision INI boundary.
  IDs such as `leg1`, `bas1`, `piki`, `ante` and `gate` were readable as text
  but stored numerically reversed, breaking model-part lookup throughout the
  game and crashing while the first Onion constructed its legs.
- Nearest-waypoint queries now return null for empty or fully filtered route
  groups instead of indexing waypoint `-1`.
- Fully initialised `CollGroup`, including its far-cull count and link/source
  pointers. Procedurally generated map groups inherited an indeterminate
  negative far-cull count, causing ground-height queries to iterate beyond the
  triangle array during the first gameplay frame. Ground queries also validate
  serialized cull counts before using them as array bounds.
- Removed dangling `stack_new` condition pointers from creature collision and
  enemy AI. The native fallback takes the address of a temporary, but iterators
  and composite conditions keep that pointer beyond the constructing
  expression. This invalidated the virtual condition object during the second
  nearby-creature collision test (observed when approaching the first Onion).
  All gameplay condition trees now use named stack objects whose lifetime
  covers their iterator, search, or interaction.
- Replaced the Onion's six-slot 32-bit pointer overlay with three real
  `GoalLeg` entries. The decompilation treated `_444` through `_458` as an
  interleaved array of fulcrum/rope pointers; native 64-bit writes extended
  that fake array into `SeContext`, while the third rope pointer was later read
  back corrupted in `GoalItem::refresh` after the red-Onion movie.
- Canonicalised serialized pellet IDs at every pellet-specific boundary:
  configuration/model IDs from `pelMgr.bin`, shape IDs from `pelAnim.bin`, and
  explicit enemy reward IDs from `TekiPersonality`. Previously a Pellet Posy
  completed its death but `newNumberPellet` compared native `'pr01'` against
  the PowerPC-ordered numeric value and returned null. The same mismatch also
  affected other pellet colours and sizes, enemy drops, and UFO-part rewards.
  PC writes now restore the original serialized byte order.

## Reproduction commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j4
./build/bin/pikmin
```

Sanitizer build:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-sanitize -j4
./build-sanitize/bin/pikmin
```
