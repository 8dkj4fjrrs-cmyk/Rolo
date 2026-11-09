# Rolo Editor guide

> Milestone 6: projects and scenes with sprites, tilemaps, colliders and
> text, imported assets, a free Scene view camera and transform tools, C++
> gameplay behaviours, Play mode, and story content: game variables,
> dialogues, quests and triggers, with saving and loading.

## Starting the editor

macOS:

```sh
./build/dev/bin/rolo_editor/rolo_editor.app/Contents/MacOS/rolo_editor
```

(or `open build/dev/bin/rolo_editor/rolo_editor.app`, without log output)

Linux / Windows: `build/dev/bin/rolo_editor/rolo_editor`

## The workspace

The editor uses the classic game-editor arrangement:

```
+------------------------------------------------------------------+
| File  Edit  View  Game  Help                                     |
| [save][undo][redo]  [main v] [>][||][>|] [revert]    [Build] [F] |  main toolbar
+-----------+-------------------------------------+----------------+
| Hierarchy |  Scene                              |  Inspector     |
|           |                                     |                |
+-----------+                                     |                |
| Assets    +-------------------------------------+                |
|           | Preview | Console | Tile Palette | Animation |          |
+-----------+-------------------------------------+----------------+
| fps                              (i) 12  (!) 0  (x) 0  Version   |  status bar
+------------------------------------------------------------------+
```

