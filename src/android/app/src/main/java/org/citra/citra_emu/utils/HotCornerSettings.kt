// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import android.content.res.Configuration
import androidx.preference.PreferenceManager
import org.citra.citra_emu.CitraApplication

/**
 * Stores per-orientation hot corner actions.
 * Defaults:
 *  - Portrait:  BL=TOGGLE_TURBO, BR=PAUSE_RESUME
 *  - Landscape: BL=PAUSE_RESUME, BR=NONE
 */
object HotCornerSettings {
    private val preferences =
        PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
    private const val KEY_RADIUS_DP = "HotCorner_radius_dp"

    enum class HotCornerPosition { BOTTOM_LEFT, BOTTOM_RIGHT }

    enum class HotCornerAction(val keySuffix: String) {
        NONE("none"),
        PAUSE_RESUME("pause_resume"),
        TOGGLE_TURBO("toggle_turbo"),
        QUICK_SAVE("quick_save"),
        QUICK_LOAD("quick_load"),
        OPEN_MENU("open_menu"),
        SWAP_SCREENS("swap_screens")
    }

    private fun key(orientation: Int, position: HotCornerPosition): String {
        val orient = if (orientation == Configuration.ORIENTATION_LANDSCAPE) "land" else "port"
        val pos = if (position == HotCornerPosition.BOTTOM_RIGHT) "br" else "bl"
        return "HotCorner_${orient}_${pos}_action"
    }

    fun getAction(orientation: Int, position: HotCornerPosition): HotCornerAction {
        val stored = preferences.getString(key(orientation, position), null)
        val defaultAction = HotCornerAction.NONE
        val value = stored ?: defaultAction.name
        return runCatching { HotCornerAction.valueOf(value) }.getOrElse { defaultAction }
    }

    fun setAction(orientation: Int, position: HotCornerPosition, action: HotCornerAction) {
        preferences.edit().putString(key(orientation, position), action.name).apply()
    }

    fun getRadiusDp(): Int {
        return preferences.getInt(KEY_RADIUS_DP, 72)
    }

    fun setRadiusDp(value: Int) {
        preferences.edit().putInt(KEY_RADIUS_DP, value).apply()
    }
}