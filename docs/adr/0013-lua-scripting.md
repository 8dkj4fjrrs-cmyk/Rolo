# ADR 0013: Lua scripting

- **Status:** Accepted; the language part (Lua 5.4) is superseded by ADR 0020 (Luau)
- **Date:** 2026-11-03
- **Revisits:** ADR 0007 (gameplay is C++ only, statically linked)

## Context

Gameplay code in Rolo was C++ behaviours, compiled into the editor and
the player (ADR 0007). The brief asked for that first, and to leave
plugins, hot reload and scripting languages for later. The consequence:
- a downloaded Rolo runs only built-in behaviours and the example games'
  modules;
- anything more needs a C++ toolchain and a build of Rolo from source.

The known limitations called this "the main thing to change after the
alpha". The project owner chose Lua.

Other engines use two ways around this:
- a scripting language the engine runs itself (Unity's C#, Godot's
  GDScript, Defold's and LÖVE's Lua);
- compiled plugins loaded at run time (Unreal, Godot's GDExtension).

The second still needs a compiler on the user's machine and a stable
binary interface between the engine and the plugin, which C++ does not
offer across compilers.

## Decisions

### 1. Lua 5.4, kept in the repository and compiled as C++

**Why Lua:**
- small (about 30 C files), fast to start, MIT;
- designed to be embedded;
- familiar from other 2D engines.

**Version: 5.4.9**, not 5.5. 5.4 is what most tutorials, tools and editor
plugins target, which matters for beginners. Moving to 5.5 later is a small
change.

**Kept unmodified in `third_party/lua/`**, like stb, not downloaded at
configure time:
- no network needed to build;
- every preset, including `core`, has it.

**Only the core is copied:** the language and the base, coroutine, string,
table, math and utf8 libraries. The io, os, debug and package libraries,
which reach files, programs and native code, are not even compiled.

**Compiled as C++,** so that a Lua error unwinds with an exception instead
of `longjmp`, and the destructors of C++ objects in the bindings run.

Rejected:
- **LuaJIT:** faster, but its JIT conflicts with macOS's rules for
  executable memory. It is also at Lua 5.1, and slower to update.
- **sol2** (the C++ binding library Axmol bundles): very large templates,
  slow to compile, and hard for beginners to read. Rolo's API is a few
  dozen functions, written directly against Lua's C API.

### 2. Scripts are assets; a Script behaviour runs them

**Scripts as assets:**
- `.lua` files are assets of type `script`, with `.meta` IDs like any
  other;
- the **Script** component (`rolo.Script`) refers to its file by ID, so
  renaming the file keeps the link.

**Script as a behaviour:**
- it is an ordinary behaviour, so it shares everything behaviours already
  have: the Inspector, saving, undo, start/update order, and "a failing
  behaviour is switched off and reported";
- its `start()` and `update()` call the file's `start(self)` and
  `update(self, dt)`.

**A file is a set of functions, not a class,** as in Defold:
- each file runs once, in an environment table of its own (its globals);
- each object gets a `self` table, with `self.object` its handle.

This is the least a beginner must learn: two functions with fixed names, and
`self` for per-object state.

**One Script per object** for now: a behaviour type appears once on an
object. Shared code goes in modules (`require`).

### 3. One sandboxed Lua state per running game

The Game owns a `ScriptHost` (`engine/gameplay/src`), made when the first
script starts and dropped when the game stops. So every press of Play:
- starts from clean state;
- reads the script files again: edit, save, Play.

Safety:
- **No harmful libraries:** besides the libraries left out, `dofile`,
  `loadfile` and `load` (which accepts bytecode that can crash the VM) are
  removed.
- **An instruction budget:** a count hook stops any single call after 50
  million instructions with an error naming the file and line. A `while
  true do end` cannot freeze the editor.
- **A memory cap:** a custom allocator caps all scripts together at 256 MB.
- **Errors carry a traceback** reduced to the script lines. The behaviour is
  switched off, the Console shows the message, and the rest of the game
  goes on.

Scripts are game data that players receive. These limits make a broken or
hostile script fail safely, but they are not a security boundary against
a determined attacker (Lua's VM itself is the trusted base, as in other
engines).

### 4. The API mirrors `Game`, in script terms

`ScriptApi.cpp` binds:
- **`game.*`:** find objects, the player, time, variables with type checks,
  events, scene changes by name, saves;
- **`input.*`:** keys by readable names, the move direction, the pointer;
- **object methods:** position, move with collisions, rotation, scale,
  active, destroy, animation, overlaps, text, flip;
- **`print`** (to the Console, with the file name) and **`require`**
  (project scripts by name, loaded once, with loop detection).

`game.run(...)` and `game.check(...)` accept the same written actions and
conditions as Triggers (`Notation.hpp`). So the whole content system is
available without a second API to keep in step.

**Names, not IDs:**
- scripts refer to scenes, dialogues and objects by the names shown in the
  editor, through `projectNotationNames()`, which moved from the editor's
  Inspector into `rolo_content` for this;
- scripts are code, not saved data, so the "IDs, not names" rule for files
  does not apply.

**Every binding is wrapped (`guarded`)** so that a C++ exception becomes a
Lua error. Without that, Lua (compiled as C++) would catch it as an unknown
error.

### 5. Editor support

- New Script in the Assets panel and the Game menu.
- The script's Inspector page: syntax check, the first lines of the code,
  and **Open in Code Editor** (the system's program for `.lua` files).
  There is no code editor inside Rolo: VS Code and others do it far
  better.
- **Check Content** and exporting compile every script (without running
  it). A syntax error stops an export.
- Help › Scripting Guide (`docs/scripting.md`).
- The Top-Down Starter's grove has fireflies driven by `firefly.lua`.

## Consequences

- Games can have custom logic without C++. The known limitation "your own
  behaviours need Rolo built from source" now applies only to C++.
- `rolo_gameplay` depends on `rolo_lua` (privately; no public header
  includes Lua).
- Every game carries Lua's MIT notice (`licenses/lua.txt`).
- New asset type `script` in `.meta` files. Older Rolos list such files
  as unknown; no file format version changes.
- Left for later:
  - script properties in the Inspector (per-object settings without
    editing code);
  - several scripts per object;
  - reloading while playing;
  - a debugger;
  - loading compiled C++ modules into a downloaded Rolo.
