# Unit tests (Catch2 v3) and smoke tests.
#
# Catch2 is used if already installed (e.g. `brew install catch2`); otherwise
# CMake downloads the pinned release below. It is a test-only dependency and
# is never linked into the engine, editor, or shipped games.

include(FetchContent)

FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.1
    GIT_SHALLOW    TRUE
    FIND_PACKAGE_ARGS 3)
FetchContent_MakeAvailable(Catch2)
include(Catch)

# ---------------------------------------------------------------------------
# Core and editor-logic tests: no backend, no window. Always built.
# ---------------------------------------------------------------------------
add_executable(rolo_tests
    core/test_engine.cpp
    core/test_log.cpp
    core/test_version.cpp
    editor/test_console_log.cpp
    editor/test_editor_paths_and_cli.cpp
    editor/test_preferences.cpp
    ui/test_ui_context.cpp
    ui/test_ui_docking.cpp
    ui/test_ui_foundations.cpp
    ui/test_ui_widgets.cpp
    ui/test_ui_inspector.cpp
    editor/test_commands.cpp
    editor/test_resources.cpp
    editor/test_export.cpp
    scene/test_scene.cpp
    scene/test_legacy_names.cpp
    scene/test_scene_file.cpp
    scene/test_project.cpp
    scene/test_tilemap.cpp
    assets/test_assets.cpp
    assets/test_animation.cpp
    gameplay/test_input.cpp
    gameplay/test_collision.cpp
    gameplay/test_game.cpp
    gameplay/test_animation_play.cpp
    gameplay/test_builtin_behaviours.cpp
    gameplay/test_game_content.cpp
    gameplay/test_scripts.cpp
    content/test_logic.cpp
    content/test_content_files.cpp
    content/test_content_library.cpp
    content/test_content_runner.cpp
    content/test_save.cpp
    content/test_notation.cpp
    audio/test_audio_engine.cpp
    physics/test_physics_world.cpp
    gameplay/test_sound.cpp
    gameplay/test_physics.cpp)
target_link_libraries(rolo_tests PRIVATE rolo::core rolo::scene rolo::assets rolo::content rolo::gameplay rolo::audio rolo::physics rolo::ui rolo_editor_core Catch2::Catch2WithMain)
if(TARGET sample_game)
    target_sources(rolo_tests PRIVATE gameplay/test_sample_game.cpp gameplay/test_lantern_keeper.cpp
        gameplay/test_templates.cpp)
    rolo_link_game_modules(rolo_tests MODULES sample_game keeper)
    target_compile_definitions(rolo_tests PRIVATE
        ROLO_SAMPLE_PROJECT_DIR="${PROJECT_SOURCE_DIR}/examples/sample_game/project"
        LANTERN_KEEPER_PROJECT_DIR="${PROJECT_SOURCE_DIR}/examples/lantern_keeper/project")
else()
    rolo_link_game_modules(rolo_tests MODULES)
