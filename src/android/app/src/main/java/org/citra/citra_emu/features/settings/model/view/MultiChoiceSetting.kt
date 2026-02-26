// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model.view

import org.citra.citra_emu.features.settings.model.AbstractMultiIntSetting
import org.citra.citra_emu.features.settings.model.AbstractSetting
import org.citra.citra_emu.features.settings.model.AbstractMultiShortSetting
import org.citra.citra_emu.features.settings.model.AbstractIntSetting
import org.citra.citra_emu.features.settings.model.AbstractShortSetting

class MultiChoiceSetting(
    setting: AbstractSetting?,
    titleId: Int,
    descriptionId: Int,
    val choicesId: Int,
    val valuesId: Int,
    val key: String? = null,
    val defaultValues: List<Int>? = null,
    override var isEnabled: Boolean = true
) : SettingsItem(setting, titleId, descriptionId) {
    override val type = TYPE_MULTI_CHOICE

    val selectedValues: List<Int>
        get() {
            if (setting == null) {
                return defaultValues!!
            }

            try {
                val setting = setting as AbstractMultiIntSetting
                return setting.ints.toList()
            } catch (_: ClassCastException) {
            }

            try {
                val setting = setting as AbstractMultiShortSetting
                return setting.shorts.map { it.toInt() }
            } catch (_: ClassCastException) {
            }

            return defaultValues!!
        }

    /**
     * Add values to multi choice backing mutable sets.
     *
     * @param selection New value of the int.
     * @return the existing setting with the new value added.
     */
    fun addSelectedValue(selection: Int): AbstractMultiIntSetting {
        val intSetting = setting as AbstractMultiIntSetting
        intSetting.ints.add(selection)
        return intSetting
    }

    fun addSelectedValue(selection: Short): AbstractMultiShortSetting {
        val shortSetting = setting as AbstractMultiShortSetting
        shortSetting.shorts.add(selection)
        return shortSetting
    }

    fun removeSelectedValue(selection: Int): AbstractMultiIntSetting {
        val intSetting = setting as AbstractMultiIntSetting
        intSetting.ints.remove(selection)
        return intSetting
    }

    fun removeSelectedValue(selection: Short): AbstractMultiShortSetting {
        val shortSetting = setting as AbstractMultiShortSetting
        shortSetting.shorts.remove(selection)
        return shortSetting
    }
}
