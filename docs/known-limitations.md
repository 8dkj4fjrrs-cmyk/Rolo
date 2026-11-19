# Known limitations (0.10.0-alpha)

Rolo is an alpha: it works end to end, from a new project to an
exported game, but much is missing or untested. This page lists what we
know. Please report anything else.

## What has been tested where

| | Linux x86_64 | macOS Apple silicon | Windows x64 |
|---|---|---|---|
| Builds | **Yes**, development and release | **Yes**, development build in CI (GitHub, macOS 15); release build by the release workflow | **Yes**, development build in CI (Windows Server 2022, MSVC); release build by the release workflow |
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

- Desktop games are exported for the system the editor runs on (a
  Windows game needs Rolo on Windows). Web games, and Android games
  (experimental), can be exported from any of the three.
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
- A game's size is set per project (1280 × 720 points unless changed;
  [screens of other shapes](exporting.md#screens-of-other-shapes)). There
  is no fullscreen setting, and no built-in title screen or options menu (games make their own; the
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
- No iOS. Web and Android games: see the next section.

## Web, touch and Android

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
  pausing, quitting), and the file for Google Play is checked with
  Google's bundletool and played from it. Not yet done by anyone: running
  on a **real phone**, exporting on a **Mac or Windows**, exporting from
  the editor's window with real tools (its dialogs were tried with
  stand-ins), and **uploading to Google Play**: the
  [publishing guide](google-play.md) is written from Google's help pages,
  not from an upload, and no pre-launch report has been seen. Sharing the
  signed `.apk` outside Google Play works in the emulator (installing,
  updating); Google's developer verification for such apps has not been
  gone through by anyone working on Rolo. Gamepads work there in the emulator (with Android's stand-in
  controller; no real one was tried, and rumble could not be tried at
  all). Keyboards work there with Android's
  injected keys (no real keyboard was tried; typing text is not a thing
  Rolo games do yet). Camera notches are
  kept clear of controls and pinned scores (tried with the emulator's
  notch; rounded corners are not measured, and with Fill the Screen off
  a notch is only avoided where the bars already cover it). If Android makes the app's
  screen anew while it runs (it does when the system's look is changed),
  the game starts over. No adaptive icon, sound unjudged. Speed on a real phone is unknown: the
  emulator draws without a graphics chip, so only Rolo's own share of a
  frame could be measured there. A graphics
  context lost in the background is recovered from (tried by making the
  emulator lose it on purpose).
- **Web games** ([exporting](exporting.md#for-the-web)): no rumble, and
  the shipped web runtime has no C++ game modules. Tested in headless
  Chromium only (by CI and by hand); Firefox and Safari are expected to
  work but have not been tried.
## Editor and scripting

- **Custom gameplay code:** write it in **Luau** ([scripting guide](scripting.md));
  a downloaded Rolo runs scripts. **C++** behaviours still need Rolo
  built from source: game modules are linked into the editor and the player
  when they are built ([gameplay guide](gameplay.md)).
- Luau scripts, for now:
  - one Script per object (share code with `require`);
  - [properties](scripting.md#properties-settings-in-the-inspector)
    (per-object settings in the Inspector) are numbers, texts, tick
    boxes, colours, points and other objects of the scene: no files
    (an image, a sound), lists or sliders yet. Another object is chosen
    from one long list of the scene's objects, and Check Content does
    not notice one that was deleted since (the script gets `nil`);
  - edits are picked up when Play is pressed again, not while playing;
  - no debugger: use `print`, which writes to the Console.
  - waiting (`task.wait`) works in `start` and in tasks, not in `update`
    or `collide`; a task cannot be stopped from outside;
  - [events](scripting.md#events) to connect to and wait for are game
    events (a name, no values), keys and actions, touching beginning
    and ending (with physics; not overlaps), an animation finishing and
    an object being destroyed. An object that is switched off misses
    what happens meanwhile;
  - an object cannot be moved to another parent from a script;
  - scripts make objects by copying one that is in the scene
    (`game.spawn`, `obj:clone()`); they cannot build one from nothing or
    add components, and the copies are not kept in saved games;
  - Luau's type annotations are accepted but not checked by Rolo (your
    code editor can check them, e.g. VS Code with the Luau extension).
- Scenes cannot be renamed, and folders cannot be created, in the editor.
  Use your file manager; the editor notices.
- One object can be selected at a time.
- [Prefabs](editor.md#prefabs-an-object-kept-as-a-file): copies follow
  their prefab, and the Inspector marks the fields that are a copy's
  own; a component the copy added or removed, a changed name and a
  Tilemap's changed tiles are counted there but can only be put back
  with **Reset** (the whole copy). A prefab can hold copies of other
  prefabs, but inside a copy such a part belongs to the outer prefab:
  it cannot be applied to its own prefab from there, and the Hierarchy
  does not mark it. A prefab cannot be a variant of another (its top
  object is always its own), and cannot be opened on its own (change a
  copy and Apply). Apply to Prefab
  cannot be undone and clears the open scene's undo history. Inside a
  copy, the prefab's objects come before the ones you added. Exporting
  warns about a scene whose prefab is missing but does not refuse it.
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
