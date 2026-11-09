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
- Scenes are `rolo::Scene` data (`engine/scene`); Axmol nodes are created only by `engine/runtime/src/SceneRenderer.*`. New component types describe their properties with `ComponentTypeBuilder` so the Inspector, files and undo handle them automatically.
- Gameplay code is C++ `Behaviour` components (`engine/gameplay`) in game modules registered with
  `ROLO_GAME_MODULE` / `rolo_add_game_module` (docs/gameplay.md), or Lua scripts run by the
  `rolo.Script` behaviour (docs/scripting.md, ADR 0013). Lua stays inside `engine/gameplay`
  (ScriptHost, ScriptApi): no other module includes Lua headers. Scripts stay sandboxed: never open
  Lua's io, os, debug or package libraries, and every API function checks its arguments and turns
  C++ exceptions into Lua errors (`guarded`). New script functions need docs/scripting.md and tests. `engine/gameplay` has no
  backend; the runtime and editor translate their input into `rolo::Input`. A game always plays a
  copy of the scene (`Game::start`), never the scene being edited.
- Sound: games ask for it through `rolo::SoundPlayer` (`engine/gameplay`, `Sound.hpp`), never a
  backend. miniaudio stays inside `engine/audio/src/AudioEngine.cpp` (ADR 0015); nothing else
  includes it.
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
