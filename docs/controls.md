# Controls: actions, remapping, button pictures and rumble

Rolo games read the keyboard, the mouse and up to four gamepads (Xbox,
PlayStation, Nintendo Switch Pro and most others). This guide covers what
makes controls feel finished: named **actions** that players can **remap**,
**button pictures** in text that always show the right button, and **rumble**.

The **Platformer Starter** (File › New Project…) has all of it: press Tab, or
Start on a gamepad, for its Controls screen.

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
screen in about 60 lines: copy it, and list your own actions at the top.

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

Pads that cannot rumble are simply not shaken.

## Device in use

`input.lastDevice()` is `"keyboard"` or `"gamepad"`: what the player touched
last (for your own prompts or menus).

See also: [scripting reference](scripting.md#input), [gameplay in C++](gameplay.md).
