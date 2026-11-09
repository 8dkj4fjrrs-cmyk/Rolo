# Luau 0.744

The language of Rolo's gameplay scripts (ADR 0019, ADR 0020): Roblox's Lua,
with types, `+=`, `continue`, string interpolation and a faster virtual
machine and garbage collector.

- **Source:** https://github.com/luau-lang/luau, tag `0.744`
  (commit `32c28089305f488f901aaf1dbf3fd1cb65fa3d11`, 2026-10-23).
- **License:** MIT, see [LICENSE.txt](LICENSE.txt) (Roblox Corporation, and
  Lua.org for the parts that come from Lua 5.1) and
  [lua_LICENSE.txt](lua_LICENSE.txt) (Lua's own notice).
- **Unmodified.** Copied: the `include/` and `src/` folders of `Common`,
  `Ast`, `Bytecode`, `Compiler` and `VM`.
- **Left out:** type checking (`Analysis`), native code (`CodeGen`),
  `Config`, `Require`, `Inliner`, the `luau` programs (`CLI`), tests,
  fuzzers and benchmarks.

Built by `CMakeLists.txt` as the static library `rolo_luau`, with the same
source files as Luau's own `Sources.cmake` lists for those libraries.

Which libraries scripts get is decided by `engine/gameplay/src/ScriptHost.cpp`:
base (without `loadstring`, `getfenv`, `setfenv`), coroutine, table, string,
math, utf8, bit32, buffer and vector. Never `os` or `debug`; Luau has no
`io` or `package`.

To update: copy the same folders from a newer release tag, check the file
lists against its `Sources.cmake`, and update this file, `licenses/luau.txt`
(`tools/release/fetch_licenses.py`) and `THIRD_PARTY_NOTICES.md`. Then run
the script tests and the stress test (`examples/stress`).
