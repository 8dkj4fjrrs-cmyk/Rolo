# RoloAxmol.cmake — brings the pinned Axmol engine into the build.
#
# What this does, in order:
#   1. Checks the tools Axmol needs (git, PowerShell).
#   2. Downloads Axmol's source at an exact, verified commit into external/axmol
#      (only the first time; the folder is reused by every build preset).
#   3. Applies Rolo's small patches to Axmol (cmake/axmol-patches/) and
#      removes yasio, which Rolo replaces (see step 6).
#   4. Downloads Axmol's shader compiler (axslcc) for this computer and checks
#      its SHA-256, so a corrupted or tampered download is rejected.
#   5. Switches off Axmol features Rolo does not use (faster builds, fewer
#      third-party licenses in every game).
#   6. Adds Axmol's core library as the CMake target named in ${_AX_CORE_LIB},
#      with Rolo's replacement for the yasio headers it includes.
#
# Nothing outside engine/runtime and player/ may use Axmol directly.
# See docs/adr/0001-runtime-foundation.md.

# ---------------------------------------------------------------------------
# Pinned versions — change only together with a docs/ADR update.
# ---------------------------------------------------------------------------
set(ROLO_AXMOL_VERSION "2.11.5")
set(ROLO_AXMOL_TAG "v2.11.5")
set(ROLO_AXMOL_COMMIT "6c0dce6db4660feafe29dabb1ab3506ee76b0931")
set(ROLO_AXMOL_REPOSITORY "https://github.com/axmolengine/axmol.git")

set(ROLO_AXSLCC_VERSION "1.14.0")

set(ROLO_EXTERNAL_DIR "${PROJECT_SOURCE_DIR}/external" CACHE PATH
    "Where downloaded third-party engines and tools are kept (shared by all build presets)")
set(ROLO_AXMOL_DIR "${ROLO_EXTERNAL_DIR}/axmol")

# ---------------------------------------------------------------------------
# 1. Required tools
# ---------------------------------------------------------------------------
find_package(Git REQUIRED)

find_program(ROLO_PWSH_EXECUTABLE NAMES pwsh)
if(NOT ROLO_PWSH_EXECUTABLE)
    if(APPLE)
        set(_pwsh_hint "Install it with:  brew install powershell")
    elseif(WIN32)
        set(_pwsh_hint "Install it with:  winget install Microsoft.PowerShell")
    else()
        set(_pwsh_hint "See https://learn.microsoft.com/powershell/scripting/install/installing-powershell-on-linux")
    endif()
    message(FATAL_ERROR
        "PowerShell 7 ('pwsh') was not found. Axmol uses it to download some of "
        "its prebuilt libraries.\n${_pwsh_hint}\n"
        "Then open a new terminal and configure again.\n"
        "(To build only the core library and tests without a window, use: "
        "cmake --preset core)")
endif()

# ---------------------------------------------------------------------------
# 2. Axmol source at the pinned commit
# ---------------------------------------------------------------------------
if(NOT EXISTS "${ROLO_AXMOL_DIR}/core/CMakeLists.txt")
    message(STATUS "Downloading Axmol ${ROLO_AXMOL_TAG} (about 130 MB, first time only)...")
    file(MAKE_DIRECTORY "${ROLO_EXTERNAL_DIR}")
    execute_process(
        # core.autocrlf=false keeps Unix line endings on Windows too, so the
        # patches in cmake/axmol-patches apply everywhere.
        COMMAND "${GIT_EXECUTABLE}" -c advice.detachedHead=false clone --depth 1
                --config core.autocrlf=false
                --branch "${ROLO_AXMOL_TAG}" "${ROLO_AXMOL_REPOSITORY}" "${ROLO_AXMOL_DIR}"
        RESULT_VARIABLE _clone_result)
    if(NOT _clone_result EQUAL 0)
        file(REMOVE_RECURSE "${ROLO_AXMOL_DIR}")
        message(FATAL_ERROR "Could not download Axmol. Check your internet connection and try again.")
    endif()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${ROLO_AXMOL_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE _axmol_head
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _rev_result)
if(NOT _rev_result EQUAL 0 OR NOT _axmol_head STREQUAL ROLO_AXMOL_COMMIT)
    message(FATAL_ERROR
        "external/axmol is not at the pinned commit.\n"
        "  expected: ${ROLO_AXMOL_COMMIT}\n"
        "  found:    ${_axmol_head}\n"
        "Delete the folder ${ROLO_AXMOL_DIR} and configure again to re-download it.")
endif()
message(STATUS "Axmol ${ROLO_AXMOL_VERSION} at ${_axmol_head}")

