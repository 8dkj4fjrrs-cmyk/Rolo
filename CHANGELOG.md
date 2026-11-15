# Changelog

All notable changes to Rolo Engine. Versions follow the milestones in
[docs/roadmap.md](docs/roadmap.md); design reasons are in [docs/adr](docs/adr).

## Unreleased

- **Publishing on Google Play** ([guide](docs/google-play.md), ADR 0028
  step 3; **Help › Publishing on Google Play**): from the developer
  account to the release, checked against Google's rules of November
  2026. Nobody has uploaded a Rolo game yet; the guide says what is and
  is not tested.
  - **Saved games are now in Android's backup** (only the `saves`
    folder): they come back on a new phone or after reinstalling.
  - Exporting with the file for Google Play also writes
    `Google Play icon (512).png` for the store's page, raises the
    project's version code for the next export, and refuses package
    names that start with `com.example` or still say `yourname`.
  - The Android workflow checks the `.aab` with Google's bundletool
    (used only on GitHub's machines; not part of Rolo or of games),
    plays the app made from it, and checks the backup.
- **Exporting for Android** (experimental; [guide](docs/exporting.md#for-android),
  [ADR 0028](docs/adr/0028-android.md)): **Export Game… › Android** makes
  an `.apk` to try on a phone or emulator and, with your upload key, a
  signed `.aab` for Google Play.
  - Needs Android Studio installed (for Google's tools: the Android SDK
    and Java), and nothing else: Rolo's download carries the player
    already built for Android. Rolo finds the tools, checks them, and
    says in plain words what is missing.
  - Asks for the package name, version, orientation and icon, and keeps
    them in the project (`project.rolo`, `"android"`).
  - **New Key…** makes an upload key. Its password is typed at every
    export and never stored.
  - From the command line: `rolo_editor --project P --export-android OUT`
    (`rolo_editor --help` lists the options).
  - Tested on GitHub's Linux machines (exported from the command line,
    built, and played in an emulator). Not yet tried on a Mac, on
    Windows, or on a real phone.
- **Android, step 1** ([ADR 0028](docs/adr/0028-android.md),
  [building](docs/building.md#for-android)): experimental. Rolo's player
  builds as an Android app with one game inside (`player/android`, a
  Gradle project), and a separate workflow on GitHub plays it in an
  emulator: the first scene, the on-screen controls, Back (as Escape),
  going to the background and back, quitting. Not yet on a real phone.
  - On Android the game is unpacked from the app into the app's own
    folder on the first start; saved games are kept beside it; messages
    go to Android's log (tag `Rolo`). The app asks for no permissions.
  - `rolo_pack PROJECT OUTPUT.pack` (`tools/pack`) packs a project into a
    game pack from the command line.
  - When a game is set aside (its window minimised; on a phone, another
    app in front), its sound now rests too and goes on where it was
    (`AudioEngine::setSuspended`, `Application::setBackgroundCallback`).
- **Screen sizes** ([ADR 0027](docs/adr/0027-screen-sizes.md),
  [guide](docs/exporting.md#screens-of-other-shapes)):
  - **Game Size**: each project has its own size (1280 × 720 unless
    changed), upright sizes for phone games included.
  - **Fill the Screen**: instead of bars, the picture fills screens of
    other shapes and shows more of the world. Scores and menus (children
    of the Camera) stay at the screen's edges by themselves; Camera
    Bounds and `game.viewSize()` know how much is visible.
  - Both are in the Inspector's *Game (whole project)* section. The
    Platformer and Top-Down Starters fill the screen.
  - Web games are sharp on high-density (Retina, phone) screens: the page
    tells the game how many pixels to draw. Before, they were always
    1280 × 720 pixels, scaled.
  - The player's window is the game's size.
- **Touch controls** ([ADR 0026](docs/adr/0026-touch-controls.md),
  [guide](docs/controls.md#touch-screens)): on phones and tablets Rolo
  shows a stick and a round button for each of the game's actions, by
  itself. Pressing a button is pressing the action, so games that ask for
  actions (and Rolo's own movement and dialogue) play on a phone with no
  more work. Web games are now playable on phones.
  - `input.touches()` gives scripts the other fingers; the first finger
    is also the pointer and the left mouse button (taps work as clicks).
  - `input.lastDevice()` can be `"touch"`; `{button:jump}` then draws
    the on-screen button.
  - Project options: **Touch Controls** (Inspector, or `"touchControls":
    false`) and `"touchButtons": [...]` to choose the buttons.
  - `rolo_player --touch` makes the left mouse button a finger, to try it
    on a computer.
  - The web page keeps fingers from scrolling or zooming it, and now
    fits phone screens: before, the game's picture overflowed the page
    there.
  - The Platformer Starter's pause menu takes the Jump button on touch.
- **No FreeType: Rolo draws game text itself**
  ([ADR 0025](docs/adr/0025-no-freetype.md)), with the font code the
  editor already used (stb_truetype, MIT or public domain). Texts,
  button pictures and the frame-rate line look as before to within a
  pixel; side-by-side screenshots were compared first.
  - Games are smaller: the Linux game program went from 13.1 MB to
    12.0 MB, the web runtime from 5.07 MB to 4.40 MB (1.75 MB as servers
    send it).
  - Chinese, Japanese and Korean text wraps between characters.
  - `licenses/freetype.txt` is gone. Every large library in games is now
    MIT or public domain; what is left that is not MIT is a handful of
    small permissive libraries inside Axmol (`THIRD_PARTY_NOTICES.md`).
  - Not supported (see [known limitations](docs/known-limitations.md)):
    colour emoji, right-to-left and joined scripts, fallback fonts.
- **No libpng and no OpenSSL in Rolo or its games**
  ([ADR 0024](docs/adr/0024-no-libpng-no-openssl.md)):
  - Rolo decodes PNG images itself, with the decoder it already used for
    JPEGs (stb_image), and writes screenshots itself (stb_image_write);
    both are MIT or public domain. Colour PNGs are drawn exactly as
    before.
  - **Fixed: greyscale PNG files were drawn tinted and see-through.** They
    now look as they should (grey, grey with transparency, 16-bit).
  - OpenSSL was in every game for two Axmol features Rolo never used
    (file digests, an encrypted settings file). It is gone; Windows games
    no longer carry `libcrypto` and `libssl` DLLs.
  - Games are smaller: the Linux game program went from 17.8 MB to
    13.1 MB, the web runtime from 5.21 MB to 5.07 MB.
  - The frame-rate overlay (`--stats`) is Rolo's own now: one line with
    frames per second, milliseconds per frame, draw calls and vertices.
  - `licenses/libpng.txt` and `licenses/openssl.txt` are gone.
- **No JPEG library in Rolo or its games** ([ADR 0023](docs/adr/0023-no-jpeg-library.md)):
  JPEG images are decoded by Rolo itself, with stb_image (MIT or public
  domain), which was in every game already. libjpeg-turbo (desktop) and
  IJG libjpeg (web) are gone, with their licenses. JPEG files in games
  work as before: plain, progressive and greyscale.
- **Games for the web** ([ADR 0022](docs/adr/0022-web-export.md),
  [exporting](docs/exporting.md#for-the-web)): **Export Game… › Web
  browsers** (or `--export-web`) makes a folder for any web host (itch.io,
  GitHub Pages): a page, Rolo's web runtime (WebAssembly) and the game as
  one file. Players open a link; nothing to install.
  - The Platformer Starter is 5.7 MB as a web game (about 2.2 MB as
    servers send it).
  - Keyboard, mouse and gamepads; saved games kept by the browser; sound;
    Luau scripts, physics and everything else of Rolo's own. Not yet:
    touch controls, rumble, C++ game modules in the shipped web runtime.
  - Rolo's download contains the web runtime; from source it is built
    with Emscripten 4.0.9 (`emcmake cmake --preset web`,
    [building](docs/building.md#for-the-web)).
  - New in web games' licenses folder: Emscripten and musl (MIT), and
    LLVM's runtime libraries (Apache-2.0 with LLVM exception).
  - `tools/web/check_web_game.js` plays an exported web game in a
    headless browser; CI does so with two templates.
- **Much faster with many objects** ([ADR 0021](docs/adr/0021-performance-layer.md),
  [measurements](examples/stress/README.md#the-performance-layer-after-luau)):
  in the stress test, 2000 scripted objects take 0.6 ms a frame instead of
  2.5, a busy scene 1.4 instead of 4.5, and 5000 still objects cost nothing
  measurable (2.2 ms before).
  - The engine no longer looks at every object every frame: the scene
    keeps lists by component and by name. `game.find` is a lookup, not a
    search.
  - Calls between scripts and Rolo are cheaper.
  - Every script function that takes `x, y` also takes one Luau vector,
    for sums without garbage ([fast scripts](docs/scripting.md#writing-fast-scripts)).
  - C++: `Scene::all<T>()`, `entitiesWith`, `findByName`, `inOrder`,
    `structureRevision`.
- **Scripts are Luau now** ([ADR 0020](docs/adr/0020-luau.md)), Roblox's
  Lua (MIT, unmodified), instead of Lua 5.4.
  - Types, `+=`, `continue`, string interpolation, `//`, `bit32`,
    `buffer` and `vector`; `.luau` files are scripts too.
  - About 2.3× faster for code that runs in the language, with shorter
    garbage-collection pauses ([stress test](examples/stress/README.md#luau-against-lua-54)).
  - The shared libraries are read-only; `loadstring`, `getfenv` and
    `setfenv` are not available.
  - A stuck script is stopped after 10 seconds, noticed by a watchdog
    thread instead of counting instructions (which slowed every script).
  - Scripts written for 0.9: see [the scripting guide](docs/scripting.md#scripts-written-for-rolo-before-luau-09-and-earlier).
    Rolo's own templates and examples needed no changes.
- **Rolo's direction** ([ADR 0019](docs/adr/0019-direction.md)): a free,
  truly MIT 2D engine for Roblox graduates, for desktop, web and phones,
  scripted in Lua-ux (unmodified Luau plus a measured performance layer).
- **A stress test** ([examples/stress](examples/stress/README.md)): seven
  scenes (2000 scripted objects, garbage-making scripts and their tidy
  twin, bullets, physics, 5000 sprites, everything at once), `rolo_stress`
  to measure gameplay frame times without a window, and the player's
  `--frame-times`. The baseline (Lua 5.4) is in its README.
  - `Game::lastFrameTimings()` (scripts, physics, the rest),
    `scriptMemory()` and `scriptMemoryAllocated()`, for measuring.
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
