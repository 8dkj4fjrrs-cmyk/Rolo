# ADR 0033: Prefabs, step 1: an object kept as a file

- **Status:** Accepted; built (2026-11-17). Two parts were changed by
  [ADR 0035](0035-linked-copies-of-prefabs.md): placed objects are now
  linked copies, and Save as Prefab no longer replaces a file
- **Follows:** ADR 0005 (scenes and files), ADR 0006 (assets), ADR 0030 (making objects while playing)

## The need

Since ADR 0030 a script makes objects by copying one: a Bullet sits in
the scene, switched off, and `game.spawn("Bullet", x, y)` copies it. That
pattern has to be in **every scene** that shoots, and changing the bullet
means changing it in each. The same goes for anything used in more than
one scene: an enemy, a coin, a door.

Other engines keep such a thing as a file: a prefab (Unity), a scene
used as a part (Godot), a model in ServerStorage (Roblox).

## Decision

**A prefab is one object, with everything inside it, kept as a file of
the project** (`.lprefab`).

- **Making one:** select an object, **Edit › Save as Prefab**. The file
  goes to the project's `prefabs` folder, named after the object. Doing
  it again for an object of the same name replaces the file: that is how
  a prefab is changed.
- **In the editor:** drag the prefab from Assets into the Scene view (or
  **Place in Scene**) to put a copy into the scene. It is one undo step.
- **In a game:** `game.spawn("Bullet", x, y)` works as before, and when
  the scene has no object called Bullet it uses the prefab called Bullet
  (the file's name, or its path). The scene needs no pattern any more.
  An object of the scene still comes first, so existing games do not
  change.

### What a placed copy is: a copy

The object put into a scene from a prefab is **an ordinary object with no
tie to the file**. Changing the prefab later does not change copies
already placed in scenes; `game.spawn` always uses the file as it is now.

This is the large thing left out, on purpose. Copies that follow their
prefab (with each copy's own changes kept on top) need a way to say, per
copy, what was changed and what follows the file, in the scene format,
the Inspector, undo and the renderer. That is step 2 and its own ADR.
Step 1 already gives what was asked for: patterns that live in one file
and are spawned in any scene.

### The file

A prefab file is a scene file under another name, without the scene's
name and gravity:

```json
{ "format": "rolo.prefab", "version": 1, "entities": [ ... ] }
```

The first entity is the object; the rest are inside it. One reader and
one writer serve both (`SceneFile.cpp`), and the prefab format's version
**is** the scene format's: when scenes get an upgrade step, prefabs get
it too, and nothing can be forgotten. A file with no object, or more than
one at the top, is refused.

The object keeps its IDs in the file (saving the same prefab again
changes only what changed); whatever puts it into a scene gives the copy
new ones.

## How it is built

- `AssetType::Prefab`, by the extension. Prefabs are in exports like
  every project file, and **Check Content** and exporting read each one
  (and check the assets it refers to) as they do scenes.
- Editor: `PlaceObject` (a command: a snapshot put at the end of the top
  level with new IDs), `EditorApp::savePrefabFromSelection`.
- Game: `Game::findPattern` looks in the scene first, then for a prefab.
  The prefab's object is **brought into the playing scene once, switched
  off, as the pattern**, and copies are made from it by the code that
  already existed (`Game::clone`: properties, texts, physics bodies all
  as in ADR 0030). The file is read once per scene and name. The game
  asks its scene loader for the file (`GameSetup::loadScene`, which now
  also reads prefabs, as a scene holding the one object), so games read
  prefabs the way they read scenes, on every platform.
  - So, as with a pattern kept in the scene, `game.find("Bullet")` may
    give the switched-off pattern.
  - The playing scene is a copy (ADR 0007): the scene being edited never
    gets the pattern.

## Tested

- Workspace (Linux, GCC 13): `[prefab]` tests for the file (round trip,
  refusals), the editor's command (place, undo, redo), and `game.spawn`
  from a prefab (by name, by path, with properties, children coming
  along, a scene's object first, a broken file, two of the same name).
- By hand, in the editor on a virtual screen (Linux): Save as Prefab,
  the prefab's page, Place in Scene; and a script spawning a prefab in
  the editor's play mode, in the player, and in an exported Linux game.
- Not tried: dragging from Assets into the Scene view with a real mouse
  (the same code as Place in Scene), a web or Android game that spawns
  from a prefab (they read prefabs by the same code as scenes).

## Not done

- **Copies that follow their prefab** (step 2).
- Opening a prefab to edit it on its own; a prefab is changed by changing
  a copy in a scene and saving it again.
- Asking before an existing prefab is replaced, and undoing it.
- Prefabs inside prefabs as such: an object saved with a placed copy
  inside simply contains it.
- Spawned copies are still not in saved games (ADR 0030).
