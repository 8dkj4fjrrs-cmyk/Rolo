// Rolo Engine: game controllers on Android.
//
// Android tells an app about controllers through events: "this button of
// that device went down", "these sticks moved". The game wants to ask once
// a frame "what is each gamepad doing now?" (rolo::GamepadReader). This
// class is the place in between: RoloActivity hands it the events as they
// come, it keeps what each gamepad is doing, and the game's side
// (engine/runtime/src/Gamepads_android.cpp) reads that every frame.
//
// The events arrive on Android's main thread and the game runs on another,
// so everything here is "synchronized": one at a time.
//
// Android gives controllers one layout (KeyEvent.KEYCODE_BUTTON_A is the
// bottom face button on every pad it knows), so there is no table of
// controller models here.
package dev.roloengine.player;

import android.os.Build;
import android.os.CombinedVibration;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

public final class RoloGamepads {
    // As in rolo::Input: up to four gamepads, 15 buttons and 6 axes each,
    // in the order of rolo::GamepadButton and rolo::GamepadAxis.
    static final int PADS = 4;
    static final int BUTTONS = 15;
    static final int AXES = 6;
    /** What read() writes for each gamepad: connected, the buttons, the axes. */
    static final int VALUES = 1 + BUTTONS + AXES;

    private static final int A = 0, B = 1, X = 2, Y = 3, LEFT_BUMPER = 4, RIGHT_BUMPER = 5, BACK = 6, START = 7,
            GUIDE = 8, LEFT_STICK = 9, RIGHT_STICK = 10, UP = 11, RIGHT = 12, DOWN = 13, LEFT = 14;
    private static final int NO_DEVICE = Integer.MIN_VALUE;

    private static final class Pad {
        int device = NO_DEVICE;
        String name = "";
        final boolean[] down = new boolean[BUTTONS];
        // A button pressed and let go between two frames would never be
        // seen; it is remembered here until the game has read it once.
        final boolean[] pressed = new boolean[BUTTONS];
        final float[] axes = new float[AXES];
        float hatX, hatY;   // the d-pad, on pads that report it as a stick

        void clear() {
            device = NO_DEVICE;
            name = "";
            java.util.Arrays.fill(down, false);
            java.util.Arrays.fill(pressed, false);
            java.util.Arrays.fill(axes, 0.0f);
            hatX = 0.0f;
            hatY = 0.0f;
        }
    }

    private static final Pad[] sPads = {new Pad(), new Pad(), new Pad(), new Pad()};

    private RoloGamepads() {}

    private static boolean isFrom(int source, int kind) {
        return (source & kind) == kind;
    }

    // Rolo's button for one of Android's key codes, or -1.
    private static int buttonOf(int keyCode) {
        switch (keyCode) {
            case KeyEvent.KEYCODE_BUTTON_A: return A;
            case KeyEvent.KEYCODE_BUTTON_B: return B;
            case KeyEvent.KEYCODE_BUTTON_X: return X;
            case KeyEvent.KEYCODE_BUTTON_Y: return Y;
            case KeyEvent.KEYCODE_BUTTON_L1: return LEFT_BUMPER;
            case KeyEvent.KEYCODE_BUTTON_R1: return RIGHT_BUMPER;
            case KeyEvent.KEYCODE_BUTTON_SELECT: return BACK;
            case KeyEvent.KEYCODE_BUTTON_START: return START;
            case KeyEvent.KEYCODE_BUTTON_MODE: return GUIDE;
            case KeyEvent.KEYCODE_BUTTON_THUMBL: return LEFT_STICK;
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return RIGHT_STICK;
            case KeyEvent.KEYCODE_DPAD_UP: return UP;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return RIGHT;
            case KeyEvent.KEYCODE_DPAD_DOWN: return DOWN;
            case KeyEvent.KEYCODE_DPAD_LEFT: return LEFT;
            default: return -1;
        }
    }

    // The gamepad a device is, giving it the first free number if it is new.
    private static Pad padOf(int device) {
        for (Pad pad : sPads) {
            if (pad.device == device) {
                return pad;
            }
        }
        for (Pad pad : sPads) {
            if (pad.device == NO_DEVICE) {
                pad.clear();
                pad.device = device;
                InputDevice info = InputDevice.getDevice(device);
                pad.name = info != null && info.getName() != null ? info.getName() : "Gamepad";
                return pad;
            }
        }
        return null;   // four gamepads already
    }

    /** A key event. True if it was a gamepad's button (and so is dealt with). */
    static synchronized boolean onKey(KeyEvent event) {
        if (!isFrom(event.getSource(), InputDevice.SOURCE_GAMEPAD) && !isFrom(event.getSource(), InputDevice.SOURCE_JOYSTICK)) {
            return false;   // a keyboard, a remote control, Android's own Back
        }
        final int button = buttonOf(event.getKeyCode());
        if (button < 0) {
            return false;
        }
        final Pad pad = padOf(event.getDeviceId());
        if (pad == null) {
            return true;
        }
        if (event.getAction() == KeyEvent.ACTION_DOWN) {
            pad.down[button] = true;
            pad.pressed[button] = true;
        } else if (event.getAction() == KeyEvent.ACTION_UP) {
            pad.down[button] = false;
        }
        return true;
    }

