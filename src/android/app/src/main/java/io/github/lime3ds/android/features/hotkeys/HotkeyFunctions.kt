// Copyright Lime3DS Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package io.github.lime3ds.android.features.hotkeys

import android.widget.Toast
import io.github.lime3ds.android.LimeApplication
import io.github.lime3ds.android.NativeLibrary
import io.github.lime3ds.android.features.settings.model.IntSetting
import io.github.lime3ds.android.features.settings.model.Settings
import io.github.lime3ds.android.features.settings.utils.SettingsFile


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
            NativeLibrary.toggleTurboSpeed(true)
            NativeLibrary.setTurboSpeedSlider(IntSetting.TURBO_SPEED.int)
            IntSetting.FRAME_LIMIT.int = IntSetting.TURBO_SPEED.int
        } else {
            NativeLibrary.toggleTurboSpeed(false)
            NativeLibrary.setTurboSpeedSlider(normalSpeed)
            IntSetting.FRAME_LIMIT.int = normalSpeed
        }

        settings.saveSetting(IntSetting.FRAME_LIMIT, SettingsFile.FILE_NAME_CONFIG)
        NativeLibrary.reloadSettings()

        val context = LimeApplication.appContext
        Toast.makeText(context
            , "Changed Emulation Speed to: ${IntSetting.FRAME_LIMIT.int}%", Toast.LENGTH_SHORT).show()
    }

    fun resetTurboSpeed() {
        if (isTurboSpeedEnabled) {
            isTurboSpeedEnabled = false
            NativeLibrary.toggleTurboSpeed(false)
        IntSetting.FRAME_LIMIT.int = normalSpeed

        settings.saveSetting(IntSetting.FRAME_LIMIT, SettingsFile.FILE_NAME_CONFIG)
        NativeLibrary.reloadSettings()
        }
    }
}