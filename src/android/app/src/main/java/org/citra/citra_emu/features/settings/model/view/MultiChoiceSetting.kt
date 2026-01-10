// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model.view
import org.citra.citra_emu.features.settings.model.AbstractSetting
import org.citra.citra_emu.features.settings.model.IntListSetting
class MultiChoiceSetting(
    setting: AbstractSetting?,
    titleId: Int,
    descriptionId: Int,
    val choicesId: Int,
    val valuesId: Int,
    val key: String? = null,
    val defaultValue: List<Int>? = null,
    override var isEnabled: Boolean = true
) : SettingsItem(setting, titleId, descriptionId) {
    override val type = TYPE_MULTI_CHOICE

    val selectedValues: List<Int>
        get() {
            if (setting == null) {
                return defaultValue!!
            }
            try {
                val setting = setting as IntListSetting
                return setting.list
            }catch (_: ClassCastException) {
            }
            return defaultValue!!
        }

    /**
     * Write a value to the backing list. If that int was previously null,
     * initializes a new one and returns it, so it can be added to the Hashmap.
     *
     * @param selection New value of the int.
     * @return the existing setting with the new value applied.
     */
    fun setSelectedValue(selection: List<Int>): IntListSetting {
        val intSetting = setting as IntListSetting
        intSetting.list = selection
        return intSetting
    }

}
