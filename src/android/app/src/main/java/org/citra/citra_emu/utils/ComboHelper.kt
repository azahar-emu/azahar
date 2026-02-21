// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.features.settings.model.Settings
import org.citra.citra_emu.overlay.InputOverlayDrawableButton

object ComboHelper {

    fun getButton(button: String): Int {
        when (button) {
            "A" -> return NativeLibrary.ButtonType.BUTTON_A
            "B" -> return NativeLibrary.ButtonType.BUTTON_B
            "X" -> return NativeLibrary.ButtonType.BUTTON_X
            "Y" -> return NativeLibrary.ButtonType.BUTTON_Y
            "L" -> return NativeLibrary.ButtonType.TRIGGER_L
            "R" -> return NativeLibrary.ButtonType.TRIGGER_R
            "ZL" -> return NativeLibrary.ButtonType.BUTTON_ZL
            "ZR" -> return NativeLibrary.ButtonType.BUTTON_ZR
            "START" -> return NativeLibrary.ButtonType.BUTTON_START
            "SELECT" -> return NativeLibrary.ButtonType.BUTTON_SELECT
        }
        return -1
    }

    fun comboActivate(button: InputOverlayDrawableButton) {
        var comboArray = Settings.comboSelection
        for (selectedbutton in comboArray) {
            var nativebutton = getButton(selectedbutton)
            if (nativebutton == -1)
            {
                println("Bad Button")
            }
            else
            {
                NativeLibrary.onGamePadEvent(NativeLibrary.TouchScreenDevice, nativebutton, button.status)
            }
        }
    }




}
