# Rolo Engine — root build configuration.
#
# Targets:
#   rolo_core     static library: engine lifecycle, logging, version info.
#                    Depends only on the C++ standard library.
#   rolo_ui       static library: Rolo's own UI toolkit (widgets, docking,
#                    text). No backend; always built and unit-tested.
#   rolo_content  static library: RPG content (variables, dialogues, quests,
#                    conditions, actions), checks, progress and save files.
#   rolo_gameplay static library: behaviours, input, collision queries,
#                    scene changes (no backend; always built and tested).
#   sample_game      the sample game module (examples/sample_game).
#   keeper           The Lantern Keeper, the complete demo game (examples/lantern_keeper).
#   rolo_runtime  static library: window, main loop, rendering backend.
#                    Wraps Axmol privately (only with ROLO_RUNTIME_BACKEND=axmol).
#   rolo_player   the game runtime application (opens a window).
#   rolo_editor_core  editor logic without UI code (always built, unit-tested).
#   rolo_editor   the standalone editor application (built with rolo_ui).
#   rolo_tests    Catch2 unit tests (registered with CTest).
#
# Backend background: docs/adr/0001-runtime-foundation.md

cmake_minimum_required(VERSION 3.25)

project(Rolo
    VERSION 0.9.1
    DESCRIPTION "Rolo Engine: a 2D game engine and editor"
    LANGUAGES C CXX)
# Pre-release label shown after the version ("0.9.0-alpha"); empty for a release.
set(ROLO_VERSION_SUFFIX "-alpha")

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # lets editors/clangd understand the code

# Put our executables in one folder so they are easy to find after a build.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(RoloCompilerWarnings)
include(RoloGameModules)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(ROLO_BUILD_EDITOR "Build the standalone editor" ON)
option(ROLO_BUILD_TESTS "Build unit tests" ON)
option(ROLO_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(ROLO_ENABLE_SANITIZERS "Build with AddressSanitizer + UBSan (Clang/GCC)" OFF)
option(ROLO_WINDOW_TESTS "Register tests that open a real window (needs a display)" ON)
option(ROLO_BUILD_SAMPLE_GAME "Build the example game modules (sample_game, keeper) and link them into the editor and player" ON)
# Folders with your own game modules (each has a CMakeLists.txt calling
# rolo_add_game_module); they are linked into the editor and the player.
set(ROLO_GAME_MODULE_DIRS "" CACHE STRING "Folders containing game modules to build in (separated by ;)")

# Which rendering/runtime backend to link:
#   axmol  the real engine runtime (default)
#   none   core library, editor shell and tests only; no window, very fast to build
set(ROLO_RUNTIME_BACKEND "axmol" CACHE STRING "Runtime backend to link")
set_property(CACHE ROLO_RUNTIME_BACKEND PROPERTY STRINGS axmol none)
if(NOT ROLO_RUNTIME_BACKEND MATCHES "^(axmol|none)$")
    message(FATAL_ERROR "ROLO_RUNTIME_BACKEND must be 'axmol' or 'none', not '${ROLO_RUNTIME_BACKEND}'.")
endif()

if(ROLO_ENABLE_SANITIZERS AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# Third-party backend
# ---------------------------------------------------------------------------
if(ROLO_RUNTIME_BACKEND STREQUAL "axmol")
    if(APPLE)
        enable_language(OBJC OBJCXX)   # Axmol's macOS platform layer is Objective-C++
    endif()
    include(RoloAxmol)
    set(ROLO_BACKEND_DISPLAY_NAME "Axmol ${ROLO_AXMOL_VERSION}")
else()
    set(ROLO_BACKEND_DISPLAY_NAME "none")
endif()

# ---------------------------------------------------------------------------
# Our modules
# ---------------------------------------------------------------------------
add_subdirectory(third_party/luau)   # the Luau language, for gameplay scripts (ADR 0020)
add_subdirectory(third_party/miniaudio)   # playing sounds (ADR 0015)
add_subdirectory(third_party/box2d)   # 2D physics (ADR 0016)
add_subdirectory(engine/core)
add_subdirectory(engine/scene)
add_subdirectory(engine/assets)
add_subdirectory(engine/content)
add_subdirectory(engine/physics)
add_subdirectory(engine/gameplay)
add_subdirectory(engine/audio)
add_subdirectory(engine/ui)

# Game modules come before the programs that link them.
if(ROLO_BUILD_SAMPLE_GAME)
    add_subdirectory(examples/sample_game)
    add_subdirectory(examples/lantern_keeper)
endif()
foreach(dir IN LISTS ROLO_GAME_MODULE_DIRS)
    get_filename_component(_name "${dir}" NAME)
    add_subdirectory("${dir}" "${CMAKE_BINARY_DIR}/game_modules/${_name}")
endforeach()

add_subdirectory(tools/stress)   # rolo_stress: gameplay frame times (examples/stress)

if(ROLO_RUNTIME_BACKEND STREQUAL "axmol")
    add_subdirectory(engine/runtime)
    add_subdirectory(player)
endif()

if(ROLO_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

if(ROLO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(RoloInstall)   # install rules and the rolo_package target

message(STATUS "Rolo ${PROJECT_VERSION}: backend=${ROLO_BACKEND_DISPLAY_NAME} "
               "editor=${ROLO_BUILD_EDITOR} tests=${ROLO_BUILD_TESTS}")
