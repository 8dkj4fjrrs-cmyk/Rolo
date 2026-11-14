# Stand-ins for classes Axmol's Java code mentions

Axmol's Android Java code (`external/axmol/core/platform/android/java/src`,
MIT) is compiled into the app as it is, except for its video player. It
mentions four classes that Rolo leaves out; these are Rolo's own stand-ins
for them, written for Rolo, so that the rest compiles. None of them contains
code from the originals. (The same idea as `engine/runtime/axmol_compat` for
C++; ADR 0012, ADR 0028.)

| Stand-in | Stands in for | Why the original is left out |
|---|---|---|
| `dev.axmol.lib.AxmolMediaEngine` | Axmol's video player | It needs Google's media libraries (androidx.media3, Guava; Apache 2.0). Rolo has no video (`AX_ENABLE_MEDIA` is off). |
| `com.jakewharton.processphoenix.ProcessPhoenix` | ProcessPhoenix (Apache 2.0), which restarts an app | Axmol only calls it with a setting Rolo does not use (`AX_ENABLE_RESTART_APPLICATION_ON_CONTEXT_LOST`). |
| `com.android.vending.expansion.zipfile.APKExpansionSupport`, `ZipResourceFile` | Google's reader for "expansion files" (Apache 2.0), an old way to ship large games | Rolo games are inside the app. |

With these, everything in a Rolo Android app is MIT-licensed or has its
license text in `licenses/`, as on the other systems.
