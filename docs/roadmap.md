# Rolo roadmap

Rolo is built milestone by milestone; each one leaves the engine
building, tested and documented. Milestones 0–8 come from the original
project brief. Items marked *added* were agreed later and are recorded here
so the plan stays in one place.

| # | Milestone | Status |
|---|---|---|
| 0 | Technical verification and foundation | Done (0.1) — [ADR 0001](adr/0001-runtime-foundation.md) |
| 1 | Runtime foundation: window, start-up, logging | Done — [ADR 0002](adr/0002-runtime-module-and-axmol-integration.md) |
| 2 | Minimal custom editor: docking, viewport, panels | Done — [ADR 0003](adr/0003-editor-architecture.md), [ADR 0004](adr/0004-rolo-ui-toolkit.md) |
| 3 | Scenes and serialization, stable IDs, undo/redo | Done — [ADR 0005](adr/0005-scenes-and-serialization.md) |
| 4 | Sprites, assets, Scene view camera, tilemaps and collision data | Done (0.4) — [ADR 0006](adr/0006-assets-sprites-and-tilemaps.md) |
| 5 | C++ gameplay behaviours, game modules, play mode, input, collision queries, scene changes | Done (0.5) — [ADR 0007](adr/0007-gameplay-and-play-mode.md) |
| 6 | RPG content systems: dialogue, quests, conditions, actions, events, content validation, save games | Done (0.6) — [ADR 0008](adr/0008-rpg-content.md) |
| 6.5 | *Added:* flipbook (sprite-sheet) animation | Done (0.6.5) — [ADR 0009](adr/0009-flipbook-animation.md) |
| 7 | Complete sample game: a small top-down RPG built from a fresh project | Done (0.7) — [ADR 0010](adr/0010-demo-game.md), [the game](../examples/lantern_keeper) |
| 8 | Public alpha: project wizard, packaging and export, clean builds, onboarding, release | Done (0.8.0-alpha) — [ADR 0011](adr/0011-public-alpha.md) |
| — | *After the alpha:* Lua scripting; no yasio or networking; macOS and Windows verified in CI | Done (0.9.0-alpha) — [ADR 0012](adr/0012-no-yasio.md), [ADR 0013](adr/0013-lua-scripting.md); [known limitations](known-limitations.md) |
| — | *After the alpha:* renamed Rolo Engine; sound and music; 2D physics; gamepads, remapping, button pictures, rumble | Done (unreleased) — [ADR 0014](adr/0014-rename-to-rolo.md), [ADR 0015](adr/0015-sound.md), [ADR 0016](adr/0016-physics.md), [ADR 0017](adr/0017-gamepads.md), [ADR 0018](adr/0018-controls.md); [sound](sound.md), [physics](physics.md), [controls](controls.md) |
| — | *Direction* ([ADR 0019](adr/0019-direction.md)): for Roblox graduates; truly MIT; light; desktop, web and mobile; Lua-ux | Agreed 2026-11-09 |
| — | *After the alpha:* Escape quitting and F5 / F9 as project settings, `game.quit()`, a pause menu in the Platformer Starter | Done (unreleased) — [controls guide](controls.md#escape-quitting-and-the-quick-save-keys) |
| — | *Added, after the alpha:* timeline animation editor | Later |

## Milestone 6.5 — Flipbook animation (*added*)

Agreed on 2026-11-01, so that the sample RPG (Milestone 7) has animated
characters instead of sliding still images. It comes before Milestone 7 and
builds on what exists: image assets and their IDs, the way tilesets cut an
image into frames, the Inspector, and play mode.

Scope:
- **Animation clips as assets**: a sprite sheet cut into frames (like a
  tileset), named clips such as `idle`, `walk-left`, `walk-right`, each a
  list of frames with a speed (frames per second) and whether it loops.
- **An Animation editor panel**: choose frames by clicking the sheet,
  reorder them, set the speed and looping, and preview the clip playing.
- **An Animator component**: which clip set an object uses and which clip
  it starts with; the Scene view and play mode show it moving.
- **Gameplay API**: `play("walk-left")`, `stop()`, `isPlaying()`, and a way
  to notice a non-looping clip has finished (e.g. an attack).
- Versioned file format, undo for every edit, tests, and the sample game's
  hero walking in four directions.

Out of scope here: keyframing other properties (the timeline editor),
state machines, and skeletal animation.

Delivered in 0.6.5 as planned. The API names became `game.play(entity,
clip)`, `game.stopAnimation(entity)`, `game.isAnimationPlaying(entity)` and
`game.isAnimationFinished(entity)` (on `Game`, like the rest of the gameplay
API). The sample hero walks in four directions and the elder blinks.

## Milestone 7 — The Lantern Keeper

Delivered in 0.7: [The Lantern Keeper](../examples/lantern_keeper), an
original ten-minute RPG (a village, a wood and an old mine; a main quest and
a side quest; four characters; saving; an ending), built from a fresh
project and documented step by step in
[Making The Lantern Keeper](making-the-lantern-keeper.md). An automated test
plays it from the first line to the ending.

Gaps found while building it, and what happened to them:
- The editor could not choose a project's start scene: added (**Make Start
  Scene** in the scene's Inspector page).
- Scenes cannot be renamed in the editor, and folders cannot be created in
  the Assets panel. Noted for Milestone 8's onboarding work.
- Arriving at the right place after a scene change, and keeping the camera
  inside the map, are written as small behaviours in the game's module.
  They are common needs, so they are candidates for built-in components.
- The game uses a dark overlay for night. Real lighting is not planned.

## Milestone 8 — Public alpha

Delivered in 0.8.0-alpha:
- the New Project wizard (Empty, Top-Down Starter, the example game);
- built-in behaviours for code-free top-down games;
- exporting stand-alone games (in the editor and from the command line);
- the installable package and release workflow;
- license texts shipped with every game;
- a welcome window and folder browser;
- guides for getting started, exporting and releasing;
- the [known limitations](known-limitations.md).

## Next, by the direction (ADR 0019)

In order:

1. **A stress test** (`examples/stress`): thousands of scripted objects,
   bullets made and removed, physics; frame times and worst frames
   measured, as the baseline for everything below.
2. ~~**Luau** in place of Lua 5.4, unmodified, with a guide for existing
   scripts.~~ **Done** ([ADR 0020](adr/0020-luau.md)): about 2.3× faster
   in the language; calls into Rolo are now the slow part.
3. **Lua-ux's performance layer**, each part measured: cheaper calls
   into Rolo (the slow part now), garbage-free vectors, APIs that make no
   garbage, collector pacing, the engine's cost per object (texts,
   behaviours, physics looking at every object; `game.find` searching all
   of them), one script for many objects, native code where allowed, a
   script profiler. Also: making and removing objects from scripts
   (spawning).
