// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model

import androidx.lifecycle.ViewModel

class SettingsViewModel : ViewModel() {
    /** represents the settings being edit, NOT the settings in-game if game is running */
    val settings = Settings()
}
