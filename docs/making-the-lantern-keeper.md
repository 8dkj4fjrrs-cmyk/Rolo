# Making The Lantern Keeper

This guide rebuilds [The Lantern Keeper](../examples/lantern_keeper), Rolo's
demo RPG, from an empty project, in the order a game usually comes
together. Each step says what to do in the editor and which engine feature
it uses. The finished game is in `examples/lantern_keeper/project`. Open it
next to your own copy whenever you want to see exactly how something was
set up.

You need:
- Rolo built (see [building](building.md));
- the art: copy the PNG files from `examples/lantern_keeper/project/assets/art`,
  or draw your own at the same sizes;
- the game's C++ module, `keeper` (four small behaviours, described in
  step 7). It is built into the editor by default.

If something is unclear, the [editor guide](editor.md) explains every panel,
and the [gameplay guide](gameplay.md) explains triggers, dialogue and C++.

## 1. A fresh project

**File > New Project…**, name it *The Lantern Keeper*. You get a folder with
`project.rolo`, `scenes/main.lscene` and an empty `assets` folder.

## 2. Art

Drag the PNG files onto the editor window (or **File > Import Assets…**).
They go into the folder chosen in the Assets panel (`assets` at first; the
finished game keeps them in `assets/art`, but any folder works).
Small images import with **Filtering: Pixel art**, which keeps their pixels
crisp.

| File | What it is |
|---|---|
| `tiles.png` | 24 tiles of 16 × 16 px: grass, path, water, walls, trees, houses, forest floor, mine… |
| `hero_walk.png` | Pip: 3 columns (standing, left foot, right foot) × 4 rows (down, left, right, up) of 16 × 24 frames |
| `maren.png`, `wren.png`, `nell.png` | villagers: two 16 × 24 frames each |
| `biscuit.png`, `flame.png` | the cat (two 16 × 16 frames), a lost flame (four) |
| `lantern.png` | the great lantern: dark, lit, lit (32 × 64 frames) |
| `shrine.png`, `gate.png`, `sign.png` | the wayshrine (two frames), the mine gate, a signpost |

## 3. The tileset

Select `tiles.png`, click **Create Tileset…**, tile size 16 × 16. Then mark
the tiles that block movement. Open the **Tile Palette** (select any
tilemap first; see step 6), tick **Edit solid tiles**, and click water,
walls, trees, house walls, roofs, doors, fences, bushes, rocks, the well,
crates and reeds.

## 4. Animations

For each sheet, select the image and click **Create Animations…**, with the
frame size from the table above. In the **Animation** panel, use **+** to add
clips, then click frames in the sheet. **Shift-click** adds a run of frames.

| Animation set | Clips (frames, speed) |
|---|---|
| `hero.lanim` | `idle_down` (0), `walk_down` (1 0 2 0, 8 fps), and the same for `left` (3–5), `right` (6–8), `up` (9–11) |
| `maren.lanim`, `wren.lanim` | `idle`: frame 0 many times, then 1, at 6 fps (a blink) |
| `nell.lanim` | `idle`: 0 0 0 1 0 1 at 4 fps (a hop) |
| `biscuit.lanim` | `idle`: 0 0 1 1 0 1 at 3 fps (a tail flick) |
| `flame.lanim` | `flicker`: 0 1 2 3 at 8 fps |
| `lantern.lanim` | `dark`: 0; `lit`: 1 2 1 1 2 at 6 fps |
| `shrine.lanim` | `idle`: 0 0 1 1 at 2 fps |

The clip names matter for `hero.lanim`: the PlayerController plays
`walk_<direction>` and `idle_<direction>`.

## 5. The story's data

**Game > New Variables** (`keeper.lvars`):

| Variable | Type | Starts | Meaning |
|---|---|---|---|
| `flames` | int | 0 | lost flames found |
| `met_maren`, `met_wren` | bool | false | they have asked for help |
| `cat_found` | bool | false | Biscuit was found |
| `has_key` | bool | false | Wren gave Pip the mine key |
| `lantern_lit` | bool | false | the lantern burns again (the ending) |
| `arrival` | text | "" | used by Scene Doors (step 7) |

**Game > New Quest**, twice:

- **lantern**: *The Dark Lantern*.
  - *Starts When*: `met_maren or flames >= 1`.
  - Objectives:
    - `find`: "Find the lost flames ({flames}/3)", *Done When* `flames >= 3`.
    - `light`: "Light the great lantern", *Done When* `lantern_lit`.