    /** A movement event. True if it was a gamepad's sticks or triggers. */
    static synchronized boolean onMotion(MotionEvent event) {
        if (!isFrom(event.getSource(), InputDevice.SOURCE_JOYSTICK) || event.getAction() != MotionEvent.ACTION_MOVE) {
            return false;
        }
        final Pad pad = padOf(event.getDeviceId());
        if (pad == null) {
            return true;
        }
        // Android's y axes point down; Rolo's, like its world, up.
        pad.axes[0] = event.getAxisValue(MotionEvent.AXIS_X);
        pad.axes[1] = -event.getAxisValue(MotionEvent.AXIS_Y);
        pad.axes[2] = event.getAxisValue(MotionEvent.AXIS_Z);
        pad.axes[3] = -event.getAxisValue(MotionEvent.AXIS_RZ);
        // Triggers: pads report them under one of two names each.
        pad.axes[4] = Math.max(event.getAxisValue(MotionEvent.AXIS_LTRIGGER), event.getAxisValue(MotionEvent.AXIS_BRAKE));
        pad.axes[5] = Math.max(event.getAxisValue(MotionEvent.AXIS_RTRIGGER), event.getAxisValue(MotionEvent.AXIS_GAS));
        pad.hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X);
        pad.hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y);
        return true;
    }

    /** A device was unplugged (or switched off). */
    static synchronized void onDeviceRemoved(int device) {
        for (Pad pad : sPads) {
            if (pad.device == device) {
                pad.clear();
            }
        }
    }

    /** The app went to the background: nothing is held down any more. */
    static synchronized void releaseAll() {
        for (Pad pad : sPads) {
            java.util.Arrays.fill(pad.down, false);
            java.util.Arrays.fill(pad.pressed, false);
            java.util.Arrays.fill(pad.axes, 0.0f);
            pad.hatX = 0.0f;
            pad.hatY = 0.0f;
        }
    }

    // --- Called by the game (Gamepads_android.cpp), once a frame ---------------------------

    /** Writes every gamepad's state into `out` (PADS x VALUES numbers; 1 is yes). */
    public static synchronized void read(float[] out) {
        if (out == null || out.length < PADS * VALUES) {
            return;
        }
        for (int p = 0; p < PADS; ++p) {
            final Pad pad = sPads[p];
            final int at = p * VALUES;
            out[at] = pad.device != NO_DEVICE ? 1.0f : 0.0f;
            for (int b = 0; b < BUTTONS; ++b) {
                out[at + 1 + b] = pad.down[b] || pad.pressed[b] ? 1.0f : 0.0f;
                pad.pressed[b] = false;
            }
            // A d-pad reported as a stick counts as the d-pad's buttons too.
            if (pad.hatY < -0.5f) { out[at + 1 + UP] = 1.0f; }
            if (pad.hatX > 0.5f) { out[at + 1 + RIGHT] = 1.0f; }
            if (pad.hatY > 0.5f) { out[at + 1 + DOWN] = 1.0f; }
            if (pad.hatX < -0.5f) { out[at + 1 + LEFT] = 1.0f; }
            for (int a = 0; a < AXES; ++a) {
                out[at + 1 + BUTTONS + a] = pad.axes[a];
            }
        }
    }

    /** The name of gamepad `index` (0 to 3) as Android knows it, e.g. "Xbox Wireless Controller". */
    public static synchronized String name(int index) {
        return index >= 0 && index < PADS ? sPads[index].name : "";
    }

    /**
     * Shakes gamepad `index`: `low` the heavy motor, `high` the light one, 0 to 1; both 0 stops.
     * False if that gamepad cannot rumble. The game stops a rumble itself when its time is up;
     * the length given to Android here is only a limit in case it never does.
     */
    public static synchronized boolean rumble(int index, float low, float high) {
        if (index < 0 || index >= PADS || sPads[index].device == NO_DEVICE) {
            return false;
        }
        try {
            final InputDevice device = InputDevice.getDevice(sPads[index].device);
            if (device == null) {
                return false;
            }
            final boolean stop = low <= 0.0f && high <= 0.0f;
            final long limit = 10000;   // milliseconds
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                // Android 12 and newer: each motor by itself.
                final VibratorManager motors = device.getVibratorManager();
                final int[] ids = motors.getVibratorIds();
                if (ids.length == 0) {
                    return false;
                }
                if (stop) {
                    motors.cancel();
                    return true;
                }
                if (ids.length >= 2) {
                    final CombinedVibration.ParallelCombination both = CombinedVibration.startParallel();
                    boolean any = false;
                    if (low > 0.0f) {
                        both.addVibrator(ids[0], VibrationEffect.createOneShot(limit, amplitude(low)));
                        any = true;
                    }
                    if (high > 0.0f) {
                        both.addVibrator(ids[1], VibrationEffect.createOneShot(limit, amplitude(high)));
                        any = true;
                    }
                    if (any) {
                        motors.vibrate(both.combine());
                    }
                } else {
                    motors.vibrate(CombinedVibration.createParallel(
                            VibrationEffect.createOneShot(limit, amplitude(Math.max(low, high)))));
                }
                return true;
            }
            final Vibrator motor = device.getVibrator();
            if (motor == null || !motor.hasVibrator()) {
                return false;
            }
            if (stop) {
                motor.cancel();
            } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                motor.vibrate(VibrationEffect.createOneShot(limit, amplitude(Math.max(low, high))));
            } else {
                motor.vibrate(limit);
            }
            return true;
        } catch (Throwable problem) {
            // (A controller unplugged this moment, or an Android that will not let this app shake it.)
            return false;
        }
    }

    // A strength from 0 to 1 as Android's 1 to 255.
    private static int amplitude(float strength) {
        return Math.max(1, Math.min(255, Math.round(strength * 255.0f)));
    }
}
