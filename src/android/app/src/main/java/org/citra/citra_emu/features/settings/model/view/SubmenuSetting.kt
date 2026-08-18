// Copyright 2023-2024 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model.view

import androidx.annotation.DrawableRes
import androidx.annotation.StringRes

class SubmenuSetting(
    @StringRes titleId: Int,
    @StringRes descriptionId: Int,
    @DrawableRes val iconId: Int,
    val menuKey: String
) : SettingsItem(null, titleId, descriptionId) {
    override val type = TYPE_SUBMENU
}
