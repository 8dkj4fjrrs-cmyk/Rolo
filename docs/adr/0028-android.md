# ADR 0028: Android

- **Status:** Proposed (the plan and what was found; nothing is built yet)
- **Date:** 2026-11-14
- **Follows:** ADR 0019 (direction: desktop, web and mobile), ADR 0022 (web export), ADR 0026 (touch), ADR 0027 (screen sizes)

## What was asked for

Decided by the owner on 2026-11-14:

- **Google Play from the start**, not only an installable file.
- **One click in Rolo**: Rolo ships a ready-made Android runtime and packs
  the game into it, as it does for the web. Nobody using Rolo installs
  Android Studio.
- There is **no Android device** to try things on.

## What was found

- **Axmol runs on Android** (OpenGL ES, a Java activity that drives the
  frames, touch, the app's files read through Android's asset manager).
  Its Android code needs nothing Rolo removed: the only such include is
  one yasio string header, which Rolo's stand-in already has. Its build
  targets Android 16 (API 36) with an NDK that supports 16 KB pages, both
  of which Google Play requires of new apps.
- Rolo's own pieces are ready for it: touch controls (ADR 0026), any
  screen shape and upright games (ADR 0027), the game as one pack file
  (ADR 0022), a run loop that returns while the game goes on (web), no
  threads needed. miniaudio and Box2D support Android.
- **Android cannot be built in the workspace Rolo is developed in**: the
  downloads it needs (Google's Android SDK and build plugin, Maven
  Central, Gradle) are not reachable from there. GitHub's build machines
  have the Android SDK and NDK installed, so every Android build and test
  has to run there (about 15 to 25 minutes a try), and the first working
  build will take a number of tries.
- **Testing** will be an Android emulator on GitHub's machines: it shows
  that the game starts, draws and reacts to touches. It says little about
  speed, sound, or how real phones' graphics chips behave. Google Play's
  own "pre-launch report" runs an uploaded app on real phones and reports
  crashes and screenshots; with no device, that is the first real-hardware
  check there will be.

## What Google Play requires (as of this writing; to be checked again when uploading)

- An **Android App Bundle** (`.aab`), not an APK, for new apps.
- 64-bit code (arm64-v8a), a recent target API level, 16 KB page sizes.
- The bundle **signed** with the maker's own "upload key"; Google signs
  what players get (Play App Signing).
- A package name that is the game's for ever (`com.yourname.yourgame`),
  a version code that goes up with each upload, icons, and a store
  listing.
- A Google Play developer account (a one-off fee, paid by the maker) and,
  for new personal accounts, a period of testing with a group of testers
  before the game can go public.

## Plan

1. **The player runs on Android.** A Gradle project in `player/android`
   (from Axmol's template) builds Rolo's player as an Android app with
   one game inside. An experimental GitHub workflow builds it and starts
   it in an emulator: the game's first scene is drawn, a touch on the
   on-screen controls is seen, a screenshot is kept. Needs: the player's
   start-up on Android (the activity calls in, as the browser does), the
   game pack unpacked to the app's own folder on first start (Rolo reads
   games from real folders), saves in the app's folder, pausing with the
   app, the back button as Escape.
2. **One-click export.** Rolo's download carries the Android runtime
   built in step 1 (as it carries the web runtime). Exporting puts the
   game pack in, and sets the app's name, package name, version, icon and
   orientation, **without Google's tools**: an `.aab` (and an `.apk` for
   trying) is a zip of known formats. The hard part is **signing**: it
   needs RSA keys, SHA-256 and certificates, and Rolo has no cryptography
   library since ADR 0024. To be decided then, with the owner: write the
   small, well-specified pieces needed (signing only, checked against
   Google's own verifier on every build), or ask the maker to have Java
   installed for this one step (its `keytool` and `jarsigner` do it).
3. **Google Play.** A guide from account to release, the upload key kept
   safely (losing it locks the maker out of updating the game), version
   codes, the store's forms, and what the pre-launch report said.
4. **Polish**, as far as an emulator and reports allow: sound, rumble
   (vibration), the notch and rounded corners, performance.

## Risks to be honest about

- With no device and no local builds, progress is slower and blinder than
  anything so far in Rolo. Bugs that only real phones show will be found
  by players or by Google's report, not before.
- Step 2's signing is the first place where the "nobody installs Google's
  tools" wish and the "no home-made cryptography" caution pull against
  each other.
- Google changes Play's requirements about once a year; a Rolo release
  can fall behind them.
