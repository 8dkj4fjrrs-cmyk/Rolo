// Rolo Engine: the Android app's one screen ("activity").
//
// Axmol's AxmolActivity does the work: it makes the surface the game is
// drawn on, loads the game's library (librolo_player.so, named in
// AndroidManifest.xml) and passes touches, keys, frames and pauses to it.
// This class adds what Rolo wants around that.
package dev.roloengine.player;

import android.content.Context;
import android.hardware.input.InputManager;
import android.os.Build;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.WindowManager;

import dev.axmol.lib.AxmolActivity;

public class RoloActivity extends AxmolActivity implements InputManager.InputDeviceListener {
    // True for the activity that runs the game (see onCreate).
    private boolean mRunsTheGame = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Full screen: Android's navigation buttons are hidden while playing
        // (a swipe from the edge shows them).
        super.setEnableVirtualButton(false);
        super.onCreate(savedInstanceState);
        if (!isTaskRoot()) {
            // Android made a second copy of the activity for a game that is
            // already running; AxmolActivity has closed it again.
            return;
        }
        mRunsTheGame = true;
        // To hear when a game controller is unplugged (RoloGamepads).
        final InputManager devices = (InputManager) getSystemService(Context.INPUT_SERVICE);
        if (devices != null) {
            devices.registerInputDeviceListener(this, null);
        }
        // Draw under a camera notch too (Android 9 and newer): the game
        // fills the whole screen.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams params = getWindow().getAttributes();
            params.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(params);
        }
    }

    // Game controllers: their buttons and sticks go to RoloGamepads, where
    // the game reads them. Everything else goes on to Axmol as before
    // (touches, Android's Back, a keyboard's keys).
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        return RoloGamepads.onKey(event) || super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        return RoloGamepads.onMotion(event) || super.dispatchGenericMotionEvent(event);
    }

    @Override
    public void onInputDeviceAdded(int deviceId) {}   // a gamepad is noticed when it is first used

    @Override
    public void onInputDeviceChanged(int deviceId) {}

    @Override
    public void onInputDeviceRemoved(int deviceId) {
        RoloGamepads.onDeviceRemoved(deviceId);
    }

    @Override
    protected void onPause() {
        super.onPause();
        // In the background Android no longer tells the app that buttons
        // were let go, so none may stay "held".
        RoloGamepads.releaseAll();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        final InputManager devices = (InputManager) getSystemService(Context.INPUT_SERVICE);
        if (devices != null) {
            devices.unregisterInputDeviceListener(this);
        }
        if (mRunsTheGame) {
            // The game was quit (or its screen closed). Android would keep
            // the program in memory, half shut down, and start the next
            // game inside it; a game starts cleanly in a new one. Saved
            // games are on disk already: they are written when saved.
            System.exit(0);
        }
    }
}
