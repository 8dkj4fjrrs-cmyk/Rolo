# Box2D 3.2.0

2D rigid-body physics for Rolo games (`engine/physics`, ADR 0016).

- **Source:** https://github.com/erincatto/box2d, tag `v3.2.0`
  (commit `178495ba35695aefe9c321e569d2277f06db3b90`, 2026-10-18).
- **License:** MIT, see [LICENSE](LICENSE). Every source file carries
  `SPDX-License-Identifier: MIT`.
- **Unmodified.** Only the library is copied: `include/box2d/` and `src/`
  (its `.c`, `.h` and `.inl` files). Left out: the samples, benchmarks, unit
  tests, documentation and the third-party code they use (`extern/`,
  `shared/`), none of which the library needs.

Built by `CMakeLists.txt` as the static library `rolo_box2d`, with the same
compiler settings as Box2D's own build file.

To update: copy `include/`, `src/` and `LICENSE` from a newer release, and
update this file, `licenses/box2d.txt` (`tools/release/fetch_licenses.py`)
and `THIRD_PARTY_NOTICES.md`.
