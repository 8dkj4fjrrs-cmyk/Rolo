// Rolo Engine: the Android app's one screen ("activity").
//
// Axmol's AxmolActivity does the work: it makes the surface the game is
// drawn on, loads the game's library (librolo_player.so, named in
// AndroidManifest.xml) and passes touches, keys, frames and pauses to it.
// This class adds what Rolo wants around that.
package dev.roloengine.player;

import android.os.Build;
import android.os.Bundle;
import android.view.WindowManager;

import dev.axmol.lib.AxmolActivity;

public class RoloActivity extends AxmolActivity {
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
        // Draw under a camera notch too (Android 9 and newer): the game
        // fills the whole screen.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams params = getWindow().getAttributes();
            params.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(params);
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (mRunsTheGame) {
            // The game was quit (or its screen closed). Android would keep
            // the program in memory, half shut down, and start the next
            // game inside it; a game starts cleanly in a new one. Saved
            // games are on disk already: they are written when saved.
            System.exit(0);
        }
    }
}
