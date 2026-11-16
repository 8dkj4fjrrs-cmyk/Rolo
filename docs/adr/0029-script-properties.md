# ADR 0029: Script properties in the Inspector

- **Status:** Accepted; built (2026-11-16)
- **Follows:** ADR 0013 (Lua scripting), ADR 0019 (direction: for people who come from Roblox), ADR 0020 (Luau)

## The need

One script, many objects, different numbers: three lifts that rise 100,
200 and 400 points; a fast enemy and a slow one. Until now a script's
numbers were written in the script, so each variation needed its own copy
of the file, or a script that looked at its object's name. Roblox answers
this with Attributes, Godot and Unity with exported variables. It was the
first gap the "Coming from Roblox" guide had to admit.

## Decision

**The script declares its properties, with the values they start with:**

```lua
properties = {
    speed = 90,        -- a number
    label = "Lift",    -- text
    returns = true,    -- a tick box
}
```

**The Inspector shows a field for each** under the object's Script
component, in the order written, labelled from the name (`walkSpeed`
becomes "Walk Speed"). **Each object can have its own values.** When the
game runs, every object's `self` starts with them: `self.speed`.

Three kinds, told from the starting value: number, text, true/false.

### Why declared in the script

- The person who writes `self.speed` is the one who knows it exists; a
  list kept elsewhere (in the editor, per object, as Roblox's Attributes
  are) can be forgotten or misspelt, and a missing one is `nil` at run
  time.
- The starting value is at hand, so a new object with the script works
  at once.
- It reads like what it is. A beginner changing `speed = 90` in the
  script still gets what they expect.

### How the editor knows them: by reading, not running

The editor needs the list while no game runs. Running the script's top
level in the editor would do it, but a script's code may do anything (it
calls `input.action`, it may loop for ever), and the editor is not a
game. So the list is **read from the script's text** with Luau's own
parser (`readScriptProperties`, in `engine/gameplay`, where Luau lives):
the top-level statement `properties = { ... }`, each entry a name and a
plain value (a number, a negative number, a text, `true` / `false`).

The price: a calculated value (`speed = 2 * 45`) cannot be shown. The
Inspector says so in a line under the Script, naming the line and the
property. `local properties` is caught the same way (the running game
could not see it either).

The running game does not parse: the script has run, so its real
`properties` table is there, and `self` is filled from it. So a
calculated value still works when playing; it just has no field.

### Where an object's own values are kept

In the Script component, as one more property, `values`: text holding
the values that differ from the script's, as compact JSON
(`{"speed":120}`; `rolo/scene/Values.hpp`). That makes them an ordinary
property to everything that exists: undo (a `SetProperty` command), copy
and paste, duplicate, the scene file.

- **Only differences are kept.** Setting a field back to the script's
  value removes the entry. So changing `speed = 90` to `100` in the
  script reaches every object that never had its own.
- **In scene files it is written as JSON itself**, not as text with
  escaped quotes: `"values": {"speed": 120}` (`PropertyOptions::data`, a
  new flag on text properties; such a property gets no text field in the
  Inspector, and is not written when empty). The scene format's version
  stays 1: the key is optional and new, and an older Rolo ignores it
  with a warning, as with the keys added before.
- **Values that no longer fit are ignored, not deleted**: a value for a
  property the script no longer declares, or of another kind than the
  script's now, is skipped when playing and when shown. If the script
  changes back, the value is still there.

### What `self` gets

`ScriptHost::ensureSelf`, when an object's `self` is made (before its
`start`): every entry of the script's `properties` table that is a
number, text or true/false, then the object's own values over those of
the same kind, then `self.object`. A property cannot be called `object`.

## Tested

Unit tests (`tests/gameplay/test_scripts.cpp`): reading declarations in
order with their lines, every kind of declaration that cannot be shown,
labels, an object's values kept and removed, a running game where two
objects with one script move at their own speeds, and scene files (JSON
values in, none written when empty, an older file without the key). The
Platformer Starter's lift and player scripts use properties, so every
test that plays it does too.

## Not done

- **More kinds**: a colour, a point, another object, a sound or image, a
  choice from a list, a range for a number. The shape allows them (a
  richer value on the right-hand side); nothing reads them yet.
- **Tooltips from comments** beside a property.
- **Changing them while the game plays** in the editor.
- A warning in Check Content for declarations that cannot be shown (the
  Inspector says so; exporting does not).

## Alternatives considered

- **Attributes set per object in the editor, read with a function**
  (Roblox's way): no declaration, so nothing to show until someone types
  a name, and a typo is silent.
- **Comments as declarations** (`-- @property speed 90`): invisible to
  the language, so the script and the comment can disagree.
- **Running the script in the editor** to ask it: see above.
- **A separate file per script** listing its properties: one more file
  to keep in step.
