// Copyright Citra Emulator Project / Lime3DS Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model.view

import org.citra.citra_emu.features.settings.model.AbstractFloatSetting
import org.citra.citra_emu.features.settings.model.AbstractIntSetting
import org.citra.citra_emu.features.settings.model.AbstractSetting
import org.citra.citra_emu.features.settings.model.FloatSetting
import org.citra.citra_emu.features.settings.model.ScaledFloatSetting
import org.citra.citra_emu.utils.Log
import kotlin.math.roundToInt

class SliderSetting(
    setting: AbstractSetting?,
    titleId: Int,
    descriptionId: Int,
    val min: Int,
    val max: Int,
    val units: String,
    val key: String? = null,
    val defaultValue: Float? = null,
    override val isEnabled: Boolean = true,
    override val requiredSettingName: String? = null
) : SettingsItem(setting, titleId, descriptionId) {
    override val type = TYPE_SLIDER
    val selectedFloat: Float
        get() {
            val setting = setting ?: return defaultValue!!.toFloat()
            return when (setting) {
                is AbstractIntSetting -> setting.int.toFloat()
                is FloatSetting -> setting.float
                is ScaledFloatSetting -> setting.float
                else -> {
                    Log.error("[SliderSetting] Error casting setting type.")
                    -1f
                }
            }
        }

    /**
     * Write a value to the backing int. If that int was previously null,
     * initializes a new one and returns it, so it can be added to the Hashmap.
     *
     * @param selection New value of the int.
     * @return the existing setting with the new value applied.
     */
    fun setSelectedValue(selection: Int): AbstractIntSetting {
        val intSetting = setting as AbstractIntSetting
        intSetting.int = selection
        return intSetting
    }

    /**
     * Write a value to the backing float. If that float was previously null,
     * initializes a new one and returns it, so it can be added to the Hashmap.
     *
     * @param selection New value of the float.
     * @return the existing setting with the new value applied.
     */
    fun setSelectedValue(selection: Float): AbstractFloatSetting {
        val floatSetting = setting as AbstractFloatSetting
        if (floatSetting is ScaledFloatSetting) {
            floatSetting.float = selection
        } else {
            floatSetting.float = selection
        }
        return floatSetting
    }
}
