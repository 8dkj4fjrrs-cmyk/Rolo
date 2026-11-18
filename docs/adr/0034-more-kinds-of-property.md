# ADR 0034: Script properties that are colours, points and other objects

- **Status:** Accepted; built (2026-11-18)
- **Follows:** ADR 0029 (script properties), ADR 0030 (making objects while playing)

## The need

Since ADR 0029 a script lists its properties and the Inspector shows a
field for each, per object. They could be numbers, texts and tick boxes.
Three things people reach for next had no field:

- **a colour** ("this lamp glows green");
- **a point** ("patrol to here");
- **another object** ("the door this button opens"), which a script had
  to look up by name with `game.find("Door")`: a name typed twice, and
  wrong the moment the door is renamed.

## Decision

Three more kinds, declared like the others, at the top of the script:

```lua
properties = {
    speed = 90,
    tint = color("#ffb84d"),           -- a colour: a swatch in the Inspector
    goal = vector.create(300, 120),    -- a point: X and Y fields
    door = object(),                   -- another object: a list of the scene's objects
}
```

| | In the Inspector | In the script (`self.name`) |
|---|---|---|
| `color("#ffb84d")` | a colour swatch and its hex text | a text, `"#ffb84dff"` |
| `vector.create(x, y)` | two number fields | a Luau vector (`.x`, `.y`), which every Rolo function that takes `x, y` accepts |
| `object()` | a list of the scene's objects, **None** first | that object, or `nil` for None (and for one deleted since) |

### Why these forms

- **Each is real Luau that means the same when the script runs.** The
  editor reads `properties` from the script's text without running it
  (ADR 0029), so a kind has to be recognisable from how it is written;
  the game runs the same line. `vector.create` is Luau's own. `color`
  and `object` are two new global functions.
- **A colour is a text in scripts** (`"#rrggbb"` or `"#rrggbbaa"`), the
  form colours have in Rolo's files and in the Inspector. `color(...)`
  checks it and tells the Inspector "this text is a colour". There is no
  colour type to learn. To make the property useful, objects got
  `obj:color()` and `obj:setColor("#ff0000")` (the colour of the
  object's Sprite, Shape and Text).
- **`object()` names no object.** Which object is a fact about one
  object in one scene, so it is chosen in the Inspector and kept with
  that object, never in the script.

### Stored with the object

As before, an object's own values are the Script component's `values`
(named values as JSON, ADR 0029). A point is written `[x, y]`, a colour
as its text, another object as its ID:

```json
"values": { "door": "504c01000000001b", "goal": [500, 120], "tint": "#3366ffff" }
```

What a value means comes from the script's declaration: a text is a
colour or an object only where the script says so. A value that does not
fit its declaration (a script changed since) is ignored and the script's
own is used, as for the first three kinds. No file format version
changes: scene files already held these values as free JSON.

### Copies point at their own parts

A copy of an object (Duplicate, a placed prefab, `game.spawn`,
`obj:clone()`) gets new IDs for everything in it. A property that points
at an object **inside what is copied** now points at the copy's
(`Scene::withNewIds`): a spawned tank's `turret` is its own turret.
One that points outside (the scene's Flag) still points there.

`game.spawn("Tank", x, y, { target = flag, goal = vector.create(1, 2) })`
accepts objects and vectors for such properties.

## Tested

- Workspace (Linux, GCC 13): `[kinds]` tests: reading the three forms
  and saying what is wrong with malformed ones; values kept, refused
  and dropped; `self` given a text, a vector and an object (nil for
  none and for a missing one); a spawned copy's own values, and its
  object property following into the copy; `obj:color()`,
  `obj:setColor`, `color()`, `object()` and their errors.
- By hand, in the editor on a virtual screen (Linux): the three fields
  on the Platformer Starter's lift; choosing an object from the list,
  typing a point and a colour; the saved scene file; the game then
  printing those values.

## Not done

- The object list is one long list of every object; no search, no
  dragging an object from the Hierarchy onto the field, no picking in
  the Scene view.
- A point has no handle in the Scene view; it is typed.
- **Check Content** does not report a property that points at an object
  deleted since (the Inspector shows "Missing"; the script gets `nil`).
- An object from another scene, a prefab, or a file (an image, a sound)
  as a property; lists of values; a number with a slider or limits.
- `color()` is text in, text out: no colour arithmetic.
