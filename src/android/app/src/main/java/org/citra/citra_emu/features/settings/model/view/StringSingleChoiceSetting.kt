// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model.view

import org.citra.citra_emu.features.settings.model.AbstractSetting
import org.citra.citra_emu.features.settings.model.Settings

class StringSingleChoiceSetting(
    val settings: Settings,
    setting: AbstractSetting<String>?,
    titleId: Int,
    descriptionId: Int,
    val choices: Array<String>,
    val values: Array<String>?,
    val key: String? = null,
    private val defaultValue: String? = null,
    override var isEnabled: Boolean = true,
    private val getValue: (()->String)? = null,
    private val setValue: ((String)-> Unit)? = null
) : SettingsItem(setting, titleId, descriptionId) {
    override val type = TYPE_STRING_SINGLE_CHOICE

    fun getValueAt(index: Int): String? {
        if (values == null) return null
        return if (index >= 0 && index < values.size) {
            values[index]
        } else {
            ""
        }
    }

    val selectedValue: String
        get() {
            if (getValue != null) return getValue.invoke()
            if (setting == null) {
                return defaultValue!!
            }
            @Suppress("UNCHECKED_CAST")
            return settings.get(setting as AbstractSetting<String>)
        }
    val selectValueIndex: Int
        get() {
            val selectedValue = selectedValue
            for (i in values!!.indices) {
                if (values[i] == selectedValue) {
                    return i
                }
            }
            return -1
        }

    /**
     * Write a value to the backing int. If that int was previously null,
     * initializes a new one and returns it, so it can be added to the Hashmap.
     *
     * @param selection New value of the int.
     */
    fun setSelectedValue(selection: String) {
        if (setValue != null) {
            setValue(selection)
        }else {
            @Suppress("UNCHECKED_CAST")
            val stringSetting = setting as AbstractSetting<String>
            settings.set(stringSetting, selection)
        }
    }
}
