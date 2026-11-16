# Contributor and AI-assistant rules

Read this before changing anything. It applies equally to human contributors
and AI assistants.

## Working rules

1. Inspect the repository before editing; preserve working code.
2. Small, reviewable changes. Every change leaves `cmake --workflow --preset dev` passing.
3. Never report a build or test as passing unless it was actually run. Say where it ran (OS, compiler).
4. No mockups or hard-coded demos presented as finished features; no TODOs reported as done.
5. Every new dependency needs a reason, a pinned version, and an entry in `THIRD_PARTY_NOTICES.md`.
6. Bugs: reproduce, find the root cause, fix, add a regression test.
7. Update docs when public APIs, build steps or file formats change.
8. Architectural changes need an ADR in `docs/adr/`.

## Architecture rules

- Dependency direction: `editor` → `engine`; never the reverse. Tests may use anything.
- Public engine headers (`engine/*/include/rolo/...`) must not include backend (Axmol) headers.
- The editor interface is built with Rolo UI (`engine/ui`, `rolo::ui`). Do not add Dear ImGui or any other third-party UI library (ADR 0004).
- `engine/ui` must not depend on Axmol, GLFW or any graphics API: it turns widgets into a list of triangles, and `engine/runtime/src/UiHost.*` draws them. Keep it unit-testable without a window.
- `editor/model/` must not include UI or runtime/backend headers: it is the editor logic, unit-tested without a window.
- Editor UI code (`editor/application`, `editor/panels`) may use `rolo/ui/*` and `rolo/runtime/*.hpp`, never Axmol headers.
- Editor operations that change scene or project data must go through the undo/redo command system: panels call the editor's `execute()` with a `Command` (`editor/model/Commands.hpp`), never `Scene` setters directly.
- Code that runs every frame must not walk the whole scene (`Scene::forEach`) or look components up
  by name: use the scene's lists (`Scene::all<T>()`, `entitiesWith`, `findByName`, `inOrder`), which
  are made again only when `structureRevision()` changes, and `Entity::get<T>()`. Changes to
  performance are measured with the stress test (`examples/stress`, `rolo_stress`) before and after,
  and kept only if the numbers move (ADR 0021).
- Scenes are `rolo::Scene` data (`engine/scene`); Axmol nodes are created only by `engine/runtime/src/SceneRenderer.*`. New component types describe their properties with `ComponentTypeBuilder` so the Inspector, files and undo handle them automatically.
- Gameplay code is C++ `Behaviour` components (`engine/gameplay`) in game modules registered with
  `ROLO_GAME_MODULE` / `rolo_add_game_module` (docs/gameplay.md), or Luau scripts run by the
  `rolo.Script` behaviour (docs/scripting.md, ADR 0013, ADR 0020). Luau stays inside `engine/gameplay`
  (ScriptHost, ScriptApi): no other module includes Luau headers. Luau is used unmodified. Scripts
  stay sandboxed: never open Luau's os or debug libraries or `loadstring`, keep the shared globals
  read-only (`luaL_sandbox`), and every API function checks its arguments and turns
  C++ exceptions into Lua errors (`guarded`). New script functions need docs/scripting.md and tests.
  Script properties (ADR 0029): the editor learns them by reading the script's text
  (`readScriptProperties`), never by running a script; an object's own values are the Script
  component's `values` text (`rolo/scene/Values.hpp`), changed only through `SetProperty`.
  Objects made while playing (ADR 0030) are copies (`Game::clone`; `game.spawn`, `obj:clone()`), never
  built from nothing; they are not in saved games, and `Game::kMostObjects` stops a runaway script. `engine/gameplay` has no
  backend; the runtime and editor translate their input into `rolo::Input`. A game always plays a
  copy of the scene (`Game::start`), never the scene being edited.
- Sound: games ask for it through `rolo::SoundPlayer` (`engine/gameplay`, `Sound.hpp`), never a
  backend. miniaudio stays inside `engine/audio/src/AudioEngine.cpp` (ADR 0015); nothing else
  includes it.
- Gamepads: games read them through `rolo::Input` (`GamepadButton`, sticks); only `engine/runtime/src/Gamepads.cpp`
  talks to GLFW's gamepad API (ADR 0017).
  Rumble's system code is in `engine/runtime/src/Rumble_*.{cpp,mm}` (ADR 0018). Text changed by code goes
  through `Game::setText` (texts are refilled from their templates every frame).
- Physics: Box2D stays inside `engine/physics/src/PhysicsWorld.cpp` (ADR 0016); the rest of Rolo uses
  `rolo::PhysicsWorld` in points and clockwise degrees. Physics runs only in scenes with a RigidBody.
- RPG content (`engine/content`) is data: conditions and actions are a fixed, typed set, never code or
  free expressions. Authored definitions (variables, dialogues, quests) stay apart from progress
  (`GameState`, save files). New action or condition types need the JSON form, the written form
  (`Notation.hpp`), checks in `ContentLibrary`, docs/formats/content.md and tests.
- Scenes refer to project files by `AssetId`, never by path; files reach the runtime only through `AssetDatabase` (`engine/assets`). Games open it read-only. `engine/scene` and `engine/assets` stay free of Axmol so they are tested in every preset.
- File formats (`docs/formats/`) are versioned: changing one means bumping its version, adding an upgrade step and a test with an old file.
- No global mutable state. Pass `Engine&` / `Logger&` explicitly.
- Ownership: RAII and `std::unique_ptr` by default; raw pointers only as non-owning observers.
- Errors: return `rolo::Status` (or a richer result type later) with a user-actionable message. No exceptions across module boundaries.
- Stable IDs, not names or addresses, for anything saved to disk.
- The editor finds its templates, examples, licenses, docs and the game runtime through
  `RoloResources` (beside the program when installed, the source tree when built); never
  hard-code paths to them. Anything a game contains must have its license text in `licenses/`.
