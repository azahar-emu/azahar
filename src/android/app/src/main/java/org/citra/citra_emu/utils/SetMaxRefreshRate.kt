// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils
import android.app.Activity
import android.os.Build
import androidx.annotation.RequiresApi

object MaxRefreshRate {
    //Since Android 15, google automatically forces "games" to be 60 hrz.
    // This functions sets the refresh rate to max supported rate, unless forced to 60 hrz.
    @RequiresApi(Build.VERSION_CODES.R)
    fun set(activity: Activity, forceSixtyHrz: Boolean) {
        val display = activity.display
        val window = activity.window
        display?.let {
            // Get all supported modes and find the one with the highest refresh rate
            val supportedModes = it.supportedModes
            val maxRefreshRate = supportedModes.maxByOrNull { mode -> mode.refreshRate }

            if (maxRefreshRate != null) {
                val layoutParams = window.attributes
                val modeId = if (forceSixtyHrz) {
                    supportedModes.firstOrNull { mode -> mode.refreshRate == 60f }?.modeId
                } else {
                    // Set the preferred display mode to the one with the highest refresh rate
                    maxRefreshRate.modeId
                }

                if (modeId != null) {
                    layoutParams.preferredDisplayModeId = modeId
                    window.attributes = layoutParams
                }
            }
        }
    }
}