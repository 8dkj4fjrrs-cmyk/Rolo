# RPG content formats: variables, dialogues, quests, saves

Rolo keeps a game's story content as data: game variables, dialogues and
quests, each in its own asset file (with a `.meta` ID like any asset, see
[scene formats](scene.md#asset-ids-meta-files)). This page describes format
**version 1** of each, the conditions and actions they use, and save games.

Code: `engine/content` (`Content.hpp`, `Logic.hpp`, `ContentLibrary.hpp`,
`GameState.hpp`). Design reasons: [ADR 0008](../adr/0008-rpg-content.md).

**Content never contains code.** Conditions and actions are a fixed list of
types, each checked when the file is read. Nothing in a project file is run
as a program.

## Identifiers

Variables, speakers, dialogue nodes, choices and objectives have an `id`: a
short name such as `gems` or `ask_help` (letters, digits and `_`, starting
with a letter or `_`, at most 64 characters). Ids are saved and referred to,
so they are never changed after creation; names and text shown to players
can change freely. Files refer to each other by **asset ID** (16 hex
digits, from the other file's `.meta`).

## Variables (`.lvars`)

```json
{
  "format": "rolo.variables",
  "version": 1,
  "variables": [
    { "id": "gems", "type": "int", "initial": 0, "description": "Gems carried" },
    { "id": "met_elder", "type": "bool", "initial": false },
    { "id": "hero_name", "type": "text", "initial": "Ana" }
  ]
}
```

| Key | Meaning |
|---|---|
| `id` | The variable's identifier; unique across the whole game (all `.lvars` files). |
| `type` | `"bool"` (true/false), `"int"` (whole number) or `"text"`. |
| `initial` | The value a new game starts with (default `false`, `0` or `""`). |
| `description` | Optional note for the editor. |

Text shown to players (dialogue lines, choices, objectives, `log` actions)
can include a variable's value: `"You have {gems} gems"`. Write `{{` for a
literal `{`.

## Conditions

A condition is an object with a `"type"`:

| Type | Fields | True when |
|---|---|---|
| `always` | — | always (the default where a condition is optional) |
| `compare` | `variable`, `op`, `value` | the variable compared with the value holds. `op`: `==` (default), `!=`, and for whole numbers also `<`, `<=`, `>`, `>=` |
| `quest` | `quest` (asset ID), `state` | the quest is `notStarted`, `active`, `completed` or `failed` |
| `objective` | `quest`, `objective` (id) | that objective is complete |
| `all` | `conditions` (list) | every one is true |
| `any` | `conditions` (list) | at least one is true |
| `not` | `condition` | it is false |

```json
{ "type": "all", "conditions": [
    { "type": "compare", "variable": "gems", "op": ">=", "value": 3 },
    { "type": "not", "condition": { "type": "quest", "quest": "5a4d1e0000000011", "state": "completed" } } ] }
```

The value must be of the variable's type. Conditions may nest 32 levels deep.

## Actions

Actions are lists of objects with a `"type"`, run in order:

| Type | Fields | Does |
|---|---|---|
| `set` | `variable`, `value` | sets a variable |
| `add` | `variable`, `value` (whole number, may be negative) | adds to a whole-number variable |
| `startQuest` | `quest` | starts a quest (if not started and its prerequisites are complete) |
| `completeObjective` | `quest`, `objective` | completes an objective of an active quest |
| `failQuest` | `quest` | fails an active quest |
| `startDialogue` | `dialogue` | starts a dialogue (unless one is showing) |
| `changeScene` | `scene` | changes to another scene at the end of the frame |
| `setActive` | `entity` (object ID or `null`), `active` | switches an object on or off |
| `destroy` | `entity` (object ID or `null`) | removes an object for good (it stays removed when the scene is visited again, and in saves) |
| `emit` | `event` | sends a named game event (gameplay code and triggers can react) |
| `log` | `text` | writes to the log / the editor's Console |
| `wait` | `seconds` (0–3600) | pauses the rest of the list (timed sequences) |
| `saveGame` | `slot` | saves the game to the named slot |
| `loadGame` | `slot` | loads the named slot |
| `playSound` | `sound` | plays a sound effect (an audio asset) |
| `playMusic` | `sound` | plays an audio asset as the music, fading from the music before over one second; the same music already playing carries on |
| `stopMusic` | — | fades the music out over one second |

`playSound`, `playMusic` and `stopMusic` were added after Rolo 0.9.1 without
changing the version: files without them read as before, and Rolo 0.9.1
and earlier report them as unknown action types.

`entity: null` means the object the actions belong to (e.g. the object with
the trigger). Object IDs only make sense inside a scene, so `setActive` and
`destroy` with an ID belong in a scene's triggers, not in dialogues or
quests.

## Dialogue (`.ldialogue`)

```json
{
  "format": "rolo.dialogue",
  "version": 1,
  "speakers": [
    { "id": "elder", "name": "Village Elder", "color": "#f2a93bff", "portrait": null }
  ],
  "start": "begin",
  "nodes": [
    { "id": "begin",
      "branches": [ { "condition": { "type": "quest", "quest": "5a4d1e0000000010", "state": "active" },
                      "next": "waiting" } ],
      "next": "greet" },
    { "id": "greet", "speaker": "elder", "text": "Welcome, {hero_name}.",
      "actions": [ { "type": "set", "variable": "met_elder", "value": true } ],
      "next": "ask" },
    { "id": "ask", "speaker": "elder", "text": "Will you find three gems for me?",
      "choices": [
        { "id": "yes", "text": "Of course.",
          "actions": [ { "type": "startQuest", "quest": "5a4d1e0000000010" } ], "next": "thanks" },
        { "id": "rich", "text": "I have {gems} already!",
          "condition": { "type": "compare", "variable": "gems", "op": ">=", "value": 3 }, "next": null },
        { "id": "no", "text": "Not now.", "next": null } ] },
    { "id": "thanks", "speaker": "elder", "text": "Thank you!", "next": null },
    { "id": "waiting", "speaker": "elder", "text": "You have {gems} of 3.", "next": null }
  ]
}
```

- **Speakers**: `name` is shown to players, `color` colours the name,
  `portrait` is an optional image asset.
- **Line nodes** have `text`, said by `speaker` (leave it out for
  narration), and either `choices` or a `next` node (`null`: the dialogue
  ends).
- **Choices** are shown only when their `condition` is true. Choosing one
  runs its `actions` and goes to its `next` node (`null`: the end).
- **Branch nodes** have no text: they go to the first branch whose
  condition is true, otherwise to `next`. Use one as `start` to begin a
  conversation differently depending on progress.
- Any node runs its `actions` when it is reached.

## Quests (`.lquest`)

```json
{
  "format": "rolo.quest",
  "version": 1,
  "name": "Gems for the Elder",
  "description": "The elder needs three gems.",
  "prerequisites": [],
  "startWhen": null,
  "objectives": [
    { "id": "collect", "text": "Find 3 gems ({gems}/3)",
      "completeWhen": { "type": "compare", "variable": "gems", "op": ">=", "value": 3 }, "optional": false },
    { "id": "return", "text": "Bring them to the elder", "completeWhen": null, "optional": false }
  ],
  "failWhen": null,
  "rewards": [ { "type": "set", "variable": "elder_happy", "value": true } ]
}
```

| Key | Meaning |
|---|---|
| `prerequisites` | Quests (asset IDs) that must be completed before this one can start. |
| `startWhen` | Starts by itself when this condition becomes true (and prerequisites are complete); `null`: only a `startQuest` action starts it. |
| `objectives` | Each completes by itself when `completeWhen` is true, or by a `completeObjective` action. `optional` objectives are not needed to finish. |
| `failWhen` | Fails while active when this becomes true; `null`: never by itself. |
| `rewards` | Actions run when the quest completes (after all required objectives). |

Quests are brought up to date every frame. Starting, completing objectives,
completing and failing are reported in the log and to gameplay code.

## Checks

When content is loaded (in the editor and in the player), Rolo checks:

- each file's shape: known fields, required fields, values of the right
  kind, unique ids (a file with an error is not loaded; unknown fields are
  warnings);
- references: variables exist and values match their type (and only whole
  numbers use `<` `<=` `>` `>=`), quests, objectives, dialogues and scenes
  exist, speakers exist, every `next` leads to a node that exists;
- shape of the story: no circle of quest prerequisites, no branch nodes
  that only lead to each other, nodes no other node leads to (a warning),
  quests without required objectives (a warning).

Messages say the file and the place in it, for example
`content/elder.ldialogue, node 'ask', choice 'yes', action 1 (startQuest):
The quest 5a4d1e0000000010 does not exist (was it deleted?).`

## Save games (`.lsave`)

A save holds progress only, never content:

```json
{
  "format": "rolo.save",
  "version": 1,
  "game": "Rolo Sample",
  "scene": "5a4d1e0000000004",
  "player": [212, 480],
  "variables": { "gems": 2, "met_elder": true, "hero_name": "Ana" },
  "quests": { "5a4d1e0000000010": { "state": "active", "objectives": ["collect"] } },
  "removed": { "5a4d1e0000000004": ["e17e000000000002"] },
  "fired": { "5a4d1e0000000004": ["e17e000000000009"] }
}
```

| Key | Meaning |
|---|---|
| `scene`, `player` | The scene being played and where the player stood. |
| `variables` | Every variable's value. |
| `quests` | Started quests: their state and completed objectives. |
| `removed` | Per scene: objects removed for good (collected, destroyed). |
| `fired` | Per scene: one-time triggers that already ran. |

Saves keep working when the game is updated: a variable or quest the game
no longer has is dropped (with a warning), a variable whose type changed
starts from its starting value, and variables added since start from their
starting values.
