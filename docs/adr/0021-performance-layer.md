# ADR 0021: The performance layer: lists in the scene, cheaper script calls, vectors

- **Status:** Accepted
- **Date:** 2026-11-14
- **Follows:** ADR 0019 (direction, "Lua-ux"), ADR 0020 (Luau)

## Context

With Luau in place, the stress test showed that scripts which mostly call
Rolo's functions had not become faster, and that 5000 objects doing
nothing cost about 2 ms a frame. A profile (callgrind) of the `sprites`
and `scripts` scenes showed where the time went:

- More than half of a frame in `Scene::forEach` and
  `Entity::component(name)`: behaviours, triggers, dialogue, texts,
  sounds and physics each walked every object every frame, comparing
  component names and using `dynamic_cast` for every component.
- In script calls: hashing the text "update" and the name of the object
  type on every call, looking the script's file up in a map, copying its
  path twice, and finding the same object up to three times in one
  `setPosition`.

Running the scripts' own code was about a quarter of the frame.

## Decision

1. **The scene keeps lists** (`Scene::entitiesWith`, `all<T>`,
   `inOrder`, `findByName`): by component type, in tree order, and the
   first entity of each name. They are made when asked for and
   `structureRevision()` has changed, which happens when an entity is
   made, removed, renamed or moved in the tree, or a component is added
   or removed; not when values change (positions, `setActive`). The
   lists are the scene's own: callers that change the structure while
   going through one take a copy (`Game::findAll` does).
2. **Components are found by identity.** `Entity::get<T>()` compares
   the address of the component's `ComponentType` (there is one per
   type); looking up by name no longer uses `dynamic_cast`.
   `Scene::edit<T>` does the same.
3. **The game keeps its list of behaviours** while the structure is
   unchanged. If a behaviour changes the structure in the middle of a
   frame, the rest of that frame looks each behaviour up again, as
   before, so nothing removed is run.
4. **Script calls:** objects are tagged userdata; the names of the
   functions Rolo calls are Luau strings made once; the last script file
   is remembered; the file being run is a pointer, not a copy; functions
   that change a Transform find the object once.
5. **Vectors:** script functions that take `x, y` also take one Luau
   `vector`, so scripts can keep points as values and do sums without
   garbage. Functions keep returning plain numbers (`local x, y =
   obj:position()`), which never made garbage.

**Not kept:** stepping the garbage collector a fixed amount each frame.
Measured over three rounds at four sizes, it was within the noise of
Luau's own incremental collector.

**Rule from now on** (AGENTS.md): code that runs every frame uses the
lists, and performance changes are measured with `rolo_stress` before and
after.

## Measurements

`examples/stress/README.md`, "The performance layer". Before and after
taking turns on one machine, gameplay milliseconds per frame: `scripts`
2.48 → 0.58, `bullets` 1.86 → 0.35, `busy` 4.45 → 1.43 (its slowest
frame in a hundred 7.7 → 2.3), `sprites` 2.23 → 0.00.

## Consequences

- `Scene` has a `mutable` cache: asking for a list may build it. A scene
  must not be read from two threads at once (it never was).
- Renaming an entity now counts as a change of structure.
- Error messages for a wrong value where an object is expected say
  "object expected".
- Still to do for Lua-ux: spawning objects from scripts, one script for
  many objects, a script profiler, native code where the system allows.
