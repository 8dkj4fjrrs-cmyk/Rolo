# Rolo file formats: projects, scenes and assets

Rolo saves projects and scenes as UTF-8 JSON text. The files are meant to
be readable, to diff cleanly in version control, and to keep working when
Rolo changes. This page describes format **version 1** of each.

Code: `engine/scene` (`SceneFile.hpp`, `Project.hpp`). Design reasons:
[ADR 0005](../adr/0005-scenes-and-serialization.md).

## A project

A project is a folder:

```
My Game/
  project.rolo      project settings
  scenes/
    main.lscene        one file per scene
  assets/              images, tilesets, animations, sounds, fonts (each with a .meta file)
```

`project.rolo`:

```json
{
  "format": "rolo.project",
  "version": 1,
  "name": "My Game",
  "startScene": "scenes/main.lscene"
}
```

| Key | Meaning |
|---|---|
| `format` | Always `"rolo.project"`. |
| `version` | Format version (an integer). |
| `name` | The game's name. |
| `startScene` | The scene the player opens first. |

**Paths inside a project are always relative to the project folder and use
forward slashes** (`scenes/main.lscene`), on every operating system. Absolute
paths and `..` that would leave the project are refused. A project can
therefore be moved, zipped, shared or put in git.

## A scene (`.lscene`)

```json
{
  "format": "rolo.scene",
  "version": 1,
  "name": "Main",
  "entities": [
    {
      "id": "43e4ca317c1f4e40",
      "name": "Rolo",
      "active": true,
      "components": [
        { "type": "rolo.Transform", "position": [640, 360], "rotation": 0, "scale": [1, 1] }
      ]
    },
    {
      "id": "c79279ca12809364",
      "parent": "43e4ca317c1f4e40",
      "name": "Flame",
      "active": true,
      "components": [
        { "type": "rolo.Transform", "position": [0, -8], "rotation": 0, "scale": [1, 1] },
        { "type": "rolo.Shape", "kind": "ellipse", "size": [44, 44], "color": "#ffb84dff",
          "filled": true, "lineWidth": 2 }
      ]
    }
  ]
}
```

### The scene

| Key | Meaning |
|---|---|
| `name` | The scene's name. |
| `gravity` | Physics: `[x, y]` in points per second per second. Optional, default `[0, -980]`; written only when it is not the default. Added after Rolo 0.9.1 without changing the version: older Rolo ignores it with a warning. |
| `entities` | The entities, below. |

### Entities

| Key | Meaning |
|---|---|
| `id` | The entity's permanent ID: 16 hexadecimal digits, unique in the scene. Never reused or changed. Other files (and later, saved games) refer to entities by this ID, not by name. |
| `parent` | ID of the parent entity. Missing for top-level entities. |
| `name` | Any text; names do not have to be unique. |
| `active` | `false` hides the entity and everything inside it. Optional, default `true`. |
| `components` | The entity's components (below). Every entity has a `rolo.Transform`. |

Entities are listed **parents before children**, and siblings in their order.
That order is also the drawing order: later siblings are drawn on top, and
children on top of their parent.

### Coordinates

World units are points. The origin is the **bottom-left** corner of the game
view and y points **up** (the convention of the runtime). Positions are
relative to the parent. Rotation is in degrees, **clockwise**. Scale 1 is
normal size.

### Components (version 1)

**`rolo.Transform`**: where the entity is. Every entity has exactly one.

| Key | Type | Default |
|---|---|---|
| `position` | `[x, y]` | `[0, 0]` |
| `rotation` | number, degrees clockwise | `0` |
| `scale` | `[x, y]` | `[1, 1]` |

**`rolo.Shape`**: a rectangle or ellipse centred on the entity.

| Key | Type | Default |
|---|---|---|
| `kind` | `"rectangle"` or `"ellipse"` | `"rectangle"` |
| `size` | `[width, height]`, not negative | `[100, 100]` |
| `color` | `"#rrggbb"` or `"#rrggbbaa"` | `"#ffffffff"` |
| `filled` | `true` / `false` | `true` |
| `lineWidth` | outline thickness when not filled | `2` |

