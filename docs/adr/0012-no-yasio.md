# ADR 0012: Build Axmol without networking, and replace yasio

- **Status:** Accepted
- **Date:** 2026-11-03
- **Follows:** ADR 0011 (which kept yasio and disclosed its terms)

## Context

Axmol's core includes yasio, a networking library. Upstream, yasio's license
adds the "Anti 996" License 1.0 to MIT. That license:
- must be displayed with every copy;
- requires following labour laws;
- is not an OSI-approved license.

For the alpha, ADR 0011 kept yasio and disclosed it, because removing it
meant changing Axmol. That still put a non-standard license into every game
made with Rolo, which the project owner does not want.

Rolo games never use the network. Axmol builds networking code into its
core regardless:
- the HTTP client, WebSocket and downloader (with curl, llhttp and
  websocket-parser);
- a remote debug console (built on yasio's sockets).

Looking at what Axmol's core includes from yasio *outside* networking found
only a small set:
- a growable buffer (`byte_buffer` / `pod_vector`);
- big-endian binary streams for the `UserDefault` settings file;
- string prefix checks, clocks, naming a thread, a reference-counting
  pointer and a Windows timer helper.

## Decision

1. **Switch networking off.**
   - A small patch (`cmake/axmol-patches/0001`) adds `AX_ENABLE_NETWORK` and
     `AX_WITH_YASIO` switches to Axmol. Rolo sets them off, with
     `AX_ENABLE_CONSOLE`, `AX_ENABLE_HTTP`, `AX_ENABLE_WEBSOCKET` and the
     curl, llhttp and websocket-parser switches.
   - The same patch removes the one `htonl()` call that only compiled through
     the console's socket headers. That build error is what had kept the
     console on.
2. **Replace the rest of yasio with Rolo's own headers**
   (`engine/runtime/axmol_compat/`, MIT).
   - They are written from scratch, from the standard library and from how
     Axmol uses each function.
   - They keep yasio's header and type names, so Axmol's ~50 files that
     include them need no edits.
   - Unit tests pin the behaviour Axmol relies on, including the exact bytes
     of the settings file.
3. **Delete `3rdparty/yasio`** from Rolo's copy of Axmol at configure
   time, so nothing can build or include it by accident.
4. **Patches are a mechanism, not a habit.** `cmake/axmol-patches/*.patch`
   are applied once, in order, after download. The rules are in that
   folder's README:
   - switches and build fixes only;
   - Axmol is cloned with Unix line endings so they apply on Windows.

Considered and rejected:
- **Keep yasio and disclose it** (ADR 0011). Every game still carries the
  terms.
- **Fork Axmol.** A fork has to be maintained, and every Axmol update becomes
  a merge. Three changed lines and a header folder do the same job.
- **Edit Axmol's sources to use different names** (for example
  `rolo::byte_buffer`). That would mean a large patch over ~50 files to
  maintain on every Axmol update, for no difference in behaviour.
- **Copy yasio's needed headers.** They are under the same license, so
  nothing would be gained.

## Consequences

- Games no longer contain yasio, curl, llhttp or websocket-parser. Their
  license texts leave `licenses/` and the notices.
  - OpenSSL stayed at first: Axmol's core mentions its digests and AES
    (`utils`, `UserDefault`). [ADR 0024](0024-no-libpng-no-openssl.md)
    removed it the same way as yasio.
- Rolo games cannot use the network. Nothing in Rolo did; a future
  online feature would need a new decision (and probably a different
  library).
- Updating Axmol now also means:
  - checking that the patch still applies;
  - adding to `axmol_compat` anything new that Axmol's core takes from yasio.

  `engine/runtime/axmol_compat/README.md` says how.
- The names `yasio/...` remain in Rolo's source tree as header names.
  The READMEs and every header say the code is Rolo's own.
- Tested on Linux only so far (like everything in ADR 0011). The Windows
  and macOS parts of the replacement (`thread_name.hpp`, `wtimer_hres.hpp`,
  the Metal shader loader's use of `fast_ibstream_view`) are first compiled
  by CI.
