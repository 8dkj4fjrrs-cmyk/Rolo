# The Lantern Keeper

A small, complete top-down RPG made with Rolo: the demo game of
Milestone 7. It takes about ten minutes to play.

> Emberwick, the night after the great storm. For a hundred years the great
> lantern has burned above the village square, keeping the night-mist away.
> Tonight, for the first time, it is dark.

You are Pip, apprentice to Keeper Maren. Find the lantern's three lost
flames in Whisperwood and the old mine, help Wren the baker find her cat
Biscuit (she has the mine's key), and light the lantern again.

## Play it

From the repository folder, after building (`cmake --workflow --preset dev`):

```sh
# macOS
./build/dev/bin/rolo_player/rolo_player.app/Contents/MacOS/rolo_player --project examples/lantern_keeper/project
# Linux / Windows
./build/dev/bin/rolo_player/rolo_player --project examples/lantern_keeper/project
```

Arrow keys or W A S D walk. E talks, reads and opens (and moves dialogue
on; arrows choose answers). Rest at the glowing wayshrine in the village to
save; F5 also saves, F9 loads. Escape quits.

Or open `examples/lantern_keeper/project` in the editor (**File > Open
Project…**) and press ▶.

## What it shows

| Engine feature | Where in the game |
|---|---|
| Tilemaps with solid tiles | the village, Whisperwood and the old mine (one tileset, 24 tiles) |
| Player movement and collision | Pip walks with the keyboard, stopped by walls, trees, water and people |
| Flipbook animation | Pip walks in four directions; villagers blink and bounce; flames flicker; the lantern burns |
| Interaction | talk to Maren, Wren, Nell and Biscuit; read signs; open the gate; rest at the wayshrine |
| Dialogue with choices and branches | every conversation changes with the story (e.g. Maren before, during and after the quest) |
| Quests | "The Dark Lantern" (main) and "Where Is Biscuit?" (side), shown in the on-screen quest log |
| Conditions, actions and events | picking up flames, the gate opening for good (`emit gate_open`), Biscuit coming home, the night lifting |
| Scene changes | three scenes joined by roads; you arrive where you came in |
| Saving and loading | the wayshrine (`saveGame`), F5 / F9; collected flames and the open gate stay that way |
| C++ behaviours | four small ones in `src/` (below); everything else is content |

## How it is made

- **`src/`**: the game module, about 200 lines of C++:
  - `PlayerController`: walking, plus choosing the walk and idle clips.
  - `SceneDoor` and `SpawnPoint`: arriving at the right place in the next scene.
  - `CameraLimits`: keeps the view inside the map.
  - `KeeperGame.cpp`: registers them with Rolo.
- **`project/`**: the game itself: scenes, art, sounds and music, tileset,
  animations, variables, dialogues and quests. Open it in the editor to change anything.
- **`tools/`**: how `project/` was produced. It starts from a fresh project
  made by the editor (`rolo_editor --new-project`).
  - `make_art.py` draws the pixel art.
  - `make_sounds.py` synthesises the sounds and music.
  - `build_project.py` adds everything else.
  - They need Python 3 with Pillow and NumPy, and ffmpeg (for the OGG
    files), but not to build or play the game.

The guide [Making The Lantern Keeper](../../docs/making-the-lantern-keeper.md)
walks through making the same game in the editor, step by step.

To rebuild `project/` from scratch:

```sh
rm -rf examples/lantern_keeper/project
./build/dev/bin/rolo_editor/rolo_editor --new-project examples/lantern_keeper/project --frames 1
python3 examples/lantern_keeper/tools/build_project.py examples/lantern_keeper/project
```

The module is built into the editor and the player with the other example
(`ROLO_BUILD_SAMPLE_GAME=ON`, the default). `tests/gameplay/test_lantern_keeper.cpp`
plays the whole story, from the first line to the ending, on every test
run.

All art, sound, music and writing here are original and, like the rest of Rolo,
MIT-licensed.
