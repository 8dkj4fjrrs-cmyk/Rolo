# Changelog

All notable changes to Rolo Engine. Versions follow the milestones in
[docs/roadmap.md](docs/roadmap.md); design reasons are in [docs/adr](docs/adr).

## Unreleased

- **Escape and quitting are the game's choice**
  ([controls guide](docs/controls.md#escape-quitting-and-the-quick-save-keys)).
  - Per project, in the scene's Inspector (**Game (whole project)**):
    **Escape Quits** and **F5 / F9 Saves**, both on by default. Off, they
    are ordinary keys, e.g. for a pause menu. Exported games follow them.
  - **`game.quit()`** (Lua and C++) ends the game from a menu. In the
    editor, it and Escape stop playing.
  - The Platformer Starter has a pause menu (Escape or Start: Resume,
    Controls, Quit). Its Controls screen opens with Tab or Back.
  - Fixed: the Escape that cancels changing a control also closed the game.
- **Lantern Engine is now Rolo Engine**, with a new logo: Rolo, a smiling
  orange on a red tile ([ADR 0014](docs/adr/0014-rename-to-rolo.md)).
  - Programs, the package and the C++ namespace are renamed (`rolo_editor`,
    `rolo::`, `#include "rolo/…"`, `ROLO_GAME_MODULE`).
  - New projects open on Rolo's face, drawn with shapes.
  - **Projects made with Lantern keep opening.** Their `project.lantern`
    file and `lantern.` names are read, and saving writes `project.rolo`
    and `rolo.` names.
  - Game modules written for Lantern need their includes, namespace and
    module macro renamed. Lua scripts need no changes.
  - The demo game keeps its name: *The Lantern Keeper*.
- **Controls** ([controls guide](docs/controls.md), [ADR 0018](docs/adr/0018-controls.md)).
  - **Actions** with default keys and buttons (`input.action("jump", {"space", "pad a"})`),
    usable wherever a key is. Rolo's own walking and talking are actions too.
  - **Remapping** by players (`input.rebind`), kept on their computer beside
    their saves.
  - **Button pictures** in any text: `{button:jump}` draws what the action
    is bound to on the device in use, as it looks on that pad (Xbox,
    PlayStation, Nintendo) or keyboard. Prompts show the button's picture.
  - **Rumble** (`input.rumble(0.5, 0.3)`): Rolo's own code on each system's
    controller API (XInput, Game Controller, Linux force feedback).
  - The Platformer Starter has a Controls screen, button pictures, and
    rumbles on hard landings.
  - Fixed: text set by a script (`obj:setText`) was put back to the text
    written in the editor in the next frame.
- **Gamepads** ([ADR 0017](docs/adr/0017-gamepads.md)).
  - Up to four, plugged in and out while playing; Xbox, PlayStation,
    Nintendo Switch Pro and most other pads, with buttons named by position
    (A is the bottom one).
  - Games without code play with them: walking follows the left stick and
    d-pad, and A talks, goes on and picks dialogue choices. Prompts say
    "[A] Talk" while a gamepad is plugged in.
  - Lua: `input.isDown("pad a")`, `input.stick("left")`,
    `input.trigger("right")`, `input.gamepad()`; C++: `GamepadButton`,
    `leftStick()`, `trigger()`.
  - The Platformer Starter's hero jumps with A.
  - Read with GLFW's gamepad support, already inside Rolo: nothing new to
    install or license.
- **2D physics** ([physics guide](docs/physics.md), [ADR 0016](docs/adr/0016-physics.md)).
  - The **Rigid Body** component: dynamic, kinematic or static, with mass,
    friction, bounciness, drag, gravity scale and lock rotation.
  - Colliders can be **circles** and **capsules** as well as boxes.
  - Each scene has a **gravity** (Inspector, with Platformer and Top-Down
    presets).
  - Solid tiles are floors and walls; colliders moved by code push things.
  - Lua: `obj:setVelocity`, `applyImpulse`, `isOnGround`, `contacts`,
    `game.setGravity`, and `collide(self, other)`. C++: the same on `Game`,
    and `Behaviour::collide`.
  - New **Platformer Starter** template: run, jump, crates, balls, a lift,
    coins and a flag.
  - Built on Box2D 3.2.0 (MIT). Scenes without a Rigid Body work exactly as
    before.
- **Sound and music** ([sound guide](docs/sound.md), [ADR 0015](docs/adr/0015-sound.md)).
  - WAV, OGG and MP3 files play in play mode and in exported games, on
    macOS, Windows and Linux, with nothing to install.
  - The **Sound** component: a sound when an object appears, a loop while
    it is there, or the scene's music. Music fades from one piece to the
    next, and carries on when the next scene has the same piece.
  - Actions for triggers, dialogues and quests: `playSound`, `playMusic`,
    `stopMusic`.
  - Lua: the `audio` table (`audio.play`, `audio.playMusic`,
    `audio.setVolume`...) and `object:playSound()`. C++: `game.playSound()`,
    `game.playMusic()` and `game.sound()`.
  - The editor: **Play** and **Stop** for sound files in the Inspector, drag
    a sound into the scene, Pause holds the game's sounds, and
    **Game > Mute Sound**.
  - `rolo_player --mute`.
  - *The Lantern Keeper* has original music and sound effects.
  - Built on miniaudio and stb_vorbis (both public domain or MIT), not
    Axmol's audio, which needs the LGPL-licensed OpenAL Soft.

## 0.9.1-alpha — Rolo opens on downloaded Macs

- **macOS called the downloaded Rolo "damaged"** and would not open it:
  its app bundles had no valid signature, which Apple silicon Macs require
  for downloaded apps.
  - The package, and every game exported on a Mac, is now signed "ad hoc".
    The first time, right-click the app and choose **Open** (see the
    [known limitations](docs/known-limitations.md)).
  - CI checks the signatures.

## 0.9.0-alpha — Lua scripting, and verified on macOS and Windows

- **Lua scripting.** Gameplay code without a compiler: write a `.lua` file,
  add a **Script** component, press Play. A downloaded Rolo runs scripts,
  and exported games carry them.
  - Scripts get `start(self)` and `update(self, dt)`, and can use:
    - objects: move with collisions, animate, destroy, set text...;
    - `game.*`: find objects, variables, events, the content notation,
      scene changes, saves;
    - `input.*`, `print` (to the Console) and `require` (modules).
  - Sandboxed: no files, programs or native code.
  - A script error names the file and line and stops only that script. A
    script stuck in a loop, or using too much memory, is stopped.
  - **New Script** (Assets panel and Game menu), and the script's Inspector
    page: syntax check, preview, **Open in Code Editor**. **Check Content**
    and exporting check scripts' syntax.
  - The Top-Down Starter has Lua fireflies as an example.
  - Guide: [docs/scripting.md](docs/scripting.md); design: ADR 0013. Lua
    5.4.9 (MIT) is kept in `third_party/lua`.
- **macOS and Windows build in CI.** macOS passes every test, including
  those in real windows. Windows passes all but the window tests, which
  its CI machines cannot run (they are labelled `window` and skipped
  there). The first runs on GitHub found and fixed:
  - Apple's standard library lacks decimal `std::from_chars`;
  - Apple Clang 15 cannot construct aggregates from parentheses;
  - MSVC warnings (`getenv`, unreachable code);
  - Axmol's Windows app setup: a CMake link-style clash (Axmol patch 0002)
    and the `WinMain` entry point;
  - the Axmol patch's line endings on Windows;
  - a missing `_sv` literal;
  - the export test on macOS (games are `.app` bundles);
  - the Windows package lacked `zlib1.dll` and the Visual C++ runtime, so
    its programs (and exported games) would not have started. The release
    now checks every packaged program's DLLs
    (`tools/release/check_windows_dlls.py`).
- **No more yasio, curl, llhttp or websocket-parser in games.** Rolo now
  builds Axmol without its network module and remote console, which Rolo
  never used. The few yasio containers and helpers that Axmol's core still
  needs are replaced by Rolo's own code (`engine/runtime/axmol_compat`).
  Exported games no longer carry yasio's non-standard "Anti 996" license
  terms. See ADR 0012.
- Axmol is now patched at configure time from `cmake/axmol-patches/`. An
  existing `external/axmol` folder is patched in place the next time you
  configure.

## 0.8.0-alpha — Public alpha (Milestone 8)

The first version meant for people other than its authors. See
[known limitations](docs/known-limitations.md) before relying on it.

- **New Project wizard** with templates:
  - Empty Project;
  - Top-Down Starter: a small game that plays at once, with no C++;
  - the example game The Lantern Keeper.

  Also `rolo_editor --new-project FOLDER --template topdown`.
- **Exporting games**:
  - **Build** (or **File > Export Game…**) checks the project and makes a
    copy of the game that runs without the editor, with the license texts it
    must carry. Also `rolo_editor --project P --export OUT`.
  - The player plays a game bundled beside it when started with no arguments.
- **Built-in gameplay behaviours**, so games need no C++ to start:
  - Top-Down Controller: walking, with walk and idle clips;
  - Scene Door: arrives at a named object in the next scene;
  - Camera Bounds.

  `Game::changeScene(scene, arriveAt)`.
- **Packaging**: install rules and `rolo_package`, which makes the
  downloadable Rolo folder and zip. Release preset and workflows.
- **Licenses**: `licenses/` holds the full license texts of everything
  inside a game, fetched from each project at the versions used, and every
  export carries it. THIRD_PARTY_NOTICES.md now also lists the header-only
  libraries compiled into games. It also records the review of yasio's
  "Anti 996" terms.
- **Onboarding**:
  - a welcome screen on first start;
  - a getting-started guide, an exporting guide and release instructions;
  - clearer messages when a project, a template or the runtime is missing.
- The editor finds its templates, examples and the game runtime in an
  installed copy or in a build folder (`RoloResources`).

## 0.7.0 — The Lantern Keeper (Milestone 7)

- The Lantern Keeper, a complete demo RPG built from a fresh project, with a
  step-by-step guide to rebuilding it. An automated test plays it to the end.
- Make Start Scene in the editor (undoable).

## 0.6.5 — Flipbook animation (Milestone 6.5)

- Animation sets (`.lanim`), the Animator component, the Animation panel,
  and `Game::play` / `isAnimationFinished`.
- Fixes: the asset database dropped animation data on refresh; Enter in a
  dialog's text field now presses its main button.

## 0.6.0 — RPG content (Milestone 6)

- Variables, dialogues with choices and branches, quests, typed conditions
  and actions, triggers, events, the dialogue box, save games, content
  checks, and form-based content editing in the Inspector.

## 0.5.0 — Gameplay (Milestone 5)

- C++ behaviours in game modules, play mode in the editor, input,
  collision queries, scene changes, and the sample game.

## 0.4.0 — Assets and tilemaps (Milestone 4)

- Asset IDs and import, sprites, the Scene view camera, tilesets, tilemaps
  and painted collision.

## Earlier milestones

- 0.3: scenes and serialization, stable IDs, undo/redo (Milestone 3).
- 0.2: the editor shell with docking panels, built with Rolo UI (Milestone 2).
- 0.1: the Axmol runtime, the window and start-up (Milestones 0 and 1).
