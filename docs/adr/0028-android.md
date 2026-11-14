# ADR 0028: Android

- **Status:** Accepted. Step 1 (the player runs on Android) is done; steps 2 to 4 are not started
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

## Step 1, as built (2026-11-14)

The player builds as an Android app with one game inside, and is built
and played in an emulator by a separate, experimental GitHub workflow
(`.github/workflows/android.yml`). Nothing of it has run on a real phone.

- **The Gradle project** is `player/android` (one module, `app`). Gradle
  runs Rolo's CMake build with the Android NDK (the player is a library
  there, `librolo_player.so`), compiles the Java side, and packs both with
  the app's assets: Axmol's shaders, the game as one game pack
  (`game.pack`) and the license texts. Versions: Android Gradle Plugin
  9.3.2 and Gradle 9.7.1 (those of Axmol 2.11.5's own Android projects),
  NDK 27.3.13750724 (r27d), CMake 3.31.5, built for Android 16 (API 36),
  running on Android 7.0 (API 24) and newer.
- **The Java side** is Axmol's (`AxmolActivity` and its helpers, MIT),
  compiled as it is but without its video player, plus Rolo's small
  `RoloActivity`. Four classes Axmol's code mentions are Rolo's own
  stand-ins (`player/android/app/src/axmol_compat`): the video player
  (which would bring Google's media libraries) and three helpers under
  the Apache licence that Rolo has no use for. The app asks for **no
  permissions**.
- **Start-up.** Android makes the screen and calls into the library;
  there is no `main()`. The player says what starting means
  (`rolo::android::setStart`, `rolo/runtime/Android.hpp`), and
  `Application::run()` sets the game up on the screen that is there and
  returns while the game goes on, as in a web browser.
- **The game's files.** The pack is copied out of the app and unpacked
  into the app's own folder on the first start after each install or
  update (a note beside it names the installation it came from). Saved
  games go to a `saves` folder beside it.
- **Touch** comes through Axmol to the same on-screen controls as on the
  web. **Back** arrives as Escape (and does not count as "a keyboard was
  used", so the on-screen controls stay). **In the background** no frames
  are made and the sound output rests (`AudioEngine::setSuspended`); the
  game goes on where it was. When the game quits, the app's process ends.
- **16 KB pages**: the libraries are laid out for them, and the workflow
  checks it.
- **Logging** goes to Android's log (logcat) under the tag `Rolo`.

What the emulator test showed (Android 14, x86_64, a 2400 × 1080 screen
drawn without a graphics card): the Platformer Starter starts, its first
scene is drawn filling the long screen (1600 × 720 points of the world),
the on-screen stick and buttons work, Back opens and closes the pause
menu, Home and coming back pause and continue the game, Quit from the
pause menu ends the app, and a second start does not unpack again.

Found on the way:
- Axmol's maths code needs the NDK's small `cpufeatures` library, which
  its own build only reaches by way of libpng. Rolo, built without libpng
  (ADR 0024), links it directly (`cmake/RoloAxmol.cmake`). It is one more
  piece inside Android games (BSD licence; `licenses/android-cpufeatures.txt`).
- `char` is unsigned on ARM unless asked otherwise; everywhere else Rolo
  runs it is signed. The Android build asks for signed (`-fsigned-char`),
  as Axmol's does, so that the emulator (x86_64) and phones (arm64) behave alike.
- The first time an app fills the screen, Android covers it with a note
  about leaving full screen that swallows touches. Players tap it away; the
  test tells the emulator it was seen.
- Android 16 no longer sends Back as a key to apps built for it unless
  they ask (`enableOnBackInvokedCallback="false"` in the manifest).

Not done in step 1, and where it belongs:
- **Gamepads and rumble** on Android (step 4). Only a controller's
  direction buttons arrive, as arrow keys.
- **Keyboards** on Android (Chromebooks, tablets): Axmol's Java side only
  passes Back, Menu, Enter and the direction keys.
- **Notches and rounded corners** (step 4): the game fills the whole
  screen and nothing is kept clear of them.
- **A lost graphics context** (Android may take it from an app in the
  background): Axmol reloads the textures it loaded from files; Rolo
  makes its textures itself, so this needs its own handling and a way to
  test it (step 4).
- **The game exists twice on the device** (packed in the app, and
  unpacked). Reading straight from the pack would halve that.
- **Backing up saved games** to the player's Google account is off
  (`allowBackup="false"`); to be decided with step 3.
- **C++ game modules** are not built in (as on the web): games are scripts
  and data.
- The workflow's app is a **debug build**: not optimised (about 28 MB of
  library per kind of processor), signed with a throwaway debug key, an
  `.apk`. Release builds, the `.aab` and signing are step 2. In the
  emulator, drawn without a graphics card, it made roughly ten frames a
  second, which says nothing about phones.
- The **Gradle wrapper** (`gradlew` and its `.jar`) is not in the
  repository: the workflow installs Gradle itself, and Android Studio
  reads the version from `gradle-wrapper.properties`. Step 2 decides how
  the editor runs Gradle on a maker's computer.

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
