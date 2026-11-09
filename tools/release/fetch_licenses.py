"""Collects the license texts of everything compiled into Rolo's game
runtime (rolo_player), into licenses/ at the top of the repository.

    python3 tools/release/fetch_licenses.py

Run it when Axmol or another dependency is updated. Texts come from each
project's own repository at the version Rolo uses (or from the copy in
external/axmol when Axmol ships one), and the results are committed: builds,
installs and game exports copy licenses/ and never need the network.
The list of libraries comes from the runtime's build (its link line and the
third-party headers its sources include); see THIRD_PARTY_NOTICES.md.
"""
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
AX = ROOT / "external" / "axmol"
GH = "https://raw.githubusercontent.com/"

# name: (what it is, license, [sources]); a source is a URL or a path under ROOT.
LIBRARIES = {
    "rolo": ("Rolo Engine", "MIT", ["LICENSE"]),
    "axmol": ("Axmol Engine 2.11.5", "MIT", ["external/axmol/LICENSE"]),
    "inter-font": ("Inter font 4.0 (Rolo's default text font)", "SIL OFL 1.1", ["assets/fonts/Inter-LICENSE.txt"]),
    "lua": ("Lua 5.4.9 (gameplay scripts)", "MIT", ["third_party/lua/LICENSE"]),
    "nlohmann-json": ("nlohmann/json 3.12.0", "MIT", [GH + "nlohmann/json/v3.12.0/LICENSE.MIT"]),
    "stb": ("stb_image, stb_truetype", "MIT or public domain", [GH + "nothings/stb/master/LICENSE"]),
    "miniaudio": ("miniaudio 0.11.26 (sound)", "public domain (Unlicense) or MIT No Attribution",
                  ["third_party/miniaudio/LICENSE"]),
    "stb-vorbis": ("stb_vorbis 1.22 (OGG sounds)", "MIT or public domain",
                   ["footer:third_party/miniaudio/stb_vorbis.c"]),
    "box2d": ("Box2D 3.2.0 (physics)", "MIT", ["third_party/box2d/LICENSE"]),
    "astcenc": ("astc-encoder 5.3.0", "Apache-2.0", [GH + "ARM-software/astc-encoder/5.3.0/LICENSE.txt"]),
    "clipper2": ("Clipper2 2.0.1", "BSL-1.0", [GH + "AngusJohnson/Clipper2/Clipper2_2.0.1/LICENSE"]),
    "concurrentqueue": ("moodycamel::ConcurrentQueue 1.0.4", "BSL-1.0 or Simplified BSD",
                        [GH + "cameron314/concurrentqueue/v1.0.4/LICENSE.md"]),
    "convertutf": ("ConvertUTF (from LLVM)", "Apache-2.0 WITH LLVM-exception",
                   [GH + "llvm/llvm-project/main/llvm/LICENSE.TXT"]),
    "doctest": ("doctest 2.4.12", "MIT", [GH + "doctest/doctest/v2.4.12/LICENSE.txt"]),
    "fastlz": ("FastLZ", "MIT", ["external/axmol/3rdparty/fastlz/LICENSE.MIT"]),
    "fmt": ("{fmt} 11.2.0", "MIT", [GH + "fmtlib/fmt/11.2.0/LICENSE"]),
    "freetype": ("FreeType 2.14.1", "FreeType License (FTL)",
                 ["external/axmol/3rdparty/freetype/LICENSE.TXT", GH + "freetype/freetype/VER-2-14-1/docs/FTL.TXT"]),
    "glad": ("Glad 2.0.8", "MIT", [GH + "Dav1dde/glad/v2.0.8/LICENSE"]),
    "glfw": ("GLFW 3.4 (modified by Axmol), with SDL_GameControllerDB's gamepad layouts", "zlib",
             [GH + "glfw/glfw/3.4/LICENSE.md", "sdlmappings:external/axmol/3rdparty/glfw/src/mappings.h"]),
    "libjpeg-turbo": ("libjpeg-turbo 3.1.3", "IJG, BSD-3-Clause, zlib",
                      [GH + "libjpeg-turbo/libjpeg-turbo/3.1.3/LICENSE.md",
                       GH + "libjpeg-turbo/libjpeg-turbo/3.1.3/README.ijg"]),
    "mio": ("mio", "MIT", [GH + "vimpunk/mio/master/LICENSE"]),
    "openssl": ("OpenSSL 3.0.18", "Apache-2.0", [GH + "openssl/openssl/openssl-3.0.18/LICENSE.txt"]),
    "libpng": ("libpng 1.6.53", "PNG Reference Library License v2", ["external/axmol/3rdparty/png/LICENSE"]),
    "poly2tri": ("poly2tri", "BSD-3-Clause", [GH + "jhasse/poly2tri/master/LICENSE"]),
    "pugixml": ("pugixml 1.13", "MIT", ["external/axmol/3rdparty/pugixml/LICENSE.md"]),
    "rapidjson": ("RapidJSON", "MIT", [GH + "Tencent/rapidjson/master/license.txt"]),
    "robin-map": ("robin-map 1.4.0", "MIT", [GH + "Tessil/robin-map/v1.4.0/LICENSE"]),
    "simdjson": ("simdjson 3.10.1", "Apache-2.0", [GH + "simdjson/simdjson/v3.10.1/LICENSE"]),
    "minizip": ("minizip (unzip) 1.2", "zlib", ["header:external/axmol/3rdparty/unzip/unzip.h"]),
    "libwebp": ("libwebp 1.6.0", "BSD-3-Clause (Google)",
                [GH + "webmproject/libwebp/v1.6.0/COPYING", GH + "webmproject/libwebp/v1.6.0/PATENTS"]),
    "xsxml": ("xsxml 1.1", "MIT", ["header:external/axmol/3rdparty/xsxml/xsxml.hpp"]),
    "xxhash": ("xxHash 0.8.3", "BSD-2-Clause", [GH + "Cyan4973/xxHash/v0.8.3/LICENSE"]),
    "zlib": ("zlib 1.3.1", "zlib", [GH + "madler/zlib/v1.3.1/LICENSE"]),
}


