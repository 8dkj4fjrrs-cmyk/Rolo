// Rolo's stand-in for Google's expansion-file reader (see ../../../../../../README.md).
// A Rolo app has no expansion files: there is never one to open.
package com.android.vending.expansion.zipfile;

import android.content.Context;

import java.io.IOException;

public final class APKExpansionSupport {
    private APKExpansionSupport() {
    }

    /** The app's expansion file: always none. */
    public static ZipResourceFile getAPKExpansionZipFile(Context context, int mainVersion, int patchVersion)
            throws IOException {
        return null;
    }
}
