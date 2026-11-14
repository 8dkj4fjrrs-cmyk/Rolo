# Controls: actions, remapping, button pictures and rumble

Rolo games read the keyboard, the mouse and up to four gamepads (Xbox,
PlayStation, Nintendo Switch Pro and most others). This guide covers what
makes controls feel finished: named **actions** that players can **remap**,
**button pictures** in text that always show the right button, and **rumble**.

The **Platformer Starter** (File › New Project…) has all of it: press Escape,
or Start on a gamepad, for its pause menu, and choose Controls (or press Tab,
or Back on a gamepad).

## Actions

An action is something the player does, with the keys and buttons that do
it. Define your game's actions in a script's `start`:

```lua
function start(self)
    input.action("jump", {"space", "w", "up", "pad a"})
    input.action("attack", {"x", "mouse left", "pad x"})
end

function update(self, dt)
    if input.wasPressed("jump") then ... end   -- any of its keys or buttons
end
```

`input.isDown`, `input.wasPressed` and `input.wasReleased` take an action's
name wherever they take a key's. Defining an action again (every time the
scene starts) is fine.

Rolo's own controls are actions too, so they can be remapped like yours:

| Action | Default | Used by |
|---|---|---|
| `move left`, `move right`, `move up`, `move down` | arrows, W A S D, the d-pad | walking (`input.direction()`, the Top-Down Controller), choosing in dialogue |
| `interact` | the Interactor's key (E), and A | talking, opening, going on in dialogue |

The left stick always steers too.

## Remapping

`input.rebind("jump")` waits for the player's next key, mouse button or
gamepad button and makes it the action's binding **for that kind of
device**: a key replaces the action's keyboard (and mouse) bindings, a
gamepad button its gamepad ones. **Backspace** (or Escape) leaves it as it
was. The press that chooses is used up: it does not also jump.

| Function | What it does |
|---|---|
| `input.rebind("jump")` | the next key or button pressed becomes the binding |
| `input.isRebinding()` | true while waiting for that press |
| `input.cancelRebind()` | stops waiting |
| `input.bindings("jump")` | its bindings now, as names: `{"Space", "Pad A"}` |
| `input.resetControls()`, `input.resetControls("jump")` | back to the defaults |

**Players' choices are kept** on their computer, beside their saved games
(the save slot `_controls`), the moment they change, and come back next time.
In the editor they are kept with play mode's saves, apart from players'.

`scripts/controls.lua` in the Platformer Starter is a complete Controls
screen in about 70 lines: copy it, and list your own actions at the top.
`scripts/pause.lua` is its pause menu (Resume, Controls, Quit).

## Escape, quitting and the quick-save keys

Every Rolo game quits when **Escape** is pressed, and **F5** and **F9** save
to and load the "quick" slot, unless the project turns them off. Select the
scene (click empty space in the Hierarchy) and look in the Inspector's
**Game (whole project)** section:

| Setting | On (the default) | Off |
|---|---|---|
| **Escape Quits** | Escape closes the game at once (in the editor: stops playing) | Escape is an ordinary key: use it for a pause menu |
| **F5 / F9 Saves** | F5 saves to the "quick" slot, F9 loads it | F5 and F9 are ordinary keys: save your own way |

They are kept in `project.rolo` (`"escapeQuits": false`,
`"quickSaveKeys": false`), so exported games follow them too.

With Escape Quits off, give players a way out: **`game.quit()`** (C++:
`game.quit()` on `rolo::Game`) ends the game this frame. The player
closes its window; in the editor, playing stops. Save first if progress
should be kept. Closing the window always quits.

The Escape that cancels changing a control (`input.rebind`) never also
quits or pauses.

## Button pictures

Write `{button:...}` in any Text, dialogue line or choice, quest text, or
script `setText`:

```
Press {button:jump} to jump
{button:interact} Open
Press {button:pad start} to pause
```

