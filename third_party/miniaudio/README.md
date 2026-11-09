# miniaudio 0.11.26 and stb_vorbis 1.22

Plays sounds in Rolo games and in the editor (`engine/audio`, ADR 0015).

- **miniaudio** — https://github.com/mackron/miniaudio, tag `0.11.26`
  (commit `877ea4a7c2601c7b0f2a44e83dfac42e6470fea6`, 2026-10-31).
  `miniaudio.h` is copied unmodified. License: public domain (Unlicense) or
  MIT No Attribution, your choice; see [LICENSE](LICENSE).
- **stb_vorbis** — the copy miniaudio ships for Ogg Vorbis decoding,
  `extras/stb_vorbis.c` from the same tag, unmodified. License: MIT or public
  domain, your choice (at the end of the file).
- `miniaudio.c` is Rolo's: it compiles the two together in the order
  miniaudio's documentation gives.

Built by `CMakeLists.txt` as the static library `rolo_miniaudio`, with
encoding and tone generation switched off.

To update: copy `miniaudio.h`, `LICENSE` and `extras/stb_vorbis.c` from a
newer release, and update this file, `licenses/`
(`tools/release/fetch_licenses.py`) and `THIRD_PARTY_NOTICES.md`.