# ---------------------------------------------------------------------------
# 3. Rolo's patches to Axmol, and removing yasio (ADR 0012)
#
# Each patch is applied once: if it can already be reversed, it is in place.
# The patches only add switches (network module, yasio) and fix one line that
# needed a header from the networking code; see cmake/axmol-patches/README.md.
# ---------------------------------------------------------------------------
file(GLOB _rolo_axmol_patches "${CMAKE_CURRENT_LIST_DIR}/axmol-patches/*.patch")
list(SORT _rolo_axmol_patches)
foreach(_patch IN LISTS _rolo_axmol_patches)
    get_filename_component(_patch_name "${_patch}" NAME)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${ROLO_AXMOL_DIR}" apply --reverse --check "${_patch}"
        RESULT_VARIABLE _already_applied OUTPUT_QUIET ERROR_QUIET)
    if(_already_applied EQUAL 0)
        continue()
    endif()
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${ROLO_AXMOL_DIR}" apply "${_patch}"
        RESULT_VARIABLE _apply_result
        ERROR_VARIABLE _apply_error)
    if(NOT _apply_result EQUAL 0)
        message(FATAL_ERROR
            "Could not apply Rolo's Axmol patch ${_patch_name}:\n${_apply_error}\n"
            "external/axmol may have been changed by hand. Delete the folder "
            "${ROLO_AXMOL_DIR} and configure again to re-download it.")
    endif()
    message(STATUS "Applied Axmol patch ${_patch_name}")
endforeach()

# yasio's license adds terms to MIT that Rolo does not want in every game
# (ADR 0012). Rolo does not build it: engine/runtime/axmol_compat provides
# the few headers Axmol's core still includes. Deleting the folder makes sure
# nothing compiles or includes the original by accident.
if(EXISTS "${ROLO_AXMOL_DIR}/3rdparty/yasio")
    file(REMOVE_RECURSE "${ROLO_AXMOL_DIR}/3rdparty/yasio")
    message(STATUS "Removed yasio from external/axmol (Rolo uses its own replacement)")
endif()

# ---------------------------------------------------------------------------
# 4. Shader compiler (axslcc), pinned and checksum-verified
# ---------------------------------------------------------------------------
if(CMAKE_HOST_APPLE)
    if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(_axslcc_asset "osx-arm64.tar.gz")
        set(_axslcc_sha256 "676133d2770433462391a0f982a82f078bf2fb99d62a5032e1aa592676a76336")
    else()
        set(_axslcc_asset "osx-x64.tar.gz")
        set(_axslcc_sha256 "648e8a8d5630c878413585c09b78bad442442d03ca25a2caaf5651ef4531aab5")
    endif()
elseif(CMAKE_HOST_WIN32)
    set(_axslcc_asset "win64.zip")
    set(_axslcc_sha256 "5ea1ae25517a14a81b578836563837fddcb3feca005581761446f3e75986e828")
else()
    set(_axslcc_asset "linux.tar.gz")
    set(_axslcc_sha256 "c8a9113f942f3025aaa5dfd46440b3256915a5b5a02fade6b2884bb4be69d7a2")
endif()

# Axmol looks for the compiler in <axmol>/tools/external/axslcc, found via the
# AX_ROOT environment variable (see AXBuildSet.cmake), so we install it there.
set(_axslcc_dir "${ROLO_AXMOL_DIR}/tools/external/axslcc")
if(CMAKE_HOST_WIN32)
    set(_axslcc_exe "${_axslcc_dir}/axslcc.exe")
else()
    set(_axslcc_exe "${_axslcc_dir}/axslcc")
endif()

set(_axslcc_version_file "${_axslcc_dir}/VERSION")
if(EXISTS "${_axslcc_version_file}")
    file(READ "${_axslcc_version_file}" _axslcc_installed)
    string(STRIP "${_axslcc_installed}" _axslcc_installed)
else()
    set(_axslcc_installed "")
endif()

if(NOT EXISTS "${_axslcc_exe}" OR NOT _axslcc_installed STREQUAL ROLO_AXSLCC_VERSION)
    file(REMOVE_RECURSE "${_axslcc_dir}")
    set(_axslcc_archive "${ROLO_EXTERNAL_DIR}/axslcc-${ROLO_AXSLCC_VERSION}-${_axslcc_asset}")
    message(STATUS "Downloading Axmol shader compiler axslcc ${ROLO_AXSLCC_VERSION}...")
    file(DOWNLOAD
        "https://github.com/axmolengine/axslcc/releases/download/v${ROLO_AXSLCC_VERSION}/axslcc-${ROLO_AXSLCC_VERSION}-${_axslcc_asset}"
        "${_axslcc_archive}"
        EXPECTED_HASH SHA256=${_axslcc_sha256}
        STATUS _dl_status)
    list(GET _dl_status 0 _dl_code)
    if(NOT _dl_code EQUAL 0)
        file(REMOVE "${_axslcc_archive}")
        message(FATAL_ERROR "Downloading axslcc failed: ${_dl_status}")
    endif()
    file(ARCHIVE_EXTRACT INPUT "${_axslcc_archive}" DESTINATION "${_axslcc_dir}")
    file(REMOVE "${_axslcc_archive}")
    if(NOT EXISTS "${_axslcc_exe}")
        message(FATAL_ERROR "axslcc archive did not contain the expected program: ${_axslcc_exe}")
    endif()
