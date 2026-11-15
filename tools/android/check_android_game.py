#!/usr/bin/env python3
"""Plays Rolo's Android player on a device or emulator and checks that it works.

    check_android_game.py APK [options]

Needs `adb` (from Android's platform-tools) and one device or emulator that
is switched on. Used by the Android workflow on GitHub, with an emulator
(.github/workflows/android.yml); it works the same with a phone on a cable.

What it does: installs the app (unless --keep-installed), starts it, waits
until the game says in the log that its first scene is shown, checks that
the screen is not one flat colour, then acts as a player (--play) and checks
what the log said (--expect, --reject, --expect-ended).

--play is a list of things to do, separated by ';':
    X,Y           a finger pressed at a place for a moment
    X,Y>X2,Y2     a finger put down at X,Y and slid to X2,Y2
    back          Android's Back button (the game sees the Escape key)
    home          Android's Home button: the app goes to the background
    return        the app is brought back to the front
    shot:FILE     a screenshot, saved as FILE (PNG)
    wait:SECONDS  nothing, for a while
Places are in the game's own units, as the game sees its screen: (0, 0) is
the bottom-left corner. A number may be "50%" (of the width or height shown)
or negative (measured from the right or top edge), because a game that fills
the screen shows more of its world on a wider phone.

The exit code is 0 if everything was as expected.
"""

import argparse
import base64
import re
import struct
import subprocess
import sys
import time

ACTIVITY = "dev.roloengine.player.RoloActivity"


def adb(*args, check=True, binary=False, timeout=120):
    """Runs adb and returns what it printed."""
    result = subprocess.run(["adb", *args], capture_output=True, timeout=timeout)
    if check and result.returncode != 0:
        raise RuntimeError("adb " + " ".join(args) + " failed: " + result.stderr.decode(errors="replace").strip())
    return result.stdout if binary else result.stdout.decode(errors="replace")


