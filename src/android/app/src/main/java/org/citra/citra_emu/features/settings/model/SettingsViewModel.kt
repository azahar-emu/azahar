// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model

import androidx.lifecycle.ViewModel

class SettingsViewModel : ViewModel() {
    // the Settings Activity will always work with a local copy of settings while
    // editing, to avoid issues with conflicting active/edited settings
    val settings = Settings()
}
