// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import org.citra.citra_emu.utils.BuildUtil
import java.io.File

object RemovableStorageHelper {
    // This really shouldn't be necessary, but the Android API seemingly
    // doesn't have a way of doing this?
    fun getRemovableStoragePath(idString: String): String? {
        BuildUtil.assertNotGooglePlay()

        // On certain Android flavours the external storage mount location can
        // vary, so add extra cases here if we discover them.
        val possibleMountPaths = listOf("/mnt/media_rw/$idString", "/storage/$idString")

        for (mountPath in possibleMountPaths) {
            val pathFile = File(mountPath);
            if (pathFile.exists()) {
                // TODO: Cache which mount location is being used for the remainder of the
                //       session, as it should never change. -OS
                return pathFile.absolutePath
            }
        }

        return null
    }
}
