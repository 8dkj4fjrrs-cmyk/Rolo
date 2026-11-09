# rolo_player — the game runtime application.
#
#   rolo_player_cli  command-line parsing (no backend; unit-tested)
#   rolo_player      the executable (a .app bundle on macOS)

add_library(rolo_player_cli STATIC runtime/CommandLine.cpp)
target_include_directories(rolo_player_cli PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/runtime")
target_compile_features(rolo_player_cli PUBLIC cxx_std_20)
target_link_libraries(rolo_player_cli PUBLIC rolo::gameplay)   # key names
rolo_set_warnings(rolo_player_cli)

add_executable(rolo_player runtime/main.cpp)
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
