package org.citra.citra_emu.features.input

import org.citra.citra_emu.features.settings.model.InputMappingSetting
import org.citra.citra_emu.features.settings.model.Settings

class InputMappingManager() {

    /**
     * input keys are represented by a single int, and can be mapped to 3ds buttons or axis directions
     * input axes are represented by an <axis, direction> pair, where direction is either 1 or -1
     * and can be mapped to output axes or an output button
     * output keys are a single int
     * output axes are also a pair
     *
     * For now, we are only allowing one output *axis* per input, but the code is designed to change
     * that if necessary. There can be more than on output *button* per input because hotkeys are treated
     * as buttons and it is possible to map an input to both a 3ds button and a hotkey.
     */

    private val keyToOutButtons = HashMap<Int, MutableList<Int>>()
    private val keyToOutAxes = HashMap<Int, MutableList<Pair<Int, Int>>>()

    private val axisToOutAxes = HashMap<Pair<Int, Int>, MutableList<Pair<Int, Int>>>()
    private val axisToOutButtons = HashMap<Pair<Int, Int>, MutableList<Int>>()

    private val outAxisToMapping = HashMap<Pair<Int, Int>, Input>()
    private val buttonToMapping = HashMap<Int, Input>()


    /** Rebuilds the input maps from the given settings instance */
    fun rebuild(settings: Settings) {
        clear()
        InputMappingSetting.values().forEach { setting ->
            val mapping = settings.get(setting) ?: return@forEach
            register(setting, mapping)
        }
    }

    fun clear() {
        axisToOutButtons.clear()
        buttonToMapping.clear()
        axisToOutAxes.clear()
        outAxisToMapping.clear()
        keyToOutButtons.clear()
        keyToOutAxes.clear()
    }

    /** Rebind a particular setting */
    fun rebind(setting: InputMappingSetting, newMapping: Input?) {
        clearMapping(setting)
        if (newMapping != null) register(setting, newMapping)
    }

    /** Clear a mapping from all hashmaps */
    fun clearMapping(setting: InputMappingSetting) {
        val outPair = if (setting.outAxis != null && setting.outDirection != null) Pair(
            setting.outAxis,
            setting.outDirection
        ) else null

        val oldMapping = if (setting.outKey != null) {
            buttonToMapping.get(setting.outKey)
        } else if (setting.outAxis != null && setting.outDirection != null) {
            outAxisToMapping.get(Pair(setting.outAxis, setting.outDirection))
        } else {
            null
        }

        val oldPair = if (oldMapping?.axis != null && oldMapping?.direction != null) Pair(
            oldMapping.axis,
            oldMapping.direction
        ) else null

        // if our old mapping was a key, remove its binds
        if (oldMapping?.key != null) {
            keyToOutButtons[oldMapping.key]?.remove(setting.outKey)
            if (outPair != null) {
                keyToOutAxes[oldMapping.key]?.remove(outPair)
            }
        }
        // if our old mapping was an axis, remove its binds
        if (oldPair != null) {
            if (setting.outAxis != null && setting.outDirection != null)
                axisToOutAxes[oldPair]?.remove(outPair)
            if (setting.outKey != null)
                axisToOutButtons[oldPair]?.remove(setting.outKey)
        }

        // remove the reverse binds
        if (setting.outKey != null) {
            buttonToMapping.remove(setting.outKey)
        }
        if (outPair != null) {
            outAxisToMapping.remove(outPair)
        }
    }

    /**
     * Add a single item to the maps based on the value of the InputMapping and the InputMappingSetting
     */
    private fun register(setting: InputMappingSetting, mapping: Input) {
        val outPair = if (setting.outAxis != null && setting.outDirection != null) Pair(
            setting.outAxis,
            setting.outDirection
        ) else null

        val inPair = if (mapping.axis != null && mapping.direction != null) Pair(
            mapping.axis,
            mapping.direction
        ) else null

        if (setting.outKey != null) {
            if (mapping.key != null)
                keyToOutButtons.getOrPut(mapping.key, { mutableListOf() }).add(setting.outKey)
            if (inPair != null) {
                if (setting.outKey != null)
                    axisToOutButtons.getOrPut(inPair, { mutableListOf() }).add(setting.outKey)
            }
            buttonToMapping[setting.outKey] = mapping
        }
        if (outPair != null) {
            if (mapping.key != null)
                keyToOutAxes.getOrPut(mapping.key, { mutableListOf() })
                    .add(outPair)
            if (inPair != null)
                axisToOutAxes.getOrPut(inPair, { mutableListOf() })
                    .add(outPair)
            outAxisToMapping[outPair] = mapping
        }
    }

    fun getOutAxesForAxis(pair: Pair<Int, Int>): List<Pair<Int, Int>> =
        axisToOutAxes[pair] ?: emptyList()

    fun getOutButtonsForAxis(pair: Pair<Int, Int>): List<Int> =
        axisToOutButtons[pair] ?: emptyList()

    fun getMappingForOutAxis(pair: Pair<Int, Int>): Input? =
        outAxisToMapping[pair]

    fun getOutButtonsForKey(keyCode: Int): List<Int> =
        keyToOutButtons[keyCode] ?: emptyList()

    fun getOutAxesForKey(keyCode: Int): List<Pair<Int, Int>> =
        keyToOutAxes[keyCode] ?: emptyList()

    fun getMappingForButton(outKey: Int): Input? =
        buttonToMapping[outKey]
}