4. **Web export** (WebAssembly).
5. **Touch controls** (taps, an on-screen stick and buttons, as actions)
   and **screen sizes** (scaling for any shape of screen).
6. **Android**, then **iOS**.
7. **A "Coming from Roblox" guide**, and script properties in the
   Inspector (Roblox's Attributes).

Alongside: the editor polish below. Later: 3D.

## After the alpha

The list made after the alpha, in its original order (some of it is
above now):

1. **Verify macOS and Windows.** ~~Put the repository on GitHub, let CI build
   and test all three systems, and fix what they reveal.~~ On GitHub, macOS
   builds and passes every test, including those in real windows, and
   Windows builds, passes the unit tests and exports games (its CI
   machines cannot open OpenGL windows). Still to do: using the editor by
   hand on both, and running Windows' window tests somewhere with a screen.
2. **Custom gameplay without building Rolo**: ~~games' own modules
   loaded by a downloaded editor~~ — **Lua scripts** do this (ADR 0013).
   Next for scripts:
   - script properties shown in the Inspector;
   - more than one script per object;
   - reloading scripts while the game plays.

   Loading compiled C++ modules is still open, for games that need C++.
3. ~~**Sound**~~ — **done** with miniaudio instead of Axmol's audio
   (ADR 0015). Next for sound: positional sound (louder near the camera),
   a volume menu for players. ~~**Gamepad input**~~ — **done** (ADR 0017),
   with rumble, remapping and button pictures (ADR 0018).
4. **Signing** of macOS apps and Windows programs, and installers.
5. **Physics, next** (ADR 0016): joints, one-way platforms, ray casts,
   collision layers.
6. Smaller gaps:
   - renaming scenes and creating folders in the editor;
   - multi-selection;
   - prefabs;
   - fullscreen and window settings for games;
   - ~~removing Axmol's unused networking libraries (and yasio's Anti 996
     terms with them)~~ — **done** after the alpha (ADR 0012).
7. The timeline animation editor (below) and a visual dialogue graph.

## After the alpha: timeline animation editor (*added*)

Agreed on 2026-11-01 as a post-alpha item. A timeline editor animates by
keyframes: rows for properties of objects (position, rotation, scale,
colour, a sprite's image), keyframes at moments in time, curves for how
values move between keyframes (steady, easing, bounce), and events at chosen
times (e.g. play a sound). Uses: doors and chests opening, effects, UI
movement, and cutscenes.

Why after the alpha: it is one of the largest editor pieces (time ruler,
property rows, keyframe dragging, curve editing, scrubbing and preview,
recording changes from the Scene view, undo, file format, runtime
playback). Before then, walking characters are covered by flipbook clips
(6.5), small effects by behaviour code, and scripted sequences by
Milestone 6's events. Rolo's property system means any component property
can become a timeline row without extra work per component, which is the
main thing the earlier milestones should keep true.

## Also noted for later

- A visual (graph) dialogue editor. Milestone 6 edits dialogues as forms in
  the Inspector, as the brief asks (runtime and validation before complex
  visual editors).
- Item and inventory definitions, and localisation of text: listed among
  the brief's eventual content editors, not yet planned in a milestone.
- Multi-line text fields in Rolo UI (long dialogue lines are edited in
  one-line fields today; they wrap in the game).

Possible later steps for animation, not planned: animation state machines (switching
clips automatically from game values) and skeletal animation (better
imported from dedicated tools such as Spine or DragonBones than built).
