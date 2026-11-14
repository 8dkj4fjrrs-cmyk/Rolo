# Building Rolo Engine

A full build produces:

| Program | What it is |
|---|---|
| `rolo_player` | The game runtime: opens a window and runs the main loop. A `.app` bundle on macOS. |
| `rolo_editor` | The editor: dockable viewport, hierarchy, inspector and console panels. A `.app` bundle on macOS. See [editor.md](editor.md). |
| `rolo_tests`, `rolo_runtime_tests` | Automated tests, run by `ctest`. |

The window and rendering come from [Axmol](https://github.com/axmolengine/axmol)
(see `docs/adr/`). CMake downloads Axmol, its shader compiler and the JSON library automatically
the first time; you only install the tools below.

## macOS (Apple silicon) — primary platform

Milestone 1 was verified on 2026-10-29 on the project owner's Apple silicon
MacBook Pro (macOS 15.4.1, Command Line Tools for Xcode 16.4 / Apple clang
17.0.0, CMake 4.4.5, Ninja 1.13.2, PowerShell 7.6.9): full Axmol build, 33/33
tests passed, including the two that open a real window. Full Xcode is
**not** required.

### 1. One-time setup

```sh
xcode-select --install            # Apple's compiler (skip if already installed)
brew install cmake ninja git      # build tools (Homebrew: https://brew.sh)
brew install powershell    # needed by Axmol's download helper
```

If that fails, install the official package from
https://aka.ms/powershell-release?tag=stable (the `osx-arm64.pkg` file).
Open a **new** Terminal
window afterwards, then check:

```sh
cmake --version    # 3.25 or newer
pwsh --version     # PowerShell 7.x
```

### 2. Configure, build, test

From the repository root:

```sh
cmake --workflow --preset dev
```

The **first** run downloads Axmol (about 130 MB) and compiles it, which takes
roughly 10–20 minutes. Later builds only recompile what changed.

### 3. Run the player

To see log messages in Terminal, run the program inside the app bundle:

```sh
./build/dev/bin/rolo_player/rolo_player.app/Contents/MacOS/rolo_player
```

Or open it like any Mac app (no log output):

```sh
open build/dev/bin/rolo_player/rolo_player.app
```

A window titled "Rolo Engine" shows Rolo, the engine's smiling orange
mascot, and a frame-rate overlay. Press **Escape** or close the window to quit.
`--help` lists the options.

### 4. Run the editor

```sh
./build/dev/bin/rolo_editor/rolo_editor.app/Contents/MacOS/rolo_editor
```

The editor window opens with its panels docked; quit with Cmd+Q or by closing
the window. A guide to the panels is in [editor.md](editor.md).

## Presets

| Preset | Purpose |
|---|---|
| `dev` | Debug build with the Axmol runtime; warnings are errors. Use this day to day. |
| `release` | Optimised build with the Axmol runtime. `cmake --workflow --preset release` also tests and packages it (below). |
| `core` | Core library, editor logic and their tests only — no Axmol, no window. Builds in seconds. |
| `asan` | `core` + AddressSanitizer/UBSan, for hunting memory bugs. |
| `web` | The game runtime for web browsers (WebAssembly), built with Emscripten: see [For the web](#for-the-web). |

Each preset builds into its own folder under `build/`. Downloaded Axmol lives
in `external/` and is shared by all presets. To start completely fresh,
delete `build/` (and `external/` to re-download Axmol).

### Packaging Rolo

```sh
cmake --workflow --preset release      # configure, build, test, package
# or, after a release build:
cmake --build --preset package
```

This makes `dist/Rolo-<version>-<system>-<processor>/` and a `.zip` of
it: the editor, the game runtime used for exports, templates, examples,
licenses and docs, laid out as the editor expects (see
[releasing](releasing.md)). On a headless Linux machine, run the test step
under a virtual display: `xvfb-run -a ctest --preset release`.

### For the web

Games exported for the web use Rolo's **web runtime**: the player, built
for WebAssembly with [Emscripten](https://emscripten.org). A downloaded
Rolo has it already. Building from source, make it once (and again when
the engine changes):

```sh
# One time: Emscripten 4.0.9 (the version Rolo is built and tested with).
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install 4.0.9 && ~/emsdk/emsdk activate 4.0.9
source ~/emsdk/emsdk_env.sh            # in each new terminal

emcmake cmake --preset web
cmake --build --preset web             # about as long as a release build
```

It lands in `build/web/bin/rolo_player/` (`index.html`, `rolo_player.js`,
`.wasm`, `.data`), which is where the editor of a source build looks when
you export for the web, and where packaging takes it from
(`ROLO_WEB_RUNTIME_DIR`).

- Only the player is built for the web: no editor, no tests.
- It is built **without threads**, so games run from any web host (pages
  that use threads need special server headers), and with WebAssembly
  exceptions and SIMD.
- Your own C++ game modules can be built in:
  `emcmake cmake --preset web -DROLO_GAME_MODULE_DIRS=...`.
- To check an exported game in a real browser, as CI does:
  `node tools/web/check_web_game.js "path/to/My Game Web"` (needs Node and
  Playwright: `npm install playwright && npx playwright install chromium`).

### For Android

**Experimental** ([ADR 0028](adr/0028-android.md), step 1): Rolo's player
builds as an Android app with one game inside. The editor cannot export
for Android yet; this is the groundwork for it. It is built and tried on
GitHub's machines (the **Android (experimental)** workflow), in an
emulator only: so far never on a real phone.

The app is the Gradle project in `player/android`: the kind of project
Android Studio opens. To build it yourself you need
[Android Studio](https://developer.android.com/studio), and in its
**SDK Manager › SDK Tools** (tick *Show Package Details*): NDK
`27.3.13750724`, CMake `3.31.5`, and Android SDK Platform 36. Then, in a
terminal:

```sh
# 1. Pack the game: one file with everything in the project.
cmake --preset core && cmake --build --preset core --target rolo_pack
build/core/bin/rolo_pack templates/platformer/project game.pack

# 2. Build the app (Gradle 9.7.1; `brew install gradle` gives a newer one,
#    which may or may not agree with Google's build plugin).
export ANDROID_HOME="$HOME/Library/Android/sdk"     # where Android Studio keeps the SDK on a Mac
cd player/android
gradle :app:assembleDebug -ProloGamePack="$PWD/../../game.pack" -ProloAbis=arm64-v8a
```

The app lands in `player/android/app/build/outputs/apk/debug/app-debug.apk`.
Drag it onto a running emulator (Android Studio › Device Manager) to
install it. `arm64-v8a` is for phones and for emulators on Apple silicon
Macs; add `x86_64` (`-ProloAbis=arm64-v8a:x86_64`) for emulators on Intel
or AMD computers. The other settings are listed at the top of
`player/android/app/build.gradle`.

**Building on a Mac has not been tried**: the steps above are what
GitHub's Linux machines do. The easier way to try it today: every run of
the workflow keeps the app it built (on the run's page, **Artifacts ›
android-check**, with screenshots and Android's log).

- The game's messages go to Android's log: `adb logcat -s Rolo`, or the
  Logcat window of Android Studio with the filter `tag:Rolo`.
- `python3 tools/android/check_android_game.py app-debug.apk` installs the
  app on the one running emulator or connected phone, plays it and checks
  the log, as the workflow does.
- Not there yet: gamepads, keyboards, keeping clear of notches. See the
  ADR for the list.

### Your game's code

Gameplay is C++ in *game modules* built into the editor and the player
([gameplay guide](gameplay.md)). Two options choose which:

| Option | Default | Meaning |
|---|---|---|
| `ROLO_BUILD_SAMPLE_GAME` | `ON` | Build the example games' modules (`examples/sample_game`, `examples/lantern_keeper`). |
| `ROLO_GAME_MODULE_DIRS` | empty | Folders with your own modules, separated by `;`. |

```sh
cmake --preset dev -DROLO_GAME_MODULE_DIRS="$HOME/Games/MyGame/code"
cmake --build build/dev
```

After changing gameplay code, build again and restart the editor.

## Linux (Ubuntu 24.04)

```sh
sudo apt install build-essential cmake ninja-build git pkg-config \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxxf86vm-dev \
    libgl1-mesa-dev libglu1-mesa-dev libfontconfig1-dev \
    libgtk-3-dev libwebkit2gtk-4.1-dev libwayland-dev libxkbcommon-dev
sudo snap install powershell --classic     # or see Microsoft's install guide
cmake --workflow --preset dev
./build/dev/bin/rolo_player/rolo_player
./build/dev/bin/rolo_editor/rolo_editor
```

On a machine without a screen (CI), run the tests under a virtual display:
`xvfb-run -a ctest --preset dev`.

## Windows

Not yet verified. Planned via CI (`.github/workflows/ci.yml`). Expected, from
a "Developer PowerShell for VS 2022":

```powershell
cmake --workflow --preset dev
```

## Troubleshooting

| Message | Fix |
|---|---|
| `fatal error: 'AppKit/NSColor.h' file not found` (macOS) | Apple's Command Line Tools are damaged. Run `sudo rm -rf /Library/Developer/CommandLineTools`, then reinstall them: `xcode-select --install`, or download "Command Line Tools for Xcode" from developer.apple.com/download/all (pick a version your macOS supports). Then delete `build/` and build again. |
| `PowerShell 7 ('pwsh') was not found` | Install it (see step 1), open a new Terminal, configure again. |
| `external/axmol is not at the pinned commit` | Delete the `external/axmol` folder and configure again. |
| `Could not apply Rolo's Axmol patch` | `external/axmol` was changed, or the patches in `cmake/axmol-patches/` were. Delete the `external/axmol` folder and configure again. |
| `Could not download Axmol` | Check your internet connection; configure again. |
| `Failed to download` / `HASH mismatch` for `json.tar.xz` | CMake downloads nlohmann/json (for scene files) from GitHub at configure time and checks it. Check your connection and configure again; a hash mismatch means the download was damaged or tampered with: delete `build/` and retry. |
