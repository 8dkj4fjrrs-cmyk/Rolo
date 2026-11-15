#!/bin/sh
# Checks, on a running emulator, what Android's backup keeps of a Rolo game:
# the saved games (the folder "saves") and nothing else. Uses the emulator's
# own stand-in for Google's backup ("local transport"), so nothing leaves
# the machine.
#
#   check_android_backup.sh GAME.apk PACKAGE
#
# The .apk must be the debug one ("run-as" only works on those).
set -eu
APK="$1"
PACKAGE="$2"
as_app() { adb shell run-as "$PACKAGE" sh -c "'$1'"; }

adb uninstall "$PACKAGE" > /dev/null 2>&1 || true
adb install "$APK" > /dev/null
# Android does not back up an app that was never started.
adb shell monkey -p "$PACKAGE" -c android.intent.category.LAUNCHER 1 > /dev/null
sleep 8
adb shell input keyevent KEYCODE_HOME
sleep 2
as_app "mkdir -p files/saves && echo kept > files/saves/backup-check.sav && echo left > files/backup-check-other.txt"

adb shell bmgr enable true
adb shell bmgr transport com.android.localtransport/.LocalTransport
adb shell settings put secure backup_local_transport_parameters is_encrypted=false
adb shell bmgr backupnow "$PACKAGE" | tee backup-out.txt
grep -q "$PACKAGE with result: Success" backup-out.txt || { echo "The backup did not succeed."; exit 1; }

adb uninstall "$PACKAGE" > /dev/null
adb install "$APK" > /dev/null
adb shell bmgr restore 1 "$PACKAGE" | tee restore-out.txt
sleep 3

SAVED="$(as_app "cat files/saves/backup-check.sav 2>/dev/null || true")"
OTHER="$(as_app "cat files/backup-check-other.txt 2>/dev/null || true")"
echo "After restoring: saved game = '$SAVED', other file = '$OTHER'"
[ "$SAVED" = "kept" ] || { echo "The saved game did not come back from the backup."; exit 1; }
[ -z "$OTHER" ] || { echo "A file outside the saved games was backed up too."; exit 1; }
adb uninstall "$PACKAGE" > /dev/null
echo "Backup check passed: only the saved games are kept."
