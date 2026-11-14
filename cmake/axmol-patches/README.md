# Rolo's patches to Axmol

Rolo uses Axmol 2.11.5 at a pinned commit (`cmake/RoloAxmol.cmake`).
The patches in this folder are applied to `external/axmol`:
- after it is downloaded;
- in order of file name;
- each only once (a patch that can already be reversed is in place).

| Patch | What it changes | Why |
|---|---|---|
| `0001-optional-network-and-yasio.patch` | Adds `AX_ENABLE_NETWORK` (core) and `AX_WITH_YASIO` (3rdparty) switches, both on by default. In `ParticleSystem.cpp`, logs a mask's four-letter code as text instead of through `htonl()`. | Rolo builds without networking and yasio ([ADR 0012](../../docs/adr/0012-no-yasio.md)). `htonl()` only compiled because the network console's socket headers were included. |
| `0002-keyword-link-delayimp.patch` | `ax_add_delay_load_options` links `delayimp` with `PRIVATE`. | On Windows, Axmol's app setup linked it in CMake's old "plain" form, and CMake refuses to mix that with the keyword form (`PRIVATE`) that Rolo uses for the editor and player. |
| `0003-optional-jpeg-on-wasm.patch` | For WebAssembly, asks Emscripten for its libjpeg port only when `AX_WITH_JPEG` is on (it was always asked for). | Rolo builds Axmol without a JPEG library ([ADR 0023](../../docs/adr/0023-no-jpeg-library.md)); on other systems `AX_WITH_JPEG=OFF` already left it out. |

Rules for patches:
- Keep them **small**: add switches or fix build problems. Do not change
  Axmol's behaviour. Rolo's own features belong in Rolo.
- Make each one with `git diff` inside `external/axmol`, from Axmol's
  pinned commit.
- Say why in this table.
- The files must have Unix line endings. Axmol is cloned with
  `core.autocrlf=false` so that the patches apply on Windows too.

To redo the patches after changing them, delete `external/axmol` and
configure again. Otherwise an edited patch fails to apply over the old one.