- An **action's name** shows what it is bound to now, on the **device the
  player is using**: the key on a keyboard, the gamepad's button when they
  last touched the gamepad. Remapping changes the picture too.
- A **key or button's name** (`space`, `mouse left`, `pad b`) always shows
  that one.

Gamepad buttons are drawn as they look on the player's pad: Xbox's coloured
A B X Y, PlayStation's cross, circle, square and triangle, Nintendo's
letters (where the bottom button is B), and LB/L1/L, Menu/Options/+ and so
on. Keys are drawn as key caps, the mouse with its button lit. The pictures
are drawn by Rolo (no image files) and sit inside the line of text. A line
with pictures is not wrapped.

The interaction prompt (a Text whose source is *Interaction Prompt*) shows
the interact button's picture: "[picture] Talk".

## Rumble

```lua
input.rumble(0.5, 0.3)                       -- strength 0 to 1, for 0.3 seconds, every gamepad
input.rumble(1, 0.2, 2)                      -- gamepad 2 only
input.rumble({low = 1, high = 0.3}, 0.5)     -- each motor: low the heavy one, high the light one
input.rumble(0, 0)                           -- stop
```

C++: `game.input().rumble(strength, seconds)`. A new rumble replaces the one
going on. Rumble stops when the game is paused or stopped.

Which pads rumble:
- **Windows**: Xbox controllers, and others that work like them (XInput).
- **macOS 11 or newer**: pads the system's Game Controller framework can
  rumble (Xbox, PlayStation and others). It has one strength, not two motors:
  the stronger of `low` and `high` is used.
- **Linux**: pads whose driver supports force feedback, if the user may
  write to the device (usual on desktop Linux).

- **Web browsers**: not yet.

Pads that cannot rumble are simply not shaken.

## Touch screens

On a phone or tablet (web games today; Android and iOS later) Rolo shows
**on-screen controls** by itself ([ADR 0026](adr/0026-touch-controls.md)):

- a **stick** at the bottom left, for moving. Its base comes to wherever
  the thumb lands in that part of the screen;
- a **round button for each of your actions**, named after it: the first
  action you define is the large button in the bottom-right corner, the
  others sit around it. Actions called `pause` or `menu` become small
  buttons at the top.

Pressing a button is pressing the action: `input.isDown("jump")`, Rolo's
movement, talking and dialogue work as they are. So define actions (above)
and ask for *them*, not for keys, and your game plays on a phone with no
more work. `{button:jump}` in a text draws the on-screen button there.

To choose the buttons and their order yourself, list the actions in
`project.rolo`:

```json
"touchButtons": ["jump", "dash", "pause"]
```

To handle touches yourself, turn **Touch Controls** off in the Inspector
(select the scene by clicking empty space in the Hierarchy; it is under
*Game (whole project)*; in the file:
`"touchControls": false`) and read the fingers:

```lua
for _, finger in ipairs(input.touches()) do
    if finger.began then spawnSparkAt(finger.x, finger.y) end
end
```

`input.touches()` lists the fingers that are not on Rolo's controls, in
world coordinates, each with `id`, `x`, `y`, `began` and `ended`. The
first finger also moves `input.pointer()` and counts as the left mouse
button, so a game made for clicking works with taps.

Menus: the stick counts as `move up`/`move down`/... when pushed past
half way. There is no Enter key on a phone, so let an action's button
pick, as the Platformer Starter's pause menu does:

```lua
local pick = input.wasPressed("enter") or input.wasPressed("pad a")
             or (input.lastDevice() == "touch" and input.wasPressed("jump"))
```

Try it on your computer: `rolo_player --project MyGame --touch` makes the
left mouse button a finger.

## Device in use

`input.lastDevice()` is `"keyboard"`, `"gamepad"` or `"touch"`: what the
player used last (for your own prompts or menus).

See also: [scripting reference](scripting.md#input), [gameplay in C++](gameplay.md).
