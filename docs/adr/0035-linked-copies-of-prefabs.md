# ADR 0035: Prefabs, step 2: copies that follow their prefab

- **Status:** Accepted; built (2026-11-18)
- **Follows:** ADR 0033 (prefabs, step 1), ADR 0005 (scenes and files)
- **Changes ADR 0033:** an object placed from a prefab is no longer a
  plain copy, and **Save as Prefab** no longer replaces a prefab file.

## The need

Step 1 made a prefab a pattern to copy. Place the Coin prefab forty
times, decide coins should be bigger, and all forty had to be changed by
hand. A prefab is only worth having if changing it changes its copies.

## Decision

**An object placed from a prefab is a linked copy: it follows the prefab
in everything that was not changed on the copy itself.**

- **Placing** a prefab (dragging it into the Scene view, **Place in
  Scene**) makes a linked copy. The Hierarchy marks its top object.
- **Edit › Save as Prefab** makes a new prefab file from the selected
  object *and links that object to it*. It never overwrites a prefab.
- A linked copy's Inspector has a **Prefab** section with three buttons:
  - **Apply to Prefab**: the prefab becomes what this copy is now. Every
    other copy follows, in every scene, keeping its own changes.
  - **Reset**: this copy drops its own changes and what was put inside
    it. It stays where it stands. Undoable.
  - **Unlink**: it becomes an ordinary object. Undoable.
- **What counts as a copy's own:** whatever differs from the prefab. Its
  place, a name, a property, a component added or removed, an object put
  inside it, an object of the prefab deleted from it. A Script's
  property values are compared one by one. There is no list of
  "overrides" kept anywhere: it is worked out when the scene is saved.
- **Where a copy stands is always its own.** Apply to Prefab does not
  move the prefab, so it does not move other copies.
- Copies placed before this (step 1) stay ordinary objects: nothing says
  which prefab they came from.

## How it works

**In the scene file, a linked copy is a link and its differences**
(scene format version 2; [the format](../formats/scene.md#a-linked-copy-of-a-prefab)):

```json
{ "id": "9f1fcc32c40069a8", "prefab": "8a506a6de0387cc9",
  "components": [ { "type": "rolo.Transform", "position": [640, 360] },
                  { "type": "rolo.Shape", "color": "#ff0000ff" } ],
  "objects": [ { "from": "504c010000000003", "id": "4372aa708148f43a" } ] }
```

**When a scene is read**, by the editor or by a game, each such entry is
turned back into ordinary objects before anything else happens: the
prefab's objects, given this scene's IDs, with the differences laid over
them (`expandLinkedCopies` in `SceneFile.cpp`, on the JSON). From there
on the whole engine sees ordinary objects. Nothing in the renderer,
physics, scripts, undo or the Scene view knows about prefabs.

Each object remembers where it came from (`PrefabLink`: the prefab on the
top object, and on every object which object of the prefab it is). That
is all the editor needs to write the scene back as links.

**When a scene is written**, each linked copy is compared with its prefab
as the prefab is at that moment, and only the differences are written
(`linkedCopyJson`).

So "the copy follows the prefab" is nothing more than: differences are
recorded against the old prefab when saving, and laid over the new one
when reading. **Apply to Prefab** in an open scene is exactly that, done
in memory (`sceneAfterApply`): write the scene against the prefab as it
was, read it against the prefab as it is.

### Details that matter

- **IDs are stable.** The file lists the scene's ID for each of the
  prefab's objects (`objects`). An object the prefab gained later has no
  entry yet: it gets an ID worked out from the copy's ID and its own, the
  same every time the scene is read, until the scene is saved. Saved
  games and script properties refer to objects by ID.
- **Applying keeps the prefab's own IDs** (`prefabTextFromCopy`), or
  other copies could not tell which object is which.
- **A property that points at an object inside the prefab** (ADR 0034)
  points inside each copy.
- **A missing prefab loses nothing.** If the prefab cannot be read, its
  copies become empty stand-ins with a warning, and their entries are
  written back to the scene file exactly as they were. Put the prefab
  back and they return.
- **Version 2 is written only where needed.** A scene without linked
  copies is still written as version 1 and stays readable by older Rolo.
  A prefab file never holds links (a linked copy inside a prefab is
  written into it in full).
- Everything that reads or writes scenes is given the project's prefabs
  (`PrefabTexts`, `prefabTextsOf`): the editor, the player, exporting,
  Check Content, the stress tool.

## Tested

- Workspace (Linux, GCC 13), `[linked]` and `[prefabs]` tests: a copy
  written as a link and read back with the same IDs; each kind of
  difference kept while the prefab changes under it (properties, names,
  components added and removed, objects added, removed and gained); an
  object the prefab lost that the copy had put something into; a
  missing and a damaged prefab, and its return; copies of copies;
  version 1 files; Save as Prefab, Apply, Reset, Unlink and their undo;
  a game playing linked copies whose scripts point at their own parts.
- By hand, in the editor on a virtual screen (Linux): Save as Prefab,
  the Hierarchy mark and the Prefab section, a second copy placed,
  Apply to Prefab (the other copy followed and kept its place), a
  colour of its own, Reset and its undo, the saved scene file. The same
  project then played in the player and as an exported Linux game, and
  in the player with the prefab file deleted (stand-ins and a warning).
- Not tried: Unlink by hand (it is tested), dragging a prefab into the
  Scene view with a real mouse, a web or Android game with linked
  copies (they read scenes by the same code).

## Added the same day: a copy's own fields in the Inspector

The Inspector marks what the selected object of a copy has of its own:
the name of each such field is drawn in the accent colour behind a small
bar, and the Prefab section sums it up ("Its own here: 2 fields, 1
component added"). A click on a marked name puts the prefab's value back
for that field; it is an ordinary `SetProperty`, so it is undone like any
change. A Script's properties are marked one by one.

- `differencesFromPrefab` (`editor/model/Prefabs.*`) compares the object
  with the prefab's, read as a scene: properties by value, named values
  by name. It runs when the scene or the project's files have changed,
  for the one object shown; the prefab is read again only when a file
  changed.
- `ui::PropertyField::setMarked` / `setOnLabelClick` are the widget's
  part; they know nothing of prefabs.
- Where the top object stands is not marked: it is always the copy's own.
- Tested: `[own]` and `[inspector]` tests; and by hand in the editor on a
  virtual screen (Linux): a copy's own colour marked, a click on its name
  putting the prefab's back, and undo.

## Not done

- **Putting back one by one** a component the copy added or removed, its
  name, or a component's other contents (a Tilemap's tiles): they are
  counted in the Inspector, and Reset puts the whole copy back.
- **Prefabs inside prefabs** that stay linked.
- **Opening a prefab on its own** to edit it.
- **Undo for Apply to Prefab:** it rewrites the prefab file and rebuilds
  the open scene, whose undo history starts afresh.
- **Order inside a copy:** on reading, the prefab's objects come first
  under their parent, then what the copy added.
- **Exporting does not refuse** a scene whose prefab is missing: it
  warns, and the game has empty stand-ins there.
- **Other open copies of a prefab in other scenes** follow when those
  scenes are next opened; there is no project-wide refresh to watch.
