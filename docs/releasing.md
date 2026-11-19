# Releasing Rolo

How a version of Rolo is prepared, checked and published. A release is a
git tag plus one downloadable `.zip` per system, built by CI.

## 1. Prepare

1. **Version**: set `VERSION` (and `ROLO_VERSION_SUFFIX`, e.g. `-alpha`,
   or empty for a final release) in the top-level `CMakeLists.txt`.
2. **Changelog**: move the changes into a section for the version in
   `CHANGELOG.md`.
3. **Licenses**: if Axmol or another dependency changed version:
   - run `python3 tools/release/fetch_licenses.py`;
   - update `THIRD_PARTY_NOTICES.md` (the libraries are those on
     `rolo_player`'s link line and the third-party headers its sources
     include: `ninja -t deps` in a build folder lists them).
4. **Known limitations**: update [known-limitations.md](known-limitations.md),
   especially the table of what was tested where.
5. **Generated example and template projects**: if their generators changed,
   rebuild them from fresh projects:
   - `examples/lantern_keeper/README.md` says how;
   - for the starter, `templates/README.md`.

## 2. Check

On each system (CI does this for every push; see `.github/workflows/`):

```sh
cmake --workflow --preset dev        # debug build + all tests (window tests need a display)
cmake --workflow --preset core       # without Axmol
cmake --build --preset asan && ctest --preset asan
cmake --workflow --preset release    # release build + tests + package into dist/
```

Then, by hand, with the **packaged** Rolo from `dist/` (not the build
folder), so the installed layout is what gets tested:

1. Unzip it somewhere new and start `rolo_editor`. The welcome screen
   appears.
2. **New Project… > Top-Down Starter**, press ▶, walk, talk, stop.
3. **Build > Export**, then move the exported folder elsewhere and start the
   game program by double-clicking it.
4. **Help > Welcome… > Try The Lantern Keeper**: it opens, and plays to
   the first dialogue.

## 3. Publish

1. Commit and push. Then start the release in one of two ways:
   - on GitHub: **Actions › Release › Run workflow**, type the version
     (`0.9.0-alpha`) and press **Run workflow**. The release creates the
     tag `v0.9.0-alpha` on the commit it built;
   - or tag yourself: `git tag -a v0.9.0-alpha -m "Rolo 0.9.0-alpha"`
     and push the tag.
2. The **release** workflow (`.github/workflows/release.yml`):
   - first checks that the version matches `CMakeLists.txt`;
   - builds, tests and packages on Linux, macOS and Windows;
   - attaches the three `.zip` files to a draft GitHub release, with that
     version's section of `CHANGELOG.md` as its notes. Its heading must
     start with the version, e.g. `## 0.9.0-alpha — ...`.
3. Check the draft: the files download and start. Then publish it.

## What a package contains

`cmake --build --preset package` (the last step of the release workflow)
installs the `rolo` component (`cmake/RoloInstall.cmake`) into
`dist/Rolo-<version>-<system>-<processor>/` and zips it:

```
Rolo-0.9.0-alpha-Linux-x86_64/
  rolo_editor         the editor (rolo_editor.exe on Windows)
  axslc/                 its compiled shaders
  runtime/               the game runtime that exports are made from
    rolo_player
    axslc/
  templates/             New Project templates
  examples/              The Lantern Keeper and the sample game (projects and C++)
  licenses/              license texts, copied into every exported game
  docs/                  these guides
  README.md  LICENSE  THIRD_PARTY_NOTICES.md  CHANGELOG.md
```

On macOS:
- the editor is `rolo_editor.app`, with everything else inside it, in
  `Contents/Resources/rolo/` (the runtime is `runtime/rolo_player.app`);
- the readme and license files sit beside the app.

The editor finds all of this from its own location
(`editor/model/RoloResources.hpp`), so the folder can live anywhere.

## Not done yet

- **Code signing.** macOS apps are signed ad hoc when packaged and
  exported, which is enough for Apple silicon Macs to open them. They need
  an Apple Developer ID and notarization to open without a warning. Windows
  programs need an Authenticode certificate. Neither is set up, so:
  - the first time, macOS users must allow the app under **System
    Settings › Privacy & Security › Open Anyway** (macOS 15 and newer;
    right-click **Open** before that);
  - Windows SmartScreen may warn.
- **Installers.** Rolo ships as zips, not `.dmg`, `.msi` or packages.
