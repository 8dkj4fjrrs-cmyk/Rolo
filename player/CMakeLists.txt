# rolo_player — the game runtime application.
#
#   rolo_player_cli  command-line parsing (no backend; unit-tested)
#   rolo_player      the executable (a .app bundle on macOS)

add_library(rolo_player_cli STATIC runtime/CommandLine.cpp)
target_include_directories(rolo_player_cli PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
target_compile_features(rolo_player_cli PUBLIC cxx_std_20)
target_link_libraries(rolo_player_cli PUBLIC rolo::gameplay)   # key names
rolo_set_warnings(rolo_player_cli)

if(ANDROID)
    # An Android app's program is a library that Android loads into the app
    # (player/android has the app around it: the Gradle project, ADR 0028).
    add_library(rolo_player SHARED runtime/main.cpp)
    # Axmol's functions that the app's Java side calls (touches, frames, the
    # surface). Nothing in the program refers to them, so a linker taking
    # only what is used from a static library would leave them out:
    # "whole archive" takes all of it.
    target_link_libraries(rolo_player PRIVATE -Wl,--whole-archive cpp_android_spec -Wl,--no-whole-archive)
else()
    add_executable(rolo_player runtime/main.cpp)
endif()
target_link_libraries(rolo_player PRIVATE rolo_player_cli rolo::runtime rolo::audio)
rolo_set_warnings(rolo_player)
rolo_link_game_modules(rolo_player)   # the game's behaviours

# Axmol's per-app setup: output folder, macOS bundle, and — most importantly —
# copying the compiled shaders next to the program so it can draw anything.
# (Axmol's helper would also link every enabled Axmol extension, using a CMake
# call style that clashes with ours; the extensions we use already come in via
# rolo_runtime, so hide that list for this directory.)
set(APP_NAME rolo_player)
set(_AX_EXTENSION_LIBS "")
ax_setup_app_config(rolo_player)

# The web: the player is rolo_player.js + .wasm + .data (Axmol's shaders),
# loaded by a page. pre.js (built into the .js) fetches the game's files and
# the saved games before the game starts; index.html is the page, copied
# beside the player so the folder can be served and exported as it is.
if(EMSCRIPTEN)
    target_link_options(rolo_player PRIVATE
        "SHELL:--pre-js ${CMAKE_CURRENT_SOURCE_DIR}/web/pre.js"
        -lidbfs.js                                      # saved games kept by the browser (IndexedDB)
        -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2     # Axmol draws with WebGL 2
        -sSTACK_SIZE=4mb)                               # as Axmol's own web apps
    set_property(TARGET rolo_player APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/pre.js")
    # The page is copied after linking; a changed page links again (LINK_DEPENDS), so it is copied again.
    set_property(TARGET rolo_player APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html")
    add_custom_command(TARGET rolo_player POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html" "$<TARGET_FILE_DIR:rolo_player>/index.html"
        VERBATIM)
endif()

# Windows: Axmol makes this a windowed program (no console window opens when
# it is double-clicked). Such programs normally start at WinMain; ours starts
# at an ordinary main(), so tell the linker to use the C runtime's start-up
# that calls main(). Output still reaches a terminal or file it is piped to.
if(MSVC)
    target_link_options(rolo_player PRIVATE "/ENTRY:mainCRTStartup")
endif()

if(APPLE)
    set(MACOSX_BUNDLE_BUNDLE_NAME "Rolo Player")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "dev.rolo-engine.player")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2026 Oscar Hird. MIT License.")
    set_target_properties(rolo_player PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in")
endif()