- **biscuit**: *Where Is Biscuit?*
  - *Starts When*: `met_wren or cat_found`.
  - Objectives:
    - `find`: "Find Biscuit in Whisperwood", *Done When* `cat_found`.
    - `tell`: "Tell Wren", with no *Done When*: the dialogue completes it.
  - Reward: `set has_key true`.

**Game > New Dialogue**, one per conversation. (Objects whose names have spaces are written in quotes in conditions and
actions: `setActive "Lantern Light" on`.) Conversations that change as
the story moves on start with a **branch**: a node without text that jumps
to the first line whose *If* is true. Maren's, for example:

```
begin (branch):  If lantern_lit                                -> after
                 If quest lantern is active and flames >= 3    -> ready
                 If met_maren                                  -> waiting
                 Otherwise                                     -> greet
greet:   "Pip! There you are. Did you see? ..."      Then -> scattered -> ask
ask:     "Will you find the lost flames...?"
           > "I'll find them, Keeper."   actions: set met_maren true   Then -> accepted
           > "Where did they go?"                                      Then -> where (-> ask)
```

The other dialogues work the same way:
- `wren`: her `found` line runs `completeObjective biscuit tell`.
- `biscuit_found`: runs `set cat_found true`.
- `lantern`: if `flames >= 3`, runs `set lantern_lit true`.
- `gate`: if `has_key`, runs `emit gate_open`.
- `flame`: shows the count.
- `intro`, `shrine`, `nell`, the signs and `biscuit_home`: just talk.

Copy the text from the finished project, or write your own.

## 6. The village

**File > New Scene**, then **File > Save Scene As…** `village` (scenes cannot
be renamed in the editor yet; delete the empty `main.lscene` in your file
manager if you like).

1. **Ground**: Hierarchy **+ > Tilemap**, choose `tiles` in the Inspector,
   and set the Transform's scale to 3 × 3. One 16 px tile is then 48
   points, so the pixel art is three times its size. Paint the village with
   the Brush and Fill tools:
   - a border of trees;
   - the cobbled square at the top;
   - Maren's house (left) and the bakery (right), each with roof, walls,
     windows and a door;
   - the road east;
   - a pond, a well, Nell's house and a fenced garden.
2. **People**: for each of Maren, Wren and Nell, add a Sprite and set it up:
   - scale 3;
   - an **Animator** with their animation set and clip `idle`;
   - a solid **Collider** around the feet (12 × 6, offset 0, −8).

   Add an empty child called *Talk Area* with a non-solid Collider (26 × 24)
   and a **Trigger**: *On* `interact`, *Prompt* "Talk", action
   `startDialogue maren` (or `wren`, `nell`).
3. **The lantern**: a sprite with `lantern.lanim` clip `dark`, with a *Look
   Area* (`startDialogue lantern`). Put a second copy, *Lantern Light*, on
   the same spot with clip `lit`, switched off (untick it).
4. **The wayshrine**: a sprite with a *Rest Area* trigger, *Prompt* "Rest
   (save)". Its actions are `saveGame quick`, then `startDialogue shrine`.
5. **Biscuit at home**: the cat by the bakery, switched off. An empty
   object *Biscuit Comes Home* has a Trigger *On* `conditionMet`, *Only If*
   `cat_found`, action `setActive Biscuit on`.
6. **The story's moments**, as triggers on empty objects:
   - *Story Start*: `sceneStart`, *Once*, `startDialogue intro`.
   - *Lantern Lights Up*: `conditionMet` `lantern_lit`. Actions:
     `setActive "Lantern Light" on`, `setActive Night off`.
   - *Ending*: `conditionMet` `lantern_lit`, *Once*. Actions: `wait 1.5`,
     `setActive "The End" on`, `wait 12`, `setActive "The End" off`.
7. **The player** (last in the Hierarchy, so it is drawn on top):
   - a sprite with `hero.lanim` clip `idle_down`, scale 3;
   - a feet Collider (10 × 5, offset 0, −9.5);
   - an **Interactor** (key E);
   - a **Player Controller** (speed 170).

   Its child **Camera** has:
   - scale ⅓, so the interface is drawn at 1:1;
   - **Camera Limits**: *Min* 0, 0; *Max* the map's size in points (36 × 48 by 24 × 48).

   Children of the Camera form the interface:
   - *Night*: a dark, half-transparent rectangle, 1280 × 720, saved switched
     off so the scene is easy to edit;
   - Texts: "Flames {flames} / 3", the quest log (*Source*: `questLog`),
     the help line, the interaction prompt (*Source*: `interactionPrompt`);
   - the dialogue box: a Shape with a **DialogueView**, and Texts for the
     speaker, line and choices;
   - *The End*: a banner, switched off.

   *Nightfall*, an empty object with a `sceneStart` trigger, *Only If*
   `not lantern_lit`, runs `setActive Night on`.
