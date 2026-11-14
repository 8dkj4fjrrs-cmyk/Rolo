# Game packs (`game.pack`)

A game pack is a project's files as **one file**, for places where a game
cannot be a folder. A game [exported for the web](../exporting.md#for-the-web)
is a pack beside Rolo's web runtime: the page downloads it and unpacks it
into the runtime's memory, where the game finds its project as it does on a
desktop.

Written by `rolo::writeGamePack` (`engine/assets`, used by the editor's
export) and read by `player/web/pre.js` in the page, and by
`rolo::readGamePackIndex` / `extractGamePack` (tests, tools).

## Layout (version 1)

| Bytes | What |
|---|---|
| 10 | The text `ROLOPACK1` and a line feed (`\n`): what the file is, and the layout's version |
| 4 | The length of the index in bytes, little-endian |
| that many | The index: JSON, UTF-8 |
| the rest | Every file's bytes, one after another, in the index's order |

The index:

```json
{
  "format": "rolo.pack",
  "version": 1,
  "name": "My Game",
  "saves": "my-game",
  "files": [
    { "path": "assets/hero.png", "size": 1523 },
    { "path": "project.rolo", "size": 96 },
    { "path": "scenes/main.lscene", "size": 2210 }
  ]
}
```

| Key | Meaning |
|---|---|
| `name` | The game's name. |
| `saves` | The folder name the game's saved games are kept under in the browser (letters, digits and `-`), so games on the same web site keep apart. |
| `files` | Each file's path inside the project (with `/`) and its size in bytes. A file's bytes begin where the previous file's end; the first begins right after the index. |

## Rules

- Files are in alphabetical order, folder by folder, and hidden files and
  folders (names starting with `.`) are left out: the same project always
  gives the same pack.
- Nothing is compressed. Web servers compress what they send, and images
  and sounds are compressed already.
- Readers refuse paths that could leave the folder a pack is unpacked into
  (absolute paths, `..`, `\`, `:`), packs cut short, and packs whose
  `version` is newer than they know.
- The format's version is in two places: the `1` of `ROLOPACK1` changes
  if the layout around the index ever does; `"version"` in the index
  changes with the index's own keys. Unknown keys are ignored.
