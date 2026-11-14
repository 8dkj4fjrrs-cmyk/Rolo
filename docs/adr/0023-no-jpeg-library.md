# ADR 0023: No JPEG library: Rolo decodes JPEG files itself

- **Status:** Accepted
- **Date:** 2026-11-14
- **Follows:** ADR 0019 ("truly MIT": when a library is not MIT, use an MIT one or write our own)

## Context

JPEG images were decoded by Axmol through a JPEG library: libjpeg-turbo on
desktops (IJG, BSD-3-Clause and zlib licenses) and, in games for the web,
the Independent JPEG Group's libjpeg from Emscripten's ports (IJG
license). Both are permissive, but neither is MIT, and each added a
license text to every game.

Rolo already contains a JPEG decoder: stb_image (MIT or public domain),
compiled into `rolo_assets` to check image files. It reads baseline and
progressive JPEGs, colour and greyscale.

## Decision

- Axmol is built **without any JPEG library**: `AX_WITH_JPEG=OFF` and
  `AX_USE_JPEG=0`, and on the web a small patch stops it asking
  Emscripten for its libjpeg port (`cmake/axmol-patches/0003`).
- **Rolo decodes JPEG files** (`rolo/assets/ImageDecode.hpp`:
  `decodeImageFile`), with stb_image, and hands the pixels to the
  renderer (`TextureStore`). PNG files are still decoded by Axmol
  (libpng).
- stb_image was preferred to writing a decoder from nothing: it is MIT (or
  public domain), already in every game, widely used, and tested against
  damaged files; a new decoder would be a place for new bugs with no
  gain in freedom.

## Consequences

- `licenses/libjpeg-turbo.txt` and `licenses/libjpeg.txt` are gone from
  Rolo and from games; the web runtime is about 150 KB smaller.
- What stb_image does not read is not supported: JPEGs with arithmetic
  coding or 12-bit samples. These are rare (print work, medical images);
  ordinary photos and exported art are fine. A file that cannot be decoded
  is reported in the log and drawn as nothing.
- Decoding large JPEGs is somewhat slower than with libjpeg-turbo (no
  SIMD on every platform). Game art is mostly PNG; JPEGs are usually
  backgrounds, loaded once per scene.
- Axmol can no longer save JPEG files; Rolo never did (screenshots are
  PNG).
