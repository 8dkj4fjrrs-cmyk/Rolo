# ADR 0028: Android

- **Status:** Proposed (the plan and what was found; nothing is built yet)
- **Date:** 2026-11-14
- **Follows:** ADR 0019 (direction: desktop, web and mobile), ADR 0022 (web export), ADR 0026 (touch), ADR 0027 (screen sizes)

## What was asked for

Decided by the owner on 2026-11-14:

- **Google Play from the start**, not only an installable file.
- **Export through Google's tools, as Godot and Cocos Creator do**
  (changed the same evening, after comparing engines: first chosen was
  "one click, nothing to install", as Defold does). The maker installs
  Android Studio (which brings the Android SDK, NDK and a JDK) once and
  tells Rolo where it is; Rolo writes the Android project and runs
  Google's build for them. Signing is done by Google's tools.
- There is **no Android device** to try things on. Android Studio's
  emulator, which the maker now has anyway, is the way to try games.

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
2. **Export from the editor.** Rolo's settings get the place of the
   Android SDK (found by itself where Android Studio puts it). **Export
   Game… › Android** asks for the app's name, package name, version,
   icon and orientation, writes the Gradle project of step 1 with the
   game pack inside into a build folder, and runs Gradle: an `.apk` to
   try (signed with a debug key Google's tools make) and an `.aab` for
   Google Play (signed with the maker's upload key, which Rolo helps
   make with the JDK's `keytool` and never stores the password of).
   Rolo checks the tools first and says in plain words what is missing
   or the wrong version: wrong SDK and NDK versions are where beginners
   get stuck in every engine that works this way.
3. **Google Play.** A guide from account to release, the upload key kept
   safely (losing it locks the maker out of updating the game), version
   codes, the store's forms, and what the pre-launch report said.
4. **Polish**, as far as an emulator and reports allow: sound, rumble
   (vibration), the notch and rounded corners, performance.

## Alternatives considered

- **One click with nothing to install** (Defold's way): Rolo would pack
  and sign the app itself. Friendlier, but Rolo would need cryptography
  code for signing (it has none since ADR 0024) and its own writers for
  Google's bundle formats, and would have to follow every change Google
  makes to them. First chosen, then set aside by the owner in favour of
  the usual way.
- **A build service in the cloud** (GDevelop's way): somebody has to run
  and pay for the servers.

## Risks to be honest about

- With no device and no local builds, progress is slower and blinder than
  anything so far in Rolo. Bugs that only real phones show will be found
  by players or by Google's report, not before.
- Exporting depends on several GB of Google's tools on the maker's
  computer, in versions Rolo must keep up with. In return Rolo needs no
  cryptography of its own, and follows Google's changes by changing
  version numbers.
- What Rolo's download must carry for Android is its sources for the
  player and Axmol's Android parts (built on the maker's computer by the
  NDK), or prebuilt libraries per phone type to save that build; to be
  settled in step 2.
- Google changes Play's requirements about once a year; a Rolo release
  can fall behind them.
