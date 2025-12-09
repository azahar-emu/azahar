// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils
import android.app.Activity
import android.os.Build
import androidx.annotation.RequiresApi

object RefreshRateUtil {
    // Since Android 15, the OS automatically runs apps categorized as games with a
    // 60hz refresh rate by default, regardless of the refresh rate set by the user.
    //
    // This function sets the refresh rate to either the maximum allowed refresh rate or
    // 60hz depending on the value of the `sixtyHz` parameter.
    //
    // Note: This isn't always the maximum refresh rate that the display is *capable of*,
    // but is instead the refresh rate chosen by the user in the Android system settings.
    // For example, if the user selected 120hz in the settings, but the display is capable
    // of 144hz, 120hz will be treated as the maximum within this function.
    @RequiresApi(Build.VERSION_CODES.R)
    fun enforceRefreshRate(activity: Activity, sixtyHz: Boolean = false) {
        val display = activity.display
        val window = activity.window

        display?.let {
            // Get all supported modes and find the one with the highest refresh rate
            val supportedModes = it.supportedModes
            val maxRefreshRate = supportedModes.maxByOrNull { mode -> mode.refreshRate }

            if (maxRefreshRate == null) {
                return
            }

            var newModeId: Int?
            if (sixtyHz) {
                newModeId = supportedModes.firstOrNull { mode -> mode.refreshRate == 60f }?.modeId
            } else {
                // Set the preferred display mode to the one with the highest refresh rate
                newModeId = maxRefreshRate.modeId
            }

            if (newModeId == null) {
                return
            }

            window.attributes.preferredDisplayModeId = newModeId
        }
    }
}