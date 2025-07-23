// Copyright 2023 Citra Emulator Project
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
     * Write a value to the backing int. If that int was previously null,
     * initializes a new one and returns it, so it can be added to the Hashmap.
     *
     * @param selection New value of the int.
     * @return the existing setting with the new value applied.
     */
    fun setSelectedValues(selection: String): AbstractMultiStringSetting {
        val stringSetting = setting as AbstractMultiStringSetting
        stringSetting.strings.add(selection)
        return stringSetting
    }

    fun setSelectedValues(selection: Short): AbstractMultiShortSetting {
        val shortSetting = setting as AbstractMultiShortSetting
        shortSetting.shorts.add(selection)
        return shortSetting
    }
}