// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model.view

class HeaderSetting(titleId: Int,descId: Int = 0) : SettingsItem(null, titleId, descId) {
    override val type = TYPE_HEADER
}