endif()
target_compile_definitions(rolo_tests PRIVATE ROLO_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    ROLO_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_include_directories(rolo_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
rolo_set_warnings(rolo_tests)
catch_discover_tests(rolo_tests)

# ---------------------------------------------------------------------------
# Runtime, player and editor tests: only when the Axmol backend is built.
# ---------------------------------------------------------------------------
if(NOT TARGET rolo_player)
    return()
endif()

# Unit tests that need no window.
add_executable(rolo_runtime_tests
    player/test_command_line.cpp
    runtime/test_axmol_compat.cpp
    runtime/test_gamepads.cpp
    runtime/test_log_bridge.cpp
    runtime/test_scene_renderer.cpp)
target_link_libraries(rolo_runtime_tests PRIVATE
    rolo_player_cli rolo::runtime ${_AX_CORE_LIB} Catch2::Catch2WithMain)
# These tests check internal headers of rolo_runtime.
target_include_directories(rolo_runtime_tests PRIVATE "${PROJECT_SOURCE_DIR}/engine/runtime/src" "${CMAKE_CURRENT_SOURCE_DIR}")
rolo_set_warnings(rolo_runtime_tests)
catch_discover_tests(rolo_runtime_tests)

add_test(NAME player.version COMMAND rolo_player --version)
set_tests_properties(player.version PROPERTIES PASS_REGULAR_EXPRESSION "backend: Axmol")

add_test(NAME player.rejects_bad_option COMMAND rolo_player --frames zero)
set_tests_properties(player.rejects_bad_option PROPERTIES WILL_FAIL TRUE)

add_test(NAME player.rejects_missing_scene COMMAND rolo_player --scene "no-such-file.lscene")
set_tests_properties(player.rejects_missing_scene PROPERTIES WILL_FAIL TRUE)

if(TARGET rolo_editor)
    add_test(NAME editor.version COMMAND rolo_editor --version)
    set_tests_properties(editor.version PROPERTIES
        PASS_REGULAR_EXPRESSION "Rolo Editor [0-9]+\\.[0-9]+\\.[0-9]+(-[a-z0-9.]+)? \\(backend: Axmol")

    add_test(NAME editor.rejects_unknown_option COMMAND rolo_editor --no-such-flag)
    set_tests_properties(editor.rejects_unknown_option PROPERTIES WILL_FAIL TRUE)
endif()

# Window tests: open a real window for a few frames. They need a screen
# (on headless Linux CI, run ctest under `xvfb-run`).
if(NOT ROLO_WINDOW_TESTS)
    return()
endif()

# Every test added from here on is labelled "window" at the end of this file,
# except those listed there that run without one. CI on systems without a
# screen (Windows runners) skips them with: ctest -LE window
get_property(_tests_without_window DIRECTORY PROPERTY TESTS)

add_test(NAME player.plays_scene_file
    COMMAND rolo_player --scene "${CMAKE_CURRENT_SOURCE_DIR}/data/test_card.lscene" --frames 30
            --screenshot "${CMAKE_CURRENT_BINARY_DIR}/player_test_card.png")
set_tests_properties(player.plays_scene_file PROPERTIES
    PASS_REGULAR_EXPRESSION "Showing scene 'Test Card' \\(4 entities\\).*Screenshot saved"
    FAIL_REGULAR_EXPRESSION "\\[ERROR\\]|\\[WARN"
    TIMEOUT 60)


add_test(NAME player.window_smoke COMMAND rolo_player --frames 120)
set_tests_properties(player.window_smoke PROPERTIES
    PASS_REGULAR_EXPRESSION "Window closed after 12[0-9] frames"
    FAIL_REGULAR_EXPRESSION "\\[ERROR\\]"
    TIMEOUT 60)

add_test(NAME player.screenshot
    COMMAND rolo_player --frames 90 --screenshot "${CMAKE_CURRENT_BINARY_DIR}/player_screenshot.png")
set_tests_properties(player.screenshot PROPERTIES
    PASS_REGULAR_EXPRESSION "Screenshot saved"
    FAIL_REGULAR_EXPRESSION "\\[ERROR\\]"
    TIMEOUT 60)

if(TARGET sample_game)
    # Gameplay end to end (fixed 1/60 s frames so every run is the same):
    # the player walks up and right to the elder, talks (E), goes on, and
    # accepts the quest. The screenshot shows the dialogue box.
    add_test(NAME player.plays_sample_game
        COMMAND rolo_player --project "${PROJECT_SOURCE_DIR}/examples/sample_game/project" --frames 200
                --fixed-step --no-stats --save-dir "${CMAKE_CURRENT_BINARY_DIR}/player-test-saves"
                --tap Up@5-34 --tap Right@40-100 --tap E@120 --tap E@140 --tap E@170
                --screenshot "${CMAKE_CURRENT_BINARY_DIR}/player_sample_dialogue.png")
    set_tests_properties(player.plays_sample_game PROPERTIES
        PASS_REGULAR_EXPRESSION "Showing scene 'Meadow'.*Quest started: Gems for the Elder.*Screenshot saved"
        FAIL_REGULAR_EXPRESSION "\\[ERROR\\]|\\[WARN"
        TIMEOUT 120)

    # The Lantern Keeper, with keys only: read the opening (five lines), walk
    # left and up to Keeper Maren, talk, and accept her request.
    add_test(NAME player.plays_lantern_keeper
        COMMAND rolo_player --project "${PROJECT_SOURCE_DIR}/examples/lantern_keeper/project" --frames 240
                --fixed-step --no-stats --save-dir "${CMAKE_CURRENT_BINARY_DIR}/player-test-keeper-saves"
                --tap E@10 --tap E@20 --tap E@30 --tap E@40 --tap E@50 --tap Left@60-120 --tap Up@125-140
                --tap E@150 --tap E@170 --tap E@190 --tap E@210
                --screenshot "${CMAKE_CURRENT_BINARY_DIR}/player_lantern_keeper.png")
    set_tests_properties(player.plays_lantern_keeper PROPERTIES
        PASS_REGULAR_EXPRESSION "Showing scene 'Village'.*Quest started: The Dark Lantern.*Screenshot saved"
        FAIL_REGULAR_EXPRESSION "\\[ERROR\\]|\\[WARN"
        TIMEOUT 120)
endif()

if(TARGET rolo_editor)
    # Editor tests keep their settings in the build folder, never in your real
    # user settings.
    set(_editor_cfg "${CMAKE_CURRENT_BINARY_DIR}/editor-test-settings")

    # 1) First run: default layout, then layout + preferences are saved.
    add_test(NAME editor.first_run
        COMMAND rolo_editor --config-dir "${_editor_cfg}" --no-project --reset-layout --frames 120
                --screenshot "${CMAKE_CURRENT_BINARY_DIR}/editor_screenshot.png")
    set_tests_properties(editor.first_run PROPERTIES
        PASS_REGULAR_EXPRESSION "Using the default panel layout.*Screenshot saved"
        FAIL_REGULAR_EXPRESSION "\\[ERROR\\]"
        FIXTURES_SETUP editor_settings
        TIMEOUT 60)

    # 2) Second run: the saved layout must be restored (persistence works).
    add_test(NAME editor.restores_layout
        COMMAND rolo_editor --config-dir "${_editor_cfg}" --no-project --frames 60)
    set_tests_properties(editor.restores_layout PROPERTIES
        PASS_REGULAR_EXPRESSION "Restored the saved panel layout"
        FAIL_REGULAR_EXPRESSION "\\[ERROR\\]|Using the default panel layout"
        FIXTURES_REQUIRED editor_settings
        TIMEOUT 60)

    # 3) Projects, end to end: the editor creates one, opens it again (as the
    #    last project used), and the player plays its start scene.
    set(_project "${CMAKE_CURRENT_BINARY_DIR}/test-projects/Window Test Game")
    set(_project_cfg "${CMAKE_CURRENT_BINARY_DIR}/editor-test-project-settings")
    add_test(NAME editor.project_cleanup
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_CURRENT_BINARY_DIR}/test-projects" "${_project_cfg}"
                "${_project_cfg}-starter")
    set_tests_properties(editor.project_cleanup PROPERTIES FIXTURES_SETUP editor_project_clean)

    add_test(NAME editor.creates_project
        COMMAND rolo_editor --config-dir "${_project_cfg}" --new-project "${_project}" --frames 30)
    set_tests_properties(editor.creates_project PROPERTIES
        PASS_REGULAR_EXPRESSION "Created project in.*Opened scene main.lscene.*Showing scene 'Main'"
        FAIL_REGULAR_EXPRESSION "\\[ERROR\\]|\\[WARN"
        FIXTURES_REQUIRED editor_project_clean
        FIXTURES_SETUP editor_project
        TIMEOUT 60)

    # A project from the Top-Down Starter template plays at once, with only
    # built-in components (no game module needed).
    set(_starter "${CMAKE_CURRENT_BINARY_DIR}/test-projects/Starter Game")
    add_test(NAME editor.creates_project_from_template
        COMMAND rolo_editor --config-dir "${_project_cfg}-starter" --new-project "${_starter}" --template topdown
                --play --frames 60)
    set_tests_properties(editor.creates_project_from_template PROPERTIES
        PASS_REGULAR_EXPRESSION "Created project in.*Playing 'Meadow'.*Quest started: Say Hello.*Stopped playing after"
        FAIL_REGULAR_EXPRESSION "\\[ERROR\\]|\\[WARN"
        FIXTURES_REQUIRED editor_project_clean
        FIXTURES_SETUP starter_project
        TIMEOUT 60)

    add_test(NAME editor.reopens_last_project
        COMMAND rolo_editor --config-dir "${_project_cfg}" --frames 30)
    set_tests_properties(editor.reopens_last_project PROPERTIES
        PASS_REGULAR_EXPRESSION "Opened project 'Window Test Game'.*Opened scene main.lscene"
        FAIL_REGULAR_EXPRESSION "\\[ERROR\\]|\\[WARN"
        FIXTURES_REQUIRED editor_project
        TIMEOUT 60)

    if(TARGET sample_game)
        # Play mode: the editor opens the sample, plays it, and the game's
        # behaviours run without errors; quitting stops the game.
        add_test(NAME editor.plays_sample_game
            COMMAND rolo_editor --config-dir "${CMAKE_CURRENT_BINARY_DIR}/editor-test-play-settings"
                    --project "${PROJECT_SOURCE_DIR}/examples/sample_game/project" --play --frames 60
                    --screenshot "${CMAKE_CURRENT_BINARY_DIR}/editor_playing.png")
        set_tests_properties(editor.plays_sample_game PROPERTIES
            PASS_REGULAR_EXPRESSION "Game module: sample_game.*Playing 'Meadow'.*Screenshot saved.*Stopped playing after [0-9]+ frames"
            FAIL_REGULAR_EXPRESSION "\\[ERROR\\]|\\[WARN"
            TIMEOUT 60)
        # Exporting: the editor checks and exports The Lantern Keeper without a
        # window; the exported program, started with no arguments, plays it.
        set(_exports "${CMAKE_CURRENT_BINARY_DIR}/test-exports")
        add_test(NAME editor.export_cleanup COMMAND ${CMAKE_COMMAND} -E rm -rf "${_exports}")
        set_tests_properties(editor.export_cleanup PROPERTIES FIXTURES_SETUP export_clean)
        add_test(NAME editor.exports_game
            COMMAND rolo_editor --project "${PROJECT_SOURCE_DIR}/examples/lantern_keeper/project"
                    --export "${_exports}")
        set_tests_properties(editor.exports_game PROPERTIES
            # macOS exports an app bundle: "Exported The Lantern Keeper.app (163 files ..."
            PASS_REGULAR_EXPRESSION "Exported The Lantern Keeper(\\.app)? \\([0-9]+ files"
            FAIL_REGULAR_EXPRESSION "\\[ERROR\\]|\\[WARN"
            FIXTURES_REQUIRED export_clean
            FIXTURES_SETUP exported_game
            TIMEOUT 120)
        # The game program: on macOS inside the app bundle (docs/exporting.md).
        if(APPLE)
            set(_exported_program "${_exports}/The Lantern Keeper.app/Contents/MacOS/rolo_player")
        else()
            set(_exported_program "${_exports}/The Lantern Keeper/The Lantern Keeper${CMAKE_EXECUTABLE_SUFFIX}")
        endif()
        add_test(NAME exported_game.plays
            COMMAND "${_exported_program}" --frames 30
                    --save-dir "${CMAKE_CURRENT_BINARY_DIR}/exported-game-saves"
                    --screenshot "${CMAKE_CURRENT_BINARY_DIR}/exported_game.png")
        set_tests_properties(exported_game.plays PROPERTIES
            PASS_REGULAR_EXPRESSION "Playing the game bundled with this program.*Window: 'The Lantern Keeper'.*Showing scene 'Village'.*Screenshot saved"
            FAIL_REGULAR_EXPRESSION "\\[ERROR\\]|\\[WARN"
            FIXTURES_REQUIRED exported_game
            TIMEOUT 60)

        add_test(NAME editor.plays_lantern_keeper
            COMMAND rolo_editor --config-dir "${CMAKE_CURRENT_BINARY_DIR}/editor-test-keeper-settings"
                    --project "${PROJECT_SOURCE_DIR}/examples/lantern_keeper/project" --play --frames 60
                    --screenshot "${CMAKE_CURRENT_BINARY_DIR}/editor_playing_keeper.png")
        set_tests_properties(editor.plays_lantern_keeper PROPERTIES
            PASS_REGULAR_EXPRESSION "Game module: keeper.*Playing 'Village'.*Screenshot saved.*Stopped playing after [0-9]+ frames"
            FAIL_REGULAR_EXPRESSION "\\[ERROR\\]|\\[WARN"
            TIMEOUT 60)
    endif()

    add_test(NAME player.plays_project
        COMMAND rolo_player --project "${_project}" --frames 30)
    set_tests_properties(player.plays_project PROPERTIES
        PASS_REGULAR_EXPRESSION "Showing scene 'Main' \\(21 entities\\)"
        FAIL_REGULAR_EXPRESSION "\\[ERROR\\]|\\[WARN"
        FIXTURES_REQUIRED editor_project
        TIMEOUT 60)
endif()

# Label the window tests (see above).
get_property(_all_tests DIRECTORY PROPERTY TESTS)
foreach(_test IN LISTS _all_tests)
    if(NOT _test IN_LIST _tests_without_window
       AND NOT _test MATCHES "^editor\\.(project_cleanup|export_cleanup|exports_game)$")
        set_property(TEST ${_test} APPEND PROPERTY LABELS window)
    endif()
endforeach()
