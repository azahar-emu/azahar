// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model.view

import org.citra.citra_emu.features.settings.model.AbstractSetting
import org.citra.citra_emu.features.settings.model.Settings

class SwitchSetting(
    val settings: Settings,
    setting: AbstractSetting<Boolean>?,
    titleId: Int,
    descriptionId: Int,
    val key: String? = null,
    val defaultValue: Boolean = false,
    override var isEnabled: Boolean = true,
    private val getValue: (() -> Boolean)? = null,
    private val setValue: ((Boolean) -> Unit)? = null
) : SettingsItem(setting, titleId, descriptionId) {
    override val type = TYPE_SWITCH

    val isChecked: Boolean
        get() {
            if (getValue != null) return getValue.invoke()
            if (setting == null) {
                return defaultValue
            }
            @Suppress("UNCHECKED_CAST")
            val setting = setting as AbstractSetting<Boolean>
            return settings.get(setting)
        }

    /**
     * Write a value to the backing boolean.
     *
     * @param checked Pretty self explanatory.
     */
    fun setChecked(checked: Boolean) {
        if (setValue != null) {
            setValue(checked)
        }else {
            @Suppress("UNCHECKED_CAST")
            val setting = setting as AbstractSetting<Boolean>
            settings.set(setting, checked)
        }
    }
}
