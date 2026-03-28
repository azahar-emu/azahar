// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model

enum class IntListSetting(
    override val key: String,
    override val section: String,
    override val defaultValue: List<Int>,
    val canBeEmpty: Boolean = true
) : AbstractSetting<List<Int>> {

    LAYOUTS_TO_CYCLE("layouts_to_cycle", Settings.SECTION_LAYOUT, listOf(0, 1, 2, 3, 4, 5), canBeEmpty = false);

    override fun valueToString(value: List<Int>): String = value.joinToString()

    override fun valueFromString(string: String): List<Int>? {
        return string.split(",")
            .mapNotNull { it.trim().toIntOrNull() }
            .takeIf { canBeEmpty || it.isNotEmpty() }
    }
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
        private val NOT_RUNTIME_EDITABLE: List<IntListSetting> = emptyList()

        fun from(key: String): IntListSetting? =
            values().firstOrNull { it.key == key }
    }
}
