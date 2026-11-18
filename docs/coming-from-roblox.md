# Coming from Roblox

If you have made things in Roblox Studio, you already know most of what
Rolo needs: the language is the same (**Luau**), and the editor has the
same three places to look. This guide says what carries over, what has
another name, and what is missing, so nothing surprises you halfway
through your first game.

New to Rolo altogether? Do [Getting started](getting-started.md) first
(half an hour, no code), then come back.

## The big differences

| | Roblox | Rolo |
|---|---|---|
| Games are | 3D, online, many players | **2D**, on the player's own device, **one player** |
| Where they run | inside Roblox | a program for Mac, Windows or Linux, a web page, or an Android app that **you** hand out ([exporting](exporting.md)) |
| Who owns the game | you, on Roblox's platform and by its rules | you, entirely: a folder of files. Rolo is MIT-licensed and takes no share |
| Players find it | through Roblox | wherever you put it (itch.io, your own site): finding players is your job |
| Internet | always | **none**: a Rolo game cannot use the network at all |
| Money | Robux, game passes | nothing built in |

So there is no server and no client, no `RemoteEvent`, no `Players`
service, no filtering, no DataStore limits to think about. A script simply
runs, on the one computer the game is on.

Rolo is an alpha. Read the [known limitations](known-limitations.md) once
so you know what is not there yet.

## The editor

| Roblox Studio | Rolo | |
|---|---|---|
| Explorer | **Hierarchy** | the scene's objects, as a tree |
| Properties | **Inspector** | the selected object's settings |
| Toolbox / Asset Manager | **Assets** | your project's own files; there is no shared marketplace |
| Output | **Console** | `print` goes here |
| Play (F5) | **▶** (⌘P / Ctrl+P) | plays a copy; stopping puts everything back |
| A place | a **scene** (a file in `scenes/`) | `game.changeScene("woods")` goes from one to another |
| Part, MeshPart | an **object** with a **Sprite** (a picture) or a **Shape** | |
| Terrain | a **Tilemap**, painted with tiles | |
| Publish to Roblox | **Build** (Export Game…) | makes the files you share |

An object in Rolo is empty until you give it **components** in the
Inspector (**Add Component**): a Sprite to be seen, a Collider to be
solid, a Rigid Body to fall and bounce, an Animator, a Text, a Sound, a
Script. That is where Roblox would have you pick a different kind of
Instance or put children inside a Part.

## Scripts: the same language, a different shape

In Roblox a script starts at the top, runs down, and connects functions
to events. In Rolo a script **defines functions with fixed names**, and
Rolo calls them (`start` is where you connect to events):

| | |
|---|---|
| `start(self)` | once, when the object enters the game; it may wait (`task.wait`) |
| `update(self, dt)` | every frame; `dt` is the seconds since the last one |
| `collide(self, other)` | when the object starts touching another (physics) |

`self` is a table that belongs to that one object: keep its state there.
`self.object` is the object the script is on, which is what
`script.Parent` usually was.

A spinning thing, both ways:

```lua
-- Roblox
local part = script.Parent
game:GetService("RunService").Heartbeat:Connect(function(dt)
    part.Orientation += Vector3.new(0, 90 * dt, 0)
end)
```

```lua
-- Rolo
function update(self, dt)
    self.object:setRotation(self.object:rotation() + 90 * dt)
end
```

Attach it with **Add Component › Gameplay › Script**. An object has one
Script. There is no ServerScriptService or StarterPlayerScripts: where a
script file lies in the project does not matter, only which object uses
it.

### What carries over, and one habit to drop

**Waiting works as you know it.** `start(self)` runs from top to bottom
and may wait, and `task.spawn` and `task.delay` begin code of their own:

```lua
-- Roblox                          -- Rolo
while true do                      function start(self)
    task.wait(2)                       while true do
    fire()                                 task.wait(2)
end                                        fire(self)
                                       end
                                   end
```

Only `update` and `collide` cannot wait (they must finish within their
frame); from there, begin a task with `task.spawn`. A loop that never
waits still freezes the game until Rolo stops the script, after ten
seconds.

