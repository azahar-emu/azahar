// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.features.settings.model.IntListSetting

object ComboHelper {
    fun comboActivate(buttonStatus: Int) {
        val comboArray = IntListSetting.COMBO_KEYS.list
        for (nativeButton in comboArray) {
            if (nativeButton == -1) {
                // We don't want to parse any bad inputs here so we continue loop
                continue
            } else {
                NativeLibrary.onGamePadEvent(NativeLibrary.TouchScreenDevice, nativeButton, buttonStatus)
            }
        }
    }
}
