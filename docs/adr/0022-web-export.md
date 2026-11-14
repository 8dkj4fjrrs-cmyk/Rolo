# ADR 0022: Games for the web

- **Status:** Accepted
- **Date:** 2026-11-14
- **Follows:** ADR 0019 (direction: "desktop, web and mobile", web first)

## Context

ADR 0019 puts web export first among the new platforms: a game that
plays from a link suits the people Rolo is for, and a browser is the
easiest new platform to test. Axmol 2.11.5 already builds for WebAssembly
with Emscripten; Rolo's own parts (Luau, Box2D, miniaudio, the player,
exporting) did not.

What a web game has to be: a folder anyone can put on any web host
(itch.io, GitHub Pages), made by the editor without extra tools, small,
and behaving like the desktop game.

## Decision

**The web runtime** is `rolo_player` built with Emscripten (preset `web`,
Emscripten 4.0.9): `rolo_player.js`, `.wasm`, `.data` (Axmol's shaders)
and the page `index.html`. It is built once per release and shipped inside
every system's download (`web-runtime/`), so any editor can export for the
web; a source build takes it from `build/web`.

- **No threads.** A page that uses threads must be served with
  cross-origin isolation headers, which plain hosts do not send. So
  Axmol's threads are off (`AX_WASM_THREADS`), and the scripts' watchdog
  thread (ADR 0020) is replaced there by Luau's interrupt looking at the
  clock every few thousand safe points.
- **WebAssembly exceptions and SIMD**, for everything, Axmol included:
  Luau's errors are exceptions, and Axmol and Box2D use SIMD. This sets
  the oldest browsers (Safari 16.4).
- **The main loop.** A page must return to the browser after every frame,
  so `Application::run` returns at once there
  (`RunReport::continuesInBackground`) and the browser calls the frames.
  The player therefore keeps everything a running game needs in one heap
  object (`Player`), which is not destroyed in a browser. (Leaving the
  stack in place with `emscripten_exit_with_live_runtime` was tried first:
  with WebAssembly exceptions it runs destructors, which destroyed the
  running game.)
- **The game's files** are one file, `game.pack`
  (`docs/formats/game-pack.md`), written by the editor in C++ and unpacked
  by the page (`player/web/pre.js`) into the runtime's memory before the
  game starts. Emscripten's own packager is a tool users do not have; a
  format of our own needs none.
- **Saved games** are files in a folder kept by the browser (IndexedDB,
  through Emscripten's IDBFS), one folder per game name, read before the
  game starts and written after each save.
- **Gamepads** are read with the browser's Gamepad API (its "standard"
  layout), since the browser's GLFW has no gamepad layouts.
- **The page** shows loading progress, errors in words (opened from a
  file, missing pack, old browser), "Thanks for playing" when the game
  quits, and scales the picture to the window.
- **Exporting**: `ExportTarget::Web` in the exporter, "Web browsers" in
  the Export dialog, `--export-web` on the command line. The export warns
  when scenes use C++ game-module behaviours, which the shipped web
  runtime does not have (a web runtime built with `ROLO_GAME_MODULE_DIRS`
  does).
- **Checked like a player**: `tools/web/check_web_game.js` serves an
  exported folder and plays it in headless Chromium; CI exports two
  templates with the editor and runs it.

## New software in web games

Web games contain Emscripten's runtime and system libraries, and IJG
libjpeg from Emscripten's ports, in place of the desktop's libjpeg-turbo:

| Software | License |
|---|---|
| Emscripten 4.0.9 (runtime, JavaScript support code) | MIT or NCSA |
| musl (the C library) | MIT |
| LLVM libc++, libc++abi, libunwind, compiler-rt | Apache-2.0 with LLVM exception |
| IJG libjpeg 9f | IJG (permissive, BSD-like) |

All permissive, none copyleft; their texts are in `licenses/` and go into
every export.

## Not done yet

- Touch controls and screen sizes (next), so phones can play.
- Rumble in browsers; crisp drawing on high-density screens; a size other
  than 1280 × 720.
- The editor serving a web export itself ("Play in browser").

## Consequences

- A release needs the web runtime built first (`release.yml`: one job,
  then each system's package includes it).
- `Application::run` has two behaviours; code after it must check
  `continuesInBackground`.
- The player's state is a struct, not `main`'s local variables.
