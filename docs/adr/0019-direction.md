# ADR 0019: Rolo's direction: who it is for, and what follows from that

- **Status:** Accepted
- **Date:** 2026-11-09
- **Changes:** the order of the roadmap; scripting's place next to C++ (ADR 0007, ADR 0013)

## Context

Rolo began as a C++ engine where gameplay is written as `Behaviour`s
compiled into game modules (ADR 0007), with Lua added later so that the
downloaded editor can run custom code (ADR 0013). By 0.10 it has an editor,
RPG content, sound, physics and controls, but no stated audience, and so
no rule for choosing what comes next.

The free 2D-engine space is crowded (Godot above all; LÖVE, Defold and
Solar2D among Lua engines). An engine stands out by who it serves and
what it makes easy, not by having an editor or a scripting language.

## Decision

**Rolo is a free, truly MIT 2D engine for people who learned to make games
on Roblox and want games of their own: light, easy to use, and able to make
small games for desktop, phones and the web.**

That means, in order of importance:

1. **Truly MIT.** Rolo and everything a game contains are MIT, or a
   licence as free (zlib, public domain), with every notice shipped
   (`licenses/`). When no such library exists, Rolo writes its own and
   says so. No fees, royalties, sign-ins or splash screens, ever.
2. **Easy first.** Scripting is the front door: games are written in a
   language Roblox developers already know, in the downloaded editor, with
   nothing to compile. Errors explain the mistake in plain words. The
   starter projects show finished-feeling games (menus, controls, saving).
3. **Light.** 2D only for now; small programs and fast start-up; features
   earn their place.
4. **Desktop, web and mobile.** Web export comes first (a link to share,
   itch.io), then touch controls and screen sizes, then Android, then iOS.
   Axmol already runs on all of them; Rolo's part is the wiring, input,
   export and testing.

### Scripting: Lua-ux

- Rolo's scripting language becomes **Luau** (Roblox's Lua, MIT), used
  **unmodified**, replacing Lua 5.4. Roblox developers write what they
  already know (types, `+=`, `continue`, string interpolation), their
  editor tools (luau-lsp) work, and Rolo gets Luau's speed and safety
  work with each update.
- **Lua-ux** is the name for Rolo's flavour of it: unmodified Luau plus
  Rolo's API and a **performance layer** aimed at the two problems
  Roblox developers know best: **stutters** (garbage made every frame)
  and **low frame rates with many objects** (the cost of crossing from
  scripts into the engine). Planned: garbage-free vectors (Luau's native
  `vector`), APIs that make no garbage, collector work spread over every
  frame, cheap calls into the engine, one script updating many objects,
  native code for hot functions where the system allows (not iOS or the
  web), and a script profiler in the editor.
- Changing the language itself is not planned: every difference is
  something Roblox developers must unlearn, and a changed parser falls
  behind upstream Luau. Small, compatible additions stay possible.
- **Measured, not guessed:** a stress test (`examples/stress`) measures
  frame times and the worst frames. Each change to scripting is kept only
  if it measurably helps.

### C++

C++ stays the language of the engine and of extending it: fast systems,
bindings to other libraries, tools. Game modules (ADR 0007) remain;
loading them into a downloaded editor is a nice-to-have, not the main way
of making games.

### Later, not now

**3D.** Axmol has basic 3D, so it is possible; it would need a 3D editor
view, model import and 3D physics. Until then, designs avoid closing the
door (e.g. scene data that could gain a third axis) without making 2D any
harder.

## Consequences

- The roadmap is reordered: the stress test and Luau come first, then web
  export, touch, Android and iOS; editor polish (script properties in the
  Inspector, prefabs) continues alongside.
- Moving from Lua 5.4 to Luau changes what scripts may write (Luau is Lua
  5.1 with additions: no `goto`, integers are not a separate type). It is
  done now, while few Rolo games exist, with a guide for existing scripts.
- A "Coming from Roblox" guide maps Roblox's ideas to Rolo's.
- Features are judged by this ADR: does it make Rolo easier for its
  audience, keep it light and MIT, or bring it to the web and phones?
