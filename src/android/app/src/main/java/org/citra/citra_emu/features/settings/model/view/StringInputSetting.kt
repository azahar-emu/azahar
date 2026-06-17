// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model.view

import androidx.annotation.StringRes
import org.citra.citra_emu.R
import org.citra.citra_emu.features.settings.model.AbstractSetting
import org.citra.citra_emu.features.settings.model.Settings

class StringInputSetting(
    val settings: Settings,
    setting: AbstractSetting<String>?,
    titleId: Int,
    descriptionId: Int,
    val defaultValue: String,
    val characterLimit: Int = 0,
    override var isEnabled: Boolean = true,
    private val getValue: (() -> String)? = null,
    private val setValue: ((String) -> Unit)? = null,
    @StringRes override var disabledMessage: Int =
        R.string.setting_disabled_description_incompatible_setting
) : SettingsItem(setting, titleId, descriptionId) {
    override val type = TYPE_STRING_INPUT

    val selectedValue: String
        @Suppress("UNCHECKED_CAST")
        get() {
            if (getValue != null) return getValue.invoke()
            setting ?: return defaultValue
            return settings.get(setting as AbstractSetting<String>)
        }

    fun setSelectedValue(selection: String) {
        if (setValue != null) {
            setValue.invoke(selection)
        } else {
            @Suppress("UNCHECKED_CAST")
            val stringSetting = setting as AbstractSetting<String>
            settings.set(stringSetting, selection)
        }
    }
}
