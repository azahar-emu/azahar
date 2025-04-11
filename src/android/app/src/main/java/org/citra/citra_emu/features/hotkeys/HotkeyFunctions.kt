// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.hotkeys

import android.widget.Toast
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.features.settings.model.Settings
import org.citra.citra_emu.features.settings.utils.SettingsFile


class HotkeyFunctions (
    private val settings: Settings
)  {
    private var normalSpeed = IntSetting.FRAME_LIMIT.int
    var isTurboSpeedEnabled = false

    // Turbo Speed
    fun setTurboSpeed(enabled: Boolean) {
        isTurboSpeedEnabled = enabled
        toggleTurboSpeed()
    }

    fun toggleTurboSpeed() {
        if (isTurboSpeedEnabled) {
            normalSpeed = IntSetting.FRAME_LIMIT.int
            IntSetting.FRAME_LIMIT.int = IntSetting.TURBO_SPEED.int
        } else {
            IntSetting.FRAME_LIMIT.int = normalSpeed
        }

        settings.saveSetting(IntSetting.FRAME_LIMIT, SettingsFile.FILE_NAME_CONFIG)
        NativeLibrary.reloadSettings()

        val context = CitraApplication.appContext
        Toast.makeText(context,
            "Changed Emulation Speed to: ${IntSetting.FRAME_LIMIT.int}%", Toast.LENGTH_SHORT).show()
    }

    fun resetTurboSpeed() {
        if (isTurboSpeedEnabled) {
            isTurboSpeedEnabled = false
            IntSetting.FRAME_LIMIT.int = normalSpeed

            settings.saveSetting(IntSetting.FRAME_LIMIT, SettingsFile.FILE_NAME_CONFIG)
            NativeLibrary.reloadSettings()
        }
    }
}