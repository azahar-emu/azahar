// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.features.settings.model.IntListSetting

object ComboHelper {
    fun getButton(button: Int): Int {
        when (button) {
            0 -> return NativeLibrary.ButtonType.BUTTON_A
            1 -> return NativeLibrary.ButtonType.BUTTON_B
            2 -> return NativeLibrary.ButtonType.BUTTON_X
            3 -> return NativeLibrary.ButtonType.BUTTON_Y
            4 -> return NativeLibrary.ButtonType.TRIGGER_L
            5 -> return NativeLibrary.ButtonType.TRIGGER_R
            6 -> return NativeLibrary.ButtonType.BUTTON_ZL
            7 -> return NativeLibrary.ButtonType.BUTTON_ZR
            8 -> return NativeLibrary.ButtonType.BUTTON_START
            9 -> return NativeLibrary.ButtonType.BUTTON_SELECT
        }
        return -1
    }
    fun comboActivate(buttonStatus: Int) {
        val comboArray = IntListSetting.COMBO_KEYS.list
        Log.info("Combo Array: $comboArray")
        for (selectedButton in comboArray) {
            val nativeButton = getButton(selectedButton)
            Log.info("Native Button: $nativeButton")
            if (nativeButton == -1)
            {
                // We don't want to parse any bad inputs here so we continue loop
                continue
            }
            else
            {
                Log.debug("Handling combo button press: $nativeButton")
                NativeLibrary.onGamePadEvent(NativeLibrary.TouchScreenDevice, nativeButton, buttonStatus)
            }
        }
    }




}
