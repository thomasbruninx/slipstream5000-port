cmake_minimum_required(VERSION 3.20)
project(slipstream LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(WIN32)
  add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX _USE_MATH_DEFINES)
endif()

# SDL3: the installed one (brew / apt / MSYS2 / vcpkg), or -DSLIP_FETCH_SDL3=ON to download and build it (static) - used by the Windows cross build
option(SLIP_FETCH_SDL3 "download and build SDL3 instead of using an installed one" OFF)
set(SLIP_SDL3_VERSION 3.4.16 CACHE STRING "SDL3 release used by SLIP_FETCH_SDL3")
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# --- core: original file parsers + game data model (no SDL, no rendering) -----------------
add_library(slipstream_core STATIC
  src/original_formats/game_data.cpp
  src/original_formats/formats.cpp
  src/original_formats/audio.cpp
  src/original_formats/gdv.cpp
  src/original_formats/ann.cpp
  src/original_formats/track.cpp
  src/game/scene.cpp
  src/game/ship_params.cpp
  src/game/panel_detail.cpp
  src/game/ship_sim.cpp
  src/game/ship_collide.cpp
  src/game/doors.cpp
  src/game/ship_ai.cpp
  src/game/weapons.cpp
  src/game/drones.cpp
  src/game/hud.cpp
  src/game/pause_menu.cpp
  src/game/settings_file.cpp
  src/game/globe.cpp
  src/game/championship.cpp
  src/game/frontend.cpp
  src/game/frontend_champ.cpp
  src/game/frontend_reporters.cpp
  src/game/frontend_config.cpp)
target_include_directories(slipstream_core PUBLIC src)

# --- net: POSIX sockets, framed transport, wire protocol, LAN discovery (SDL-free) --------------------
add_library(slipstream_net STATIC src/net/socket.cpp src/net/transport.cpp src/net/protocol.cpp src/net/discovery.cpp src/net/session.cpp)
target_include_directories(slipstream_net PUBLIC src)
if(WIN32)
  target_link_libraries(slipstream_net PUBLIC ws2_32 iphlpapi)
endif()

# game side of the multiplayer mode (snapshots, interpolation, combat relay)
add_library(slipstream_netplay STATIC src/game/netplay.cpp)
target_link_libraries(slipstream_netplay PUBLIC slipstream_core slipstream_net)

# --- renderer: software "compatibility" renderer ------------------------------------------
add_library(slipstream_renderer STATIC src/renderer/software_renderer.cpp)
target_link_libraries(slipstream_renderer PUBLIC slipstream_core)

# --- audio: sample mixer + FluidSynth music + SDL3 playback ---------------------------------------
if(SLIP_FETCH_SDL3)
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3 URL https://github.com/libsdl-org/SDL/releases/download/release-${SLIP_SDL3_VERSION}/SDL3-${SLIP_SDL3_VERSION}.tar.gz)
  FetchContent_MakeAvailable(SDL3)
else()
  find_package(SDL3 CONFIG)
  if(NOT SDL3_FOUND)
    message(STATUS "SDL3 is not installed: downloading and building it (set SLIP_FETCH_SDL3=ON to always do this)")
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3 URL https://github.com/libsdl-org/SDL/releases/download/release-${SLIP_SDL3_VERSION}/SDL3-${SLIP_SDL3_VERSION}.tar.gz)
    FetchContent_MakeAvailable(SDL3)
  endif()
endif()
# -Wall -Wextra for GCC / Clang (MSVC is not a supported compiler: use MinGW-w64 / clang on Windows, see docs/building.md)
add_compile_options($<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall> $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>)
find_package(PkgConfig QUIET)
set(SLIP_FLUIDSYNTH_FOUND OFF)
if(PkgConfig_FOUND AND NOT CMAKE_CROSSCOMPILING)  # a cross build must not pick up the host's libraries
  pkg_check_modules(FLUIDSYNTH IMPORTED_TARGET fluidsynth)
  if(FLUIDSYNTH_FOUND)
    set(SLIP_FLUIDSYNTH_FOUND ON)
  endif()
endif()
add_library(slipstream_audio STATIC src/audio/mixer.cpp src/audio/music_player.cpp src/audio/audio_system.cpp)
target_link_libraries(slipstream_audio PUBLIC slipstream_core SDL3::SDL3)
target_compile_definitions(slipstream_audio PRIVATE SLIP_SOURCE_RESOURCE_DIR="${CMAKE_SOURCE_DIR}/resources")
if(SLIP_FLUIDSYNTH_FOUND)
  target_link_libraries(slipstream_audio PUBLIC PkgConfig::FLUIDSYNTH)
  target_compile_definitions(slipstream_audio PUBLIC SLIP_HAVE_FLUIDSYNTH=1)
  message(STATUS "FluidSynth found: MIDI music enabled")
else()
  message(WARNING "FluidSynth not found: the game is built without MIDI music (brew install fluid-synth / apt install libfluidsynth-dev / MSYS2 mingw-w64-x86_64-fluidsynth)")
endif()

# --- application (SDL3) -------------------------------------------------------------------
add_executable(slipstream src/platform/main.cpp src/platform/viewer_app.cpp src/platform/viewer_intro.cpp)
target_link_libraries(slipstream PRIVATE slipstream_netplay slipstream_renderer slipstream_audio slipstream_core SDL3::SDL3)

# --- tools / tests (need the user's original game files; skipped when SLIPSTREAM_DATA unset) -
add_executable(slipstream_inspect tools/inspect/inspect.cpp)
target_link_libraries(slipstream_inspect PRIVATE slipstream_core)

enable_testing()
add_executable(slipstream_tests tests/format_tests.cpp)
target_link_libraries(slipstream_tests PRIVATE slipstream_core)
add_test(NAME format_tests COMMAND slipstream_tests)

add_executable(slipstream_physics_tests tests/physics_tests.cpp)
target_link_libraries(slipstream_physics_tests PRIVATE slipstream_core)
add_test(NAME physics_tests COMMAND slipstream_physics_tests)

add_executable(slipstream_weapons_tests tests/weapons_tests.cpp)
target_link_libraries(slipstream_weapons_tests PRIVATE slipstream_core)
add_test(NAME weapons_tests COMMAND slipstream_weapons_tests)

add_executable(slipstream_champ_tests tests/champ_tests.cpp)
target_link_libraries(slipstream_champ_tests PRIVATE slipstream_core)
add_test(NAME champ_tests COMMAND slipstream_champ_tests)

add_executable(slipstream_net_tests tests/net_tests.cpp)
target_link_libraries(slipstream_net_tests PRIVATE slipstream_net)
add_test(NAME net_tests COMMAND slipstream_net_tests)

add_executable(slipstream_gdv_tests tests/gdv_tests.cpp)
target_link_libraries(slipstream_gdv_tests PRIVATE slipstream_core)
add_test(NAME gdv_tests COMMAND slipstream_gdv_tests)

add_executable(slipstream_ann_tests tests/ann_tests.cpp)
target_link_libraries(slipstream_ann_tests PRIVATE slipstream_core)
add_test(NAME ann_tests COMMAND slipstream_ann_tests)

add_executable(slipstream_audio_tests tests/audio_tests.cpp)
target_link_libraries(slipstream_audio_tests PRIVATE slipstream_audio slipstream_core)
add_test(NAME audio_tests COMMAND slipstream_audio_tests)

# offline renderer: music / effects to a WAV file (no sound device needed)
add_executable(slipstream_audio_dump tools/audio_dump/audio_dump.cpp)
target_link_libraries(slipstream_audio_dump PRIVATE slipstream_audio slipstream_core)
