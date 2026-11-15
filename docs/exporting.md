# Exporting games

Exporting makes a copy of your game that runs **without the editor**, to give
to players: a **program** for the kind of computer you are on, a
**web page** that plays in a browser ([for the web](#for-the-web), below),
or an **Android app** ([for Android](#for-android), experimental).
It contains:
- Rolo's game runtime (the player program, renamed after your game);
- your project's files;
- the license texts of the software inside it.

## In the editor

Click **Build** in the toolbar, or **File > Export Game…**:

| Field | Meaning |
|---|---|
| Export for | **This computer** (a program for macOS, Windows or Linux, whichever you are on), **Web browsers** (a folder for a web host) or **Android** (an app; it asks for more in a second step, [below](#for-android)). |
| Game name | Names the game's folder (or macOS app) and its program. Letters, digits, spaces, `-` and `_` are kept. |
| Export into | The folder the game is made in (by default `Documents/Rolo Exports`). It cannot be inside the project. |

If the open scene has unsaved changes, it is saved first. **Exporting again**
replaces the earlier export of the same game. A folder of the same name
that is *not* an export is never touched.

### Checks before exporting

Rolo first reads everything the game needs, as the player program would.
That is:
- the project file and its start scene;
- every scene, with its components, the assets it uses, and its triggers'
  conditions and actions;
- the story content: variables, dialogues and quests.

**Errors stop the export.** For example, a missing start scene, a scene
using an unknown behaviour, a dialogue leading to a line that does not exist,
or a trigger naming an object that is not in its scene. The Console lists
each one with its file and place. **Warnings** are reported but do not stop
it. **Game > Check Content** runs most of the same checks at any time.

## From the command line

```sh
rolo_editor --project "path/to/My Game" --export "path/to/Exports"
```

This runs the same checks and export without opening a window. It prints
the game program's path and exits with code 0, or 1 if it failed. It is
useful in scripts and continuous integration. `--export-web` in place of
`--export` exports for the web, and `--export-android` for Android (with
more options: `rolo_editor --help`).

## What you get

```
Linux and Windows                    macOS
My Game/                             My Game.app/
  My Game           (My Game.exe)      Contents/MacOS/rolo_player
  axslc/            shaders            Contents/Resources/axslc/
  game/             your project       Contents/Resources/game/
  licenses/         license texts      Contents/Resources/licenses/
  README.txt        how to play        Contents/Info.plist   (your game's name)
```

To share the game, **zip the whole folder** (or the `.app`), not only the
program, and **keep the `licenses` folder with it**. The licenses of
Rolo and the libraries in it require that their texts go with every copy;
see [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md) and
[licenses/](../licenses/README.md).

Players run the program. When the runtime starts with no arguments, it looks
for `game/` beside itself (on macOS, in the app's Resources) and plays it:
- the window is titled with your game's name;
- the frame-rate overlay is hidden (`--stats` shows it);
- saved games go to the player's own user folder, per game.

## For the web

Choose **Web browsers** in the Export dialog (or `--export-web`). You get
a folder, `My Game Web`, that is a small web site:

```
My Game Web/
  index.html          the page players open (your game's name is in it)
  rolo_player.js      Rolo's game runtime for browsers (WebAssembly)
  rolo_player.wasm
  rolo_player.data    its shaders
  game.pack           your project, as one file
  licenses/           license texts
  README.txt          how to share it
```

It is the same on every system: a web export made on a Mac runs in any
browser. The Platformer Starter is about 5.7 MB, and about 2.2 MB as web
servers send it (compressed).

**Sharing it**
- **itch.io**: zip the *contents* of the folder (so `index.html` is at the
  top of the zip), make a project of the kind **HTML**, upload the zip and
  tick **This file will be played in the browser**. Set the viewport to
  your game's size (1280 × 720 unless you changed it).
- **Any web host** (GitHub Pages, Netlify, your own site): upload the folder
  as it is. No special server settings are needed.

**Trying it on your computer.** Opening `index.html` straight from the
folder does not work (browsers only let a page load its files from a web
address; the page says so). Start a small web server in the folder:

```sh
cd "My Game Web"
python3 -m http.server 8000      # then open http://localhost:8000
```

**What is different in a browser**
- **Controls**: keyboard, mouse and gamepads. A gamepad appears to the page
  once a button on it is pressed. No rumble yet.
- **Phones and tablets** get an on-screen stick and a button for each of
  your game's actions ([touch screens](controls.md#touch-screens)), with
  nothing for you to set up.
- **Saved games** are kept by the player's browser, for the page's web
  address. Clearing the browser's site data removes them.
- **Sound** starts with the player's first click or key press: browsers do
  not let a page make sound before that.
- **Quitting** (`game.quit()`, or Escape where it quits) shows "Thanks for
  playing" and a Play again button: a page cannot close itself.
- **Scripts**: Luau scripts and all of Rolo's own components work. **C++
  game modules do not**: the web runtime that comes with Rolo has none
  (the export warns if your scenes use any). To have yours on the web,
  build the web runtime yourself with `ROLO_GAME_MODULE_DIRS`
  ([building](building.md#for-the-web)).
- **The picture** fits the window (with bars, or filling it: see
  [screens of other shapes](#screens-of-other-shapes)) and is drawn with
  as many pixels as it has on that screen, so it is sharp on Retina and
  phone screens; there is a Fullscreen button.
- **Browsers**: current Chrome, Firefox, Edge and Safari (WebAssembly with
  exceptions and SIMD, WebGL 2: Safari 16.4 or newer).

You may edit `index.html` (colours, text around the game); the comment at
its top says what must stay. Exporting again replaces it, so keep your
version elsewhere.

The page loads `game.pack`, whose format is described in
[formats/game-pack.md](formats/game-pack.md).

## For Android

**Experimental.** Android exports are built and played in an emulator on
GitHub's machines with every change to them, but have **not yet run on a
real phone**, and exporting has not yet been tried on a Mac or on Windows
by hand ([known limitations](known-limitations.md)).

Android apps are built by Google's own tools, as in Godot and Cocos
Creator. Rolo writes an Android project with your game in it and has
those tools build it. So, once:

1. Install **[Android Studio](https://developer.android.com/studio)** (free;
   several GB). It brings the tools Rolo uses: the Android SDK and Java.
2. Open it once and let its setup wizard finish (it downloads the SDK).
   You need not make a project in it, or keep it open.

Then choose **Android** in the Export dialog and click **Continue**:

| Field | Meaning |
|---|---|
| Package name | The app's identity on devices and in Google Play, like `com.yourname.yourgame`: something that is yours (your name, or a web address you own, backwards), then the game. Small letters, digits and dots. **Once a game is published it can never change.** Names starting with `com.example` are not accepted by Google Play. |
| Version | What players see, e.g. `1.0`. |
| Version code | A whole number. Google Play wants it **higher with every upload** (1, 2, 3...); after each export with the file for Google Play, the editor raises it by one. |
| Held | Sideways (landscape) or upright (portrait). By default it follows the game's size. |
| Icon | A square PNG picture in your project (512 × 512 is best), or Rolo's own. |
| Also make the file for Google Play | Makes the `.aab` as well, signed with your **upload key** (below). Not with a package name that starts with `com.example` or still says `yourname`. |

These are kept in the project (`project.rolo`, under `"android"`), so the
next export starts from them. If Rolo cannot find Google's tools it says
what is missing and where it looked; **Tools…** lets you choose their
folders yourself.

The export takes a few minutes. The **first one** takes longer: Google's
build tool (Gradle) downloads itself and its parts, a few hundred MB,
once. You can go on working; the Console shows the progress.

You get a folder:

```
My Game Android/
  My Game.apk     the app, to try
  My Game.aab     the app for Google Play (only with an upload key)
  project/        the Android project both were built from (it opens in Android Studio)
  README.txt
```

**Trying it.** In Android Studio, start an emulator (**More Actions ›
Virtual Device Manager**, the ▶ beside a device) and drag `My Game.apk`
onto it. Or copy the file to a phone and open it there; the phone asks
once whether to allow installing apps from files. The `.apk` is signed
with a throwaway "debug" key: fine for trying, not accepted by Google
Play.

**The upload key.** Google Play only accepts updates of a game that are
signed with the same key as its first upload: the key proves they come
from you. **New Key…** makes one (a file, by default in `Documents/Rolo
Keys`, protected by a password you choose). Use the same key for all your
games.

- Rolo remembers where the key file is, and **never stores its password**:
  you type it at every export for Google Play.
- Keep the file and the password safe, with a copy of both somewhere else.
  If either is lost, the game can only be updated after asking Google for
  a new key.
- Never put the key file inside a project you share or publish.

**Publishing** has its own guide, from the developer account to the
release: [Publishing on Google Play](google-play.md). With the file for
Google Play, the export folder also gets `Google Play icon (512).png` (the
icon as the store's page wants it, if the game has its own), and the
editor raises the project's version code by one for the next export.

**Good to know**
- The app runs on Android 7.0 and newer, on phones and tablets (arm64)
  and on emulators and Chromebooks with Intel or AMD processors (x86_64).
- It asks for **no permissions** and does not use the internet.
- Android's **Back** button is the Escape key to your game.
- Saved games are kept on the device in the app's own folder. They (and
  nothing else of the game) are part of Android's own backup: if the
  player has it switched on, they come back on a new phone or after
  installing the game again.
- Luau scripts and Rolo's own components run there; C++ game modules do
  not (as on the web). No gamepads or keyboards yet.
- From the command line: `rolo_editor --project "My Game" --export-android
  Exports --android-package com.yourname.mygame`. With `--android-key
  upload-key.jks` the `.aab` is made too; the key's password is read from
  the environment variable `ROLO_ANDROID_KEYSTORE_PASSWORD` (never from
  the command line, where other programs could see it).
  `--new-android-key FILE --android-key-owner "Your Name"` makes a key.

## Screens of other shapes

Your game has a **size** (1280 × 720 unless you change it): what the
player sees. Phones, tablets and monitors come in other shapes. Two
settings decide what happens there ([ADR 0027](adr/0027-screen-sizes.md)).
Select the scene (click empty space in the Hierarchy) and look in the
Inspector's **Game (whole project)** section:

| Setting | What it does |
|---|---|
| **Game Size** | The size the game is made for. Choose an upright size (720 × 1280) for a phone game held upright. Other sizes: `"width"` and `"height"` in `project.rolo`. |
| **Fill the Screen** | Off: the game is shown whole, with bars beside it; every player sees the same. On: the picture fills the screen, and wider or taller screens show more of the world. |

With **Fill the Screen** on:

- Scores, prompts and menus that are **children of the Camera** stay at
  the screen's edges by themselves: what you placed near the left edge
  stays as far from the left edge, and so on for each side; what is in the
  middle stays in the middle. A rectangle covering the whole game (a
  night veil) is stretched over the whole screen.
- **Camera Bounds** keeps the camera inside your map on wide screens too.
- In scripts, `game.viewSize()` gives how much of the world the screen
  shows.
- Make sure the level looks right with more of it in view. A scene that
  is exactly one screen, with nothing around it, is better with bars.

The Platformer and Top-Down Starters fill the screen. Try other shapes on
your computer: `rolo_player --project MyGame --width 1000 --height 400`,
or resize the game's window.

## Each system exports its own games

A program export contains the runtime of the computer it was made on, so:
- export on macOS to make the macOS version;
- export on Windows for Windows;
- export on Linux for Linux.

There is no cross-export of programs yet (web and Android exports are the
same from every system).

- **macOS**: the editor signs the exported app "ad hoc", but it is not
  signed or notarized with an Apple Developer ID. So the first time, players:
  - right-click the app and choose **Open**, then **Open** again;
  - on newer macOS, if it is still blocked: **System Settings › Privacy &
    Security**, then **Open Anyway**.

  Developer ID signing and notarization are on the roadmap. Exported apps
  run on the same kind of Mac they were made on (Apple silicon from an Apple
  silicon build).
- **Windows**: the game is a folder with `My Game.exe` and the DLLs it needs.
- **Linux**: the game is a folder; the program needs a desktop with OpenGL,
  as the editor does.

The [known limitations](known-limitations.md) say which of these have been
tested.

## Size

A game made from a **release build** of Rolo (or a downloaded Rolo)
is the runtime plus your files: the Top-Down Starter exports to about 16 MB
on Linux, most of it the rendering engine and its libraries. Zipped, it is
smaller.

The editor of a *development* build (`--preset dev`) exports its debug
runtime, which is much larger (about 90 MB), and slower. Use a release
build, or a downloaded Rolo, for games you share.
