cmake_minimum_required(VERSION 3.20)
project(slipstream LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# --- core: original file parsers + game data model (no SDL, no rendering) -----------------
add_library(slipstream_core STATIC
  src/original_formats/game_data.cpp
  src/original_formats/formats.cpp
  src/original_formats/track.cpp
  src/game/scene.cpp
  src/game/ship_params.cpp
  src/game/panel_detail.cpp
  src/game/ship_sim.cpp
  src/game/ship_collide.cpp
  src/game/doors.cpp
  src/game/ship_ai.cpp)
target_include_directories(slipstream_core PUBLIC src)
target_compile_options(slipstream_core PRIVATE -Wall -Wextra)

# --- renderer: software "compatibility" renderer ------------------------------------------
add_library(slipstream_renderer STATIC src/renderer/software_renderer.cpp)
target_link_libraries(slipstream_renderer PUBLIC slipstream_core)
target_compile_options(slipstream_renderer PRIVATE -Wall -Wextra)

# --- application (SDL3) -------------------------------------------------------------------
find_package(SDL3 REQUIRED CONFIG)
add_executable(slipstream src/platform/main.cpp src/platform/viewer_app.cpp)
target_link_libraries(slipstream PRIVATE slipstream_renderer slipstream_core SDL3::SDL3)
target_compile_options(slipstream PRIVATE -Wall -Wextra)

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
