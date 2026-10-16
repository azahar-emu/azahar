// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

package org.citra.citra_emu.features.settings.model

import androidx.lifecycle.ViewModel

class SettingsViewModel : ViewModel() {
    /** a local copy of settings that is used while editing, but not currently active */
    val editingSettings = Settings()
}
