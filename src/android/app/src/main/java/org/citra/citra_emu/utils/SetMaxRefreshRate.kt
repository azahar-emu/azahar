// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils
import android.app.Activity
import android.os.Build
import androidx.annotation.RequiresApi

object MaxRefreshRate {
    @RequiresApi(Build.VERSION_CODES.R)
    fun set(activity: Activity) {
        val display = activity.display
        val window = activity.window
        display?.let {
            val supportedModes = it.supportedModes
            val maxRefreshRate = supportedModes.maxByOrNull { mode -> mode.refreshRate }

            if (maxRefreshRate != null) {
                val layoutParams = window.attributes
                layoutParams.preferredDisplayModeId = maxRefreshRate.modeId
                window.attributes = layoutParams
            }
        }
    }
}