- Saved files use project-relative paths and an explicit format version.
- The web (ADR 0022): the player also builds with Emscripten (preset `web`), without threads. Code in
  `engine/` must not need threads to work (ScriptHost's watchdog has a clock fallback), and nothing
  after `Application::run` may assume the game has ended (`RunReport::continuesInBackground`): the
  player keeps a running game's state in its `Player` object. Browser-only code is behind
  `__EMSCRIPTEN__`, in `engine/runtime` and `player/` only. Check web changes with
  `tools/web/check_web_game.js`.
- Android (ADR 0028; experimental): the player also builds as an Android app, by the Gradle project
  in `player/android` (which runs this CMake build with the NDK). It cannot be built where Rolo is
  developed: build and test it with the **Android (experimental)** workflow
  (`.github/workflows/android.yml`, `tools/android/check_android_game.py`), which is separate from CI
  and must stay so. Android-only code is behind `__ANDROID__`, in `engine/runtime`, `player/` and
  `engine/core/src/Log.cpp` only; there is no `main()` and no GLFW there (`rolo/runtime/Android.hpp`,
  `UiHostNone.hpp`; gamepads come from `RoloGamepads.java` through `Gamepads_android.cpp`, keyboards from `RoloKeys.java` through `android::takeKeys`), and `Application::run` returns while the game goes on, as on
  the web. The app's Java is Axmol's plus `RoloActivity`; classes Axmol mentions but Rolo leaves out
  are Rolo's own stand-ins in `player/android/app/src/axmol_compat` (never copy the originals), and
  the app asks for no permissions and has no Maven dependencies: keep it so.
  Exporting (step 2): `editor/model/AndroidExport.*` writes the Gradle project (a copy of the Android
  runtime, `gradle :app:roloAndroidRuntime`, plus the game) and gives the commands for Java, Gradle and
  keytool; other programs are only ever started through `editor/model/Process.*` (no shell), and a
  key's password only travels in the environment of the program that needs it: never store it, log
  it or put it on a command line. The export is built on a worker thread (`AndroidExportJob`), which
  must not touch the Logger or the UI.
  Google Play (step 3): `docs/google-play.md` states Google's rules with the month they were checked
  and a link each; check them again before changing it, and never write that something was uploaded
  or seen in the Play Console unless it was. Only the `saves` folder is in Android's backup
  (`res/xml/rolo_backup.xml`, `rolo_data_extraction.xml`): keep it so.
  A lost graphics context: any texture Rolo makes on Android must come back after one, either read
  again by `TextureStore::reloadAll` or handed to Axmol with `VolatileTextureMgr::addImage`; the
  workflow's `--lose-context` run shows whether it does.
- Images: Rolo decodes PNG and JPEG files itself (`rolo/assets/ImageDecode.hpp`, stb_image inside
  `rolo_assets`) and writes screenshots itself (`ImageEncode.hpp`, stb_image_write). Axmol is built
  with no JPEG library, no libpng and no OpenSSL (`AX_WITH_JPEG`, `AX_WITH_PNG`, `AX_WITH_OPENSSL`
  off; ADR 0023, ADR 0024): do not turn them back on, and never hand Axmol an image file to load.
  `engine/runtime/axmol_compat/include/openssl` is Rolo's stand-in (no cryptography in it); never
  copy OpenSSL code into it.
- Screen sizes (ADR 0027): the game's size is the project's (`ProjectSettings::width/height`), never a
  literal 1280 x 720. With `screenFit: expand` a game window shows more than that; ask
  `Game::viewSize()` / `Application::viewSize()`. The Camera's children are pinned by
  `pinToScreen` in the renderer; the editor always shows exactly the game's size. A phone's camera
  notch is `ScreenInsets` (from `rolo::android::safeInsets`): things placed at the screen's edges
  (`pinToScreen`, `TouchControls::layout`) take them; the picture itself still fills the screen.
- Touch (ADR 0026): on-screen controls are made from the game's actions (`rolo/gameplay/TouchControls.hpp`,
  logic only; `Application` reports fingers and draws them). Gameplay code asks for actions, never for
  keys, so that it works on touch screens. On the web, touches are read from the canvas directly, not
  through Axmol.
- Text: Rolo draws game text itself (`rolo/ui/TextImage.hpp`, stb_truetype; sprites made in
  `engine/runtime/src/TextSprites.hpp`). Axmol is built without FreeType (`AX_WITH_FREETYPE=OFF`,
  ADR 0025): never use `ax::Label`, which cannot make fonts in Rolo.
- Axmol is used unmodified except for the small patches in `cmake/axmol-patches/` (switches and
  build fixes only, each explained in its README). Rolo builds Axmol without networking or
  yasio; `engine/runtime/axmol_compat` provides the yasio headers Axmol's core includes, written
  for Rolo (ADR 0012). Never copy yasio code into it.

## Code style

- C++20, `.clang-format` in the root. 4-space indent.
- Namespace `rolo`. Types `PascalCase`, functions `camelCase`, members `m_name`, constants `kName`.
- Headers in `include/rolo/<module>/`, sources in `src/`.
- Write comments for a reader who is learning engine programming: explain *why*.

## Commands

```sh
cmake --workflow --preset dev   # configure + build + test (window tests need a display;
                                # on headless Linux: xvfb-run -a ctest --preset dev)
cmake --workflow --preset core  # fast: no Axmol, no window
cmake --build --preset asan && ctest --preset asan   # memory-error check
cmake --workflow --preset release                     # release build, tests, and the package in dist/
```

Releases: see `docs/releasing.md`. When a dependency changes, refresh
`licenses/` with `python3 tools/release/fetch_licenses.py`.
