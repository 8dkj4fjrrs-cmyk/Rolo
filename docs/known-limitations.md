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
- **Gamepads**: up to four, with the button layouts GLFW knows (most
  pads). Rumble ([controls guide](controls.md)) works on Windows with
  XInput pads (Xbox and alike), on macOS 11+ with pads the system can
  rumble (one strength, not two motors), and on Linux with force-feedback
  drivers. It was written without a real gamepad to try it on. The mouse position is available to C++ behaviours and scripts.
- Text with button pictures is not wrapped.
- The window is 1280 × 720 points. There is no fullscreen setting, and no
  built-in title screen or options menu (games make their own; the
  Platformer Starter has a pause menu). Escape quitting and the F5 / F9
  keys can be turned off per project ([controls guide](controls.md#escape-quitting-and-the-quick-save-keys)).
- **Physics** ([guide](physics.md)) has no joints, one-way platforms, ray
  casts or collision layers yet. Tilemaps changed while playing keep their
  old collision for physics. `Game::move` and overlap queries use boxes (a
  circle collider counts as the square around it; rotated colliders as the
  box around them).
- No lighting: the night in The Lantern Keeper is a dark overlay.
- Text is one font size per Text object. There is no localisation.
- Text is drawn by Rolo's own font renderer ([ADR 0025](adr/0025-no-freetype.md)):
  TrueType and OpenType fonts, with kerning. Not supported: colour emoji,
  writing that joins letters or runs right to left (Arabic, Hebrew, Indic
  scripts), and a second font for letters the chosen font lacks.
- Desktop only: no mobile or web.

## Editor

- **Touch** ([controls](controls.md#touch-screens)): the on-screen stick and
  buttons have one look and cannot be remapped; no gestures (scripts get the
  raw fingers); tried only in a desktop browser acting as a phone, not yet
  on a real phone.
- **Screen shapes** ([exporting](exporting.md#screens-of-other-shapes)):
  with Fill the Screen, what is pinned to which edge follows a rule of
  thumb (by where a Camera's child is placed), with no setting per object;
  changing the game's size does not move anything. Not tried on a real phone.
- **Android** ([exporting](exporting.md#for-android), [ADR 0028](adr/0028-android.md)):
  experimental. The editor exports games for Android (an `.apk` to try
  and a signed `.aab` for Google Play), built by Google's tools from
  Android Studio. On GitHub's machines (Linux) an export from the command
  line is built and played in an emulator (Android 14: touch, Back,
  pausing, quitting). Not yet done by anyone: running on a **real
  phone**, exporting on a **Mac or Windows**, exporting from the editor's
  window with real tools (its dialogs were tried with stand-ins), and
  uploading to Google Play. No gamepads or keyboards there, no care for
  notches, no adaptive icon, sound and speed unjudged, and a graphics
  context lost in the background is not handled.
- **Web games** ([exporting](exporting.md#for-the-web)): no rumble, and
  the shipped web runtime has no C++ game modules. Tested in headless
  Chromium only (by CI and by hand); Firefox and Safari are expected to
  work but have not been tried.
- **Custom gameplay code:** write it in **Luau** ([scripting guide](scripting.md));
  a downloaded Rolo runs scripts. **C++** behaviours still need Rolo
  built from source: game modules are linked into the editor and the player
  when they are built ([gameplay guide](gameplay.md)).
- Luau scripts, for now:
  - one Script per object (share code with `require`);
  - no per-object settings in the Inspector (put them in the script, or
    look at the object's name);
  - edits are picked up when Play is pressed again, not while playing;
  - no debugger: use `print`, which writes to the Console.
  - scripts cannot make new objects while playing yet (no spawning or
    cloning): switch premade ones on and off with `setActive`;
  - Luau's type annotations are accepted but not checked by Rolo (your
    code editor can check them, e.g. VS Code with the Luau extension).
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
