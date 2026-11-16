# ADR 0028: Android

- **Status:** Accepted. Steps 1 (the player runs on Android) and 2 (export from the editor) are done; step 3 (Google Play) is done as far as it can be without an upload (the upload and the pre-launch report are open); step 4 (polish) is done as far as an emulator allows. Nothing has run on a real phone
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
  the editor runs Gradle on a maker's computer. (It did: the `.jar` is in
  the repository since step 2, below.)

## Step 2, as built (2026-11-15)

**Export Game… › Android** in the editor, and `rolo_editor --export-android`
on the command line, make an `.apk` to try and, with the maker's upload
key, a signed `.aab` for Google Play. The guide is
[exporting, "For Android"](../exporting.md#for-android).

Two things step 1 left open were decided:

- **Rolo's download carries the player already built**, not sources. The
  "Android runtime" (`gradle :app:roloAndroidRuntime`, built by the
  release and the Android workflow) is the Gradle project of step 1 with
  the player's libraries for arm64-v8a and x86_64 compiled in (optimised,
  without debugging information: about 23 MB for both), the shaders and
  Axmol's Java. An export copies it and adds the game. So a maker needs
  **only Android Studio's SDK and Java: no NDK, no CMake, no C++ build**,
  which is where the versions go wrong in engines that compile on the
  maker's computer, and an export takes a minute or two instead of ten.
  The price: games' own C++ modules cannot be in an Android game exported
  this way (as on the web); they need a runtime built from source.
- **Gradle is started by its own wrapper**, run with Java directly
  (`java -classpath gradle-wrapper.jar org.gradle.wrapper.GradleWrapperMain`:
  what the `gradlew` scripts do, without a shell). The wrapper downloads
  the pinned Gradle the first time. For this, `gradle-wrapper.jar`
  (Apache 2.0, 45 KB) is now in the repository and in the runtime; the
  workflow checks it is Gradle's own. Makers need no Gradle of their own.

How it works:

- **The tools are found by themselves**: the SDK where `ANDROID_HOME`
  says or where Android Studio puts it on each system, Java inside
  Android Studio (its "jbr" folder) or where `JAVA_HOME` says. Before
  anything is built Rolo checks them (the SDK folder, SDK Platform 36,
  Java 17 or newer, by running it) and says in plain words what is
  missing and how to get it. The folders can be chosen by hand (kept in
  the editor's preferences).
- **The app's details** (package name, version code and name, orientation,
  icon) are asked for in a dialog, checked (a package name's rules, with
  the reason when one is refused), and kept in `project.rolo` under
  `"android"`, changed through an undoable command. The orientation
  follows the game's size unless chosen. The icon is a PNG of the project,
  scaled by Rolo to Android's five sizes.
- **The export** writes `<Game> Android/project` (the runtime, the game
  pack, license texts, `rolo-game.json` with the details, the name and
  icon as Android resources), runs Gradle (`:app:assembleDebug`, and with
  a key `:app:bundleRelease`), and puts `<Game>.apk` and `<Game>.aab`
  beside the project with a README. In the editor it runs on a worker
  thread; the window stays usable and the Console shows Gradle's output.
  Known failures (a wrong password, no internet for the first download,
  a missing SDK part, a full disk, a wrong Java) are explained in plain
  words; others point at Gradle's own.
- **Signing**: the `.apk` gets Google's throwaway debug key (made by its
  tools). The `.aab` is signed by Google's tools with the maker's upload
  key. **New Key…** makes one with Java's `keytool` (RSA 2048, valid about
  27 years, a PKCS12 file, never overwriting an existing file). The
  password is typed at every export, kept only in memory, and handed to
  `keytool` and Gradle through their environment: it is on no command
  line, in no file, and not in the log. Rolo still has no cryptography of
  its own (ADR 0024).
- **Other programs** are started through one small piece of code
  (`editor/model/Process`), without a shell, on all three systems.

Tested (2026-11-15), and where:
- Unit tests on Linux, macOS and Windows (CI): the project settings, the
  tool checks, the written project, icons, the commands for Google's
  tools, running programs (also cancelling them), the password field.