**`rolo.Sprite`**: an image centred on the entity, stretched to `size`.

| Key | Type | Default |
|---|---|---|
| `image` | an image asset's ID, or `null` for none | `null` |
| `size` | `[width, height]` in points | `[100, 100]` (the editor sets the image's size when one is chosen) |
| `color` | tint, `"#rrggbbaa"` (white = the image's own colours) | `"#ffffffff"` |
| `flipX`, `flipY` | mirror the image | `false` |

**`rolo.Animator`**: plays a flipbook clip from an animation set (below)
on the entity's Sprite: the frames replace the Sprite's image, and the Sprite
still decides the size, colour and flipping. Without a Sprite, frames are
drawn at their size in pixels. Gameplay code switches clips
([gameplay guide](../gameplay.md#animation)).

| Key | Type | Default |
|---|---|---|
| `animations` | an animation asset's ID, or `null` | `null` |
| `clip` | the clip's id, e.g. `"walk_left"` (empty: the set's first clip) | `""` |
| `speed` | 0 to 20; 2 plays twice as fast | `1` |
| `playing` | `false` shows the clip's first frame | `true` |

**`rolo.Camera`**: what the player sees. The first active Camera (in
hierarchy order) centres the game view on its entity's position.

| Key | Type | Default |
|---|---|---|
| `zoom` | 0.05 to 20; 2 shows everything twice as large | `1` |

**`rolo.Tilemap`**: tiles from a tileset in layers, plus painted collision.
Cell (0, 0) has its bottom-left corner at the entity's position; x counts to
the right, y upwards.

| Key | Type | Default |
|---|---|---|
| `tileset` | a tileset asset's ID, or `null` | `null` |
| `cellSize` | `[width, height]` of one cell in points | `[16, 16]` |
| `data` | the layers and collision (below) | one empty layer "Ground" |

```json
"data": {
  "layers": [
    { "name": "Ground", "visible": true,
      "rows": [
        [0, -2, 3, 3, 3, -1, 5],
        [1, 0, 7, 7]
      ] }
  ],
  "solid": [[0, 4, 1, 1, 0, 1]]
}
```

Each row is `[y, first x, then one tile number per cell]`, so one row of
the map is one line of the file and a small change gives a small diff.
`-1` is an empty cell inside a row. `solid` uses the same layout with 1
(blocks movement) and 0 (gap). A cell blocks movement if it is painted
solid, or if a visible layer has a tile there that the tileset marks solid.

**`rolo.Collider`**: a shape for collision, centred on the entity (moved
by `offset`), in the entity's own coordinates (so scaled with it). Used by
gameplay code ([gameplay guide](../gameplay.md)) and physics ([physics](../physics.md)).

| Key | Type | Default |
|---|---|---|
| `size` | `[width, height]`, not negative: the box the shape fits in | `[32, 32]` |
| `offset` | `[x, y]` from the entity's centre | `[0, 0]` |
| `solid` | `true`: blocks movement; `false`: an area gameplay notices (a door, a pick-up) | `true` |
| `shape` | `"box"`, `"circle"` (the largest circle in the box) or `"capsule"` (the box with round ends). Optional (added after 0.9.1) | `"box"` |

**`rolo.RigidBody`**: physics moves the entity ([physics](../physics.md)). Needs a Collider.

| Key | Type | Default |
|---|---|---|
| `bodyType` | `"dynamic"`, `"kinematic"` or `"static"` | `"dynamic"` |
| `mass` | kilograms, more than 0 | `1` |
| `gravityScale` | how strongly gravity pulls it | `1` |
| `friction` | 0 to 2 | `0.4` |
| `bounciness` | 0 to 1 | `0` |
| `drag` | 0 or more | `0` |
| `lockRotation` | `true`: never turns | `false` |
| `fast` | `true`: continuous collision checks (bullets) | `false` |

A component's properties can never be called `type` or `data`: those keys
name the component and hold its extra data.

**`rolo.Text`**: text in the world, or on screen as a child of the Camera.
The entity's position is the top of the text (its left end, middle or right
end, by `align`).

| Key | Type | Default |
|---|---|---|
| `text` | the words; `{variable}` shows a game variable while playing | `"Text"` |
| `source` | `"text"`, `"dialogueSpeaker"`, `"dialogueLine"`, `"dialogueChoices"`, `"interactionPrompt"`, `"questLog"` | `"text"` |
| `font` | a font asset's ID, or `null` for Rolo's built-in font | `null` |
| `size` | points, 4 to 512 | `16` |
| `color` | `"#rrggbbaa"` | `"#ffffffff"` |
| `align` | `"left"`, `"center"`, `"right"` | `"left"` |
| `width` | wrap long lines at this width in points (0: no wrapping) | `0` |

**`rolo.Interactor`**: marks the player. `key`: `"E"`, `"Space"`, `"Enter"`
or `"Z"` (default `"E"`).

**`rolo.Trigger`**: runs actions when something happens (see the
[gameplay guide](../gameplay.md#story-content-dialogue-quests-and-triggers-without-code)).

| Key | Type | Default |
|---|---|---|
| `on` | `"interact"`, `"enterArea"`, `"leaveArea"`, `"sceneStart"`, `"timer"`, `"gameEvent"`, `"conditionMet"` | `"interact"` |
| `prompt` | shown for `interact`, e.g. `"Talk"` | `"Talk"` |
| `event` | the event name for `gameEvent` | `""` |
| `delay` | seconds, for `timer` | `1` |
| `once` | run once per playthrough | `false` |
| `data` | `{ "condition": <condition or null>, "actions": [<actions>] }` ([conditions and actions](content.md#conditions)) | no condition, no actions |

**`rolo.DialogueView`**: shown only during dialogues. `marker`: written
before the selected choice (default `"> "`).

**`rolo.TopDownController`**, **`rolo.SceneDoor`**, **`rolo.CameraBounds`**:
Rolo's built-in behaviours (added in 0.8; see the
[gameplay guide](../gameplay.md#built-in-behaviours-games-without-c)).

| Type | Key | Type | Default |
|---|---|---|---|
| `rolo.TopDownController` | `speed` | points per second, not negative | `150` |
| `rolo.SceneDoor` | `scene` | a scene asset's ID, or `null` | `null` |
| | `arriveAt` | the name of an object in that scene (empty: where the scene puts the player) | `""` |
| `rolo.CameraBounds` | `min`, `max` | `[x, y]` corners of the area the view stays in, world points | `[0, 0]`, `[1920, 1080]` |
| | `view` | `[width, height]` of the game's view, points | `[1280, 720]` |

**Behaviours** (gameplay code from a game module) are stored like any other
component: their `type` is the ID their module chose (e.g.
`"sample.PlayerController"`) and their keys are their properties. A scene
opened by a program that does not have the module keeps such components
exactly as they are and writes them back unchanged.

```json
{ "type": "sample.SceneDoor", "scene": "5a4d1e0000000005" }
```

## Asset IDs (`.meta` files)

Every image, tileset, animation set, sound, font, scene and script in a project has a small file
beside it holding its permanent ID:

```
assets/hero.png
assets/hero.png.meta    {"format": "rolo.asset", "version": 1,
                         "id": "8c1f02e94b7d3a60", "type": "image",
                         "filter": "pixel"}
```

| Key | Meaning |
|---|---|
| `id` | The asset's permanent ID: 16 hex digits. Scenes refer to assets by this ID. |
| `type` | `"image"`, `"tileset"`, `"animation"`, `"audio"`, `"font"`, `"scene"` or `"script"` (`.lua`), and the content types (from the file extension). |
| `filter` | Images: `"pixel"` (hard pixel edges, for pixel art) or `"smooth"`. |

Keep each `.meta` file next to its asset when moving files outside the
editor; renaming or moving inside the editor does this for you. A file
without one gets a new ID automatically (references to an old ID then show
as missing). A copied file *and* `.meta` is noticed and the copy gets a new
ID. Supported files: PNG and JPEG images, `.ltileset` tilesets, `.lanim` animation sets, `.lua` scripts, WAV, OGG and
MP3 sounds, TTF and OTF fonts, and `.lscene` scenes (scenes have IDs so that
gameplay can refer to them, e.g. a door leading to another scene; the
project's `startScene` stays a path). Files written by Rolo 0.4 have no
`"scene"` type; they simply gain `.meta` files for their scenes when opened
with 0.5.

## Tilesets (`.ltileset`)

```json
{
  "format": "rolo.tileset",
  "version": 1,
  "image": "8c1f02e94b7d3a60",
  "tileSize": [16, 16],
  "margin": 0,
  "spacing": 0,
  "solid": [3, 4, 17]
}
```

`image` is the image asset (by ID) cut into tiles of `tileSize` pixels,
with `margin` pixels around the edge and `spacing` between tiles. Tiles are
numbered from 0, left to right, then top to bottom. `solid` lists the tiles
that block movement wherever they are painted.

## Animation sets (`.lanim`)

A sprite sheet cut into equal frames (like a tileset), and named clips that
show some of those frames one after another. Added in Rolo 0.6.5.

```json
{
  "format": "rolo.animations",
  "version": 1,
  "image": "5a4d1e0000000007",
  "frameSize": [16, 24],
  "margin": 0,
  "spacing": 0,
  "clips": [
    { "id": "idle_down", "frames": [0], "fps": 1, "loop": true },
    { "id": "walk_down", "frames": [1, 0, 2, 0], "fps": 8, "loop": true },
    { "id": "attack", "frames": [12, 13, 14], "fps": 12, "loop": false }
  ]
}
```

| Key | Meaning |
|---|---|
| `image` | The sprite sheet (an image asset, by ID), or `null`. |
| `frameSize` | `[width, height]` of one frame in pixels (1 to 4096). |
| `margin`, `spacing` | Pixels around the edge of the sheet, and between frames (0 to 4096). |
| `clips` | The clips, each with an `id` (letters, digits and `_`, starting with a letter or `_`; unique in the file), `frames` (frame numbers, from 0, left to right, then top to bottom; a frame may repeat), `fps` (frames per second, more than 0 and at most 240; default 8) and `loop` (default `true`; `false` stays on the last frame). |

A clip with no frames is allowed (it shows nothing until frames are added).
Frame numbers past the end of the sheet are not an error in the file (the
sheet may be changed later); they are drawn as the sheet's first frame.

## How Rolo reads files

Loading is forgiving when that is safe, and strict when guessing would lose
or scramble data.

| Situation | What happens |
|---|---|
| A property or field that is not known | Skipped, with a warning. |
| A value of the wrong type or out of range | Replaced by the default (or clamped), with a warning. |
| A component type that is not known (from a newer Rolo, or a game module that is not loaded) | **Kept unchanged** and written back on save, with a warning. Nothing is lost. |
| An entity without a Transform | Gets the default Transform (position 0, 0). |
| A missing, malformed or duplicate `id` | **Error**: the file is not loaded. |
| A `parent` that is not an entity listed earlier in the file | **Error**. (Requiring parents first also makes loops impossible.) |
| `version` newer than this Rolo understands | **Error** asking you to update Rolo. |
| Not JSON, or `format` is not `rolo.scene` | **Error**. |

Every warning and error says where the problem is (entity name and ID,
component, property) and, where possible, what to do. The editor shows them
in the Console; the player prints them.

## How Rolo writes files

- Keys always appear in the same order, numbers are written as short as
  possible, short lists stay on one line: saving an unchanged scene gives an
  identical file, and a small change gives a small diff.
- Files are written **atomically**: to a temporary file first, then renamed
  over the old one. A crash or full disk never leaves a half-written scene.
- Editor state is never saved in a scene: no selection, no undo history, no
  panel layout, no runtime objects.

## Versions and migration

Each file states its format version. When a format changes, its version
number goes up, and Rolo gets an *upgrade step* that turns a version N
document into version N + 1 (`kUpgrades` in `SceneFile.cpp`). Older files are
upgraded step by step when they are read, and saved in the newest version.
Files from a **newer** Rolo are refused rather than half-read.