| Panel | What it does |
|---|---|
| **Hierarchy** | The scene's objects as a tree; the top row is the scene itself. Click to select. **+** creates an object (Empty, Rectangle, Ellipse, Sprite, Camera, Tilemap, or one inside the selected object). Type in **Search name** to show only matching objects. Delete or Backspace deletes the selected object. |
| **Assets** | The project's files, with each image's size and any problem. Click an asset to see it in the Inspector; double-click a scene to open it; **+** creates a scene or imports files. Files added, changed or deleted outside the editor show up within a few seconds. |
| **Scene** | The scene as you edit it, with a free camera: zoom, pan, and move, rotate, scale or resize objects (see [The Scene view](#the-scene-view)). The white frame shows what the player will see. |
| **Assets Preview** | The folder chosen in Assets, as thumbnails. Click to inspect, double-click to open, and drag an image or tileset into the Scene view to place it. |
| **Tile Palette** | The selected tilemap's tileset: click a tile to paint with it. With **Edit solid tiles** on, click tiles to make them block movement. |
| **Animation** | The selected animation set: its sprite sheet, its clips, the frames of the clip being edited, and a preview playing it (see [Animation](#animation)). |
| **Console** | Messages from the engine, the runtime and the editor, including any problems found when opening a scene. Filter by level or text, copy, clear. |
| **Inspector** | The selected object: its on/off switch and name, then one folding section per component with fields you can edit. **Add Component** adds a component (your game's behaviours are listed under *Gameplay*); a section's **⋮** menu removes or resets it; **(?)** explains it. With the scene row selected it shows the scene's name and file; with an asset selected, its preview and settings (see [Assets](#assets)). |

The toolbar's **Build** button exports the game for players (see
[Exporting](#exporting-a-game)). ▶ ⏸ ⏭ play the game (see
[Playing the game](#playing-the-game)).

The status bar shows the frame rate, how many messages, warnings and errors
the Console holds, and the Rolo version.

## Projects and scenes

A project is a folder holding your game: a `project.rolo` settings file,
a `scenes` folder and an `assets` folder (see [file formats](formats/scene.md)).

- **File > New Project…** asks what to start from, a name and a location
  (by default `Documents/Rolo Projects`; **Browse…** to choose), and
  creates the project's folder. To start from:
  - **Empty Project**: one empty scene.
  - **Top-Down Starter**: a small game that plays at once. Its parts:
    - two scenes joined by a path;
    - a hero who walks in four directions;
    - a friend to talk to and a first quest;
    - the dialogue box.

    It uses only built-in components, so no C++ is needed.
  - **Example: The Lantern Keeper**: a copy of the demo RPG to study and
    change.
- **File > Open Project…** lists recently opened projects. **Browse…**
  finds a project's folder (projects are marked with Rolo's flame), or
  type or paste its path. The editor reopens the last project at start.
- **Welcome**: when the editor starts without a project, it offers to start a
  new project, open the example, or open a project (**Help > Welcome…**
  shows it again).
- Without a project, the editor shows the start scene; you can edit it and
  save it into a new project.
- **File > New Scene**, **Save Scene**, **Save Scene As…** (also how you
  duplicate a scene) and **Revert to Saved**. The scene selector in the
  toolbar switches between the project's scenes.
- **The start scene** (where the game begins): select the scene row at the
  top of the Hierarchy; the Inspector shows whether this is the start scene,
  and **Make Start Scene** makes it so (undoable, saved in
  `project.rolo`).

A "•" after the scene's name (in the toolbar and the window title) means it
has unsaved changes. If you quit, close the window or open another scene
with unsaved changes, the editor asks whether to save them first.

## Assets

Images (PNG, JPEG), sounds (WAV, OGG, MP3) and fonts (TTF, OTF) are
*imported* into the project: copied into its `assets` folder (or the folder
chosen in Assets) and given a permanent ID, kept in a small `.meta` file
next to the file. Scenes refer to assets by ID, so renaming or moving an
asset in the editor never breaks anything.

- **Import:** drag files from Finder (or Explorer) onto the editor window,
  or use **File > Import Assets…**. You can also copy files into the
  project's folder yourself; they appear within a few seconds.
- **Select an asset** to see its preview, size, where it is used and its
  settings in the Inspector. An image's **Filtering** is *Pixel art* (crisp
  square pixels; the default for images up to 256 pixels) or *Smooth*.
- **Rename** an asset by editing its name in the Inspector; **Delete…**
  removes it (scenes that used it show a pink placeholder).
- **Place an image** in the scene: drag it from Assets Preview into the
  Scene view, or click **Place in Scene**. It becomes a Sprite at the image's
  own size (1 pixel = 1 point).
- **Listen to a sound:** select it and press **Play** (and **Stop**) in the
  Inspector. Dragging a sound into the Scene view (or **Place in Scene**)
  makes an object with a **Sound** component that plays it when the scene
  starts. See the [sound guide](sound.md).
- A damaged or unsupported file is listed with a warning, and the problem is
  shown in the Inspector and the Console.

## The Scene view

| To | Do |
|---|---|
| Zoom | Scroll (wheel or two fingers), around the mouse; or the zoom menu at the top right |
| Pan | Drag with the right or middle mouse button, or hold ⌥ (Alt) and drag |
| See the whole game view | The picture button in the Scene toolbar, or *Show the Game View* in the zoom menu |
| Frame the selection | F |
| Select | Click an object |

The tools (top left of the Scene view):

| Tool | Key | Use |
|---|---|---|
| Move | W | Drag the object, the red (x) or green (y) arrow, or the blue square. Shift snaps to the grid. Arrow keys nudge by 1 point (Shift: 10). |
| Rotate | E | Drag the circle. Shift turns in 15° steps. |
| Scale | R | Drag the red or green square along the object's own axes, or the middle one for both. |
| Rect | T | Drag a corner or edge of a shape or sprite to resize it. Shift keeps the proportions. |

Every drag is one undo step.

Colliders are drawn as outlines: green for solid ones (they block
movement), blue for areas such as doors and pick-ups. Select an object that
has only a collider by clicking inside its outline.

## Playing the game

Press ▶ in the toolbar (or **Game > Play**, ⌘P / Ctrl+P) to play the open
scene. The game runs in the Scene view, seen through the game's camera;
everything outside the player's view is dimmed and the view gets an amber
border.

- **Keyboard:** the game gets the keys while the Scene view has focus.
  Click the game first if the view says so. ⌘/Ctrl shortcuts still go to
  the editor.
- **Sound:** the game's sounds and music play; Pause holds them and Stop
  stops them. **Game > Mute Sound** silences the editor (remembered).
- **⏸ Pause** (⇧⌘P) freezes the game; press again to resume. **⏭ Step**
  (⌥⌘P) pauses and runs exactly one frame, to watch something closely.
- **■ Stop** (⌘P again) ends the game. The game always plays a *copy* of
  the scene, so stopping brings back the scene exactly as it was before
  playing, whatever the game did.
- While playing, the scene cannot be edited (the Console says so if you
  try). Saving still saves the scene as edited, not as played.
- Errors from gameplay code appear in the Console. A behaviour that fails
  is switched off until the next play; the rest of the game continues.
- If the game changes scene (e.g. through a door), the next scene is
  loaded from the project; the scene you are editing is used as it is now,
  unsaved changes included.
- **F5** saves the game to the "quick" slot and **F9** loads it. Saves made
  in Play mode are kept in the editor's settings folder (`play-saves`),
  never with the real game's saves.

Gameplay is written in Lua or C++: see the [scripting guide](scripting.md)
and the [gameplay guide](gameplay.md).

## Story content: dialogues, quests, variables

An RPG's story is content, not code ([formats](formats/content.md), and the
[gameplay guide](gameplay.md#story-content-dialogue-quests-and-triggers-without-code)
for how scenes use it).

**Create** content with **Game > New Dialogue / New Quest / New Variables**
or the Assets panel's **+**. Files go in the project's `content` folder;
rename them in the Inspector. Select one in Assets to edit it in the
Inspector:

- **Variables**: the game's variables, each with a type (bool, int, text)
  and the value a new game starts with. Type a name in the last row and
  press Enter to add one.
- **Quest**: name and description; *Starts When* and *Fails When*
  conditions (empty: only by an action / never); *Prerequisites*;
  *Objectives* (text shown to the player, *Done When*, *Optional*);
  *Rewards*.
- **Dialogue**: speakers (name and colour), where it starts, and its lines.
  A line has a speaker, what is said, and either *Then* (the next line) or
  choices (*Player Says*, *Shown When*, *Then*, actions). A *branch* has no
  text: it goes to the first line whose *If* is true, else *Otherwise*.
  The **⋮** menu of a line moves it up or deletes it.

**Conditions and actions are typed in a short form**, checked when you
leave the field (a mistake is shown under it in red, and nothing changes):

| Conditions | Actions (one per field) |
|---|---|
| `met_elder`, `not met_elder` | `set met_elder true` |
| `gems >= 3`, `name == "Ana"` | `add gems 1` |
| `quest gems is completed` | `startQuest gems`, `failQuest gems` |
| `objective gems return done` | `completeObjective gems return` |
| `gems >= 3 and (a or not b)` | `startDialogue elder`, `changeScene cave` |
| *(empty: always)* | `setActive Door off`, `destroy this` |
| | `emit door_opened`, `log "Hi {name}"`, `wait 1.5`, `saveGame quick` |
| | `playSound door_creak`, `playMusic theme`, `stopMusic` |

Quests, dialogues, scenes and sounds are named by file name (`gems` for
`content/gems.lquest`); objects by their name in the scene, or `this`.
Files store the full form, so renaming a file or an object never breaks
anything.

Every change is saved to the file at once and can be undone (⌘Z). Problems
in a file (a line leading nowhere, a variable that does not exist...) are
listed at the top of its Inspector page. **Game > Check Content** checks
everything, including the open scene's triggers, and lists the problems in
the Console; Play does the same each time it starts.

A **Trigger** on an object shows *Only If* and its *Actions* the same way,
below its other settings.

## Physics

Add a **Rigid Body** (**Add Component › Physics**) to an object with a
**Collider** and press Play: it falls, lands, and pushes and is pushed. A
Collider's **Shape** can be a box, a circle or a capsule; the Scene view
draws each as its shape (orange for objects with a Rigid Body). Select the
scene in the Hierarchy to set its **Gravity** in the Inspector, or press
**Platformer** (down) or **Top-Down** (none). See the [physics guide](physics.md).

## Tilemaps

1. Make a **tileset**: select an image of tiles in Assets and click **Create
   Tileset…** in the Inspector. Set the tile size (and margin and spacing if
   the image has gaps); the Inspector shows how many tiles that gives.
2. Make a **tilemap**: Hierarchy **+ > Tilemap** (it uses the project's first
   tileset), or drag the tileset into the Scene view.
3. With the tilemap selected, pick a tile in the **Tile Palette** and paint
   in the Scene view:

| Tool | Key | Use |
|---|---|---|
| Brush | B | Paint the chosen tile (click or drag). |
| Eraser | X | Remove tiles. |
| Fill | G | Fill the connected area of the same tile, up to the edges of the view. |
| Picker | I | Click a tile in the map to paint with it. |

**Layers** are listed in the tilemap's Inspector section, top layer first:
click a layer's brush button to paint on it, untick it to hide it, edit its
name, move it down, delete it, or **Add Layer**.

**Collision** (cells that block movement, used by gameplay in Milestone 5)
comes from two places, both shown in red in the Scene view while the
tilemap is selected:

- tiles marked solid in the tileset: in the Tile Palette, tick **Edit solid
  tiles** and click tiles. This is saved in the tileset, so every tilemap
  using it agrees;
- cells painted on the **Collision** layer: click its brush button in the
  Inspector and paint with the Brush (Eraser removes).

Each stroke, fill, and layer change is one undo step. So is marking a tile
solid or changing a tileset's settings.

## Animation

Characters come alive with **flipbook animation**: a sprite sheet (one image
holding every pose, in a grid of equal frames) and named **clips** such as
`walk_left` or `idle_down`, each a list of frames with a speed and whether
it loops.

1. **Make an animation set**: select the sprite sheet image in Assets and
   click **Create Animations…** in the Inspector. Give it a name and the size
   of one frame in pixels. The new `.lanim` file opens in the **Animation**
   panel. Its Inspector page sets the sheet, frame size, margin and spacing.
2. **Make clips** in the Animation panel: **+** adds a clip (named with
   letters, digits and `_`), the rename and **×** buttons rename or delete
   it, and **Clip** chooses which one you edit.
3. **Choose frames**: click a frame in the sheet to add it to the clip (after
   the selected frame, or at the end). **Shift-click** adds every frame from
   the last one up to the one clicked: a whole row in two clicks. The strip
   below shows the clip's frames in order; click one to select it, then use
   the arrows (or Alt+Left / Alt+Right) to move it and the eraser (or Delete)
   to remove it. Frames used by the clip are outlined in the sheet.
4. **Speed and looping**: *Speed* is frames per second; untick *Loop* for a
   clip that plays once and stays on its last frame (e.g. an attack). The
   preview plays the clip as the game will; **Pause** it to see the selected
   frame.
5. **Use it**: give an object an **Animator** component (Inspector **Add
   Component**), choose the animation set in *Animations* and the starting
   *Clip*. The object's Sprite keeps its size, colour and flipping; choosing
   the set sizes the Sprite to one frame. The Scene view plays the clip while
   you edit, and gameplay code switches clips while the game runs
   ([gameplay guide](gameplay.md#animation)).

Every change is saved to the file at once and is one undo step (typing a
speed is one step).

## Editing

Every change can be undone: **Edit > Undo** (⌘Z) and **Redo** (⇧⌘Z). The
Edit menu names what will be undone, e.g. "Undo Rename". The undo history
belongs to the open scene; opening another scene starts a new one.

Changes to assets in the Inspector or Tile Palette (an image's filtering, a
tileset's tile size or solid tiles) are undoable too, but they are saved to
the asset's file at once, so they do not count as unsaved scene changes.
Undoing one changes the file back, for every scene that uses it.

| To | Do |
|---|---|
| Create an object | Hierarchy **+**, or **Edit > New Rectangle** (and others) |
| Rename it | Edit the name at the top of the Inspector, or press F2 |
| Duplicate it (with its children) | ⌘D |
| Delete it | Delete / Backspace in the Hierarchy, or **Edit > Delete** |
| Change its drawing order | **Edit > Move Up / Move Down** (later siblings are drawn on top) |
| Hide it | Untick the box next to its name in the Inspector |
| Change a number | Type it and press Enter (Esc cancels), use ↑ ↓ to nudge, or drag the small label at the field's right end (X, Y, °) left and right |
| Change a colour | Type a hex value (`#ffb84d`), or click the swatch to pick one |

Positions are in points from the parent's position, with y pointing up.
Rotation is in degrees, clockwise.

### Arranging panels

- Drag a panel's tab to move it. A highlighted area shows where it will land:
  near the edge of another panel to put it beside, above or below that panel,
  or in the middle to add it as a tab there.
- Drag the borders between panels to resize.
- Close a panel with the **x** that appears when you hover over its tab, or
  from the panel's **☰** menu (top right of each panel); bring it back from
  the **View** menu.
- **View > Reset Layout** restores the default arrangement.

The layout and which panels are open are remembered between sessions.

## Keyboard shortcuts

| Shortcut | Action |
|---|---|
| ⌘S / ⇧⌘S | Save scene / Save scene as |
| ⌘N | New scene |
| ⌘Z / ⇧⌘Z (Ctrl+Z / Ctrl+Y) | Undo / Redo |
| ⌘D | Duplicate the selected object |
| ⌘P / ⇧⌘P / ⌥⌘P | Play or stop / Pause or resume / Step one frame |
| W, E, R, T | Move, Rotate, Scale, Rect tool |
| B, X, G, I | Brush, Eraser, Fill, Picker (with a tilemap selected) |
| F | Frame the selected object in the Scene view |
| F2 | Rename the selected object |
| Delete or Backspace (in the Hierarchy) | Delete the selected object |
| ⌘Q (macOS) / Ctrl+Q | Quit |
| Tab / Shift+Tab | Move keyboard focus between controls |
| ↑ ↓ ← → | Move through the Hierarchy (← → fold and unfold) and the Console |
| ⌘A, ⌘C, ⌘X, ⌘V (Ctrl on Windows/Linux) | Select all, copy, cut, paste in text fields |
| ⌥← ⌥→ (Ctrl on Windows/Linux) | Move by word in text fields |
| Esc | Close a menu or dialog |

## Where settings are kept

| OS | Folder |
|---|---|
| macOS | `~/Library/Application Support/Rolo Engine/Editor` |
| Windows | `%APPDATA%\Rolo Engine\Editor` |
| Linux | `~/.config/rolo-engine/editor` (or `$XDG_CONFIG_HOME/rolo-engine/editor`) |

Two files live there: `panels.dock` (panel arrangement, one line of text) and
`editor.prefs` (open panels, window size, recent projects). Deleting them is safe — the
defaults come back. (`layout.ini` and `layout.dock`, left by earlier preview
builds, are no longer used and can be deleted.)

On Windows and Linux, read Ctrl for ⌘ and Alt for ⌥.

## Exporting a game

**Build** (or **File > Export Game…**) makes a copy of the game that runs
without the editor, for the system the editor runs on. Choose:
- the game's name;
- the folder to export into (**Browse…**).

Rolo checks the project first: errors stop the export and are listed in
the Console. It then makes a folder (on macOS, an app) holding:
- the game's program;
- its files;
- the license texts that must go with it.

Zip it to share. Details: [exporting](exporting.md).

**Help** has the welcome window, the getting-started and exporting guides
(their folder opens), and About.

## Command-line options

```
--help                Show all options
--version             Print version information
--project FOLDER      Open the Rolo project in FOLDER
--new-project FOLDER  Create a new project in FOLDER and open it
--template NAME       With --new-project: start from a template (empty, topdown, lantern_keeper)
--export OUTPUT       With --project: check and export the game into OUTPUT, then quit (no window)
--no-project          Start without opening the last project
--reset-layout        Start with the default panel layout
--play                Start playing the opened scene at once (used by tests)
--config-dir DIR      Keep settings in DIR instead (useful for testing)
--frames N            Quit after N frames (used by automated tests)
--screenshot FILE     Save the last frame as PNG (needs --frames)
```
