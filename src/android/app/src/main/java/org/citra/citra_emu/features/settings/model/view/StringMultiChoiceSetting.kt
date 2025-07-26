// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model.view

import org.citra.citra_emu.features.settings.model.AbstractSetting
import org.citra.citra_emu.features.settings.model.AbstractMultiShortSetting
import org.citra.citra_emu.features.settings.model.AbstractMultiStringSetting

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
                val setting = setting as AbstractMultiStringSetting
                return setting.strings.toList()
            } catch (_: ClassCastException) {
            }

            try {
                val setting = setting as AbstractMultiShortSetting
                return setting.shorts.map { it.toString() }
            } catch (_: ClassCastException) {
            }
            return defaultValue!!
        }

    val selectValueIndices: BooleanArray
        get() {
            val noneList = values?.let {
                ArrayList(BooleanArray(it.size) { false }.toList())
            } ?: ArrayList()

            val chosenindices = mutableListOf<Boolean>()
            val selectedValues = selectedValues
            for (i in values!!.indices) {
                if (values[i] in selectedValues) {
                    chosenindices.add(true)
                } else {
                    chosenindices.add(false)
                }
            }
            if (chosenindices == null) {
                return noneList.toBooleanArray()
            } else {
                return chosenindices.toBooleanArray()
            }

        }

    /**
     * Add values to multi choice through the backing mutable sets.
     *
     * @param selection New value of the int.
     * @return the existing setting with the new value added.
     */
    fun addSelectedValue(selection: String): AbstractMultiStringSetting {
        val stringSetting = setting as AbstractMultiStringSetting
        stringSetting.strings.add(selection)
        return stringSetting
    }

    fun addSelectedValue(selection: Short): AbstractMultiShortSetting {
        val shortSetting = setting as AbstractMultiShortSetting
        shortSetting.shorts.add(selection)
        return shortSetting
    }

    fun removeSelectedValue(selection: String): AbstractMultiStringSetting {
        val stringSetting = setting as AbstractMultiStringSetting
        stringSetting.strings.remove(selection)
        return stringSetting
    }

    fun removeSelectedValue(selection: Short): AbstractMultiShortSetting {
        val shortSetting = setting as AbstractMultiShortSetting
        shortSetting.shorts.remove(selection)
        return shortSetting
    }
}