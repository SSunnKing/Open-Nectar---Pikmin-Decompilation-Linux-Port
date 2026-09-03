###############################################################################
#  Pikmin PC Port — CMake Build System
#  Stage 1: Compilation Skeleton
#
#  This CMakeLists.txt compiles the decompiled Pikmin source code natively
#  for Linux x86_64, replacing all GameCube Dolphin SDK calls with our stubs.
#
#  Usage:
#    mkdir build && cd build
#    cmake ..
#    make -j$(nproc)
###############################################################################
cmake_minimum_required(VERSION 3.16)
project(PikminPC VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

# ─── Compiler Flags ───
# Force-include our PC types header BEFORE anything else.
# This overrides MetroWerks and MSVC macros, fixes type sizes, etc.
set(PC_PORT_DIR "${CMAKE_SOURCE_DIR}/pc_port")

# Legacy code (decompiled GameCube sources) needs permissive compilation.
# Modern GCC would reject many Metrowerks-era constructs.
set(LEGACY_COMPILE_OPTIONS
    -include "${PC_PORT_DIR}/pc_types.h"
    -fpermissive
    -Wno-narrowing
    -Wno-multichar
    -Wno-write-strings
    -Wno-sign-compare
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-parentheses
    -Wno-delete-non-virtual-dtor
    -Wno-reorder
    -Wno-switch
    -Wno-format
    -Wno-attributes
    -Wno-comment
)

# Native code (pc_port, tests, launcher) should compile with strict warnings.
# We keep -fpermissive because native code includes legacy Dolphin headers.
set(NATIVE_COMPILE_OPTIONS
    -include "${PC_PORT_DIR}/pc_types.h"
    -fpermissive
    -Wall
    -Wextra
    -Wno-changes-meaning
    -Wno-narrowing
    -Wno-multichar
    -Wno-write-strings
    -Wno-sign-compare
    -Wno-unused-variable
    -Wno-unused-but-set-variable
    -Wno-parentheses
    -Wno-delete-non-virtual-dtor
    -Wno-reorder
    -Wno-attributes
    -Wno-comment
    -Werror=return-type
    -Werror=switch
)

# ─── Version define for USA Rev 1 ───
add_compile_definitions(
    PIKI_PC_PORT=1
    VERSION_GPIE01_01
    DTK_CONFIG_NONMATCHING=1
    # The Linux renderer translates the game's native GX command stream to
    # OpenGL.  Selecting the old Win32/OpenGL code path here mixes two
    # incompatible texture/rendering backends and leaves GXTexObj uninitialised.
    PIKI_USE_DGX=1
    PIKI_USE_JAUDIO=0
    # Some SDL2 builds (including current Arch configurations) remap main to
    # SDL_main unless the application explicitly owns its entry point.
    SDL_MAIN_HANDLED=1
    # In-game settings menu (F1). Remove this to disable the overlay entirely.
    PIKI_PC_SETTINGS_MENU=1
)

# ─── Include paths ───
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/Dolphin
    ${CMAKE_SOURCE_DIR}/include/Dolphin/OS
    ${CMAKE_SOURCE_DIR}/include/Dolphin/GX
    ${PC_PORT_DIR}
    ${PC_PORT_DIR}/gl
)

###############################################################################
#  Source file lists
#  We compile only the game logic modules, NOT the original Dolphin SDK
#  implementations (those are replaced by our stubs).
###############################################################################