def header_comment(path):
    """The license comment at the top of a source file."""
    text = path.read_text(errors="replace")
    start = text.find("/*")
    end = text.find("*/", start)
    return text[start:end + 2] if start >= 0 and end > start else ""


def footer_comment(path):
    """The license at the end of a single-file library (stb style)."""
    text = path.read_text(errors="replace")
    start = text.rfind("This software is available under 2 licenses")
    end = text.rfind("*/")
    return text[start:end].strip() if 0 <= start < end else ""


def sdl_mappings_notice(path):
    """The licence of the gamepad layouts GLFW copies from SDL_GameControllerDB
    (the // comment above the list in mappings.h)."""
    text = path.read_text(errors="replace")
    start = text.find("// All gamepad mappings not labeled GLFW")
    end = text.find("const char* _glfwDefaultMappings")
    if start < 0 or end <= start:
        return ""
    return "\n".join(line[3:] if line.startswith("// ") else line.lstrip("/") for line in text[start:end].strip().splitlines())


def fetch(source):
    if source.startswith("http"):
        with urllib.request.urlopen(source, timeout=30) as r:
            return r.read().decode("utf-8", errors="replace")
    if source.startswith("sdlmappings:"):
        return sdl_mappings_notice(ROOT / source[len("sdlmappings:"):])
    if source.startswith("footer:"):
        return footer_comment(ROOT / source[len("footer:"):])
    if source.startswith("header:"):
        return header_comment(ROOT / source[len("header:"):])
    return (ROOT / source).read_text(errors="replace")


def main():
    out = ROOT / "licenses"
    out.mkdir(exist_ok=True)
    index = ["# Licenses of software in games made with Rolo", "",
             "Every game exported by Rolo contains the programs and libraries below.",
             "Their license texts are in this folder; games carry a copy of it.", "",
             "| File | Software | License |", "|---|---|---|"]
    failed = []
    for name, (what, licence, sources) in LIBRARIES.items():
        parts = []
        for src in sources:
            try:
                text = fetch(src).strip()
            except Exception as e:   # report every problem, then fail
                failed.append(f"{name}: {src}: {e}")
                continue
            if not text:
                failed.append(f"{name}: {src}: empty")
            parts.append(text)
        (out / f"{name}.txt").write_text(f"{what}\nLicense: {licence}\n\n" + "\n\n".join(parts) + "\n")
        index.append(f"| `{name}.txt` | {what} | {licence} |")
    (out / "README.md").write_text("\n".join(index) + "\n")
    if failed:
        print("Could not get:\n  " + "\n  ".join(failed))
        sys.exit(1)
    print(f"{len(LIBRARIES)} licenses written to {out}")


if __name__ == "__main__":
    main()
