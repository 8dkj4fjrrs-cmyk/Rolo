# Scripting with Luau

Rolo games are programmed in **Luau**, the Lua that Roblox uses: if you
have scripted on Roblox, you already know the language. Scripts need no
compiler and no special setup: write a `.lua` (or `.luau`) file, attach it
to an object, press ▶. A downloaded Rolo runs them, and so does every game
you export.

(Rolo also accepts gameplay code in C++; see the [gameplay guide](gameplay.md).
C++ needs Rolo built from source. Luau does not.)

## Your first script

1. In the **Assets** panel, click **+** and choose **New Script**. Rolo
   makes `scripts/new_script.lua` with a small example in it. Rename it in
   the Inspector if you like, for example to `spinner.lua`.
2. Select the file and click **Open in Code Editor**. Any text editor works.
   [Visual Studio Code](https://code.visualstudio.com) is free; its
   **Luau Language Server** extension colours Luau code and checks it as you
   type.
3. Replace the text with:

   ```lua
   function start(self)
       self.speed = 90   -- degrees per second
   end

   function update(self, dt)
       local angle = self.object:rotation()
       self.object:setRotation(angle + self.speed * dt)
   end
   ```

   Save the file.
4. Select an object in the scene, click **Add Component › Gameplay ›
   Script**, and choose `spinner.lua` in its **Script** field.
5. Press ▶. The object spins. Change `90` to `360`, save, stop and press ▶
   again: it spins faster. Every press of Play reads the scripts again.

`print("hello")` writes to the **Console** panel, with the script's name.
That is the quickest way to see what a script is doing.

## How a script runs

A script file defines functions with special names. Rolo calls them for
each object that has a **Script** component pointing at the file:

| Function | When |
|---|---|
| `start(self)` | once, in the first frame the object is in the game |
| `update(self, dt)` | every frame after that; `dt` is the time since the last frame, in seconds (about 1/60) |
| `collide(self, other)` | physics: when the object starts touching `other` (see [Physics](#physics)) |

All are optional. Code outside any function runs once, when the file is
first used. Put settings and helper functions there.

**`self`** is a table that belongs to one object. Put the object's own state
in it (`self.speed`, `self.health`). `self.object` is the object itself.

**Variables:**
- Use `local` for temporary values inside a function.
- Values a whole file shares (all fireflies, say) can be plain globals, like
  `count = 0` at the top. Each file has its own globals: two scripts cannot
  overwrite each other's by accident.

**Positions** are two numbers, `x` and `y`, in points. `y` goes **up**.
**Angles** are in degrees.

**One script per object.** To share code between scripts, put it in a
module (see `require` below).

### When something goes wrong

If a script has an error, the Console shows its file and line, for example:

```
Script on 'Coin' failed in update(): scripts/coin.lua:12: attempt to index nil with 'gem'
  called from: scripts/coin.lua:12 function update
```

That object's script stops for the rest of the run; everything else keeps
going.

**Game › Check Content** finds syntax errors (a missing `end`, a typo)
without playing. **Exporting refuses** a project with a broken script.

Two limits stop a broken script from hanging or crashing the game:
- a script call (`start`, `update`, ...) that runs for more than 10
  seconds without finishing (a loop that never ends) is stopped;
- all scripts together may use at most 256 MB of memory.

## Examples

**Collect coins.** Put this on each coin, with a Collider that is not solid:

```lua
function update(self, dt)
    local player = game.player()
    if player and self.object:isTouching(player) then
        game.set("coins", game.get("coins") + 1)   -- a whole-number variable in a .lvars file
        self.object:destroy()
    end
end
```

**A door that opens with a key:**

```lua
function update(self, dt)
    if game.check("has_key") and input.wasPressed("e") and self.object:isTouching(game.player()) then
        game.run("startDialogue door_opens")
        game.changeScene("cellar", "From Upstairs")
    end
end
```

**A patrolling guard** with a walk animation, turning round at walls:

```lua
function start(self)
    self.direction = 1
end

function update(self, dt)
    local _, _, blocked = self.object:move(80 * self.direction * dt, 0)
    if blocked then
        self.direction = -self.direction
    end
    self.object:play(self.direction > 0 and "walk_right" or "walk_left")
end
```

The **Top-Down Starter** template's fireflies (`scripts/firefly.lua`) are a
complete example to read and change.

## Everything a script can use

### Objects

Objects are values like `self.object` or `game.find("Door")`. Call their
methods with a colon: `obj:name()`. An object stays usable while it exists.
Using one after it was destroyed (or after the scene changed) is an error;
`obj:isValid()` checks first.

| Method | What it does |
|---|---|
| `obj:name()` | its name, as in the Hierarchy |
| `obj:isValid()` | true while it is still in the game |
| `obj:position()` | its position: `x, y` (relative to its parent, if it has one) |
| `obj:setPosition(x, y)` | moves it there, through walls. (Like every function that takes `x, y`, it also takes one vector: `obj:setPosition(v)`; see [fast scripts](#writing-fast-scripts)) |
| `obj:worldPosition()` | its position in the world, parents included |
| `obj:move(dx, dy)` | moves it by `dx, dy`, stopping at anything solid (give it a Collider). Returns `movedX, movedY, blocked, hit`: how far it went, whether something stopped it, and the object it ran into (nil for a wall of tiles) |
| `obj:rotation()`, `obj:setRotation(degrees)` | turning |
| `obj:scale()`, `obj:setScale(x, y)` | size; `setScale(2)` doubles both ways |
| `obj:isActive()`, `obj:setActive(true/false)` | switches it (and its children) on or off |
| `obj:destroy()` | removes it at the end of the frame. An object that was in the scene stays gone, also in saved games |
| `obj:play("walk_left")` | plays a clip of its Animator. Calling it every frame is fine. `obj:play(clip, true)` starts the clip again |
| `obj:stopAnimation()` | stops on the clip's first frame |
| `obj:clip()` | the clip it plays |
| `obj:isAnimationPlaying()`, `obj:isAnimationFinished()` | animation state (finished: a clip that does not loop has reached its end) |
| `obj:overlapping()` | a list of the objects whose Colliders overlap its Collider |
| `obj:isTouching(other)` | true if their Colliders overlap |
| `obj:text()`, `obj:setText("Score: 3")` | the text of a Text component; `{variable}` and `{button:jump}` in it are filled in, as in the editor |
| `obj:setFlipX(true/false)` | mirrors its Sprite left to right |
| `obj:playSound()` | plays its Sound component (as music, if the Sound is set to Music). Returns a handle for `audio.stop`, or `nil` |
| `obj:stopSound()` | stops what its Sound component plays |

`tostring(obj)` gives `object "Door"`. `a == b` is true for the same object.

### `game`

| Function | What it does |
|---|---|
| `game.find("Name")` | the first object with that name, or `nil` |
| `game.player()` | the player (the object with the Interactor), or `nil` |
| `game.time()` | seconds since the game started |
| `game.get("gems")` | a game variable's value (variables are made in a `.lvars` file) |
| `game.set("gems", 3)` | changes one. The value must have the variable's type: true/false, a whole number, or text |
| `game.check("gems >= 3 and met_elder")` | tests a condition, written as in a Trigger's condition field |
| `game.run("startDialogue elder")` | runs an action, written as in a Trigger's action fields. For example: `startQuest find_cat`, `completeObjective find_cat search`, `add gems 1`, `emit gate_open` |
| `game.emit("gate_open")` | sends a game event. Triggers and scripts hear it in the next frame |
| `game.wasEmitted("gate_open")` | true if the event was sent in the previous frame |
| `game.changeScene("woods")` | goes to another scene (by its file name) at the end of the frame |
| `game.changeScene("woods", "From Village")` | ...with the player placed at the object of that name |
| `game.inDialogue()` | true while a dialogue is shown |
| `game.save("slot1")`, `game.load("slot1")` | saves or loads the player's progress |
| `game.quit()` | ends the game, e.g. from a menu's Quit (in the editor: stops playing). See [Escape and quitting](controls.md#escape-quitting-and-the-quick-save-keys) |

### `input`

| Function | What it does |
|---|---|
| `input.isDown("space")` | the key is held down |
| `input.wasPressed("space")` | it went down this frame |
| `input.wasReleased("space")` | it went up this frame |
| `input.direction()` | where the player steers, `x, y` (up is +1): the arrow keys or W A S D (each -1, 0 or 1), or else a gamepad's d-pad or left stick (values in between too). Never longer than 1 |
| `input.pointer()` | the mouse position in the world: `x, y` |
| `input.isDown("pad a")` (and `wasPressed`, `wasReleased`) | a gamepad button, on any gamepad; `input.isDown("pad a", 2)` asks gamepad 2 only (1 to 4) |
| `input.stick("left")`, `input.stick("right")` | a gamepad stick, `x, y`, each -1 to 1 (up is +1), with a small dead zone; `0, 0` without a gamepad |
| `input.trigger("left")`, `input.trigger("right")` | a gamepad trigger, 0 (released) to 1 |
| `input.gamepad()`, `input.gamepad(2)` | true if a gamepad (or gamepad 2) is plugged in |
| `input.action("jump", {"space", "pad a"})` | names an action and its default keys and buttons; then `input.isDown("jump")` (and `wasPressed`, `wasReleased`) |
| `input.rebind("jump")`, `input.isRebinding()`, `input.cancelRebind()` | remapping: the next key or button pressed becomes the action's (Backspace keeps it) |
| `input.bindings("jump")` | the action's keys and buttons now, as names: `{"Space", "Pad A"}` |
| `input.resetControls()`, `input.resetControls("jump")` | back to the defaults |
| `input.rumble(strength, seconds)`, `input.rumble(strength, seconds, pad)` | shakes the gamepad (strength 0 to 1; a table `{low = , high = }` sets each motor) |
| `input.lastDevice()` | `"keyboard"` or `"gamepad"`: what the player used last |

Actions, remapping, button pictures (`{button:jump}` in texts) and rumble
are explained in the [controls guide](controls.md).

Key names, in any case:
- letters `"a"` to `"z"` and digits `"0"` to `"9"`;
- `"space"`, `"enter"`, `"escape"`, `"tab"`, `"backspace"`;
- `"left"`, `"right"`, `"up"`, `"down"`;
- `"left shift"`, `"right shift"`, `"left control"`, `"right control"`,
  `"left alt"`, `"right alt"`, or `"shift"`, `"control"` and `"alt"` for
  either side;
- `"f1"` to `"f12"`;
- `"mouse left"`, `"mouse right"`, `"mouse middle"`;
- gamepad buttons, named by position as on an Xbox pad: `"pad a"` (the
  bottom button: Cross on PlayStation, B on Nintendo), `"pad b"` (right),
  `"pad x"` (left), `"pad y"` (top), `"pad lb"`, `"pad rb"` (bumpers),
  `"pad start"`, `"pad back"`, `"pad guide"`, `"pad ls"`, `"pad rs"` (pressing
  a stick in), and the d-pad `"pad up"`, `"pad down"`, `"pad left"`,
  `"pad right"`.

Gamepads are plugged in and out while the game runs; up to four work at
once. Games without code are playable with one too: the Top-Down
Controller and `input.direction()` follow the left stick and d-pad, and A
talks, goes on in dialogue and picks a choice (the stick or d-pad moves
between choices). These are Rolo's own actions (`move left`, `move right`,
`move up`, `move down`, `interact`), so players can remap them.

### Physics

For objects with a **Rigid Body** (see the [physics guide](physics.md)).
Using these on an object without one is an error that says so.

| Function | What it does |
|---|---|
| `obj:velocity()` | its speed, `x, y`, in points per second |
| `obj:setVelocity(x, y)` | sets its speed |
| `obj:applyImpulse(x, y)` | a sudden push, in kg·points/s: a 1 kg body pushed by `0, 500` gains 500 points per second upwards |
| `obj:applyForce(x, y)` | a steady push for this frame, in kg·points/s² |
| `obj:spin()`, `obj:setSpin(degrees)` | turning speed, degrees per second, clockwise |
| `obj:isOnGround()` | true if it stands on something (below it, against gravity) |
| `obj:contacts()` | a list of the objects it touches now |
| `game.gravity()`, `game.setGravity(x, y)` | the scene's gravity, points per second per second (while the game runs) |

A script can also define `collide(self, other)`: it is called when the
object starts touching `other` (another object with a Collider; tiles are
not objects).

### `audio`

Sounds are named like scenes: by file name (`"coin"`) or path
(`"sounds/coin.wav"`). See the [sound guide](sound.md).

| Function | What it does |
|---|---|
| `audio.play("coin")` | plays a sound effect; returns a handle, or `nil` if it could not play |
| `audio.play("coin", { volume = 0.5, pitch = 1.2, pan = -1, loop = true })` | ...with options: volume 0–2 (1 as recorded), pitch 0.1–4 (2 is an octave up), pan -1 (left) to 1 (right), loop |
| `audio.stop(handle)` | stops that sound (a `nil` handle is ignored) |
| `audio.isPlaying(handle)` | true while it plays |
| `audio.playMusic("theme")` | changes the music, fading over a second. The same music already playing carries on |
| `audio.playMusic("theme", { fade = 3, volume = 0.8 })` | ...with a fade in seconds (0–30) and a volume |
| `audio.stopMusic()`, `audio.stopMusic(2)` | fades the music out (over 1 second, or the given seconds) |
| `audio.music()` | the music playing now (its name), or `nil` |
| `audio.setVolume("music", 0.5)` | a channel's volume, 0–1: `"master"`, `"music"` or `"effects"` |
| `audio.volume("music")` | a channel's volume |

### Luau's own library, and `require`

- **Available:** Luau's `string`, `table`, `math`, `utf8`, `coroutine`,
  `bit32`, `buffer` and `vector` libraries, and the basics (`pairs`,
  `ipairs`, `tostring`, `tonumber`, `type`, `typeof`, `pcall`, `error`,
  `assert`, `select`, `setmetatable`...).
- **Not available,** so a game's scripts cannot harm a player's computer:
  reading or writing files, running programs, and loading other code
  (`os`, `debug`, `loadstring`, `getfenv`, `setfenv`; Luau has no `io` or
  `package`).
- **The shared libraries are read-only:** `math.pi = 3` or changing `game`
  is an error, so one script cannot break another. Your own globals live
  in your file and can be changed freely.
- **`require("name")`** loads another script of the project as a module:
  - the name can be the file name without `.lua` or `.luau`, or its path;
  - the module runs once, and every `require` of it returns the same value:
    what the file returns, or, if it returns nothing, its functions;
  - modules do not need to be attached to objects.

```lua
-- scripts/maths.lua
function clamp(value, low, high)
    return math.max(low, math.min(high, value))
end
```
```lua
-- scripts/player_health.lua
local maths = require("maths")
function update(self, dt)
    self.health = maths.clamp((self.health or 100) - dt, 0, 100)
end
```

## Writing fast scripts

Rolo is built so that thousands of scripted objects fit in a frame (the
[stress test](../examples/stress/README.md) measures it). What costs time
in your scripts is mostly **garbage**: tables and strings made every frame
and thrown away, which Luau must clear up. Too much of it is felt as
stutter.

- **Keep numbers in locals or in `self`**, not in a new table each frame:
  `local x, y = self.object:position()` makes no garbage (Rolo's functions
  return plain numbers).
- **Use a vector for a point or a direction.** A Luau vector is a value,
  like a number: making one leaves no garbage, and vectors add, subtract
  and multiply. Every function here that takes `x, y` also takes one
  vector:

  ```lua
  function start(self)
      self.home = vector.create(self.object:position())   -- x, y
      self.speed = vector.create(120, 0, 0)
  end

  function update(self, dt)
      local at = vector.create(self.object:position())
      self.object:setPosition(at + self.speed * dt)
      if vector.magnitude(at - self.home) > 300 then self.speed = -self.speed end
  end
  ```

  (`v.x`, `v.y`; vectors have a `z` too, which Rolo ignores.)
- **Build text only when it changes**: `"Score: " .. score` every frame
  makes a new string every frame.
- **Find objects once**, in `start`, and keep them: `self.door =
  game.find("Door")`.
- **Reuse objects**: scripts cannot make new objects yet, so keep a pool
  switched off and switch them on (`setActive`), as the stress test's
  bullets do.

In the stress test, 1000 objects doing the same sums take 1.3 ms a frame
written with a new table per point, and 0.5 ms with locals or vectors.

## Luau, the language

Luau is Lua 5.1 with additions: what Lua 5.1 tutorials teach works, and
so does the newer syntax Roblox developers use:

```lua
type Enemy = { name: string, health: number }   -- types (checked by editors; Rolo runs the code either way)

local function hurt(enemy: Enemy, amount: number)
    enemy.health -= amount                         -- compound assignment: += -= *= /= //= %= ^= ..=
    print(`{enemy.name} has {enemy.health} left`)  -- string interpolation
end

for i = 1, 10 do
    if i % 2 == 0 then continue end                -- continue
end

local half = 7 // 2                                -- floor division: 3
local v = vector.create(1, 2, 3)                   -- vectors (v.x, v.y, v.z)
```

What is **not** in Luau, if you come from other Lua: `goto`, Lua 5.4's
bitwise operators (`&`, `|`, `<<`; use `bit32.band` and friends) and
`<const>` / `<close>` variables. Numbers are one kind (as in Lua 5.1):
`3` and `3.0` are the same, and print as `3`.

What is not here, if you come from **Roblox**: Roblox's own objects
(`workspace`, `Instance.new`, `task.wait`, `:Connect`). Rolo's are `game`,
`input`, `audio` and each script's `self.object` (above).

Learning more:
- [luau.org](https://luau.org): the language guide and its library.
- [Programming in Lua](https://www.lua.org/pil/contents.html), first
  edition, free online: the basics, which are the same.

Rolo uses **Luau 0.744**. It is built into Rolo; you do not need to
install it.

### Scripts written for Rolo before Luau (0.9 and earlier)

Rolo 0.9 ran Lua 5.4. Almost every script runs unchanged. Check for:
- **`goto`**, **bitwise operators** (`a & b`, `a << 2`) and **`<const>`**:
  rewrite with loops, `bit32` and plain `local`;
- **numbers printed with `.0`**: `print(2.0)` showed `2.0` and now shows
  `2`;
- **changing a shared library** (`string.trim = ...`): put your function in
  your own file or module instead;
- **error message wording**, if a script compares messages (e.g.
  "attempt to index nil with 'gem'").

**Game › Check Content** reports syntax errors in every script.
