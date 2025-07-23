// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model

interface AbstractMultiBooleanSetting : AbstractSetting {
    var booleans: MutableSet<Boolean>
}
