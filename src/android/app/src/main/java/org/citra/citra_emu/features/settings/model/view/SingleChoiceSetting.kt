// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model.view

import org.citra.citra_emu.features.settings.model.AbstractSetting
import org.citra.citra_emu.features.settings.model.Settings

class SingleChoiceSetting(
    val settings: Settings,
    setting: AbstractSetting<*>?,
    titleId: Int,
    descriptionId: Int,
    val choicesId: Int,
    val valuesId: Int,
    val key: String? = null,
    val defaultValue: Int? = null,
    override var isEnabled: Boolean = true,
    private val getValue: (()->Int)? = null,
    private val setValue: ((Int)-> Unit)? = null
) : SettingsItem(setting, titleId, descriptionId) {
    override val type = TYPE_SINGLE_CHOICE

    val selectedValue: Int
        get() {
            if (getValue != null) {
                return getValue.invoke()
            }
            @Suppress("UNCHECKED_CAST")
            val s = (setting as? AbstractSetting<Int>) ?: return defaultValue!!
            return settings.get(s)
        }

    /**
     * Write a value to the backing int .
     * @param selection New value of the int.
     */
    fun setSelectedValue(selection: Int) {
        if (setValue != null) {
            setValue(selection)
        }else {
            @Suppress("UNCHECKED_CAST")
            val backSetting = setting as AbstractSetting<Int>
            settings.set(backSetting, selection)
        }
    }
}
