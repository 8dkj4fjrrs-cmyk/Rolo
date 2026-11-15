#!/bin/sh
# Checks, on a running emulator, what happens to players who got the game
# as a file: a newer export signed with the same key installs over the old
# one, and a copy signed with another key (the app "to try") does not.
#
#   check_android_update.sh FIRST.apk NEXT.apk SIGNED_OTHERWISE.apk PACKAGE
set -eu
FIRST="$1"
NEXT="$2"
OTHER="$3"
PACKAGE="$4"
version() { adb shell dumpsys package "$PACKAGE" | grep -m1 versionCode | sed 's/.*versionCode=\([0-9]*\).*/\1/'; }

adb uninstall "$PACKAGE" > /dev/null 2>&1 || true
adb install "$FIRST" > /dev/null
BEFORE="$(version)"
adb install -r "$NEXT" > /dev/null
AFTER="$(version)"
echo "Installed version code $BEFORE, then the update: $AFTER"
[ "$AFTER" -gt "$BEFORE" ] || { echo "The update did not replace the first version."; exit 1; }

# Going back to an older version is refused by Android...
if adb install -r "$FIRST" > downgrade-out.txt 2>&1; then
    echo "An older version was installed over a newer one."; exit 1
fi
grep -o "INSTALL_FAILED[A-Z_]*" downgrade-out.txt | head -1
grep -q "INSTALL_FAILED_VERSION_DOWNGRADE" downgrade-out.txt || { cat downgrade-out.txt; exit 1; }
# ...and so is the same game signed with another key, even a newer version of it.
if adb install -r "$OTHER" > other-out.txt 2>&1; then
    echo "An app signed with another key was installed over the game."; exit 1
fi
grep -o "INSTALL_FAILED[A-Z_]*" other-out.txt | head -1
grep -q "INSTALL_FAILED_UPDATE_INCOMPATIBLE" other-out.txt || { cat other-out.txt; exit 1; }
[ "$(version)" = "$AFTER" ] || { echo "The installed game changed."; exit 1; }
adb uninstall "$PACKAGE" > /dev/null
echo "Update check passed."
