// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.features.settings.model.view
import androidx.annotation.StringRes
import org.citra.citra_emu.R
import org.citra.citra_emu.features.settings.model.AbstractSetting
import org.citra.citra_emu.features.settings.model.CitraSettings
import org.citra.citra_emu.features.settings.model.IntListSetting

class MultiChoiceSetting(
    val settings: CitraSettings,
    setting: AbstractSetting<List<Int>>?,
    titleId: Int,
    descriptionId: Int,
    val choicesId: Int,
    val valuesId: Int,
    val key: String? = null,
    val defaultValue: List<Int>? = null,
    override var isEnabled: Boolean = true,
    private val getValue: (() -> List<Int>)? = null,
    private val setValue: ((List<Int>) -> Unit)? = null,
    @StringRes override var disabledMessage: Int =
        R.string.setting_disabled_description_incompatible_setting
) : SettingsItem(setting, titleId, descriptionId) {
    override val type = TYPE_MULTI_CHOICE

    val selectedValues: List<Int>
        get() {
            if (getValue != null) {
                return getValue.invoke()
            }
            if (setting == null) {
                return defaultValue!!
            }
            try {
                return settings.get(setting as IntListSetting)
            } catch (_: ClassCastException) {
            }
            return defaultValue!!
        }

    /**
     * Write a value to the backing list. If that int was previously null,
     * initializes a new one and returns it, so it can be added to the Hashmap.
     *
     * @param selection New value of the int.
     */
    fun setSelectedValue(selection: List<Int>) {
        if (setValue != null) {
            setValue(selection)
        } else {
            val intSetting = setting as IntListSetting
            settings.setBySettingsContext(intSetting, selection)
        }
    }
}