- **The real thing, on GitHub's Linux machines** (the Android workflow):
  the runtime is built; the editor makes a key and exports the Platformer
  Starter from the command line; Gradle builds the `.apk` and the `.aab`
  with only the SDK and Java (35 seconds there; an `.apk` of 23 MB for
  both kinds of processor, an `.aab` of 9 MB); the app's name, package
  and version, its 16 KB pages and the bundle's signature are checked; an
  export with the wrong password must fail saying so; and the exported
  `.apk` is played in the emulator as in step 1.
- The editor's dialogs were tried by hand on a virtual screen on Linux,
  with a real `keytool` (a key was made) and stand-ins for the SDK and the
  runtime (so no app was built from the window).

Not tested, and to be honest about:
- **Exporting on a Mac or on Windows** has not been done at all: finding
  Android Studio's tools there is written from where Android Studio is
  documented to put them, and checked only with stand-in folders.
- **No real phone**, still, and nothing has been uploaded to Google Play:
  that the `.aab` is accepted there is step 3's first question.
- The release workflow now builds and packs the Android runtime; that
  part runs for the first time at the next release. If it fails, the
  packages are still made, without Android export.

Left for later:
- An **adaptive icon** (Android 8+ shapes icons itself from two layers);
  the app has a plain one.
- **C++ game modules** in exported Android games.
- The exported project is written anew at every export, so Gradle's own
  build cache inside it is lost (its downloads are kept elsewhere).
- Very long folder names on Windows may exceed what its tools accept.

## Step 3, as built (2026-11-15)

**What could and could not be done.** Step 3 asked for a guide, the key
kept safely, version codes, the store's forms, "and what the pre-launch
report said". Rolo's developers have no Google Play account and no Android
device, so nothing was uploaded and **no pre-launch report exists**. That
part of the step stays open until a maker does it; the guide asks them to
report back. Everything else was done, and checked as far as Google's own
tools allow outside the store.

**The guide** is `docs/google-play.md` (Help › Publishing on Google Play),
written from Google's help pages as they stood in November 2026, with a
link beside each rule because they change yearly: US$25 once; identity
checks; a new personal account must show an Android device and run a
closed test with 12 testers for 14 days before applying for production;
target API level 36 since 31 August 2026 (what Rolo builds for); the App
content forms; the listing's pictures.

**A correction to the plan above.** It said that losing the upload key
locks the maker out of updating the game. That is not so for new apps:
with Play App Signing, Google holds the key players' copies are signed
with, and replaces a lost upload key on request. The guide says so, and
still asks for a safe copy, because the reset takes days.

**Backups: decided, on.** Step 2 left `allowBackup` off "to be decided
with step 3". A player who changes phone expects saved games to follow.
The manifest now allows backup of **only the `saves` folder**
(`res/xml/rolo_backup.xml` for Android 11 and older,
`rolo_data_extraction.xml` for 12 and newer, cloud backup and
phone-to-phone transfer alike); the unpacked game beside it is left out,
as it comes with the app. The files go to the player's own Google account
by Android's service; neither the maker nor Rolo receives them, so the
guide's answer to the Data safety form stays "collects nothing" (with the
advice to read Google's definitions, the answers being the maker's).

