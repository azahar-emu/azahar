// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model

enum class StringListSetting(
    override val key: String,
    override val section: String,
    override val defaultValue: List<String>,
    val canBeEmpty: Boolean = true
) : AbstractListSetting<String> {
    COMBO_KEYS("combo_keys", Settings.SECTION_CONTROLS, listOf("A", "B", "X", "Y", "L", "R", "ZL", "ZR", "Start", "Select"));

    private var backingList: List<String> = defaultValue
    private var lastValidList : List<String> = defaultValue

    override var list: List<String>
        get() = backingList
        set(value) {
            if (!canBeEmpty && value.isEmpty()) {
                backingList = lastValidList
            } else {
                backingList = value
                lastValidList = value
            }
        }

    override val valueAsString: String
        get() = list.joinToString()


    override val isRuntimeEditable: Boolean
        get() {
            for (setting in NOT_RUNTIME_EDITABLE) {
                if (setting == this) {
                    return false
                }
            }
            return true
        }

    companion object {
        private val NOT_RUNTIME_EDITABLE:List<StringListSetting> = listOf();


        fun from(key: String): StringListSetting? =
            values().firstOrNull { it.key == key }

        fun clear() = values().forEach { it.list = it.defaultValue }
    }
}