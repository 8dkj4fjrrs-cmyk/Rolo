# ADR 0030: Making objects while playing

- **Status:** Accepted; built (2026-11-16)
- **Follows:** ADR 0013 (Lua scripting), ADR 0021 (performance layer: "still to do: making and removing objects from scripts"), ADR 0029 (script properties)

## The need

Bullets, enemies that keep coming, coins that burst out of a chest: a
game makes things while it runs. Until now scripts could only switch
objects on and off, so every such thing had to be put in the scene
beforehand and counted ("pool them"). It was the first thing the "Coming
from Roblox" guide had to tell people to unlearn (`Instance.new`,
`:Clone()`).

## Decision

**A script makes an object by copying one that exists.**

```lua
local bullet = game.spawn("Bullet", x, y)               -- a copy of the object called Bullet, placed and switched on
local fast   = game.spawn("Bullet", x, y, { speed = 400 })   -- ...with its own value for the script's property
local twin   = self.object:clone()                      -- an exact copy, beside the original
```

- **`obj:clone()`** copies the object and everything inside it (children,
  components), beside the original: same parent, same place, same name,
  on or off as the original is.
- **`game.spawn(what, x, y, properties)`** is the everyday form: `what` is
  an object or a name; the copy is placed (if a place is given), gets its
  own values for its script's properties (if given), and is **switched
  on**. So the usual set-up is one "pattern" object in the scene,
  **switched off**: drawn and edited like anything else in the editor, and
  never in the game itself.
- The copy's scripts (and other behaviours) are new: their `start` runs
  in the next frame, with a fresh `self`.
- `obj:destroy()` removes a copy like any object.

### Why copy, and not "new"

Rolo's objects are made of components set up in the Inspector. Something
built from nothing in a script (`Instance.new("Part")` and twenty lines
of settings) would move that work out of the editor into code, where a
beginner cannot see it. A pattern in the scene is the thing itself: seen,
selected, changed with the mouse. (Prefabs, patterns kept as files and
shared between scenes, are still on the roadmap; `spawn` will take them
when they exist.)

### How a script hands something to its copy

Scripts cannot reach each other's `self`. What the maker knows and the
copy needs (which way to fly, how hard to hit) goes through **the copy's
script properties** (ADR 0029): the table given to `spawn` becomes the
copy's own values, exactly as if set in the Inspector. Names the copy's
script does not declare are not used. Position, rotation, velocity and
the like are set on the returned object as usual.

### Copies and saved games

A saved game holds progress, not objects (docs/formats/content.md): which
of the scene's own objects are gone, the variables, the quests. Copies
are **not saved**, and destroying one is not recorded. After loading, or
after leaving the scene and coming back, the scene is as it was made, less
what was destroyed of it. For bullets and effects that is right; for
things that must last (a placed building), keep count in game variables
and make them again in `start`. This is said in the guide.

### A limit

A script that makes an object every frame and never destroys any would
slow the game to a halt without a word. Copying stops, with a message
naming the remedy, when the scene holds 10 000 objects
(`Game::kMostObjects`).

## How it is built

- `Game::clone` uses what the editor's Duplicate uses: the scene's
  snapshot of an object and its contents, given fresh IDs, restored
  beside the original. Behaviours copy their settings and not their
  running state, so a copy starts fresh. A Text's template (`Coins
  {coins}`) is carried over to the copy, not the filled-in text.
- Physics, drawing and the list of behaviours already follow the scene's
  structure, so copies need nothing special there (a test drops a copied
  crate onto a floor).
- `game.spawn("Name")` remembers which object a name meant
  (`Game::findPattern`): making an object changes the scene, and looking
  the name up afresh for every copy made 4000 copies in one frame take 15
  seconds in the test; remembered, 0.1.

## Cost, honestly

Making or destroying an object changes the scene's structure, and in a
frame where that happens Rolo builds what it draws again (as it always
has when an object is destroyed). That is fine for a shot now and then
and for scenes of hundreds of objects. For hundreds of bullets a second a
pool switched on and off is still cheaper, as the stress test does it;
making that rebuild smaller is ADR 0021's remaining work.

## Tested

`tests/gameplay`: a clone with its child and its own fresh `self`; spawn
by name and by object, placed by numbers and by a vector, switched on,
with its own properties, destroying itself; what is said for a missing
name, properties without a Script and a property of the wrong kind
(nothing is made in those cases); the limit; a copied body falling in
physics; copying an object that is gone.

## Not done

- Prefabs (patterns as files).
- Making an object from nothing, or adding components from a script.
- Copies in saved games.
- An object's parent and children from scripts (next).
