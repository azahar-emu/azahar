// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model.view

import org.citra.citra_emu.features.settings.model.AbstractSetting
import org.citra.citra_emu.features.settings.model.FloatSetting
import org.citra.citra_emu.features.settings.model.Settings
import org.citra.citra_emu.utils.Log
import kotlin.math.pow
import kotlin.math.roundToInt

class SliderSetting(
    val settings: Settings,
    setting: AbstractSetting<*>?,
    titleId: Int,
    descriptionId: Int,
    val min: Int,
    val max: Int,
    val units: String,
    val key: String? = null,
    val defaultValue: Float? = null,
    val rounding: Int = 2,
    override var isEnabled: Boolean = true,
    private val getValue: (()->Float)? = null,
    private val setValue: ((Float)-> Unit)? = null
) : SettingsItem(setting, titleId, descriptionId) {
    override val type = TYPE_SLIDER
    val selectedFloat: Float
        get() {
            if (getValue != null) return getValue.invoke()
            val s = setting ?: return defaultValue!!

            val ret = when (s.defaultValue) {
                is Int -> {
                    @Suppress("UNCHECKED_CAST")
                    settings.get(s as AbstractSetting<Int>).toFloat()
                }

                is Float -> {
                    @Suppress("UNCHECKED_CAST")
                    settings.get(s as AbstractSetting<Float>)
                }

                else -> {
                    Log.error("[SliderSetting] Error casting setting type.")
                    -1f
                }
            }
            return ret.coerceIn(min.toFloat(), max.toFloat())
        }
    fun roundedFloat(value: Float): Float {
        val factor = 10f.pow(rounding)
        return (value * factor).roundToInt() / factor
    }

    val valueAsString: String
        get() = setting?.let {
            when (it.defaultValue) {
                is Int -> {
                    @Suppress("UNCHECKED_CAST")
                    settings.get(it as AbstractSetting<Int>).toString()
                }

                is Float -> {
                    @Suppress("UNCHECKED_CAST")
                    roundedFloat(settings.get(it as AbstractSetting<Float>)).toString()
                }

                else -> ""
            }
        } ?: defaultValue?.toString() ?: ""

    fun setSelectedValue(selection: Int): AbstractSetting<Int> {
        @Suppress("UNCHECKED_CAST")
        val intSetting = setting as AbstractSetting<Int>
        settings.set(intSetting, selection)
        return intSetting
    }

    /**
     * Write a value to the backing float. If that float was previously null,
     * initializes a new one and returns it, so it can be added to the Hashmap.
     *
     * @param selection New value of the float.
     */
    fun setSelectedValue(selection: Float) {
        if (setValue != null) {
            setValue(selection)
        }else {
            @Suppress("UNCHECKED_CAST")
            val floatSetting = setting as AbstractSetting<Float>
            settings.set(floatSetting, selection)
        }
    }
}
