# Writing gameplay in C++

> **New to programming, or using a downloaded Rolo?** Write gameplay in
> **Lua** instead: no compiler, no rebuilding Rolo. See the
> [scripting guide](scripting.md). This guide is for C++, which needs
> Rolo built from source.

Rolo games can be written in C++. You write **behaviours**: small classes
attached to objects, like components, that run every frame while the game
plays. This guide shows how, using the sample game in
[`examples/sample_game`](../examples/sample_game) as the running example.

Design reasons: [ADR 0007](adr/0007-gameplay-and-play-mode.md).

## Built-in behaviours: games without C++

Rolo comes with three behaviours for top-down games. Add them in the
Inspector (**Add Component > Gameplay**). The New Project wizard's
**Top-Down Starter** uses all three.

| Behaviour | Put it on | What it does |
|---|---|---|
| **Top-Down Controller** (`rolo.TopDownController`) | the player (with a Collider and an Interactor) | Walks with the arrow keys or W A S D (*Speed*, points per second), stopped by anything solid, and stands still during dialogues. With an Animator it plays `walk_down`, `walk_left`, `walk_right`, `walk_up` and `idle_…` clips when the animations have them; otherwise it flips the Sprite to face left or right. |
| **Scene Door** (`rolo.SceneDoor`) | an object with a non-solid Collider at a map's edge | When the player walks into it, the game changes to *Scene*. The player appears at the object named *Arrive At* in that scene: put an empty object a step inside the way back. Empty: where that scene puts the player. |
| **Camera Bounds** (`rolo.CameraBounds`) | the Camera (a child of the player) | The view follows the player but stays inside *Min*–*Max* (world points, e.g. the map's corners), so it never shows past the map's edges. |

From C++, `game.changeScene(scene, "From the Village")` does what a Scene
Door does.

The rest of this guide is for when you want more than these: your own
behaviours in C++.

## A first behaviour

```cpp
#include "rolo/gameplay/Behaviour.hpp"
#include "rolo/gameplay/Game.hpp"

struct Spinner : rolo::BehaviourBase<Spinner> {
    float speed = 90.0f;   // degrees per second

    void update(rolo::Game& game, float dt) override
    {
        game.transform(entity())->rotation += speed * dt;
    }

    static const rolo::ComponentType& componentType()
    {
        static const rolo::ComponentType type =
            rolo::ComponentTypeBuilder<Spinner>("mygame.Spinner", "Spinner")
                .category("Gameplay")
                .description("Turns the object round and round.")
                .property("speed", "Speed", &Spinner::speed, {.step = 15.0f, .unit = "°/s"})
                .build();
        return type;
    }
};
```

- `BehaviourBase<Spinner>` makes it a component Rolo can copy, save and
  show in the Inspector.
- `componentType()` describes it: an ID that is saved in scene files (choose
  it once and never change it, e.g. `"mygame.Spinner"`), the name shown in
  the editor, and its **properties**. Properties appear in the Inspector, are
  saved in the scene, and can be undone like any other edit. Supported types:
  `bool`, `int`, `float`, `Vec2`, `Color`, `std::string`, enums (a choice),
  and `AssetId` (an image, tileset or scene; set `.assetType`).
- `.category("Gameplay")` lists it under *Gameplay* in **Add Component**.

### When behaviours run

While the game runs, every active object's behaviours get:

| Function | When |
|---|---|
| `start(Game&)` | once, in the first frame the object is active |
| `update(Game&, float dt)` | every frame; `dt` is the frame's length in seconds |

Objects run in hierarchy order (parents first, top to bottom), and an
object's behaviours in the order they are listed. Frames are never longer
than 0.1 s (`Game::kMaxStep`), so a hiccup cannot make things jump through
walls.

`entity()` is the object the behaviour is on.

## What `Game` offers

`rolo::Game` is the game being played ([`Game.hpp`](../engine/gameplay/include/rolo/gameplay/Game.hpp)).

**Input** — `game.input()`:

```cpp
const rolo::Input& in = game.input();
in.isDown(rolo::Key::Space);       // held down
in.wasPressed(rolo::Key::Space);   // went down this frame (a tap)
in.wasReleased(rolo::Key::Space);  // went up this frame
in.moveDirection();                   // arrows or W A S D, length at most 1
in.isDown(rolo::MouseButton::Left);
in.pointer();                         // the mouse, in world coordinates
in.wasPressed(rolo::GamepadButton::A);  // any gamepad (or pass 0-3 for one)
in.leftStick();                       // y up, dead zone applied, at most 1 long
in.trigger(rolo::GamepadAxis::RightTrigger);   // 0 to 1
in.isGamepadConnected();
```

`moveDirection()` includes the left stick and the d-pad, so behaviours that
use it work with gamepads unchanged. Actions, remapping and rumble
(`defineAction`, `wasActionPressed`, `startRebind`, `rumble`) are in the
[controls guide](controls.md); to change a Text from C++, use
`game.setText(id, "...")` (it fills in `{variable}` and `{button:...}`).

**Objects:**

```cpp
rolo::Transform* t = game.transform(entity());   // move, turn, scale
t->position = t->position + rolo::Vec2{0, 10};
game.get<rolo::Sprite>(entity())->flipX = true;  // any component
rolo::EntityId door = game.find("Door");         // by name
auto gems = game.findAll<Collectible>();            // every object with one
game.worldPosition(entity());                       // parents included
game.destroy(entity());                             // removed at the end of the frame
```

Changing a `Transform` is cheap (the object is just moved). Changing other
components (a sprite's image, a colour) redraws the object, so only do it
when the value actually changes.

**Collision** — needs a **Collider** component on the objects involved, or
solid tiles in a tilemap:

```cpp
game.move(entity(), {speed * dt, 0});      // moves, stopping at walls and solid tiles
game.overlapping(entity());                // objects whose colliders overlap this one's
game.isBlocked(rolo::Box::around(p, {16, 16}));
game.colliderBox(entity());                // the collider's box in the world
```

`move()` slides along walls (it moves along x, then y) and returns what it
hit. Colliders are axis-aligned boxes; a **Solid** collider blocks movement,
a non-solid one is an area that gameplay code notices (a door, a pick-up).
A tilemap cell blocks movement when it is painted on the *Collision* layer,
or holds a tile marked solid in the tileset (Tile Palette > *Edit solid
tiles*).

**Scenes:**

```cpp
game.changeScene(sceneAssetId);   // at the end of this frame
```

Scenes are assets with IDs, so give your behaviour an `AssetId` property
with `.assetType = "scene"` and choose the scene in the Inspector (see
`SceneDoor` in the sample). The new scene starts fresh: objects of the old
one, and their behaviours, are gone.

### Physics

In scenes with a Rigid Body ([physics guide](physics.md)):

```cpp
game.setVelocity(entity(), {280, game.velocity(entity()).y});   // run, keep falling
if (game.isOnGround(entity())) {
    game.applyImpulse(entity(), {0, 640});                        // jump (1 kg body)
}
game.touching(entity());                                          // what it touches now
game.setGravity({0, 0});                                          // the scene's gravity
```

A behaviour can also override `collide(rolo::Game&, rolo::EntityId other)`,
called when its object starts touching another.

### Sound

```cpp
game.playSound(entity());                          // this object's Sound component
rolo::SoundHandle h = game.playSound(coinSound, {.volume = 0.5f, .pitch = 1.2f});
game.sound().stop(h);
game.playMusic(themeSound, 2.0f);                  // fades over 2 seconds
game.sound().setChannelVolume(rolo::SoundChannel::Music, 0.5f);
```

Give your behaviour an `AssetId` property with `.assetType = "audio"` to
choose a sound in the Inspector. Everything about sound is in the
[sound guide](sound.md).

### Animation

An object with an **Animator** (and usually a Sprite) plays flipbook clips
from an animation set (`.lanim`, made in the editor's Animation panel; see
the [editor guide](editor.md#animation)). Gameplay code picks the clip:

```cpp
game.play(entity(), "walk_left");          // carries on if it is already playing
game.play(entity(), "attack", /*restart=*/true);
if (game.isAnimationFinished(entity())) {  // a clip that does not loop reached its end
    game.play(entity(), "idle_down");
}
game.stopAnimation(entity());              // holds the clip's first frame
game.currentClip(entity());                // "walk_left"
game.isAnimationPlaying(entity());
```

Calling `play()` every frame with the same clip is fine and cheap: the clip
only starts again when it changes (or with `restart`). Clips run on game
time, so pausing the game pauses them, and switching clips does not redraw
the scene. The sample's `PlayerController` plays `walk_<direction>` while
moving and `idle_<direction>` while standing.

**Other:** `game.time()` (seconds of game time), `game.deltaTime()`,
`game.log().info("game", "...")` (shown in the editor's Console).

## Story content: dialogue, quests and triggers without code

Much of an RPG is content rather than code: who says what, what quests
there are, what happens when the player walks somewhere. Rolo keeps this
as data ([content formats](formats/content.md)) and runs it with built-in
components:

| Component | Put it on | What it does |
|---|---|---|
| **Interactor** | the player (with a Collider) | Triggers notice it entering their area and pressing its interact key (E by default). |
| **Trigger** | anything with a non-solid Collider (or nothing, for scene start, timers, events and conditions) | When something happens, runs actions if its condition is true (below). |
| **Text** | an object (a child of the Camera stays on screen) | Draws text. `{gems}` shows a variable. Its *Source* can instead show the dialogue's speaker, line or choices, the interaction prompt (a picture of the interact button, then "Talk") or the quest log. |
| **Dialogue View** | the dialogue box | Shown only while a dialogue is going on. |

A Trigger's **When** is one of:

| When | Runs |
|---|---|
| Interact | the player presses the interact key inside the area (the prompt, e.g. "Talk", is shown meanwhile) |
| Enter Area / Leave Area | the player walks in / out |
| Scene Start | when the scene starts |
| Timer | *Delay* seconds after the scene starts, and again every *Delay* seconds unless *Once* |
| Game Event | an `emit` action (or `game.emit()`) sent the event named *Event* |
| Condition Met | its condition turns true |

*Once* makes it run only once per playthrough (remembered in saves). Its
actions can start dialogues and quests, set variables, change scene, remove
objects for good, wait, save and load... (see the [list of actions](formats/content.md#actions)).

While a dialogue is shown, the interact key, Enter or Space go on, and the
arrow keys (or W/S) pick a choice. Behaviours should usually not move the
player meanwhile:

```cpp
if (game.isInDialogue()) {
    return;
}
```

From C++, content is at hand through `game.content()`:

```cpp
game.content().state().variable("gems");        // a variable's value
game.content().run({action});                    // run actions, like content does
game.content().startDialogue(elderDialogueId);
game.content().state().questState(questId);      // notStarted, active, completed, failed
game.emit("door_opened");                        // Game Event triggers hear it next frame
if (game.wasEmitted("door_opened")) { ... }      // sent last frame
```

### Saving

`game.save("slot")` saves the player's progress (variables, quests, objects
removed for good, one-time triggers that ran, the scene and where the
player stands) and `game.load("slot")` brings it back. The `saveGame` and
`loadGame` actions do the same from content. In the player and in Play
mode, **F5** saves to the slot "quick" and **F9** loads it (unless the
project turns that off). `game.quit()` ends the game, e.g. from a menu
([Escape and quitting](controls.md#escape-quitting-and-the-quick-save-keys)).

Saves made by the player go to the user's data folder (macOS
`~/Library/Application Support/<game>/Saves`, Windows
`%APPDATA%\<game>\Saves`, Linux `~/.local/share/<game>/saves`); saves made
in the editor's Play mode go to the editor's settings folder, so testing
never overwrites a real save. Objects removed with `game.destroy()` (or a
`destroy` action) stay removed for the rest of the playthrough, also after
leaving and coming back to a scene.

## Game modules: joining your code to Rolo

Behaviours live in a **game module**: a static library linked into the
editor and the player when Rolo is built. A module has one registration
function:

```cpp
// MyGame.cpp
#include "rolo/gameplay/GameModule.hpp"

ROLO_GAME_MODULE(my_game)
{
    registry.add(Spinner::componentType());
    registry.add(PlayerController::componentType());
}
```

and a `CMakeLists.txt`:

```cmake
rolo_add_game_module(my_game SOURCES MyGame.cpp Spinner.cpp PlayerController.cpp)
```

Build Rolo with your module's folder added:

```sh
cmake --preset dev -DROLO_GAME_MODULE_DIRS=/path/to/my_game
cmake --build build/dev
```

The editor's Console then says `Game module: my_game` at start-up, your
behaviours appear under *Gameplay* in **Add Component**, and both Play mode
and the player run them. Several folders can be listed, separated by `;`.

The module name must use only letters, digits and underscores, and must
match in `ROLO_GAME_MODULE(...)` and `rolo_add_game_module(...)`.

Scenes that use a behaviour from a module that is not linked still open:
the component is kept unchanged (and saved back), shown as unknown.

**Rebuilding is needed after changing C++ code.** Rolo does not load code
while the editor runs (no hot reload) yet: quit the editor, build, start it
again. Your scenes are not affected.

## Playing

- **In the editor:** press ▶ in the toolbar (or ⌘P / Ctrl+P). The game runs
  inside the Scene view, seen through the game camera; click it so it gets
  the keyboard. ⏸ pauses, ⏭ runs one frame, ■ stops. The game plays a copy
  of the scene: stopping always returns to the scene exactly as it was.
  Scene edits are refused while playing.
- **In the player:** `rolo_player --project path/to/project` plays the
  project's start scene in its own window.

## Errors

If a behaviour throws a C++ exception, the error is shown in the Console
(with the behaviour's and object's names), that behaviour is switched off
for the rest of the run, and the rest of the game keeps going. A crash, such
as using a null pointer, cannot be caught this way and closes the program,
editor included: save before playing new code.

## The example games

The Lantern Keeper's own `keeper.PlayerController`, `keeper.SceneDoor`,
`keeper.SpawnPoint` and `keeper.CameraLimits` came first: the built-in
behaviours above grew out of them. They stay in its module as an example of
writing your own.

**The Lantern Keeper** ([`examples/lantern_keeper`](../examples/lantern_keeper))
is a small, complete RPG: three scenes, four characters, a cat, two quests,
saving and an ending, with only four short behaviours in C++
(`PlayerController` with walk animations, `SceneDoor` and `SpawnPoint` for
arriving in the right place, `CameraLimits`). Everything else is content.
[Making The Lantern Keeper](making-the-lantern-keeper.md) rebuilds it step by
step.

```sh
./build/dev/bin/rolo_player/rolo_player --project examples/lantern_keeper/project
```

**The sample game** [`examples/sample_game`](../examples/sample_game) has four behaviours:
`PlayerController` (walking with collision), `SceneDoor` (changes scene when
the player walks in), `Collectible` (picked up on touch, adding to a game
variable) and `Spinner`. Its project (`examples/sample_game/project`) has a
meadow and a cave, and content: Elder Maren's dialogue, a quest to bring her
three gems (picked up by Triggers, no code), a boulder that rolls aside as
the reward, and an on-screen gem count, quest log and dialogue box. It is
the engine's test bed. Both modules are built in by default
(`ROLO_BUILD_SAMPLE_GAME`):

```sh
./build/dev/bin/rolo_player/rolo_player --project examples/sample_game/project
```

(On macOS the program is inside `rolo_player.app/Contents/MacOS/`.)
Arrow keys or W A S D walk, E talks, F5 saves, F9 loads.
