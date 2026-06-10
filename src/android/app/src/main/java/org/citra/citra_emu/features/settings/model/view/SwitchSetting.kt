// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model.view

import androidx.annotation.StringRes
import org.citra.citra_emu.R
import org.citra.citra_emu.features.settings.model.AbstractBooleanSetting

class SwitchSetting(
    setting: AbstractBooleanSetting,
    titleId: Int,
    descriptionId: Int,
    val key: String? = null,
    val defaultValue: Boolean = false,
    override var isEnabled: Boolean = true,
    @StringRes override var disabledMessage: Int =
        R.string.setting_disabled_description_incompatible_setting
) : SettingsItem(setting, titleId, descriptionId) {
    override val type = TYPE_SWITCH

    val isChecked: Boolean
        get() {
            if (setting == null) {
                return defaultValue
            }
            val setting = setting as AbstractBooleanSetting
            return setting.boolean
        }

    /**
     * Write a value to the backing boolean.
     *
     * @param checked Pretty self explanatory.
     * @return the existing setting with the new value applied.
     */
    fun setChecked(checked: Boolean): AbstractBooleanSetting {
        val setting = setting as AbstractBooleanSetting
        setting.boolean = checked
        return setting
    }
}