8. **The road out**: an object at the east edge with a non-solid Collider
   (48 × 96) and a **Scene Door**: *Scene* `woods`, *Arrive At* "From
   Village". Next to it, a little way in, an empty object called *From
   Woods* with a **Spawn Point**. That is where you arrive when coming back.

Select the scene row in the Hierarchy and click **Make Start Scene** in the
Inspector, so the game begins in the village.

Press ▶ to try it. The intro plays, you can talk to everyone, and the
Console lists any problem. **Game > Check Content** checks every file.

## 7. The C++ behaviours

`examples/lantern_keeper/src/KeeperBehaviours.cpp` holds everything the
content cannot express:

- **PlayerController**: walks with the keyboard (`game.move` stops at
  anything solid), and chooses `walk_left` / `idle_up`… with
  `game.play(entity(), clip)`.
- **SceneDoor**: when the player walks into it, it sets the `arrival`
  variable to *Arrive At* and calls `game.changeScene(scene)`.
- **SpawnPoint**: when its scene starts, if `arrival` is its name, it moves
  the player there and clears `arrival`. Starting the game and loading a
  save leave the player where the scene or the save puts them.
- **CameraLimits**: moves the camera (a child of the player) so the view
  never leaves the map.

A game module registers them (`KeeperGame.cpp`), and CMake links the module
into the editor and the player (`rolo_add_game_module`; see the
[gameplay guide](gameplay.md#game-modules-joining-your-code-to-lantern)).

## 8. Whisperwood and the old mine

Make two more scenes the same way (**File > Save Scene As…** of the village
is a quick start: keep the player and interface, then repaint):

- **woods**:
  - forest, a clearing in the north with **Biscuit**: her talk area runs
    `startDialogue biscuit_found`. Her child *Runs Home* (`conditionMet
    cat_found`) runs `destroy Biscuit`, which removes her for good;
  - a **Lost Flame** by the pond: a flame sprite (`flicker`) with a Trigger
    *On* `enterArea` running `add flames 1`, `startDialogue flame`,
    `destroy this`;
  - the **Mine Gate**: a solid sprite in a gap in a stone cliff. Its *Lock*
    child is an `interact` trigger (`startDialogue gate`), and its *Opens*
    child is a `gameEvent` trigger for `gate_open` that runs
    `destroy "Mine Gate"`;
  - doors west (to `village`, arrive at *From Woods*) and east (to `mine`,
    arrive at *From Woods*), with spawn points *From Village* and
    *From Mine*.
- **mine**: tunnels, rails and rooms, two more lost flames, a miners' sign,
  and the way out (to `woods`, arrive at *From Mine*).

Objects removed with `destroy` (flames, Biscuit, the gate) stay removed
when you come back, and in saved games.

## 8½. Sound and music

Import the sounds from `examples/lantern_keeper/project/assets/sounds` (or
your own; see the [sound guide](sound.md)):

- **Music:** in each scene, an object *Music* with a **Sound** component,
  **Music** on and a **Music Fade** of 2 seconds: `night` in the village and
  the woods (so it carries on when you walk between them), `mine` in the
  mine (it fades over from the night music).
- **Effects**, as the first action of triggers:
  - each Lost Flame's trigger: `playSound flame`;
  - the wayshrine's Rest Area: `playSound rest`;
  - the gate's *Opens*: `playSound gate`;
  - *Lantern Lights Up*: `playSound lantern_lit`.

## 9. Play it through

Talk to Maren and Wren, find Biscuit, get the key from Wren, rest at the
wayshrine, open the gate, find the flames, and light the lantern.
`tests/gameplay/test_lantern_keeper.cpp` does exactly this automatically on
every test run, so the game stays finishable as Rolo changes.

## How the shipped project was produced

`examples/lantern_keeper/project` was made the same way, but with scripts:
1. `rolo_editor --new-project` made the fresh project.
2. `tools/make_art.py` drew the art, and `tools/make_sounds.py` made the
   sounds and music (synthesised from sine waves and noise: original, like
   the art).
3. `tools/build_project.py` added everything above, writing the same files
   the editor writes.

The scripts keep the shipped files identical every time (fixed IDs, the same
maps). The project is then an ordinary project: open it in the editor and
change anything.
