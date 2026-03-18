// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.features.settings.model.view

import android.view.InputDevice.MotionRange
import android.view.KeyEvent
import org.citra.citra_emu.features.input.Input
import org.citra.citra_emu.features.settings.model.CitraSettings
import org.citra.citra_emu.features.settings.model.InputMappingSetting

class InputBindingSetting(
    val inputSetting: InputMappingSetting,
    val settings: CitraSettings,
    titleId: Int
) : SettingsItem(inputSetting, titleId, 0) {
    val value: String
        get() {
            val mapping = settings.get(inputSetting) ?: return ""
            return inputSetting.displayValue(mapping)
        }

    fun removeOldMapping() {
        /* Default value is cleared */
        settings.setBySettingsContext(inputSetting, inputSetting.defaultValue)
    }

    /**
     * Saves the provided key input to the setting
     *
     * @param keyEvent KeyEvent of this key press.
     */
    fun onKeyInput(keyEvent: KeyEvent) {
        val code = translateEventToKeyId(keyEvent)
        val mapping = Input(key = code)
        settings.setBySettingsContext(inputSetting, mapping)
    }

    /**
     * Saves the provided motion input setting as an Android preference.
     *
     * @param motionRange MotionRange of the movement
     * @param axisDir     Either -1 or 1
     */
    fun onMotionInput(motionRange: MotionRange, axisDir: Int) {
        val mapping = Input(axis = motionRange.axis, direction = axisDir, threshold = 0.5f)
        settings.setBySettingsContext(inputSetting, mapping)
    }

    private fun translateEventToKeyId(event: KeyEvent): Int = if (event.keyCode == 0) {
        event.scanCode
    } else {
        event.keyCode
    }

    override val type = TYPE_INPUT_BINDING
}
