# ADR 0024: No libpng and no OpenSSL

- **Status:** Accepted
- **Date:** 2026-11-14
- **Follows:** ADR 0019 ("truly MIT"), ADR 0012 (no yasio), ADR 0023 (no JPEG library)

## Context

After ADR 0023, two libraries that are not MIT were still in every game
through Axmol, each for very little:

- **libpng** (PNG Reference Library License) decoded PNG images and wrote
  screenshots. Rolo already contains a PNG decoder: stb_image (MIT or
  public domain), which has decoded JPEGs for the renderer since ADR 0023.
- **OpenSSL** (Apache-2.0), about 3 MB of it in a desktop game and two
  DLLs beside every Windows game. Axmol's core mentions it in two places:
  MD5 and other digests of files and text (`utils::computeDigest`), and
  optional encryption of its settings file (`UserDefault`). Rolo uses
  neither. With networking gone (ADR 0012), nothing else needed it.

## Decision

**libpng**

- Axmol is built without it: `AX_WITH_PNG=OFF` and `AX_USE_PNG=0`
  (`cmake/axmol-patches/0004` adds the switch).
- **Rolo decodes every image file** (`rolo/assets/ImageDecode.hpp`), PNG
  as well as JPEG, and hands the pixels to the renderer (`TextureStore`).
  For images with transparency, Rolo multiplies the colours by alpha
  first (`premultiplyAlpha`), with the same sum Axmol used, so pictures
  blend as they did.
- **Rolo writes screenshots itself** (`rolo/assets/ImageEncode.hpp`), with
  stb_image_write (MIT or public domain), compiled inside `rolo_assets`
  like stb_image.
- FreeType is built without PNG-compressed glyph bitmaps
  (`FT_DISABLE_PNG`), its only use of libpng (colour emoji fonts, which
  Rolo does not draw).

**OpenSSL**

- Axmol is built without it: `AX_WITH_OPENSSL=OFF` (the same patch).
- `engine/runtime/axmol_compat/include/openssl/` holds **Rolo's stand-in
  headers** for the few names Axmol's core mentions, so that Axmol's
  sources compile unedited, as for yasio (ADR 0012). They are Rolo's own
  code and contain no cryptography:
  - digests: no algorithm is known, so `computeDigest` and the MD5
    helpers return an empty text (Axmol's own answer for an unknown
    algorithm);
  - encryption: asking for it stops the program with a message. Writing
    an "encrypted" file as plain text would be worse than stopping.

stb's libraries were preferred to writing a PNG decoder and encoder from
nothing, as in ADR 0023: they are MIT, small, widely used and tested
against damaged files. Cryptography was not rewritten at all: Rolo has no
use for it, and home-made cryptography is a known way to get it wrong.

## Consequences

- `licenses/libpng.txt` and `licenses/openssl.txt` are gone from Rolo and
  from games. Windows games no longer carry `libcrypto-3-x64.dll` and
  `libssl-3-x64.dll`.
- Games are smaller: measured on Linux (GCC 13.3, Release), the
  game program went from 17.8 MB to 13.1 MB (15.2 MB to 11.1 MB without
  debug symbols), nearly all of it OpenSSL. The web runtime went from
  5.21 MB to 5.07 MB (2.09 MB to 2.04 MB as servers send it); it had
  little of OpenSSL in it to begin with. Windows and macOS were not
  measured here.
- **Greyscale PNG files are now drawn correctly.** Axmol's libpng path
  handed them to the renderer as one- or two-channel textures, which came
  out tinted and see-through (grey as red, 16-bit grey as transparent).
  Every PNG kind now comes out as its picture: colour, grey, palette,
  8 or 16 bits, interlaced, with or without transparency
  (`tests/data/png_project`, test `player.draws_png_images`). Colour
  PNGs are drawn exactly as before, pixel for pixel.
- Textures from PNGs without transparency use 4 bytes per pixel in
  graphics memory where libpng's path used 3. On most graphics cards
  3-byte textures are padded to 4 anyway.
- Decoding PNGs may be somewhat slower than libpng on very large images
  (no SIMD). It happens once per image, when a scene first shows it.
- Screenshot files are a little larger than libpng's (a simpler
  compressor); the pictures are identical.
- Axmol's digest helpers and settings encryption do not work in Rolo.
  Code in `engine/runtime` must not call them; nothing does.
- Updating Axmol: check that patch 0004 applies, and that its core has
  not started using more of OpenSSL (a compile error naming an
  `openssl/` header or function says so; see `axmol_compat/README.md`).
- **Still not MIT in every game after this:** FreeType (FreeType
  License), and smaller permissive ones listed in
  `THIRD_PARTY_NOTICES.md` (zlib, libwebp, xxHash, Clipper2, astcenc,
  simdjson, poly2tri, ConvertUTF). FreeType went next
  ([ADR 0025](0025-no-freetype.md)): game text is drawn by Rolo's own
  font renderer.