endif()
file(WRITE "${_axslcc_dir}/VERSION" "${ROLO_AXSLCC_VERSION}\n")
# Only affects this configure run; nothing is written to your shell profile.
set(ENV{AX_ROOT} "${ROLO_AXMOL_DIR}")

# ---------------------------------------------------------------------------
# 5. Axmol feature switches
#
# Only what Rolo needs: 2D rendering, windowing, images, text.
# Later milestones switch features back on deliberately (and update
# THIRD_PARTY_NOTICES.md), e.g. audio, physics. The editor's interface is
# Rolo's own (engine/ui), so none of Axmol's UI extensions are used.
# ---------------------------------------------------------------------------
set(AX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(AX_EXT_HINT OFF CACHE BOOL "" FORCE)            # no optional extensions
set(AX_ENABLE_EXT_IMGUI OFF CACHE BOOL "" FORCE)    # Rolo has its own UI (rolo_ui)
set(AX_ENABLE_EXT_INSPECTOR OFF CACHE BOOL "" FORCE)
set(AX_ENABLE_EXT_SDFGEN OFF CACHE BOOL "" FORCE)
set(AX_ENABLE_3D OFF CACHE BOOL "" FORCE)
set(AX_ENABLE_PHYSICS OFF CACHE BOOL "" FORCE)
set(AX_ENABLE_AUDIO OFF CACHE BOOL "" FORCE)        # also keeps LGPL OpenAL Soft out for now
set(AX_ENABLE_OPUS OFF CACHE BOOL "" FORCE)
set(AX_ENABLE_MEDIA OFF CACHE BOOL "" FORCE)
# No networking (ADR 0012): Rolo games do not download, open web sockets or
# take remote console commands. This leaves out yasio, curl, llhttp and
# websocket-parser. (The console's ParticleSystem build error is fixed by
# cmake/axmol-patches/0001.)
set(AX_ENABLE_CONSOLE OFF CACHE BOOL "" FORCE)
set(AX_ENABLE_NETWORK OFF CACHE BOOL "" FORCE)      # added by Rolo's Axmol patch
set(AX_ENABLE_HTTP OFF CACHE BOOL "" FORCE)
set(AX_ENABLE_WEBSOCKET OFF CACHE BOOL "" FORCE)
set(AX_WITH_YASIO OFF CACHE BOOL "" FORCE)          # added by Rolo's Axmol patch
set(AX_WITH_CURL OFF CACHE BOOL "" FORCE)
set(AX_WITH_LLHTTP OFF CACHE BOOL "" FORCE)
set(AX_WITH_WEBSOCKET_PARSER OFF CACHE BOOL "" FORCE)
set(AX_UPDATE_BUILD_VERSION OFF CACHE BOOL "" FORCE)
# No JPEG library (ADR 0023): Rolo decodes JPEG files itself, with the
# decoder it already has (rolo/assets/ImageDecode.hpp). This leaves out
# libjpeg-turbo, and on the web Emscripten's IJG libjpeg (patch 0003).
set(AX_WITH_JPEG OFF CACHE BOOL "" FORCE)

# ---------------------------------------------------------------------------
# 6. Add Axmol's core library
# (mirrors Axmol's own templates/common/cmake/modules/AXGameEngineSetup.cmake)
# ---------------------------------------------------------------------------
set(_AX_ROOT "${ROLO_AXMOL_DIR}")
set(_is_axmol_embed TRUE)
set(_AX_USE_PREBUILT FALSE)
list(APPEND CMAKE_MODULE_PATH "${_AX_ROOT}/cmake/Modules/")

include(AXBuildSet)

# SYSTEM: Axmol's headers are not ours, so our strict warnings must not apply to them.
add_subdirectory("${_AX_ROOT}/core" "${ENGINE_BINARY_PATH}/axmol/core" SYSTEM)

if(NOT TARGET ${_AX_CORE_LIB})
    message(FATAL_ERROR "Axmol did not create its core library target.")
endif()
# Axmol's image code without its JPEG parts (see AX_WITH_JPEG above).
target_compile_definitions(${_AX_CORE_LIB} PUBLIC AX_USE_JPEG=0)

# Rolo's replacement for the yasio headers Axmol's core includes (ADR 0012).
# Linked through Axmol's "3rdparty" interface target, which the core and
# everything using it already link.
add_library(rolo_axmol_compat INTERFACE)
target_include_directories(rolo_axmol_compat SYSTEM INTERFACE
    "${PROJECT_SOURCE_DIR}/engine/runtime/axmol_compat/include")
target_link_libraries(3rdparty INTERFACE rolo_axmol_compat)

