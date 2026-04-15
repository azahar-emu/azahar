// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.features.settings.model.IntListSetting

object ComboHelper {
    fun comboActivate(buttonStatus: Int) {
        val comboArray = IntListSetting.COMBO_KEYS.list
        Log.info("Combo Array: $comboArray")
        for (nativeButton in comboArray) {
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
