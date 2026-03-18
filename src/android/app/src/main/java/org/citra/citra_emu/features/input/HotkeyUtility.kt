// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.input

import android.content.Context
import android.view.KeyEvent
import android.widget.Toast
import androidx.preference.PreferenceManager
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.utils.EmulationLifecycleUtil
import org.citra.citra_emu.utils.TurboHelper
import org.citra.citra_emu.display.ScreenAdjustmentUtil
import org.citra.citra_emu.features.settings.model.view.InputBindingSetting
import org.citra.citra_emu.features.settings.model.Settings
import kotlin.math.abs

class HotkeyUtility(
    private val screenAdjustmentUtil: ScreenAdjustmentUtil,
    private val context: Context,
    private val settings: Settings
) {

    private val hotkeyButtons = Hotkey.entries.map { it.button }
    private var hotkeyIsEnabled = false
    var hotkeyIsPressed = false
    private val currentlyPressedButtons = mutableSetOf<Int>()
    /** Store which axis directions are currently pressed as (axis, direction) pairs. */
    private val pressedAxisDirections = HashSet<Pair<Int, Int>>() // (outAxis, outDir)

    fun handleKeyPress(key: Int, descriptor: String): Boolean {
        var handled = false
        val buttonSet = settings.inputMappingManager.getOutButtonsForKey(key)
        val axisSet = settings.inputMappingManager.getOutAxesForKey(key)
        val enableButtonMapped = settings.inputMappingManager.getMappingForButton(Hotkey.ENABLE.button) != null
        val thisKeyIsEnableButton = buttonSet.contains(Hotkey.ENABLE.button)
        val thisKeyIsHotkey =
            !thisKeyIsEnableButton && Hotkey.entries.any { buttonSet.contains(it.button) }
        hotkeyIsEnabled = hotkeyIsEnabled || !enableButtonMapped || thisKeyIsEnableButton

        // Now process all internal buttons associated with this keypress
        for (button in buttonSet) {
            currentlyPressedButtons.add(button)
            //option 1 - this is the enable command, which was already handled
            if (button == Hotkey.ENABLE.button) {
                handled = true
            }
            // option 2 - this is a different hotkey command
            else if (hotkeyButtons.contains(button)) {
                if (hotkeyIsEnabled) {
                    handled = handleHotkey(button) || handled
                }
            }
            // option 3 - this is a normal key
            else {
                // if this key press is ALSO associated with a hotkey that will process, skip
                // the normal key event.
                if (!thisKeyIsHotkey || !hotkeyIsEnabled) {
                    handled = NativeLibrary.onGamePadEvent(
                        descriptor,
                        button,
                        NativeLibrary.ButtonState.PRESSED
                    ) || handled
                }
            }
        }
        // Handle axes in helper functions
        updateAxisStateForKey(axisSet,true)
        handled = sendAxisState(descriptor, axisSet) || handled
        return handled
    }

    fun handleKeyRelease(key: Int, descriptor: String): Boolean {
        var handled = false
        val buttonSet = settings.inputMappingManager.getOutButtonsForKey(key)
        val axisSet = settings.inputMappingManager.getOutAxesForKey(key)
        val thisKeyIsEnableButton = buttonSet.contains(Hotkey.ENABLE.button)
        val thisKeyIsHotkey =
            !thisKeyIsEnableButton && Hotkey.entries.any { buttonSet.contains(it.button) }
        if (thisKeyIsEnableButton) {
            handled = true; hotkeyIsEnabled = false
        }

        for (button in buttonSet) {
            // this is a hotkey button
            if (hotkeyButtons.contains(button)) {
                currentlyPressedButtons.remove(button)
                if (!currentlyPressedButtons.any { hotkeyButtons.contains(it) }) {
                    // all hotkeys are no longer pressed
                    hotkeyIsPressed = false
                }
            } else {
                // if this key ALSO sends a hotkey command that we already/will handle,
                // or if we did not register the press of this button, e.g. if this key
                // was also a hotkey pressed after enable, but released after enable button release, then
                // skip the normal key event
                if ((!thisKeyIsHotkey || !hotkeyIsEnabled) && currentlyPressedButtons.contains(
                        button
                    )
                ) {
                    handled = NativeLibrary.onGamePadEvent(
                        descriptor,
                        button,
                        NativeLibrary.ButtonState.RELEASED
                    ) || handled
                    currentlyPressedButtons.remove(button)
                }
            }
        }
        updateAxisStateForKey(axisSet,false)
        handled = sendAxisState(descriptor, axisSet) || handled
        return handled
    }

    fun handleHotkey(bindedButton: Int): Boolean {
        when (bindedButton) {
            Hotkey.SWAP_SCREEN.button -> screenAdjustmentUtil.swapScreen()
            Hotkey.CYCLE_LAYOUT.button -> screenAdjustmentUtil.cycleLayouts()
            Hotkey.CLOSE_GAME.button -> EmulationLifecycleUtil.closeGame()
            Hotkey.PAUSE_OR_RESUME.button -> EmulationLifecycleUtil.pauseOrResume()
            Hotkey.TURBO_LIMIT.button -> TurboHelper.toggleTurbo(true, settings)
            Hotkey.QUICKSAVE.button -> {
                NativeLibrary.saveState(NativeLibrary.QUICKSAVE_SLOT)
                Toast.makeText(
                    context,
                    context.getString(R.string.saving),
                    Toast.LENGTH_SHORT
                ).show()
            }

            Hotkey.QUICKLOAD.button -> {
                val wasLoaded = NativeLibrary.loadStateIfAvailable(NativeLibrary.QUICKSAVE_SLOT)
                val stringRes = if (wasLoaded) {
                    R.string.loading
                } else {
                    R.string.quickload_not_found
                }
                Toast.makeText(
                    context,
                    context.getString(stringRes),
                    Toast.LENGTH_SHORT
                ).show()
            }

            else -> {}
        }
        hotkeyIsPressed = true
        return true
    }

    private fun updateAxisStateForKey(axisSet: List<Pair<Int, Int>>, pressed: Boolean) {
        axisSet.forEach { (outAxis, outDir) ->
            if (pressed) pressedAxisDirections.add(Pair(outAxis, outDir))
            else pressedAxisDirections.remove(Pair(outAxis, outDir))
        }
    }

    /**
     * Update axis state based on currently pressed buttons
     */
    private fun sendAxisState(descriptor: String, affectedAxes: List<Pair<Int, Int>>): Boolean {
        val stickAccumulator = HashMap<Int, Pair<Float, Float>>()
        // to make sure that when both directions are released, we still get a return to 0, but only for
        // axes that have been touched by buttons
        affectedAxes.forEach { (outAxis, outDir) ->
            val component = GamepadHelper.getJoystickComponent(outAxis) ?: return@forEach
            stickAccumulator.putIfAbsent(component.joystickType, Pair(0f,0f))
        }

        pressedAxisDirections.forEach{ (outAxis, outDir) ->
            val component = GamepadHelper.getJoystickComponent(outAxis) ?: return@forEach
            val current =
                stickAccumulator.getOrDefault(component.joystickType, Pair(0f, 0f))
            // if opposite directions of the same stick are held, this will let them cancel each other out
            stickAccumulator[component.joystickType] = if (component.isVertical) {
                Pair(current.first, current.second + outDir)
            } else {
                Pair(current.first + outDir, current.second)
            }
        }
        var handled = false
        stickAccumulator.forEach { (joystickType, value) ->
            handled = NativeLibrary.onGamePadMoveEvent(descriptor, joystickType, value.first, value.second) || handled
        }
        return handled

    }
}
