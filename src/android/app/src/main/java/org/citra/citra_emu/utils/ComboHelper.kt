// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.utils

import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.features.settings.model.CitraSettings
import org.citra.citra_emu.features.settings.model.IntListSetting

object ComboHelper {
    fun comboActivate(buttonStatus: Int) {
        val comboArray = CitraSettings.activeSettings.get(IntListSetting.COMBO_BUTTON_BUTTONS)
        for (nativeButton in comboArray) {
            if (nativeButton == -1) {
                // We don't want to parse any bad inputs here so we continue loop
                continue
            } else {
                NativeLibrary.onGamePadEvent(
                    NativeLibrary.TOUCHSCREEN_DEVICE,
                    nativeButton,
                    buttonStatus
                )
            }
        }
    }
}
