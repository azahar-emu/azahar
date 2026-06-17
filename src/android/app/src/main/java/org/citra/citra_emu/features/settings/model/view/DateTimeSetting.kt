// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model.view

import androidx.annotation.StringRes
import org.citra.citra_emu.R
import org.citra.citra_emu.features.settings.model.AbstractSetting
import org.citra.citra_emu.features.settings.model.Settings

class DateTimeSetting(
    private val settings: Settings,
    setting: AbstractSetting<String>?,
    titleId: Int,
    descriptionId: Int,
    val key: String? = null,
    private val defaultValue: String? = null,
    override var isEnabled: Boolean = true,
    private val getValue: (() -> String)? = null,
    private val setValue: ((String) -> Unit)? = null,
    @StringRes override var disabledMessage: Int =
        R.string.setting_disabled_description_incompatible_setting
) : SettingsItem(setting, titleId, descriptionId) {
    override val type = TYPE_DATETIME_SETTING

    @Suppress("UNCHECKED_CAST")
    val value: String
        get() = getValue?.invoke()
            ?: if (setting != null) {
                settings.get(setting as AbstractSetting<String>)
            } else {
                defaultValue!!
            }

    @Suppress("UNCHECKED_CAST")
    fun setSelectedValue(datetime: String) {
        if (setValue != null) {
            setValue(datetime)
        } else {
            val stringSetting = setting as AbstractSetting<String>
            settings.set(stringSetting, datetime)
        }
    }
}
