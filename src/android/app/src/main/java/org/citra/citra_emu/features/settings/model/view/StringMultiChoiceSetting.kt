// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model.view
import org.citra.citra_emu.features.settings.model.AbstractSetting
import org.citra.citra_emu.features.settings.model.StringListSetting
class StringMultiChoiceSetting(
    setting: AbstractSetting?,
    titleId: Int,
    descriptionId: Int,
    val choices: Array<String>,
    val values: Array<String>?,
    val key: String? = null,
    private val defaultValue: List<String>? = null,
    override var isEnabled: Boolean = true
) : SettingsItem(setting, titleId, descriptionId) {
    override val type = TYPE_STRING_MULTI_CHOICE

    fun getValueAt(index: Int): String? {
        if (values == null) return null
        return if (index >= 0 && index < values.size) {
            values[index]
        } else {
            ""
        }
    }
    val selectedValues: List<String>
        get() {
            if (setting == null) {
                return defaultValue!!
            }
            try {
                val setting = setting as StringListSetting
                return setting.list
            }catch (_: ClassCastException) {
            }
            return defaultValue!!
        }

    /**
     * Write a value to the backing list. If that list was previously null,
     * initializes a new one and returns it, so it can be added to the Hashmap.
     *
     * @param selection New value of the int.
     * @return the existing setting with the new value applied.
     */
    fun setSelectedValue(selection: List<String>): StringListSetting {
        val stringSetting = setting as StringListSetting
        stringSetting.list = selection
        return stringSetting
    }
}