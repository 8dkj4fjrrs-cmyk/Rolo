# The Rolo editor.
#
#   rolo_editor_core  editor logic with no UI toolkit and no backend:
#                        scene documents, undo/redo commands, console log,
#                        preferences, settings paths, command line.
#                        Always built and unit-tested (also in the `core` preset).
#   rolo_editor       the editor application: panels built with Rolo UI
#                        (engine/ui) on top of rolo_runtime. Needs the
#                        Axmol backend.
#
# Dependency rule: the editor may depend on the engine; the engine must
# never depend on the editor.

add_library(rolo_editor_core STATIC
    model/CommandLine.cpp
    model/Commands.cpp
    model/ConsoleLog.cpp
    model/EditorPaths.cpp
    model/EditorState.cpp
    model/GameExport.cpp
    model/RoloResources.cpp
    model/Preferences.cpp
    model/SceneDocument.cpp)
target_include_directories(rolo_editor_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/model")
target_link_libraries(rolo_editor_core PUBLIC rolo::core rolo::scene rolo::assets
    PRIVATE nlohmann_json::nlohmann_json)
rolo_set_warnings(rolo_editor_core)
# Only RoloResources.cpp reads JSON (template.json); nlohmann/json needs
# char8_t in C++20 mode (see engine/scene/CMakeLists.txt).
if(MSVC)
    set_source_files_properties(model/RoloResources.cpp PROPERTIES COMPILE_OPTIONS /Zc:char8_t)
else()
    set_source_files_properties(model/RoloResources.cpp PROPERTIES COMPILE_OPTIONS -fchar8_t)
endif()

if(NOT TARGET rolo_runtime)
    message(STATUS "rolo_editor (the window) needs ROLO_RUNTIME_BACKEND=axmol; building editor logic only.")
    return()
endif()

add_executable(rolo_editor
    application/main.cpp
    application/EditorApp.cpp
    application/Exporting.cpp
    panels/AssetsPanel.cpp
    panels/ConsolePanel.cpp
    panels/FolderPicker.cpp
    panels/HierarchyPanel.cpp
    panels/PanelCommon.cpp
    panels/TilePalettePanel.cpp
    panels/AnimationPanel.cpp
    panels/InspectorPanel.cpp
    panels/InspectorContent.cpp
    panels/ViewportPanel.cpp)

# The runtime gives the editor Rolo's own headers only — never Axmol's —
# so editor code cannot reach into the backend by accident.
target_link_libraries(rolo_editor PRIVATE rolo_editor_core rolo::runtime rolo::audio)
rolo_set_warnings(rolo_editor)
rolo_link_game_modules(rolo_editor)   # behaviours for the Inspector and play mode
# Where a build of the editor (not an installed copy) finds templates,
# examples and licenses (the source tree) and the game runtime it exports
# games with (the player built beside it). See model/RoloResources.hpp.
if(APPLE)
    set(_rolo_dev_player "$<TARGET_BUNDLE_DIR:rolo_player>")
else()
    set(_rolo_dev_player "$<TARGET_FILE:rolo_player>")
endif()
target_compile_definitions(rolo_editor PRIVATE
    ROLO_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    ROLO_DEV_PLAYER="${_rolo_dev_player}")
add_dependencies(rolo_editor rolo_player)   # exports need it

# Before Milestone 2 the editor was a single file at bin/rolo_editor; it is
# now a folder (program + shaders). Remove the old file from existing builds.
if(EXISTS "${CMAKE_BINARY_DIR}/bin/rolo_editor" AND NOT IS_DIRECTORY "${CMAKE_BINARY_DIR}/bin/rolo_editor")
    file(REMOVE "${CMAKE_BINARY_DIR}/bin/rolo_editor")
endif()

# Axmol's per-app setup (output folder, macOS bundle, compiled shaders).
set(APP_NAME rolo_editor)
set(_AX_EXTENSION_LIBS "")   # see player/CMakeLists.txt
ax_setup_app_config(rolo_editor)

# Windows: Axmol makes this a windowed program (no console window opens when
# it is double-clicked). Such programs normally start at WinMain; ours starts
# at an ordinary main(), so tell the linker to use the C runtime's start-up
# that calls main(). Output still reaches a terminal or file it is piped to.
if(MSVC)
    target_link_options(rolo_editor PRIVATE "/ENTRY:mainCRTStartup")
endif()

if(APPLE)
    set(MACOSX_BUNDLE_BUNDLE_NAME "Rolo Editor")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "dev.rolo-engine.editor")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2026 Oscar Hird. MIT License.")
    set_target_properties(rolo_editor PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in")
endif()
