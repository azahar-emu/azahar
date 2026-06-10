// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model.view

import androidx.annotation.StringRes
import org.citra.citra_emu.R
import org.citra.citra_emu.features.settings.model.AbstractSetting
import org.citra.citra_emu.features.settings.model.AbstractStringSetting

class StringInputSetting(
    setting: AbstractSetting?,
    titleId: Int,
    descriptionId: Int,
    val defaultValue: String,
    val characterLimit: Int = 0,
    override var isEnabled: Boolean = true,
    @StringRes override var disabledMessage: Int =
        R.string.setting_disabled_description_incompatible_setting
) : SettingsItem(setting, titleId, descriptionId) {
    override val type = TYPE_STRING_INPUT

    val selectedValue: String
        get() = setting?.valueAsString ?: defaultValue

    fun setSelectedValue(selection: String): AbstractStringSetting {
        val stringSetting = setting as AbstractStringSetting
        stringSetting.string = selection
        return stringSetting
    }
}
