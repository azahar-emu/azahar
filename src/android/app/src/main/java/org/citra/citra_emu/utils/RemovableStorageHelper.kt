// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import org.citra.citra_emu.utils.BuildUtil
import java.io.File

object RemovableStorageHelper {
    // This really shouldn't be necessary, but the Android API seemingly
    // doesn't have a way of doing this?
    // Apparently, on certain devices the mount location can vary, so add
    // extra cases here if we discover any new ones.
    fun getRemovableStoragePath(idString: String): String? {
        BuildUtil.assertNotGooglePlay()

        var pathFile: File

        pathFile = File("/mnt/media_rw/$idString");
        if (pathFile.exists()) {
            return pathFile.absolutePath
        }

        return null
    }
}
