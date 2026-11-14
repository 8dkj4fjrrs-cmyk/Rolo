# Axmol compatibility headers (`axmol_compat`)

Headers Axmol's core includes from libraries Rolo leaves out: yasio (most
of this page) and OpenSSL (near the end).

Rolo builds Axmol without yasio (see
[ADR 0012](../../../docs/adr/0012-no-yasio.md)). Axmol's core still includes a
few yasio headers outside its networking code: a growable buffer, binary
streams for its settings file, clocks and string helpers. This folder
provides those headers.

- **It is Rolo's own code** (MIT), written from scratch for Rolo. It
  contains no yasio code.
- **It keeps yasio's file and type names** (`yasio/byte_buffer.hpp`,
  `yasio::byte_buffer`, `cxx20::starts_with`, ...), so Axmol's sources compile
  without being edited. Only what Axmol's core uses is provided, nothing more.
- **Behaviour Axmol relies on is kept**, and tested in
  `tests/runtime/test_axmol_compat.cpp`:
  - buffers use `malloc`/`free`, because Axmol hands their memory to code that
    calls `free()`;
  - `resize()` leaves new elements uninitialised;
  - `pod_vector<T> v{n}` means *n* elements;
  - the settings-file byte layout (big-endian numbers, 7-bit encoded string
    lengths), so settings saved by other Axmol games still load;
  - `insert_sorted` keeps equal elements in insertion order (the scheduler
    depends on it).

| Header | Used by Axmol for |
|---|---|
| `byte_buffer.hpp`, `pod_vector.hpp` | file data, images, vertices, the scheduler (`axstd::byte_buffer`, `axstd::pod_vector`) |
| `ibstream.hpp`, `obstream.hpp`, `byte_order.hpp` | `UserDefault`'s file; the Metal shader loader |
| `string_view.hpp` | path prefix checks |
| `utils.hpp` | log time stamps, timing |
| `thread_name.hpp` | naming the job system's threads |
| `ref_ptr.hpp`, `object_pool.hpp` | `SimpleTimer` |
| `sz.hpp` | an alignment macro |
| `wtimer_hres.hpp` | the 1 ms timer on Windows |

`byte_order.hpp` is the only header with a name yasio does not have.

## OpenSSL (`include/openssl/`)

Rolo also builds Axmol without OpenSSL
([ADR 0024](../../../docs/adr/0024-no-libpng-no-openssl.md)). Axmol's core
mentions it in two places, neither used by Rolo:

| Header | Axmol uses it for | Rolo's stand-in |
|---|---|---|
| `evp.h` | `utils::computeDigest` and the MD5 helpers | knows no digest: Axmol returns an empty text |
| `aes.h` | optional encryption of `UserDefault`'s file | stops the program with a message if encryption is ever asked for |
| `modes.h`, `rc4.h` | included, nothing used | empty |

These are Rolo's own code and contain **no cryptography**. Never copy
OpenSSL code here, and do not write ciphers or digests here either: if
Rolo ever needs them, that is a new decision (ADR), not a compat header.

## FreeType (`src/FontFreeTypeNone.cpp`)

Rolo builds Axmol without FreeType too
([ADR 0025](../../../docs/adr/0025-no-freetype.md)). Axmol's
`2d/FontFreeType.cpp` is left out of the build; this file provides that
class's functions, each doing nothing (no font can be made), so the rest of
Axmol's core links unedited. It contains no FreeType code. Rolo draws
text itself (`engine/runtime/src/TextSprites.hpp`).

## How it is wired in

`cmake/RoloAxmol.cmake` does three things:
1. it deletes `external/axmol/3rdparty/yasio`;
2. it sets `AX_WITH_YASIO`, `AX_ENABLE_NETWORK` and `AX_ENABLE_CONSOLE`
   off;
3. it links the `rolo_axmol_compat` interface target into Axmol's
   `3rdparty` target.

The patch that adds those two switches to Axmol is in
[`cmake/axmol-patches/`](../../../cmake/axmol-patches/README.md).

## When Axmol is updated

Build it. A compile error naming a `yasio/` header or a `yasio::`, `cxx17::`
or `cxx20::` name means Axmol's core started using something new. Add it
here, written from the standard library or from how Axmol uses it, never
copied from yasio. Then add a test for any behaviour Axmol depends on.