# ─── External Dependencies (SDL2 & OpenGL) ───
find_package(SDL2 QUIET)
if (NOT SDL2_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
endif()

find_package(OpenGL REQUIRED)

set(PC_PORT_SOURCES
    pc_port/pc_main.cpp
    pc_port/pc_window.cpp
    pc_port/gl/pc_gfx.cpp
    pc_port/gl/pc_tev_shader.cpp
    pc_port/timing/pc_frame_scheduler.cpp
    pc_port/timing/pc_render_phase.cpp
    pc_port/timing/pc_visual_snapshot.cpp
    pc_port/timing/pc_camera_snapshot.cpp
    pc_port/timing/pc_visual_runtime.cpp
    pc_port/timing/pc_render_packet.cpp
    pc_port/timing/pc_tick_profiler.cpp
    pc_port/audio/pc_audio.cpp
    pc_port/audio/pc_envelope.cpp
    pc_port/audio/pc_jam.cpp
    pc_port/audio/pc_instrument_bank.cpp
    pc_port/audio/pc_wave_bank.cpp
    pc_port/audio/pc_sequence_archive.cpp
    pc_port/settings/pc_settings.cpp
    src/jaudio/pikiseq.c
    pc_port/dolphin_stubs/os_stubs.cpp
    pc_port/dolphin_stubs/gx_stubs.cpp
    pc_port/dolphin_stubs/dvd_stubs.cpp
    pc_port/dolphin_stubs/pad_stubs.cpp
    pc_port/dolphin_stubs/vi_stubs.cpp
    pc_port/dolphin_stubs/card_stubs.cpp
    pc_port/dolphin_stubs/audio_stubs.cpp
    pc_port/dolphin_stubs/misc_stubs.cpp
)

include_directories(
    ${SDL2_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
)

# ─── System Common (platform-independent game engine) ───
file(GLOB SYSCOMMON_SOURCES "src/sysCommon/*.cpp")

# ─── System Dolphin (GameCube-specific system layer — we compile it but stubs handle the calls) ───
set(SYSDOLPHIN_SOURCES
    src/sysDolphin/controllerMgr.cpp
    src/sysDolphin/dgxGraphics.cpp
    src/sysDolphin/gameApp.cpp
    src/sysDolphin/sysNew.cpp
    src/sysDolphin/system.cpp
    src/sysDolphin/texture.cpp
)

# ─── JAudio library (GameCube audio system) ───
file(GLOB JAUDIO_SOURCES "src/jaudio/*.c")

# ─── Game plugin modules (the actual game code) ───
file(GLOB PLUGPIKI_KANDO_SOURCES   "src/plugPikiKando/*.cpp")
file(GLOB PLUGPIKI_NAKATA_SOURCES  "src/plugPikiNakata/*.cpp")
file(GLOB PLUGPIKI_NISHIMURA_SOURCES "src/plugPikiNishimura/*.cpp")
file(GLOB PLUGPIKI_COLIN_SOURCES   "src/plugPikiColin/*.cpp")
file(GLOB PLUGPIKI_OGAWA_SOURCES   "src/plugPikiOgawa/*.cpp")
file(GLOB PLUGPIKI_YAMASHITA_SOURCES "src/plugPikiYamashita/*.cpp")

###############################################################################
#  Files we EXCLUDE from compilation
#  These are GameCube hardware drivers that our stubs replace entirely.
###############################################################################
# We do NOT compile:
#   src/os/          — Dolphin OS implementation (replaced by os_stubs)
#   src/gx/          — GX hardware driver (replaced by gx_stubs)
#   src/dvd/         — DVD hardware driver (replaced by dvd_stubs)
#   src/pad/         — PAD hardware driver (replaced by pad_stubs)
#   src/vi/          — VI hardware driver (replaced by vi_stubs)
#   src/card/        — Memory card driver (replaced by card_stubs)
#   src/dsp/         — DSP driver (replaced by audio_stubs)
#   src/ai/          — Audio interface (replaced by audio_stubs)
#   src/ar/          — ARAM driver (replaced by os_stubs)
#   src/si/          — Serial interface (replaced by misc_stubs)
#   src/exi/         — Expansion interface (replaced by misc_stubs)
#   src/jaudio/      — JAudio library (replaced by audio_stubs)
#   src/mtx/         — Matrix math (replaced by misc_stubs)
#   src/mtxD/        — Matrix math (replaced by misc_stubs)
#   src/base/        — PPCArch (replaced by misc_stubs)
#   src/db/          — Debug (replaced by misc_stubs)
#   src/hio/         — Host I/O (replaced by misc_stubs)
#   src/hvqm4dec/    — Video decoder (not needed yet)
#   src/MSL_C/       — MetroWerks Standard Library (use system libc)
#   src/Runtime/     — CodeWarrior Runtime (use system runtime)
#   src/TRK_MINNOW_DOLPHIN/ — Debugger (not needed)
#   src/OdemuExi2/   — Dev kit hardware (not needed)
#   src/amcExi2/     — Dev kit hardware (not needed)
#   src/amcnotstub/  — Dev kit hardware (not needed)
#   src/amcstubs/    — Dev kit hardware (not needed)
#   src/odemustubs/  — Dev kit hardware (not needed)
#   src/odenotstub/  — Dev kit hardware (not needed)
#   src/sysCore/     — Windows DLL code (uses Win32 APIs, will adapt later)
#   src/sysBootup.cpp — Original entry point (replaced by pc_main.cpp)

###############################################################################
#  Build the executable
###############################################################################
# Create a static library for legacy (decompiled) sources with permissive flags.
# This isolates the legacy compilation from the strict native compilation.
add_library(pikmin_legacy STATIC
    ${SYSCOMMON_SOURCES}
    ${SYSDOLPHIN_SOURCES}
    ${PLUGPIKI_KANDO_SOURCES}
    ${PLUGPIKI_NAKATA_SOURCES}
    ${PLUGPIKI_NISHIMURA_SOURCES}
    ${PLUGPIKI_COLIN_SOURCES}
    ${PLUGPIKI_OGAWA_SOURCES}
    ${PLUGPIKI_YAMASHITA_SOURCES}
)
target_compile_options(pikmin_legacy PRIVATE ${LEGACY_COMPILE_OPTIONS})

# Native executable with strict flags
add_executable(pikmin_pc
    ${PC_PORT_SOURCES}
)
target_compile_options(pikmin_pc PRIVATE ${NATIVE_COMPILE_OPTIONS})
target_link_libraries(pikmin_pc PRIVATE pikmin_legacy)

# GX display lists, game logic and the software audio mixer execute on the host
# CPU. Keep optimization level, machine-specific ISA and link-time optimization
# independent so release archives can remain portable.
target_compile_options(pikmin_pc PRIVATE
    $<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:C,GNU,Clang>>:-O3>
    $<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>>:-O3>
    $<$<AND:$<CONFIG:RelWithDebInfo>,$<COMPILE_LANG_AND_ID:C,GNU,Clang>>:-O3>
    $<$<AND:$<CONFIG:RelWithDebInfo>,$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>>:-O3>)

option(PIKMIN_NATIVE_OPTIMIZE "Use this build machine's CPU instruction set" ON)
if(PIKMIN_NATIVE_OPTIMIZE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(pikmin_pc PRIVATE -march=native)
endif()

option(PIKMIN_ENABLE_IPO "Enable portable interprocedural/link-time optimization" ON)
if(PIKMIN_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT PIKMIN_IPO_SUPPORTED OUTPUT PIKMIN_IPO_ERROR)
    if(PIKMIN_IPO_SUPPORTED)
        set_property(TARGET pikmin_pc PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "IPO/LTO unavailable: ${PIKMIN_IPO_ERROR}")
    endif()
endif()

# Link against math, SDL2, and OpenGL libraries
target_link_libraries(pikmin_pc PRIVATE m ${SDL2_LIBRARIES} OpenGL::GL)
# Export symbol names so the diagnostic backtrace in pc_gfx.cpp prints function
# names rather than bare addresses. Costs nothing at runtime.
target_link_options(pikmin_pc PRIVATE -rdynamic)

# Set output name
set_target_properties(pikmin_pc PROPERTIES
    OUTPUT_NAME "pikmin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_custom_command(TARGET pikmin_pc POST_BUILD
    COMMAND /bin/chmod 775 "$<TARGET_FILE:pikmin_pc>"
    COMMENT "Ensuring the native Pikmin executable is runnable")

# Standalone first-run launcher. It accepts an original, uncompressed
# GameCube ISO/GCM supplied by the user, extracts its FST into the per-user
# data directory and then starts the game from there. Keeping the extractor in
# a small library also lets CI test the legal install path without game assets.
add_library(pikmin_disc_image STATIC
    pc_port/launcher/gamecube_image.cpp)
target_include_directories(pikmin_disc_image PUBLIC
    ${CMAKE_SOURCE_DIR}/pc_port/launcher)
target_compile_options(pikmin_disc_image PRIVATE ${NATIVE_COMPILE_OPTIONS})

add_executable(pikmin_launcher
    pc_port/launcher/launcher_main.cpp
    pc_port/launcher/installer_ui.cpp)
target_include_directories(pikmin_launcher PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(pikmin_launcher PRIVATE pikmin_disc_image ${SDL2_LIBRARIES})
target_compile_options(pikmin_launcher PRIVATE ${NATIVE_COMPILE_OPTIONS})
set_target_properties(pikmin_launcher PROPERTIES
    OUTPUT_NAME "pikmin-launcher"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(gamecube_image_test
    pc_port/launcher/gamecube_image_test.cpp)
target_link_libraries(gamecube_image_test PRIVATE pikmin_disc_image)
target_compile_options(gamecube_image_test PRIVATE ${NATIVE_COMPILE_OPTIONS})

add_executable(installer_ui_test
    pc_port/launcher/installer_ui_test.cpp
    pc_port/launcher/installer_ui.cpp)
target_include_directories(installer_ui_test PRIVATE
    ${CMAKE_SOURCE_DIR}/pc_port/launcher
    ${SDL2_INCLUDE_DIRS})
target_link_libraries(installer_ui_test PRIVATE ${SDL2_LIBRARIES})
target_compile_options(installer_ui_test PRIVATE ${NATIVE_COMPILE_OPTIONS})

enable_testing()
add_test(NAME gamecube_image_test COMMAND gamecube_image_test)
add_test(NAME installer_ui_test COMMAND installer_ui_test)

add_executable(pc_generator_cache_validation_test
    pc_port/save/pc_generator_cache_validation_test.cpp)
target_include_directories(pc_generator_cache_validation_test PRIVATE pc_port/save)
target_compile_options(pc_generator_cache_validation_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
add_test(NAME pc_generator_cache_validation_test COMMAND pc_generator_cache_validation_test)

# Both executables must remain side by side: the launcher resolves the game
# binary relative to itself, so this also works in relocatable archives.
install(TARGETS pikmin_pc pikmin_launcher
    RUNTIME DESTINATION bin)

# Envelope smoke test executable (not linked into main game)
add_executable(pc_envelope_test pc_port/audio/pc_envelope_test.cpp pc_port/audio/pc_envelope.cpp)
target_link_libraries(pc_envelope_test PRIVATE m)
target_compile_options(pc_envelope_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
add_test(NAME pc_envelope_test COMMAND pc_envelope_test)

add_executable(pc_jam_test
    pc_port/audio/pc_jam_test.cpp
    pc_port/audio/pc_jam.cpp
    pc_port/audio/pc_instrument_bank.cpp
    pc_port/audio/pc_sequence_archive.cpp
    pc_port/audio/pc_wave_bank.cpp
    src/jaudio/pikiseq.c)
target_compile_options(pc_jam_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/dataDir/SndData/Banks/pikibank.bx")
    add_test(NAME pc_jam_test COMMAND pc_jam_test)
    set_tests_properties(pc_jam_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

add_executable(pc_audio_mixer_test
    pc_port/audio/pc_audio_mixer_test.cpp
    pc_port/audio/pc_audio.cpp
    pc_port/audio/pc_envelope.cpp
    pc_port/audio/pc_jam.cpp
    pc_port/audio/pc_instrument_bank.cpp
    pc_port/audio/pc_sequence_archive.cpp
    pc_port/audio/pc_wave_bank.cpp
    src/jaudio/pikiseq.c)
target_link_libraries(pc_audio_mixer_test PRIVATE m ${SDL2_LIBRARIES})
target_compile_options(pc_audio_mixer_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/dataDir/SndData/Banks/pikibank.bx")
    add_test(NAME pc_audio_mixer_test COMMAND pc_audio_mixer_test)
    set_tests_properties(pc_audio_mixer_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

add_executable(pc_frame_scheduler_test
    pc_port/timing/pc_frame_scheduler_test.cpp
    pc_port/timing/pc_frame_scheduler.cpp)
target_compile_options(pc_frame_scheduler_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
add_test(NAME pc_frame_scheduler_test COMMAND pc_frame_scheduler_test)

add_executable(pc_render_phase_test
    pc_port/timing/pc_render_phase_test.cpp
    pc_port/timing/pc_render_phase.cpp)
target_compile_options(pc_render_phase_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
add_test(NAME pc_render_phase_test COMMAND pc_render_phase_test)

add_executable(pc_visual_snapshot_test
    pc_port/timing/pc_visual_snapshot_test.cpp
    pc_port/timing/pc_visual_snapshot.cpp)
target_compile_options(pc_visual_snapshot_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
add_test(NAME pc_visual_snapshot_test COMMAND pc_visual_snapshot_test)

add_executable(pc_camera_snapshot_test
    pc_port/timing/pc_camera_snapshot_test.cpp
    pc_port/timing/pc_camera_snapshot.cpp)
target_compile_options(pc_camera_snapshot_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
add_test(NAME pc_camera_snapshot_test COMMAND pc_camera_snapshot_test)

add_executable(pc_visual_runtime_test
    pc_port/timing/pc_visual_runtime_test.cpp
    pc_port/timing/pc_visual_runtime.cpp
    pc_port/timing/pc_render_phase.cpp
    pc_port/timing/pc_visual_snapshot.cpp)
target_compile_options(pc_visual_runtime_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
add_test(NAME pc_visual_runtime_test COMMAND pc_visual_runtime_test)

add_executable(pc_tev_shader_test
    pc_port/gl/pc_tev_shader_test.cpp
    pc_port/gl/pc_tev_shader.cpp
)
target_include_directories(pc_tev_shader_test PRIVATE pc_port/gl)
target_compile_options(pc_tev_shader_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
add_test(NAME pc_tev_shader_test COMMAND pc_tev_shader_test)

add_executable(pc_render_packet_test
    pc_port/timing/pc_render_packet_test.cpp
    pc_port/timing/pc_render_packet.cpp)
target_compile_options(pc_render_packet_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
add_test(NAME pc_render_packet_test COMMAND pc_render_packet_test)

add_executable(pc_tick_profiler_test
    pc_port/timing/pc_tick_profiler_test.cpp
    pc_port/timing/pc_tick_profiler.cpp)
target_include_directories(pc_tick_profiler_test PRIVATE pc_port/timing)
target_compile_options(pc_tick_profiler_test PRIVATE ${NATIVE_COMPILE_OPTIONS})
add_test(NAME pc_tick_profiler_test COMMAND pc_tick_profiler_test)

message(STATUS "")
message(STATUS "╔══════════════════════════════════════════╗")
message(STATUS "║   Pikmin PC Port - Build Configuration   ║")
message(STATUS "╠══════════════════════════════════════════╣")
message(STATUS "║   Version:  USA Rev 1 (GPIE01_01)       ║")
message(STATUS "║   Graphics: Native GX → OpenGL          ║")
message(STATUS "║   Audio:    SDL mixer/STX; JAudio WIP   ║")
message(STATUS "║   Input:    Native SDL keyboard/gamepad ║")
message(STATUS "╚══════════════════════════════════════════╝")
message(STATUS "")