**So does connecting**, with a small first letter and on Rolo's own
events ([scripting guide](scripting.md#events)):

```lua
-- Roblox                                   -- Rolo
part.Touched:Connect(function(hit)          function start(self)
    print(hit.Name)                             self.object:collided():connect(function(other)
end)                                                print(other:name())
                                                end)
                                            end
```

There are three kinds so far: `game.event("name")` (your own, sent with
`game.emit`), `input.pressed("jump")` / `input.released("jump")`, and
`obj:collided()`, and a few more (`obj:separated()`,
`obj:animationFinished()`, `obj:destroyed()`). Each has `:connect(f)`, `:once(f)` and `:wait()`, and
`connect` gives back a connection to `:disconnect()`. Connect inside
`start`, not at the top of the file: a connection belongs to an object,
and goes when the object does. For everything else, ask in `update`
("am I touching the player?").

The habit to drop: **`Instance.new`.** Nothing is built from nothing in
a script. Make the thing once in the editor and copy it when you need
one: `game.spawn("Bullet", x, y)`. That is `:Clone()` with the parent and
the place set for you; `obj:clone()` is the plain copy. Where Roblox
keeps the template in ServerStorage or ReplicatedStorage, Rolo keeps it
as a **prefab**, a file (**Edit › Save as Prefab**), or switched off in
the scene.

### `game` is not the same `game`

In Roblox, `game` is the whole place and everything hangs off it
(`game.Workspace`, `game:GetService(...)`). In Rolo, `game` is a small
list of functions: `game.find("Door")`, `game.get("coins")`,
`game.changeScene("woods")`. Rolo has four such lists and they are the
whole API: **`game`**, **`input`**, **`audio`**, and the methods of
**objects**. All of it fits on one page: the
[scripting guide](scripting.md#everything-a-script-can-use).

Names are written `likeThis`, not `LikeThis`: `obj:setPosition(...)`, not
`:SetPosition(...)`. Functions return plain numbers where Roblox returns
objects: `local x, y = obj:position()`.

## The phrase book

### Finding and changing things

| Roblox | Rolo |
|---|---|
| `script.Parent` | `self.object` |
| `workspace:FindFirstChild("Door")` | `game.find("Door")` (anywhere in the scene, by name) |
| `part.Position = Vector3.new(4, 0, 2)` | `obj:setPosition(40, 20)` |
| `part.Position` | `local x, y = obj:position()` |
| `part.Orientation`, `CFrame.Angles` | `obj:rotation()`, `obj:setRotation(degrees)` |
| `part.Size` | `obj:setScale(2)` |
| `part.Transparency = 1`, parenting to `nil` | `obj:setActive(false)` |
| `part:Destroy()` | `obj:destroy()` |
| `template:Clone()`, then `.Parent = workspace` | `game.spawn("Bullet", x, y)`: a copy of the object called Bullet, placed and switched on. `obj:clone()` for an exact copy |
| a template kept in ServerStorage / ReplicatedStorage | a **prefab**: select the object, **Edit › Save as Prefab**; `game.spawn("Bullet", x, y)` then works in every scene ([editor guide](editor.md#prefabs-an-object-kept-as-a-file)) |
| a Package (a model whose copies update together) | a prefab's **linked copies**: change one, click **Apply to Prefab**, and the others follow, keeping their own changes |
| `Instance.new("Part")` | not available: copy something you made in the editor |
| setting attributes on the clone | `game.spawn("Bullet", x, y, { speed = 400 })` gives the copy its own properties |
| `part.Parent`, `:GetChildren()` | `obj:parent()`, `obj:children()` |
| `part:FindFirstChild("Wheel")` | `obj:child("Wheel")`; anywhere inside (`FindFirstChild(name, true)`): `obj:find("Wheel")` |
| `part.Parent = other` | not available: an object stays where it is in the tree |
| `task.wait(2)`, `task.spawn(f)`, `task.delay(2, f)` | the same, in `start` and in tasks |
| `event:Connect(f)`, `:Once(f)`, `:Wait()`, `connection:Disconnect()` | `event:connect(f)`, `:once(f)`, `:wait()`, `connection:disconnect()`, on `game.event("name")`, `input.pressed("jump")`, `input.released("jump")` and `obj:collided()` |
| `textLabel.Text = "Score: 3"` | `obj:setText("Score: 3")` on an object with a Text |
| a ModuleScript and `require(script.Parent.Maths)` | any script file and `require("maths")` |
| Attributes (`:GetAttribute("Speed")`) | **properties**: the script lists them (`properties = { speed = 90 }`), the Inspector shows a field for each on every object with the script, and the script reads `self.speed` ([scripting guide](scripting.md#properties-settings-in-the-inspector)) |
| a `Color3` attribute, `part.Color = ...` | `tint = color("#ffb84d")` in `properties`; colours are texts: `obj:setColor("#ff0000")`, `obj:color()` |
| a `Vector3` attribute | `goal = vector.create(300, 120)` in `properties`: an X and a Y in the Inspector |
| an `ObjectValue` pointing at another Instance | `door = object()` in `properties`: pick the object in the Inspector, use `self.door` (`nil` for none) |

Positions are in **points**, not studs: one point is one pixel of the
game's size (1280 × 720 unless you change it). There is an `x` and a `y`;
**`y` goes up**. Angles are in degrees.

`Vector3` and `Vector2` become Luau's own `vector`: `vector.create(3, 4)`,
with `+`, `-`, `*` and `vector.magnitude(v)`. Every Rolo function that
takes `x, y` takes one vector as well.

### Touching

| Roblox | Rolo |
|---|---|
| `part.Touched:Connect(function(hit) ... end)` | for solid things with physics: `function collide(self, other) ... end`, or `obj:collided():connect(function(other) ... end)` for any object (a Collider on both, a Rigid Body on at least one). For areas you walk into (a coin, a checkpoint: a Collider that is not solid): `if self.object:isTouching(other) then` in `update` |
| `part.TouchEnded:Connect(function(hit) ... end)` | `obj:separated():connect(function(other) ... end)` |
| `track.Stopped:Wait()`, `track.Ended` (an animation finishing) | `obj:animationFinished():wait()`, or `:connect(function(clip) ... end)`, for a clip that does not loop |
| `instance.Destroying:Connect(...)` | `obj:destroyed():connect(function() ... end)`, from another object's script |
| `workspace:GetPartsInPart(part)` | `obj:overlapping()` |
| `part.CanCollide = false` | a Collider that is not **Solid** |
| `part.Anchored = true` | no Rigid Body, or one whose Type is **Static** |
| `part.AssemblyLinearVelocity` | `obj:velocity()`, `obj:setVelocity(x, y)` |
| `part:ApplyImpulse(v)` | `obj:applyImpulse(x, y)` |
| `workspace.Gravity` | `game.gravity()`, `game.setGravity(x, y)` |
| a ProximityPrompt | an **Interactor** on the player and a **Trigger** on the thing: "press E", with no code |

A coin, both ways:

```lua
-- Roblox
local coin = script.Parent
coin.Touched:Connect(function(hit)
    local player = game.Players:GetPlayerFromCharacter(hit.Parent)
    if player then
        player.leaderstats.Coins.Value += 1
        coin:Destroy()
    end
end)
```

```lua
-- Rolo
function update(self, dt)
    local player = game.player()
    if player and self.object:isTouching(player) then
        game.set("coins", game.get("coins") + 1)
        self.object:destroy()
    end
end
```

### The player's character

There is no Humanoid and no automatic character. The player is an object
like any other, moved by a component or a script you can read:

- **Top-down** (Zelda-like): the **Top-Down Controller** component. No
  code.
- **Platformer**: the Platformer Starter's `scripts/player.lua`, sixty
  lines: a Rigid Body, `setVelocity` for running, and a jump when
  `isOnGround()`. `Humanoid.WalkSpeed` and `JumpPower` are the two
  numbers at its top.

`game.player()` gives the player's object (the one with the Interactor).
Health, lives and the like are yours to make: a number in `self`, or a
game variable.

### Keys, gamepads and touch

| Roblox | Rolo |
|---|---|
| `UserInputService.InputBegan` | `input.pressed("space"):connect(f)`, or `input.wasPressed("space")` in `update` |
| `UserInputService:IsKeyDown(Enum.KeyCode.E)` | `input.isDown("e")` |
| `ContextActionService:BindAction("Jump", ...)` | `input.action("jump", {"space", "pad a"})`, then `input.wasPressed("jump")` |
| `humanoid.MoveDirection` | `input.direction()` (keys, d-pad, stick or the on-screen stick) |
| the mobile thumbstick and jump button | made by Rolo from your actions; nothing to set up |
| `HapticService` | `input.rumble(0.5, 0.2)` |

Ask for **actions**, not keys, and the game works with a keyboard, a
gamepad and a phone's screen alike, and players can remap it
([controls](controls.md)).

### Values that last, and saving

| Roblox | Rolo |
|---|---|
| an `IntValue` in ReplicatedStorage, leaderstats | a **game variable**: made in a `.lvars` file, read with `game.get("coins")`, changed with `game.set("coins", 5)`, shown in a Text as `{coins}` |
| `BindableEvent:Fire()` / `.Event:Connect` | `game.emit("gate_open")` and `game.event("gate_open"):connect(f)` (heard in the next frame; an event has a name but carries no values) |
| DataStoreService | `game.save("slot1")`, `game.load("slot1")`: a file on the player's own device, with no limits and no waiting |
| `TeleportService` to another place | `game.changeScene("woods", "From Village")` |

A save holds the scene, where the player stands, the game variables,
the quests and which objects were destroyed. A script's own `self` is not
in it: put what must survive in game variables.

### Sound, text and menus

| Roblox | Rolo |
|---|---|
| `sound:Play()` | `audio.play("coin")`, or `obj:playSound()` for the object's Sound component |
| background music | `audio.playMusic("theme")` (it fades from the last one) |
| a ScreenGui with TextLabels | **Text** objects made **children of the Camera**: they stay on screen as the camera moves |
| TextButton, Frame, UIListLayout | none yet. Menus are texts and a script that moves a marker (the Platformer Starter's `scripts/pause.lua` is one to copy) |
| TweenService | none: change the value a little in each `update` (`x += (target - x) * 5 * dt` eases nicely) |
| dialogue you would script by hand | built in, without code: dialogues, quests and triggers ([gameplay guide](gameplay.md)) |

## What is not here

So you do not go looking:

- **Multiplayer and anything online**: no servers, no chat, no
  leaderboards, no friends.
- **3D.**
- **Building objects from nothing** (`Instance.new`): copy one made in
  the editor instead (`game.spawn`). Copies are not kept in saved games.
- **Most of Roblox's events**: Rolo has a handful to connect to (game
  events, keys and actions, touching beginning and ending, an animation
  finishing, an object being destroyed). No `Changed`, no `ChildAdded`,
  no events that carry your own values. Also no `task.cancel` or
  `task.defer`.
- **GUI buttons and layouts**, **tweens**, **particles**, **lighting**.
- **A marketplace** of models and scripts, and **Team Create**. A Rolo
  project is a folder of plain files, which works well with Git.
- **Type checking in the editor**: Rolo runs typed Luau but does not
  check the types. Visual Studio Code with the *Luau Language Server*
  extension does, as you type.

And some things Roblox does not give you: the game runs with no
internet, is yours to sell or give away anywhere, and Rolo's own code is
open for you to read and change.

## A first hour

1. **File › New Project… › Platformer Starter**, and press ▶.
2. Open `scripts/player.lua` (select it in Assets, **Open in Code
   Editor**). Its `properties` are the player's speed and jump: change
   them in the script, or select the Player and change them in the
   Inspector, and press ▶ again.
3. Read `scripts/lift.lua` (a moving platform) and `scripts/pause.lua`
   (a menu): with the player's script they use most of what this page
   lists.
4. Make a spinning hazard: a new object with a Sprite and a Collider, and
   the spinner script from the top of this page with a line added to
   send the player back when `self.object:isTouching(game.player())`.
5. **Build**, and send the folder to a friend.

Then: the [scripting guide](scripting.md) for the whole API, and
[physics](physics.md), [controls](controls.md) and [sound](sound.md) for
each of those.
