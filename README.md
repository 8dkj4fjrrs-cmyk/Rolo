<p align="center"><a href="https://github.com/8dkj4fjrrs-cmyk/Rolo"><img width="160" src="docs/images/rolo-logo.svg" alt="Rolo Engine logo"></a></p>

# Rolo Engine

## A 2D Game Engine and Editor.

**Rolo Engine** is an open-source, C++20 game engine with its own
standalone editor, for making all kinds of 2D games. Build a game in the
editor with no code, or with **Lua scripts** and no compiler. Then export it
as a program players run without Rolo.

Please read **[Getting started](docs/getting-started.md)** to go from a new
project to a game you can share in half an hour.

> **Status: 0.9.1-alpha.**
>
> - Built and tested on Linux, macOS and Windows by CI.
> - Used by hand on Linux and macOS so far.
> - Read the **[known limitations](docs/known-limitations.md)** before relying on it.

***

[![CI](https://github.com/8dkj4fjrrs-cmyk/Rolo/actions/workflows/ci.yml/badge.svg)](https://github.com/8dkj4fjrrs-cmyk/Rolo/actions/workflows/ci.yml)

[![Latest Release](https://img.shields.io/github/v/release/8dkj4fjrrs-cmyk/Rolo?include_prereleases&label=release)](https://github.com/8dkj4fjrrs-cmyk/Rolo/releases)
[![LICENSE](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![cxxstd](https://img.shields.io/badge/cxxstd-c++20-8A2BE2.svg)](https://github.com/8dkj4fjrrs-cmyk/Rolo)
[![Lua](https://img.shields.io/badge/scripting-Lua%205.4-000080.svg)](docs/scripting.md)
[![Platforms](https://img.shields.io/badge/platforms-macOS%20%7C%20Windows%20%7C%20Linux-lightgrey.svg)](docs/known-limitations.md)

![issues](https://img.shields.io/github/issues/8dkj4fjrrs-cmyk/Rolo?style=plastic)
![forks](https://img.shields.io/github/forks/8dkj4fjrrs-cmyk/Rolo?style=plastic)
![stars](https://img.shields.io/github/stars/8dkj4fjrrs-cmyk/Rolo?style=plastic)
![GitHub code size in bytes](https://img.shields.io/github/languages/code-size/8dkj4fjrrs-cmyk/Rolo?style=plastic)

[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-blue.svg)](https://github.com/8dkj4fjrrs-cmyk/Rolo/pulls)
[![Made with Axmol](https://img.shields.io/badge/runs%20on-Axmol%202.11-orange.svg)](https://github.com/axmolengine/axmol)

***

<p align="center"><img src="docs/images/editor.png" alt="The Rolo editor playing the Top-Down Starter: fireflies moved by a Lua script"></p>

## ⚡️ Getting it

* **Download it:** [Latest Release](https://github.com/8dkj4fjrrs-cmyk/Rolo/releases),
  for macOS (Apple silicon), Windows and Linux. Unzip it and start
  `rolo_editor`.
  - On a Mac, the first time: right-click the app and choose **Open**, or
    **System Settings › Privacy & Security › Open Anyway**. Rolo is not
    signed by Apple yet.
* **Build it from source:** [docs/building.md](docs/building.md). For
  example, on macOS:

  ```sh
  brew install cmake ninja git powershell   # once
  cmake --workflow --preset dev
  ./build/dev/bin/rolo_editor/rolo_editor.app/Contents/MacOS/rolo_editor
  ```

## Learning and docs

* [Getting started](docs/getting-started.md): your first game, step by step
* [Editor guide](docs/editor.md): every panel and tool
* [Scripting in Lua](docs/scripting.md): gameplay code without a compiler, with the full API
* [Sound and music](docs/sound.md): effects, music and volumes, with or without code
* [Gameplay in C++](docs/gameplay.md): behaviours and game modules (Rolo built from source)
* [Exporting games](docs/exporting.md): making a program to share
* [Making The Lantern Keeper](docs/making-the-lantern-keeper.md): a whole RPG, built step by step
* [File formats](docs/formats/scene.md) · [Known limitations](docs/known-limitations.md) ·
  [Roadmap](docs/roadmap.md) · [Changelog](CHANGELOG.md)

## General information

Rolo is an editor first: scenes, art, maps, dialogue and quests are made
by clicking, and saved as readable, versioned text files that work well with
git. Gameplay is added with ready-made components, Lua scripts or C++. A
game always plays a copy of the scene, so stopping the game never leaves
changes behind.

**Supported platforms**:
* Desktop: macOS (Apple silicon), Windows (x64), Linux (x64)
* Exported games run on the system they were exported on

**Languages**:
* Lua 5.4, for game scripts
* C++20, for the engine, and for game modules when building from source

**Renderer backends** (through [Axmol](https://github.com/axmolengine/axmol)):
* Metal on macOS
* OpenGL 3.3+ on Windows and Linux

**Architectures**:
* macOS: arm64
* Windows: x64
* Linux: x64

## Features

Some highlights:

- **A complete editor**, built with Rolo's own UI toolkit:
  - Hierarchy, Inspector, Assets, Console and docking panels;
  - full undo and redo;
  - layouts that are remembered.
- **Scenes and art**: sprites, tilemaps painted with tilesets, colliders,
  text, cameras and shapes, with pixel-perfect filtering for pixel art.
- **Flipbook animation**: cut sprite sheets into clips in the Animation
  panel, and play them from components or scripts.
- **RPG content without code**:
  - dialogue with choices;
  - quests with objectives;
  - game variables;
  - triggers with conditions and actions;
  - save games (F5 / F9).
- **Lua scripting**:
  - `start(self)` and `update(self, dt)`, with objects, input, collisions,
    animation, variables, events and scene changes;
  - sandboxed, so scripts cannot reach files or programs;
  - errors point at the file and line;
  - endless loops are stopped.
- **Sound and music**: WAV, OGG and MP3, from a Sound component, trigger
  actions or scripts. Music fades between scenes, and carries on when the
  next scene has the same piece.
- **Play mode** inside the editor: ▶ to play, ■ to stop. Errors show in the
  Console, and the scene being edited is never changed.
- **New Project wizard**:
  - an Empty project;
  - a **Top-Down Starter** that plays at once;
  - a copy of the demo game.
- **Exporting**: **Build** checks the whole project, then makes a
  stand-alone game with the license texts it must carry (an app bundle on
  macOS).
- **[The Lantern Keeper](examples/lantern_keeper)**, an original ten-minute
  RPG made with Rolo. An automated test plays it to the end on every
  change.
- **Clean licensing**: everything inside a game is MIT or similarly
  permissive. Rolo builds Axmol without its networking libraries (ADR 0012).
- **Tested on every change** by CI on Linux, macOS and Windows: over 300
  tests, including ones that play games in real windows.

Please read the [changelog](CHANGELOG.md) for everything that changed, and
the [roadmap](docs/roadmap.md) for what comes next.

## Contributions

How to contribute:

* Suggestions, bug reports, fixes and improvements are very welcome!
  - [Instructions for contributing](CONTRIBUTING.md)
  - [Contributor and architecture rules](AGENTS.md)
  - [Design decisions (ADRs)](docs/adr)
* Adding a star on GitHub helps Rolo grow. Thank you!

## Repository layout

```
engine/core/     Engine lifecycle, logging, version (no third-party deps)
engine/scene/    Scenes, entities, components, scene and project files (no backend)
engine/assets/   Asset IDs (.meta files), import, image checks, tilesets (no backend)
engine/content/  RPG content: variables, dialogue, quests, conditions, actions, saves (no backend)
engine/gameplay/ Behaviours, Lua scripts, game modules, input, collision queries, scene changes (no backend)
engine/ui/       Rolo UI: the editor's own interface toolkit (widgets, docking, text)
engine/runtime/  Window and main loop (rolo::Application); wraps Axmol privately
player/          rolo_player, the game runtime application
editor/          The editor: model (logic, undo), panels, application
examples/        The Lantern Keeper (the demo RPG) and the sample game
templates/       New Project templates (Empty, Top-Down Starter)
third_party/     Small libraries kept in the repository (Lua, stb)
licenses/        License texts of everything inside an exported game
tests/           Catch2 unit tests and window tests
tools/release/   Release helpers (license texts, Windows DLL check)
cmake/           CMake helpers, the pinned Axmol download and its patches
docs/            Guides, file formats and architecture decision records
```

## Reference links

- [Axmol Engine](https://github.com/axmolengine/axmol): the runtime Rolo
  runs on (v2.11.5, pinned to an exact commit). Why Axmol:
  [ADR 0001](docs/adr/0001-runtime-foundation.md).
- [Lua](https://www.lua.org): the scripting language.
- [Releasing Rolo](docs/releasing.md): how a version is built and published.

## License

Rolo Engine is released under the [MIT License](LICENSE).
Copyright (c) 2026 Oscar Hird.

Third-party components keep their own licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and [licenses/](licenses/README.md).
Games exported with Rolo carry those license texts.