**Small helps in the export.**
- With the file for Google Play, the export writes `Google Play icon
  (512).png` (the store's listing wants the icon uploaded separately).
- After such an export the editor raises the project's version code by
  one (an undoable command), since Google Play refuses a code it has
  seen. The command line does not change the project; it takes
  `--android-version-code`.
- The file for Google Play is refused for package names starting with
  `com.example` (Google Play rejects them) or still containing Rolo's
  placeholder `yourname` (the name is for ever). Exports to try stay
  allowed with a note, as before.

**Checks added to the Android workflow** (GitHub's Linux machines):
- Google's **bundletool** (Apache-2.0; downloaded there, not a dependency
  of Rolo or of games) validates the `.aab`, and its manifest is checked:
  package, version code, target level 36, backup on, no permissions, not
  debuggable. It then builds the app from the bundle as Google Play does,
  reports the download size (about 5 MB per phone for the Platformer
  Starter, with bundletool 1.18.3), and that app is played in the emulator.
- `tools/android/check_android_backup.sh`: with the emulator's local
  stand-in for Google's backup, a saved game is backed up, the game
  removed and installed again, and the saved game returns while a file
  outside `saves` does not.

Not tested: an upload, Google's review, Play App Signing, the pre-launch
report, a real phone, a backup to a real Google account.

## After step 3: the `.apk` to share (2026-11-15)

The owner cannot use Google Play, so games need another way to players.
Until now the `.apk` was always a debug build signed with Google's
throwaway key: installable, but impossible to update (the next export on
another computer has another debug key) and marked debuggable.

- **With the maker's key, the `.apk` is now a release build signed with
  that key** (`:app:assembleRelease`), next to the `.aab` (same key).
  Without a key it stays the debug build "to try". One file name either
  way, so there is never a choice of files to send; the README and the
  dialog say which kind it is. The dialog's box is now "Sign with my own
  key".
- The checks for Google Play apply to it as well (no `com.example`, no
  `yourname`), and the editor raises the version code after such an
  export: Android, too, only takes a higher one as an update.
- **Developer verification.** Google has begun to require that makers of
  apps installed from files register with it (enforced in four countries
  since 30 September 2026, elsewhere from 2027 by its plan). Rolo can do
  nothing about that but say so: the export guide explains it and links
  Google's page. Nobody working on Rolo has registered.

Checked by the Android workflow: the `.apk`'s certificate is the key
file's; it is not debuggable; the no-key export is a debug build signed
otherwise; in the emulator, version code 8 installs over 7, and going
back to 7 or installing the debug-signed copy over it is refused.

## Step 4, part 1: gamepads and rumble (2026-11-15)

- **Events kept, then read.** Android reports controllers as events to
  the app's Java side; the game asks once a frame. `RoloGamepads.java`
  (in `RoloActivity`'s `dispatchKeyEvent` / `dispatchGenericMotionEvent`)
  keeps each of up to four gamepads' buttons and axes, and
  `Gamepads_android.cpp` fetches them all with one call a frame. Android
  has one layout for the controllers it knows, so there is no table of
  models. A button pressed and released between two frames is remembered
  until read once (the emulator's ten frames a second showed the need).
- Only events whose source is a gamepad or joystick are taken; keyboards,
  remote controls and Android's Back go on to Axmol as before. A
  controller's direction buttons are now its d-pad, no longer arrow keys.
- A gamepad gets its number when first used and gives it up when Android
  reports the device gone. In the background everything counts as let go.
- **Rumble** asks Android to shake the controller (`InputDevice`'s
  vibrators; both motors separately on Android 12 and newer). The phone's
  own vibration would need a permission, and the app still asks for none.

Tested in the emulator (GitHub, Android 14) with Android's injected
gamepad events: the first button press announces "Gamepad 1 plugged in",
the on-screen controls disappear and the hints show controller pictures,
the left stick moves the hero, Start opens the pause menu, d-pad and A
choose Quit. Not tested: any real controller; several at once; unplugging;
rumble (the injected gamepad has no motors).

## Step 4, part 2: camera notches (2026-11-15)

The game goes on filling the whole screen, under a notch too (bars beside
a game are worse than a notch over a bit of sky). What must not be hidden
is what the player reads and touches:

- `RoloActivity` learns the notch's place from Android (the display
  cutout's safe insets, whenever they change) and the game asks for them
  (`rolo::android::safeInsets`) when fitting the screen and about twice a
  second after, because turning the phone round moves the notch without
  changing the screen's size.
- `insetsInView` (rolo/scene/ScreenFit.hpp) turns the pixels into the
  game's units, less what bars already cover. `pinToScreen` and
  `TouchControls::layout` take these `ScreenInsets`: things at an edge
  move in by that edge's inset; things in the middle and rectangles
  covering the screen are left alone.

Tested: unit tests for the rules (all systems), and in the emulator,
whose screen has a cutout of 128 pixels on the left when held sideways:
the log says "kept clear for the camera notch: left 85 ... points", and
the score, the stick and the Pause button are drawn that much further in.
Not done: rounded corners (not measured; the controls' own margin has to
do), objects that are not children of the Camera, and no real phone.

## Step 4, part 3: keyboards, and a lost graphics context (2026-11-15)

**Keyboards.** Axmol's Android code passes on nine keys. `RoloKeys.java`
takes every key event that comes from a keyboard (leaving Android's own
buttons, and Back, which stays the game's Escape by way of Axmol), keeps
them in order, and the game fetches them once a frame
(`rolo::android::takeKeys`), where Android's key codes become `rolo::Key`.
Held keys count as let go when the app leaves the front.

**A lost graphics context.** Android may throw away an app's graphics
context while it is in the background; Axmol asks it not to
(`setPreserveEGLContextOnPause`), which most phones honour, but not
always. Step 1 guessed that Rolo's textures would not survive it. To find
out, `RoloActivity` has a switch for tests (`am start ... --ez
rolo.loseContext true`) that stops asking, so every trip to the
background loses the context. The guess was right: the game came back
with its texts and shapes, and without its sprites, tiles and button
labels. Axmol refills the textures it loaded itself, and Rolo loads its
own (ADR 0023).

- Images from files: `TextureStore::reloadAll` reads them again into the
  same texture objects, on the first frame after the context is back (not
  inside Axmol's "recreated" event, where Axmol is still rebuilding).
  Nothing is kept in memory for this; coming back costs the loading time.
- Text pictures are small and made from fonts: Axmol is told to keep
  their pixels (`VolatileTextureMgr::addImage`) and refills them itself.

Tested in the emulator: keys injected by Android reach the game (D,
Space, Escape, arrows, Enter: the hints turn to key pictures, the pause
menu opens, Quit is chosen); with the switch on, Home and back loses the
context, the log says so, and the picture afterwards shows the level and
the labelled buttons again. Not tested: a real keyboard, a phone that
loses the context by itself.

## Step 4, part 4: performance (2026-11-16)

What could be measured without a phone was measured; what a phone's
graphics chip does with it is still unknown.

- **A speed line in the log.** A maker cannot see a frame counter on a
  phone but can read its log. After 300 frames of play, and when the game
  ends, the app writes `Speed: N frames at F a second (slowest frame ...).
  Rolo's own work in a frame: ... on average, ... at most`. "Own work" is
  the time inside Rolo's frame function (scripts, physics, bringing the
  drawn scene up to date); the rest is the graphics chip and waiting for
  the screen.
- **That line found a real waste, on every system.** In the emulator the
  Platformer Starter's own work was 12 to 13 ms a frame, and 8 ms on a
  Linux desktop (debug build): far too much for so small a scene. The
  renderer was building the whole drawn scene again every frame, because
  the pause menu's script says `setActive(false)` every frame and
  `Scene::setActive` counted that as a change. Now setting an entity's
  active state, or any property, to the value it already has changes
  nothing (`Scene::setActive`, `Scene::setProperty`; a regression test in
  `tests/scene`). Measured after: 0.4 ms on the desktop; in the emulator
  0.4 ms standing still and 4 to 7 ms in the runs that open menus and
  move, where real changes (a menu shown, a text changed) still rebuild
  the drawing. Making those rebuilds smaller is ADR 0021's kind of work,
  not Android's, and is left for there.
- **No litter per frame from the new Android code**: the Java methods
  for gamepads, keys and the notch are looked up once, and the list the
  gamepads are read into is made once.
- The emulator shows 17 to 22 frames a second before and after: it draws
  without a graphics chip, and that is what its frames wait for.

Not done, and why: reading the game straight from the pack (it exists
twice on the device: a bigger change to how games are read); a real
phone's numbers (there is none).

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
