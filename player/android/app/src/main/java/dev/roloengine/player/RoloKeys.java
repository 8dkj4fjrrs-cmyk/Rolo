// Rolo Engine: a keyboard on Android (a Chromebook's, a tablet's, one
// plugged into a phone).
//
// Axmol's own Android code only passes on a handful of keys (Back, Enter,
// the arrows). RoloActivity hands every keyboard key here instead; they are
// kept in order, and the game takes them once a frame
// (rolo::android::takeKeys, engine/runtime/src/Android.cpp), which also
// knows which of Rolo's keys each of Android's is.
//
// Events arrive on Android's main thread and the game runs on another, so
// everything here is "synchronized": one at a time.
package dev.roloengine.player;

import android.view.InputDevice;
import android.view.KeyEvent;

import java.util.ArrayList;
import java.util.HashSet;

public final class RoloKeys {
    // What happened since the game last asked: a key's code, then 1 (down) or 0 (up).
    private static final ArrayList<Integer> sEvents = new ArrayList<>();
    private static final HashSet<Integer> sDown = new HashSet<>();
    private static final int MOST = 512;   // if the game never asks (it is loading), the oldest are dropped

    private RoloKeys() {}

    /** A key event. True if it was a keyboard's key (and so is dealt with). */
    static synchronized boolean onKey(KeyEvent event) {
        final int code = event.getKeyCode();
        if ((event.getSource() & InputDevice.SOURCE_KEYBOARD) != InputDevice.SOURCE_KEYBOARD) {
            return false;
        }
        // Not a keyboard's keys, though they come the same way: Android's
        // own buttons. Back goes on to Axmol (it is the game's Escape); the
        // others are Android's business.
        switch (code) {
            case KeyEvent.KEYCODE_BACK:
            case KeyEvent.KEYCODE_HOME:
            case KeyEvent.KEYCODE_MENU:
            case KeyEvent.KEYCODE_APP_SWITCH:
            case KeyEvent.KEYCODE_POWER:
            case KeyEvent.KEYCODE_VOLUME_UP:
            case KeyEvent.KEYCODE_VOLUME_DOWN:
            case KeyEvent.KEYCODE_VOLUME_MUTE:
            case KeyEvent.KEYCODE_CAMERA:
                return false;
            default:
                break;
        }
        if (event.getAction() == KeyEvent.ACTION_DOWN) {
            if (sDown.add(code)) {   // (a key held down repeats: only its first "down" counts)
                add(code, 1);
            }
        } else if (event.getAction() == KeyEvent.ACTION_UP) {
            sDown.remove(code);
            add(code, 0);
        } else {
            return false;
        }
        return true;
    }

    private static void add(int code, int down) {
        if (sEvents.size() >= MOST) {
            sEvents.remove(0);
            sEvents.remove(0);
        }
        sEvents.add(code);
        sEvents.add(down);
    }

    /** The app went to the background: every key held counts as let go. */
    static synchronized void releaseAll() {
        for (int code : sDown) {
            add(code, 0);
        }
        sDown.clear();
    }

    /** Called by the game once a frame: the events since last time (code, down, code, down...), or null. */
    public static synchronized int[] take() {
        if (sEvents.isEmpty()) {
            return null;
        }
        final int[] out = new int[sEvents.size()];
        for (int i = 0; i < out.length; ++i) {
            out[i] = sEvents.get(i);
        }
        sEvents.clear();
        return out;
    }
}
