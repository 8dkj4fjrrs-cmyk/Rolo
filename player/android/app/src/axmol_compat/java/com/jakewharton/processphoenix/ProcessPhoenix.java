// Rolo's stand-in for ProcessPhoenix (see ../../../../README.md).
// Axmol would ask it to restart the app after losing its graphics context,
// with a setting Rolo does not use; here that request does nothing.
package com.jakewharton.processphoenix;

import android.content.Context;

public final class ProcessPhoenix {
    private ProcessPhoenix() {
    }

    public static void triggerRebirth(Context context) {
    }
}
