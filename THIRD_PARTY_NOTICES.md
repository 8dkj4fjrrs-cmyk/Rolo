# Third-Party Notices

Rolo Engine's own code is MIT-licensed (see `LICENSE`). This file lists
third-party software it uses, what for, and whether it ends up inside games
built with Rolo ("shipped").

Large dependencies are not copied into this repository: Axmol is downloaded
by CMake into `external/` at a pinned commit (see `cmake/RoloAxmol.cmake`).
Small ones are kept in the repository, unmodified and with their license
files: `third_party/stb/stb_truetype.h`, `third_party/stb/stb_image.h`, Lua's
sources in `third_party/lua/`, miniaudio and stb_vorbis in `third_party/miniaudio/`, Box2D in `third_party/box2d/` and the Inter fonts in `assets/fonts/`.

## Runtime backend

| Component | Version | License | Shipped? |
|---|---|---|---|
| [Axmol Engine](https://github.com/axmolengine/axmol) | 2.11.5 (`6c0dce6`), with Rolo's patches (`cmake/axmol-patches/`) | MIT | Yes |
| [axslcc](https://github.com/axmolengine/axslcc) shader compiler | 1.14.0 | see upstream | No — build tool only |

### Libraries bundled by Axmol and linked into `rolo_player`

Taken from the Linux build: the libraries on `rolo_player`'s link line,
plus the header-only libraries whose headers the compiled sources include
(checked with `ninja -t deps`, last after removing the networking
libraries). Versions are from Axmol's
`3rdparty/README.md`. The macOS set is similar but uses Apple's Metal instead
of Glad/GLFW-on-X11; it is re-checked when a macOS build runs in CI.

**The full license texts are in [`licenses/`](licenses/README.md)**, collected
from each project's repository at these versions by
`tools/release/fetch_licenses.py`. Every exported game, and every installed
copy of Rolo, carries that folder.

| Library | Version | License |
|---|---|---|
| astcenc | 5.3.0 | Apache-2.0 |
| Clipper2 | 2.0.1 | BSL-1.0 |
| moodycamel::ConcurrentQueue (header-only) | 1.0.4 | BSL-1.0 or Simplified BSD |
| ConvertUTF | git 20451cb | Apache-2.0 WITH LLVM-exception |
| doctest (linked by Axmol's core; no tests run in games) | 2.4.12 | MIT |
| FastLZ | git 344eb40 | MIT |
| {fmt} | 11.2.0 | MIT |
| FreeType | 2.14.1 | FreeType License (BSD-style) |
| Glad | 2.0.8 | MIT |
| GLFW (modified by Axmol) | 3.4 | zlib |
| libjpeg-turbo | 3.1.3 | IJG / BSD-3-Clause / zlib |
| mio (header-only) | git 8b6b7d8 | MIT |
| OpenSSL (Axmol's core uses its digests and AES) | 3.0.18 | Apache-2.0 |
| libpng | 1.6.53 | PNG Reference Library License v2 |
| poly2tri | git 81612cb | BSD-3-Clause |
| pugixml | 1.13 | MIT |
| RapidJSON (header-only) | 1.1.0-24b5e7a | MIT |
| robin-map (header-only, modified by Axmol) | 1.4.0 | MIT |
| simdjson | 3.10.1 | Apache-2.0 |
| stb_image (Axmol's copy) | 2.30 | MIT or public domain |
| minizip (unzip) | 1.2 (modified) | zlib |
| libwebp | 1.6.0 | BSD-3-Clause (Google) |
| xsxml (header-only) | 1.1 | MIT |
| xxHash | 0.8.3 | BSD-2-Clause |
| zlib | 1.3.1 | zlib |

**Not included** in Rolo builds: Axmol's optional extensions (its Dear ImGui
integration, inspector, SDF generator, and so on) are switched off.
**Not included** either: OpenAL Soft (LGPL-2.1), because
Axmol audio is switched off. It must be dynamically linked or replaced when
audio is enabled.

**Not included: Axmol's networking.** Rolo games make no network
connections, so Rolo builds Axmol without its network module (HTTP,
WebSocket, downloader) and without its remote debug console. That leaves out
**curl, llhttp, websocket-parser and yasio** (ADR 0012).

yasio was also used outside networking, for a few containers and helpers in
Axmol's core. Rolo replaces those with its own code,
`engine/runtime/axmol_compat/` (MIT, written for Rolo, no yasio code), and
deletes yasio from its copy of Axmol so nothing builds it. The reason: yasio's
upstream license adds the "Anti 996" License 1.0 to MIT, a non-standard
license that Rolo does not want every game to carry.

**Windows only: the Microsoft Visual C++ runtime** (`msvcp140.dll`,
`vcruntime140.dll`, `vcruntime140_1.dll`) is copied beside the editor and the
game runtime, so programs start on PCs without the Visual C++
Redistributable. These are Microsoft's redistributable files, shipped under
the Visual Studio license terms for redistributable code; they are not open
source.

## Gameplay scripts (`engine/gameplay`)

| Component | Version | License | Shipped? |
|---|---|---|---|
| [Lua](https://www.lua.org) (`third_party/lua/`) | 5.4.9 (`312b9ef`, from the official mirror github.com/lua/lua) | MIT | Yes: runs Lua scripts in the editor and in games |

Kept in this repository, unmodified, with its license file. Only the language
core and the base, coroutine, string, table, math and utf8 libraries are
copied and built; the io, os, debug and package libraries are left out on
purpose (see `third_party/lua/README.md` and ADR 0013). Its MIT notice goes
with every game in `licenses/lua.txt`.

## Sound (`engine/audio`)

| Component | Version | License | Shipped? |
|---|---|---|---|
| [miniaudio](https://github.com/mackron/miniaudio) (`third_party/miniaudio/`) | 0.11.26 (`877ea4a`) | public domain (Unlicense) or MIT No Attribution | Yes: plays sounds in the editor and in games |
| [stb_vorbis](https://github.com/nothings/stb) (miniaudio's copy, `third_party/miniaudio/stb_vorbis.c`) | 1.22 | MIT or public domain | Yes: reads OGG sounds |

Kept in this repository, unmodified, with their license texts (see
`third_party/miniaudio/README.md` and ADR 0015). miniaudio was chosen over
Axmol's own audio, which needs the LGPL-licensed OpenAL Soft. Neither license
requires a notice in games; `licenses/miniaudio.txt` and
`licenses/stb-vorbis.txt` go with every game anyway.

## Physics (`engine/physics`)

| Component | Version | License | Shipped? |
|---|---|---|---|
| [Box2D](https://github.com/erincatto/box2d) (`third_party/box2d/`) | 3.2.0 (`178495b`) | MIT | Yes: physics in the editor's play mode and in games |

Kept in this repository, unmodified, with its license file; only the
library (`include/`, `src/`) is copied (see `third_party/box2d/README.md`
and ADR 0016). Its MIT notice goes with every game in `licenses/box2d.txt`.

## Rolo UI (the editor's interface toolkit, `engine/ui`)

Rolo's UI toolkit is Rolo's own code. It uses:

| Component | Version | License | Shipped? |
|---|---|---|---|
| [stb_truetype](https://github.com/nothings/stb) (`third_party/stb/stb_truetype.h`) | 1.26 (`003501f6`) | MIT or public domain (Unlicense), at our choice: MIT | Yes — in the editor; in games only once they use Rolo UI |
| [Inter](https://rsms.me/inter/) Regular and SemiBold (`assets/fonts/`) | 4.0 (font version 4.001) | SIL Open Font License 1.1 (`assets/fonts/Inter-LICENSE.txt`) | Yes — embedded in the program file |

The fonts are embedded unmodified. The OFL allows bundling and embedding them
in software, including commercial software, as long as the license text goes
with them and the fonts are not sold on their own. "Inter" is a Reserved Font
Name only if the font itself is modified — we do not modify it.

## Development and test tools (not shipped)

| Component | Version | License | Used for |
|---|---|---|---|
| [Catch2](https://github.com/catchorg/Catch2) | v3.16.1 | BSL-1.0 | Unit tests |
| [PowerShell](https://github.com/PowerShell/PowerShell) | 7.x | MIT | Axmol's download helper during configure |

## Scenes and project files (`engine/scene`)

| Component | Version | License | Shipped? |
|---|---|---|---|
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 (release archive, SHA-256 checked) | MIT | Yes: reads and writes project and scene files in the editor and in games |

Downloaded by CMake at configure time (`engine/scene/CMakeLists.txt`), not
copied into this repository. It is a header-only library; its MIT notice
must accompany shipped games: it is in `licenses/`, which exports include.

## Assets (`engine/assets`)

| Component | Version | License | Shipped? |
|---|---|---|---|
| [stb_image](https://github.com/nothings/stb) (`third_party/stb/stb_image.h`) | 2.30 (`003501f6`, the same stb commit as stb_truetype) | MIT or public domain (Unlicense), at our choice: MIT | Yes: checks image files (size, validity) in the editor and in games |

Compiled into `AssetDatabase.cpp` only, with every function `static` and
only the PNG and JPEG decoders, because Axmol carries its own copy of
stb_image for loading textures (listed with Axmol's libraries). The two
copies never meet at link time.
