// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model

import androidx.lifecycle.ViewModel

class SettingsViewModel : ViewModel() {
   // the settings activity primarily manipulates its own copy of the settings object
    // syncing it with the active settings only when saving
    val settings = Settings()
}
