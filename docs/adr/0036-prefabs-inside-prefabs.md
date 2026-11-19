# ADR 0036: Prefabs inside prefabs

- **Status:** Accepted; built (2026-11-19)
- **Follows:** ADR 0033 (prefabs), ADR 0035 (copies that follow their prefab)
- **Changes ADR 0035:** a prefab file may hold linked copies. ("A linked
  copy inside a prefab is written into it in full" no longer holds.)

## The need

A House prefab with a Door inside, where the Door is a prefab too. Until
now, saving the House wrote the door into the house's file in full: the
house had a door of its own from then on, and changing the Door prefab
changed every door except the ones in houses.

## Decision

**A prefab keeps a copy of another prefab inside it as a link, and
follows it.**

- **Edit › Save as Prefab** on an object with linked copies inside keeps
  them linked in the new prefab's file.
- **Apply to Prefab** does the same: parts the prefab has from another
  prefab stay links, and a linked copy that was put into the copy
  becomes a link in the prefab.
- Change the Door prefab, and every house has the new door: in every
  scene, in games, and in copies made by `game.spawn("House")`.
- A house may say what *its* door differs in (where it hangs, a colour).
  Those are the house prefab's own changes to the door, kept as a
  linked copy's are in a scene.
- As deep as you like: a Street of Houses with Doors.
- **A prefab cannot hold itself**, directly or through another prefab.
  Apply to Prefab refuses and says why. A file that does so anyway (made
  by hand) is read with an empty stand-in in that place.

### In a scene, an inner prefab is simply part of the copy

Select the door of a house copy and the Inspector says it is one of the
objects of 'House', and that the prefab has this part from Door. What is
changed there is the *house copy's* own, and Apply to Prefab from the
house applies it to the House prefab (as the house's change to its
door), never to Door. To change Door itself, change a copy of Door that
stands on its own.

**Unlink** on a house copy makes the house ordinary objects; its door
becomes a linked copy of Door again, so it goes on following Door.

## How it works

Almost nothing is new. ADR 0035 reads a linked copy by taking "the
prefab's objects" and laying the copy's differences over them, and
writes it by comparing with "the prefab's objects". Now "the prefab's
objects" means the prefab *with the copies inside it already turned into
objects* (`prefabEntities` calls `expandLinkedCopies` on the prefab's
own entities, and so on inwards). So:

- a scene's copy of House is compared with the whole house, door and
  all, and never mentions Door. The scene file format does not change;
- when Door changes, the whole house changes, and with it what every
  copy of House is laid over.

**In the prefab's file**, the door is an entry exactly like a linked
copy in a scene (`"prefab"`, differences, `"objects"`, `"removed"`).
The `"objects"` list gives the IDs the door's objects have *in the
house*; those are the IDs the scene's copies know them by (`"from"`).
An object Door gains later gets an ID worked out from the door's ID in
the house and its own, the same on every reading.

**Marks.** Read as a prefab (`parsePrefab`), the house's door carries the
same marks a linked copy has in a scene (`PrefabLink`). Seen from
outside, in a scene's copy of House, every object is marked as House's
(`expandLinkedCopies` drops the inner marks): one level at a time. Apply
to Prefab gets the inner marks back from the prefab as it was
(`prefabTextFromCopy`), Unlink from the prefab as it is
(`linksAfterUnlinking`).

**Never without end.** While prefabs are being read, the ones being read
are remembered (`open`); meeting one of them again gives a stand-in.
`prefabHolds` answers "does this prefab hold that one?" from the files,
for Apply to Prefab (`applyWouldLoop`).

**Format version 3.** A prefab file with a link inside is written as
version 3, so that an older Rolo says "saved by a newer version" where
it would otherwise drop the link without a word. A prefab without one
is still version 1, a scene with linked copies still version 2. Older
files are valid as they stand; the upgrade step changes nothing.

Everything that reads a prefab file is now given the project's prefabs,
as scene readers are: the editor (placing, Reset, the Inspector, playing),
the player, exporting and Check Content.

## Tested

- Workspace (Linux, GCC 13), `[nested]` tests: a prefab written with a
  link inside and read back to the same text; the inner prefab changing
  (a property, a new object) under a prefab and under a scene's copy of
  it, with the copy's own change kept and IDs the same on every reading;
  the inner prefab missing and returning; a prefab holding itself
  through another; version 1 prefabs and version 2 scenes; Save as
  Prefab, Apply (other copies follow; then the inner prefab applied,
  and both houses follow), the refusal to loop, Unlink; a game spawning
  a prefab whose inner prefab changed after it was made. Also under the
  address sanitizer.
- By hand (Linux, virtual screen): a project with a Door prefab, a House
  prefab holding it, and two houses in a scene, one with a knob colour
  of its own. Played in the player; the Door file changed and played
  again (both doors followed, the colour stayed); exported as a Linux
  game and run. In the editor: the Hierarchy and the Inspector's words
  for the door, Unlink on a house (its door stayed linked), Save, and
  Save as Prefab of that house (the new file holds the door as a link);
  a second Door placed, moved into a house with Edit › Move Into the
  Object Above (it stayed where it stood) and applied (the house's file
  then held two links); a House moved into a House and applied (refused,
  with the message).
- Not tried: a web or Android game (they read prefabs by the same
  code); anything on macOS or Windows beyond CI's tests.

## Added with it: moving an object into another

Building a house with a door inside needs a way to put one object into
another, and the editor had none (an object could only be *made* inside
another). **Edit › Move Into the Object Above** makes the selected
object the last child of the object just above it at the same level;
**Move Out of Its Object** puts it after the object it was in. Both keep
the object where it stands by changing its position to match
(`moveKeepingPlace`: a `MoveEntity` and a `SetProperty` as one undo
step). Rotation and scale are left alone, so inside a turned or resized
object it may look different. Dragging rows in the Hierarchy is not
built.

## Not done

- **Applying to the inner prefab from inside the outer copy.** Change a
  copy of Door that stands on its own.
- **A mark in the Hierarchy** for the inner prefab's top object inside a
  copy; the Inspector says it.
- **A prefab whose top object is a copy of another prefab** ("variants"):
  Save as Prefab on a copy still writes the object in full.
- Reading is not cached: a prefab used by many copies in a scene is
  read once for each, and now its inner prefabs with it. Not measured.