class Check:
    def __init__(self, package, log_file, pictures_in_output=False):
        self.package = package
        self.log_file = log_file
        self.pictures_in_output = pictures_in_output
        self.start_extras = []   # more arguments for "am start"

    # --- the log -------------------------------------------------------------------------------
    def log(self):
        """Rolo's lines in Android's log (tag "Rolo"), oldest first."""
        return [line for line in adb("logcat", "-d", "-v", "brief", "-s", "Rolo:V").splitlines() if "Rolo" in line]

    def whole_log(self):
        return adb("logcat", "-d", "-v", "threadtime", check=False)

    def crashed(self):
        """A line of Android's log that says the app crashed, if there is one."""
        text = adb("logcat", "-d", "-v", "brief", "-s", "AndroidRuntime:E", "DEBUG:F", "libc:F", check=False)
        for line in text.splitlines():
            if "FATAL EXCEPTION" in line or "Fatal signal" in line or "Abort message" in line:
                return line.strip()
        return None

    def running(self):
        return adb("shell", "pidof", self.package, check=False).strip() != ""

    def fail(self, message):
        print("FAILED: " + message)
        print("--- the game's log ---")
        for line in self.log():
            print(line)
        crash = adb("logcat", "-d", "-v", "brief", "-s", "AndroidRuntime:E", "DEBUG:F", "libc:F", check=False)
        if crash.strip():
            print("--- Android's crash report ---")
            print("\n".join(crash.splitlines()[-80:]))
        mentions = [line for line in self.whole_log().splitlines()
                    if self.package in line or "AxmolActivity" in line or "ActivityTaskManager" in line]
        if mentions:
            print("--- what Android's log says about the app (the last lines) ---")
            print("\n".join(mentions[-40:]))
        self.save_log()
        sys.exit(1)

    def save_log(self):
        if self.log_file:
            with open(self.log_file, "w", encoding="utf-8") as out:
                out.write(self.whole_log())

    def wait_for(self, text, seconds, what):
        """Waits until a line of Rolo's log contains `text`; returns that line."""
        until = time.time() + seconds
        while time.time() < until:
            for line in self.log():
                if text in line:
                    return line
            crash = self.crashed()
            if crash:
                self.fail("the app crashed while waiting for " + what + ": " + crash)
            time.sleep(1.0)
        self.fail("after " + str(seconds) + " seconds the log still had not said \"" + text + "\" (" + what + ")")

    def start(self):
        """Starts the app, and makes sure it did start: an emulator that has
        only just booted sometimes loses the request, or covers the app with
        a "not responding" note about one of its own programs."""
        for attempt in range(1, 5):
            print(adb("shell", "am", "start", "-W", "-n", self.package + "/" + ACTIVITY, *self.start_extras, check=False).strip())
            until = time.time() + 45
            while time.time() < until:
                if self.log():
                    return
                time.sleep(1.0)
            print("The app has not said anything yet (attempt " + str(attempt) + "). What Android shows:")
            print(adb("shell", "dumpsys", "activity", "activities", check=False, timeout=60)
                  .partition("ResumedActivity")[1:3][-1][:300].strip())
            # Tap away a "... isn't responding" note (its Wait button), if there is one.
            adb("shell", "input", "keyevent", "KEYCODE_DPAD_DOWN", check=False)
            adb("shell", "input", "keyevent", "KEYCODE_ENTER", check=False)
            adb("shell", "am", "force-stop", self.package, check=False)
            time.sleep(5.0)
        self.fail("the app did not start (it never wrote to the log)")

    # --- the screen ----------------------------------------------------------------------------
    def screen(self):
        """(pixels wide, pixels high, points shown wide, points shown high), from the game's log."""
        found = None
        for line in self.log():
            m = re.search(r"Screen: (\d+)x(\d+) pixels, showing (\d+)x(\d+) points", line)
            if m:
                found = tuple(int(g) for g in m.groups())
        return found

    def place(self, text):
        """A place in the game's units ("200,200", "-112,112", "50%,-79") as pixels for adb."""
        pixels_w, pixels_h, shown_w, shown_h = self.screen()

        def along(value, size):
            value = value.strip()
            if value.endswith("%"):
                return size * float(value[:-1]) / 100.0
            number = float(value)
            return size + number if value.startswith("-") else number

        x_text, y_text = text.split(",")
        x = along(x_text, shown_w) * pixels_w / shown_w
        y = pixels_h - along(y_text, shown_h) * pixels_h / shown_h   # Android counts from the top
        return str(int(round(x))), str(int(round(y)))

    def screenshot(self, file):
        png = adb("exec-out", "screencap", "-p", binary=True)
        with open(file, "wb") as out:
            out.write(png)
        print("Screenshot saved: " + file + " (" + str(len(png)) + " bytes)")
        if self.pictures_in_output:
            # For reading a screenshot out of a build machine's log when its
            # files cannot be fetched: the PNG as text (base64).
            print("-----BEGIN PNG " + file + "-----")
            text = base64.b64encode(png).decode("ascii")
            for start in range(0, len(text), 120):   # (build logs cut very long lines)
                print(text[start:start + 120])
            print("-----END PNG " + file + "-----")

    def colours_on_screen(self):
        """How many different colours a grid of points on the screen has."""
        raw = adb("exec-out", "screencap", binary=True)
        if len(raw) < 16:
            return 0
        width, height = struct.unpack_from("<II", raw, 0)
        header = len(raw) - width * height * 4   # 12 or 16 bytes, by Android version
        if width == 0 or height == 0 or header < 0:
            return 0
        colours = set()
        for row in range(1, 40):
            for column in range(1, 40):
                at = header + ((height * row // 40) * width + (width * column // 40)) * 4
                colours.add(raw[at:at + 3])
        return len(colours)

    # --- playing -------------------------------------------------------------------------------
    def play(self, step):
        step = step.strip()
        if not step:
            return
        print("  " + step)
        if step == "back":
            adb("shell", "input", "keyevent", "4")
        elif step == "home":
            adb("shell", "input", "keyevent", "3")
        elif step == "return":
            adb("shell", "am", "start", "-n", self.package + "/" + ACTIVITY)
        elif step.startswith("key:"):
            # A keyboard's key, pressed and let go (e.g. key:SPACE, key:ESCAPE).
            adb("shell", "input", "keyboard", "keyevent", "KEYCODE_" + step[4:])
        elif step.startswith("pad:"):
            # A gamepad's button, pressed and let go (e.g. pad:BUTTON_A, pad:DPAD_DOWN).
            adb("shell", "input", "gamepad", "keyevent", step[4:])
        elif step.startswith("stick:"):
            # A gamepad's left stick, moved to x,y (-1 to 1; Android's y points down).
            x, y = step[6:].split(",")
            adb("shell", "input", "joystick", "motionevent", "MOVE", x, y)
        elif step.startswith("shot:"):
            self.screenshot(step[5:])
            return
        elif step.startswith("wait:"):
            time.sleep(float(step[5:]))
            return
        elif ">" in step:
            start, end = step.split(">")
            # Slowly enough that the game sees the finger down for several frames.
            adb("shell", "input", "swipe", *self.place(start), *self.place(end), "700")
        else:
            # A "swipe" that stays in one place: a finger held for a moment.
            # (adb's "tap" lifts it at once, which a slow emulator can miss.)
            adb("shell", "input", "swipe", *self.place(step), *self.place(step), "350")
        time.sleep(0.8)


def main():
    parser = argparse.ArgumentParser(description="Plays Rolo's Android player and checks that it works.")
    parser.add_argument("apk")
    parser.add_argument("--package", default="dev.roloengine.player")
    parser.add_argument("--keep-installed", action="store_true",
                        help="do not install: start the app that is on the device (a second start)")
    parser.add_argument("--screenshot", help="save a screenshot of the first scene here (PNG)")
    parser.add_argument("--play", default="", help="what to do, as a player (see the top of this file)")
    parser.add_argument("--expect", action="append", default=[], help="text the game's log must contain")
    parser.add_argument("--reject", action="append", default=[], help="text the game's log must not contain")
    parser.add_argument("--expect-ended", action="store_true", help="playing must end the game and the app")
    parser.add_argument("--log", help="save Android's whole log here")
    parser.add_argument("--lose-context", action="store_true",
                        help="have Android take the graphics context away whenever the game is in the background")
    parser.add_argument("--pictures-in-output", action="store_true",
                        help="also print each screenshot as text (base64), to read it out of a build log")
    args = parser.parse_args()

    check = Check(args.package, args.log, args.pictures_in_output)
    if args.lose_context:
        # RoloActivity's switch for tests: the graphics context is thrown away in the background.
        check.start_extras = ["--ez", "rolo.loseContext", "true"]
    adb("wait-for-device")
    if not args.keep_installed:
        adb("uninstall", args.package, check=False)   # a clean start: nothing unpacked yet
        print(adb("install", args.apk, timeout=300).strip())
    # The first time an app fills the whole screen, Android covers it with a
    # note about how to leave full screen, which swallows touches until it
    # is tapped away. A test has nobody to do that: say it was seen.
    adb("shell", "settings", "put", "secure", "immersive_mode_confirmations", "confirmed", check=False)
    adb("shell", "am", "force-stop", args.package, check=False)
    adb("logcat", "-c", check=False)
    check.start()

    # The first start unpacks the game and compiles shaders; an emulator without
    # a graphics card is slow at it.
    check.wait_for("Showing scene", 240, "the first scene")
    check.wait_for("Screen: ", 60, "the screen's size")
    time.sleep(5.0)   # some frames of play
    if not check.running():
        check.fail("the app is not running any more")
    print("The game is running. Its screen: %dx%d pixels, showing %dx%d points of the world." % check.screen())

    colours = check.colours_on_screen()
    if args.screenshot:
        check.screenshot(args.screenshot)
    if colours < 8:
        check.fail("the screen is (nearly) one flat colour: nothing seems to be drawn (" + str(colours) + " colours)")
    print("The screen shows a picture (" + str(colours) + " different colours in a grid of points).")

    if args.play:
        print("Playing:")
        for step in args.play.split(";"):
            check.play(step)
            crash = check.crashed()
            if crash:
                check.fail("the app crashed after '" + step.strip() + "': " + crash)

    if args.expect_ended:
        check.wait_for("The game has ended", 30, "the game ending")
        until = time.time() + 20
        while check.running() and time.time() < until:
            time.sleep(1.0)
        if check.running():
            check.fail("the game ended but the app is still running")
        print("The game ended and the app closed.")
    elif not check.running():
        check.fail("the app stopped running")

    lines = check.log()
    for text in args.expect:
        if not any(text in line for line in lines):
            check.fail("the log never said \"" + text + "\"")
    for text in args.reject:
        if any(text in line for line in lines):
            check.fail("the log said \"" + text + "\", which it should not have")
    errors = [line for line in lines if line.startswith("E/")]
    if errors:
        check.fail("the game logged an error: " + errors[0])
    if any("Could not load the image" in line for line in lines):
        check.fail("an image could not be loaded")

    check.save_log()
    print("--- the game's log ---")
    for line in lines:
        print(line)
    print("OK: the game was played on Android (" + str(len(lines)) + " log lines).")


if __name__ == "__main__":
    main()
