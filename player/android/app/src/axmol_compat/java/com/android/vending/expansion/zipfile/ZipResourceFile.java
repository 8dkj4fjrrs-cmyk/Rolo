// Rolo's stand-in for Google's expansion-file reader (see ../../../../../../README.md).
// Never made (APKExpansionSupport gives none); it exists for Axmol's code to compile.
package com.android.vending.expansion.zipfile;

import android.content.res.AssetFileDescriptor;

public final class ZipResourceFile {
    private ZipResourceFile() {
    }

    public AssetFileDescriptor getAssetFileDescriptor(String assetPath) {
        return null;
    }
}
