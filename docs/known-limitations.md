# Known limitations (0.9.1-alpha)

Rolo is an alpha: it works end to end, from a new project to an
exported game, but much is missing or untested. This page lists what we
know. Please report anything else.

## What has been tested where

| | Linux x86_64 | macOS Apple silicon | Windows x64 |
|---|---|---|---|
| Builds | **Yes**, development and release | **Yes**, development build in CI (GitHub, macOS 14, Xcode 15); release build not yet | **Yes**, development build in CI (Windows Server 2022, MSVC); release build not yet |
| Automated unit tests | **Yes** | **Yes**, in CI | **Yes**, in CI |
| Automated tests in real windows (playing, the editor, exported games) | **Yes**, on a virtual display | **Yes**, in CI | Not yet: CI machines have no screen that runs OpenGL 3 |
| Exporting a game | **Yes** | **Yes**, in CI (an `.app`, then started) | **Yes**, in CI (the export; starting it needs a screen) |
| Editor used by hand | **Yes**, on a virtual display | **Yes**: the downloaded 0.9.1-alpha, by its author | No |

So far:
- the macOS app bundles, Metal through Axmol and the exported `.app` have
  only run on CI machines;
- on Windows, the editor and games have been built and exported, but not
  yet opened in a window.

Please report what you find.

## Exporting

- Games are exported for the system the editor runs on; there is no
  cross-export.
- macOS apps are signed only "ad hoc", **not with an Apple Developer ID**,
  and not notarized. The first time, players right-click the app and choose
  **Open** (on newer macOS: **System Settings › Privacy & Security › Open
  Anyway**). The same goes for Rolo itself.
- No installer: games are folders (or `.app` bundles) to zip and share.
- Every game contains the whole runtime (about 16 MB on Linux), even the
  parts a small game does not use.
- Games cannot use the network: Rolo builds Axmol without HTTP,
  WebSocket and downloads (ADR 0012).

## Games

- **Sound** ([guide](sound.md)) is effects and music, without positions:
  no 3D or distance effects (a script can set an effect's volume and pan),
  no filters or reverb, and no options menu for players to change volumes
  (a script can, with `audio.setVolume`). MP3 files do not loop seamlessly;
  use OGG for music.
- **Keyboard only**: no gamepads or touch. The mouse position is available
  to C++ behaviours.
- The window is 1280 × 720 points. There is no fullscreen
  setting, title screen, pause menu or options menu. Escape quits at once,
  and **F5 / F9 save and load** in every game.
- Collision uses axis-aligned boxes and tiles; there is no physics engine.
  Rotated colliders use the box around them.
- No lighting: the night in The Lantern Keeper is a dark overlay.
- Text is one font size per Text object. There is no localisation.
- Desktop only: no mobile or web.

## Editor

- **Custom gameplay code:** write it in **Lua** ([scripting guide](scripting.md));
  a downloaded Rolo runs scripts. **C++** behaviours still need Rolo
  built from source: game modules are linked into the editor and the player
  when they are built ([gameplay guide](gameplay.md)).
- Lua scripts, for now:
  - one Script per object (share code with `require`);
  - no per-object settings in the Inspector (put them in the script, or
    look at the object's name);
  - edits are picked up when Play is pressed again, not while playing;
  - no debugger: use `print`, which writes to the Console.
- Scenes cannot be renamed, and folders cannot be created, in the editor.
  Use your file manager; the editor notices.
- One object can be selected at a time. There are no prefabs (reusable
  objects); copy and paste or duplicate instead.
- Dialogue is edited as forms in the Inspector, with one-line text fields.
  There is no visual dialogue graph.
- No timeline animation: flipbook clips only (see the [roadmap](roadmap.md)).
- Rolo has its own folder browser, not your system's file dialog.
- Changes to anything but positions rebuild the drawn scene; fine for
  scenes of hundreds of objects, not tested with thousands.

## Files

- Formats are versioned and documented ([formats](formats/scene.md)). Until
  1.0 a format may still change. Rolo will upgrade older files when it
  does, as it promises in each format's description.
- Saves made with one version of a game keep working after the game changes
  (see [content formats](formats/content.md#save-games-lsave)). Saves are
  not encrypted or protected.
