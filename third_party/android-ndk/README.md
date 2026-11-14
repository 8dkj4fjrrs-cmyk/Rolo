# Android NDK: cpufeatures

Nothing of the Android NDK is kept in this repository except this notice.

`cpufeatures` is a small library in the NDK (`sources/android/cpufeatures`)
that tells a program what the processor can do. Axmol's maths code uses it,
so it is compiled into Rolo's Android player from the NDK the app is built
with (`cmake/RoloAxmol.cmake`; ADR 0028). `cpufeatures-LICENSE.txt` is the
license notice at the top of its source file, from NDK r27d (27.3.13750724),
kept here so that `tools/release/fetch_licenses.py` can put it into
`licenses/` without an NDK at hand.
