// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import android.content.Context
import android.os.storage.StorageManager

object RemovableStorageHelper {

    private val pathCache = mutableMapOf<String, String?>()
    private var scanned = false

    private fun scanVolumes(context: Context) {
        if (scanned) return

        val storageManager = context.getSystemService(Context.STORAGE_SERVICE) as StorageManager

        for (volume in storageManager.storageVolumes) {
            if (!volume.isRemovable) continue

            val uuid = volume.uuid ?: continue
            val dir = volume.directory ?: continue

            pathCache[uuid.uppercase()] = dir.absolutePath
        }

        scanned = true
    }

    fun getRemovableStoragePath(context: Context, idString: String): String? {
        BuildUtil.assertNotGooglePlay()
        val key = idString.uppercase()

        if (!scanned) {
            scanVolumes(context)
        }

        return pathCache[key]
    }
}
