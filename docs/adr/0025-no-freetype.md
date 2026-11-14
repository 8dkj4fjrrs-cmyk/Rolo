# ADR 0025: No FreeType: Rolo draws game text itself

- **Status:** Accepted
- **Date:** 2026-11-14
- **Follows:** ADR 0019 ("truly MIT"), ADR 0024 (no libpng, no OpenSSL), ADR 0004 (Rolo's UI and its font code)

## Context

Game text (the Text component, button pictures' labels, the frame-rate
line) was drawn by Axmol's labels, which use FreeType (FreeType License:
permissive, not MIT, and its text must go with every game). FreeType was
the last large library in games that Rolo itself did not need: the
editor's whole interface is already drawn with stb_truetype (MIT or
public domain) through `rolo_ui`.

What FreeType has that stb_truetype has not is **hinting**: nudging
letter shapes onto the pixel grid at small sizes. Rolo did not benefit
from it: game text was drawn at twice its size and scaled down (to stay
sharp when the camera zooms and on Retina screens), which washes hinting
out. Screenshots of both renderers at 10, 12, 16 and 24 px, on an
ordinary and on a Retina-like screen, were compared before deciding; they
were close to indistinguishable, and the owner chose Rolo's own.

## Decision

- **Rolo draws game text with its own renderer**:
  `rolo/ui/TextImage.hpp` (`drawTextImage`, `measureTextLine`) draws a
  block of text into one picture with stb_truetype: several lines,
  wrapping at spaces (and between Chinese, Japanese and Korean
  characters), left, centre and right alignment, kerning. Letters are
  placed at fractions of a pixel across and on whole pixels down. No
  hinting.
- The runtime turns such pictures into sprites
  (`engine/runtime/src/TextSprites.hpp`), still made at twice the shown
  size. A Text is one picture (lines with button pictures: one per run of
  words). Axmol's labels are not used anywhere.
- **Axmol is built without FreeType**: `AX_WITH_FREETYPE=OFF`
  (`cmake/axmol-patches/0005`). Axmol's `FontFreeType.cpp` is left out;
  `engine/runtime/axmol_compat/src/FontFreeTypeNone.cpp` provides that
  class's functions (no font can be made), so the rest of Axmol's core
  links unedited. On Linux, the patch also switches off Axmol's
  system-font text (`Device::getTextureDataForText`), which used
  FreeType directly.
- The built-in font is no longer written to a temporary file for Axmol
  to load: it is read from memory.

## Consequences

- `licenses/freetype.txt` is gone from Rolo and from games.
- Games are smaller: measured on Linux (GCC 13.3, Release), the
  game program went from 13.1 MB to 12.0 MB; the web runtime from
  5.07 MB to 4.40 MB (2.04 MB to 1.75 MB as servers send it). With
  ADR 0024, that is 17.8 MB to 12.0 MB on Linux and 5.21 MB to 4.40 MB
  on the web. Windows and macOS were not measured
  here.
- Text looks very slightly different (no hinting; spacing differs by
  fractions of a pixel). Positions and wrapping match the old renderer to
  within a pixel or two in Rolo's own games and templates (a long line
  can come out a few pixels wider or narrower, so a line that only just
  fitted may wrap differently).
- Drawing a text's picture is fast (measured, Release, on the cloud
  Linux machine: 0.03 ms for a 12-letter line at 44 px, 0.27 ms for a
  114-letter wrapped paragraph), so text that changes every frame (a
  score, a timer) is fine. Each changed text makes a new picture; there
  is no shared glyph atlas for game text yet.
- **Not supported**, as before or newly:
  - colour emoji, and fonts whose letters are bitmaps;
  - writing that needs shaping or runs right to left (Arabic, Hebrew,
    Indic scripts): letters come out unjoined and in file order. Axmol's
    labels did not shape either;
  - a second font for letters the chosen font lacks: a missing letter
    shows the font's "missing" box;
  - outlines, shadows and glow on text (Axmol's label effects; Rolo
    never exposed them).
- Axmol's `Label::createWithTTF` and system-font labels give nothing in
  Rolo. Code in `engine/runtime` must use `TextSprites.hpp`.
- Updating Axmol: check that patch 0005 applies, and that
  `FontFreeTypeNone.cpp` still provides every function of `FontFreeType`
  that Axmol's core calls (a link error names any that is missing).
- **Still not MIT in every game after this:** only small permissive
  libraries Axmol links (zlib, libwebp, xxHash, Clipper2, astcenc,
  simdjson, poly2tri, ConvertUTF, GLFW), listed in
  `THIRD_PARTY_NOTICES.md`, and in web games the compiler's runtime
  (ADR 0022).
