// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.utils

import androidx.drawerlayout.widget.DrawerLayout
import androidx.preference.PreferenceManager
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.CitraSettings
import org.citra.citra_emu.features.settings.model.IntSetting

object EmulationMenuSettings {
    private val preferences =
        PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
    private val activeSettings = CitraSettings.activeSettings
    var joystickRelCenter: Boolean
        get() = activeSettings.get(BooleanSetting.OVERLAY_RELATIVE_STICK_CENTER)
        set(value) {
            activeSettings.setByCurrentState(BooleanSetting.OVERLAY_RELATIVE_STICK_CENTER, value)
        }
    var dpadSlide: Boolean
        get() = activeSettings.get(BooleanSetting.OVERLAY_DPAD_SLIDING)
        set(value) {
            activeSettings.setByCurrentState(BooleanSetting.OVERLAY_DPAD_SLIDING, value)
        }
    var buttonSlide: Int
        get() = activeSettings.get(IntSetting.OVERLAY_BUTTON_SLIDING)
        set(value) {
            activeSettings.setByCurrentState(IntSetting.OVERLAY_BUTTON_SLIDING, value)
        }

    var hapticFeedback: Boolean
        get() = activeSettings.get(BooleanSetting.OVERLAY_HAPTIC_FEEDBACK)
        set(value) {
            activeSettings.setByCurrentState(BooleanSetting.OVERLAY_HAPTIC_FEEDBACK, value)
        }
    var swapScreens: Boolean
        get() = activeSettings.get(BooleanSetting.SWAP_SCREEN)
        set(value) {
            activeSettings.setByCurrentState(BooleanSetting.SWAP_SCREEN, value)
        }
    var showOverlay: Boolean
        get() = activeSettings.get(BooleanSetting.SHOW_CONTROLLER_OVERLAY)
        set(value) {
            activeSettings.setByCurrentState(BooleanSetting.SHOW_CONTROLLER_OVERLAY, value)
        }

    // TODO: Move this to a regular IntSetting?
    var drawerLockMode: Int
        get() = preferences.getInt(
            "EmulationMenuSettings_DrawerLockMode",
            DrawerLayout.LOCK_MODE_LOCKED_CLOSED
        )
        set(value) {
            preferences.edit()
                .putInt("EmulationMenuSettings_DrawerLockMode", value)
                .apply()
        }
}
