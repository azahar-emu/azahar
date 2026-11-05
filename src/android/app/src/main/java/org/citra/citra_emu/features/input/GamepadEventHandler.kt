// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.features.input

import android.content.Context
import android.view.KeyEvent
import android.view.MotionEvent
import android.widget.Toast
import kotlin.math.abs
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.display.ScreenAdjustmentUtil
import org.citra.citra_emu.utils.ComboHelper
import org.citra.citra_emu.utils.EmulationLifecycleUtil
import org.citra.citra_emu.utils.Log
import org.citra.citra_emu.utils.TurboHelper

class GamepadEventHandler(
    private val screenAdjustmentUtil: ScreenAdjustmentUtil,
    private val context: Context
) {

    private val hotkeyButtons = Hotkey.entries.map { it.button }
    private var hotkeyIsEnabled = false
    var hotkeyIsPressed = false
        private set
    private val currentlyPressedButtons = mutableSetOf<Int>()

    /** Store which axes (outAxis, outDir) are being activated by key binds */
    private val heldKeyAxes = HashSet<Pair<Int, Int>>()

    /** Buttons that are currently being pressed by axes. This stores the (inAxis, inDir) pair. */
    private val heldAxisButtons = HashSet<Pair<Int, Int>>()

    /** Handles the press of an input key */
    fun handleKeyPress(event: KeyEvent): Boolean {
        val key = if (event.keyCode == 0) event.scanCode else event.keyCode
        val descriptor = event.device.descriptor
        val axes = InputMappingManager.getOutAxesForKey(key)
        val buttons = InputMappingManager.getOutButtonsForKey(key)
        var handled = handleOutButtonsPress(buttons, descriptor)
        updateAxisStateForKeyPress(axes, true)
        handled = sendAxisState(descriptor, axes) || handled
        return handled
    }

    /** Given a list of 3DS buttons or hotkeys, this handles their press event */
    fun handleOutButtonsPress(buttons: List<Int>, descriptor: String): Boolean {
        var handled = false
        val enableButtonMapping = InputMappingManager
            .getMappingForButton(Hotkey.ENABLE.button)

        val enableButtonMapped = enableButtonMapping != null && !enableButtonMapping.empty
        val thisKeyIsEnableButton = buttons.contains(Hotkey.ENABLE.button)
        val thisKeyIsHotkey =
            !thisKeyIsEnableButton && Hotkey.entries.any { buttons.contains(it.button) }
        hotkeyIsEnabled = hotkeyIsEnabled || !enableButtonMapped || thisKeyIsEnableButton

        for (button in buttons) {
            Log.debug("Button pressed: " + button)
            currentlyPressedButtons.add(button)
            if (button == Hotkey.ENABLE.button) {
                handled = true
            } else if (hotkeyButtons.contains(button)) {
                if (hotkeyIsEnabled) {
                    handled = handleHotkey(button) || handled
                }
            } else {
                if (!thisKeyIsHotkey || !hotkeyIsEnabled) {
                    handled = NativeLibrary.onGamePadEvent(
                        descriptor,
                        button,
                        NativeLibrary.ButtonState.PRESSED
                    ) || handled
                }
            }
        }
        return handled
    }

    /** Handles the release of an input key */
    fun handleKeyRelease(event: KeyEvent): Boolean {
        val key = if (event.keyCode == 0) event.scanCode else event.keyCode
        val descriptor = event.device.descriptor
        val buttons = InputMappingManager.getOutButtonsForKey(key)
        val axes = InputMappingManager.getOutAxesForKey(key)
        var handled = handleOutButtonsRelease(buttons, descriptor)
        updateAxisStateForKeyPress(axes, false)
        handled = sendAxisState(descriptor, axes) || handled
        return handled
    }

    /** Given a list of 3DS buttons or hotkeys this handles their release events */
    fun handleOutButtonsRelease(buttons: List<Int>, descriptor: String): Boolean {
        var handled = false
        val thisKeyIsEnableButton = buttons.contains(Hotkey.ENABLE.button)
        val thisKeyIsComboButton = buttons.contains(Hotkey.COMBO_BUTTON.button)
        val thisKeyIsHotkey =
            !thisKeyIsEnableButton && Hotkey.entries.any { buttons.contains(it.button) }
        if (thisKeyIsEnableButton) {
            handled = true
            hotkeyIsEnabled = false
        }
        if (thisKeyIsComboButton) {
            ComboHelper.comboActivate(NativeLibrary.ButtonState.RELEASED)
            handled = true
        }

        for (button in buttons) {
            Log.debug("Button released: " + button)
            // this is a hotkey button
            if (hotkeyButtons.contains(button)) {
                currentlyPressedButtons.remove(button)
                handleHotkeyRelease(button)
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
        return handled
    }

    fun handleHotkeyRelease(bindedButton: Int): Boolean {
        Log.debug("Handling hotkey button release: " + bindedButton)
        if (bindedButton == Hotkey.TURBO_HOLD.button) {
            TurboHelper.setTurboEnabled(false, false)
        }
        return true
    }

    fun handleHotkey(bindedButton: Int): Boolean {
        Log.debug("Handling hotkey button press: " + bindedButton)
        when (bindedButton) {
            Hotkey.SWAP_SCREEN.button -> screenAdjustmentUtil.swapScreen()

            Hotkey.CYCLE_LAYOUT.button -> screenAdjustmentUtil.cycleLayouts()

            Hotkey.CLOSE_GAME.button -> EmulationLifecycleUtil.closeGame()

            Hotkey.PAUSE_OR_RESUME.button -> EmulationLifecycleUtil.pauseOrResume()

            Hotkey.TURBO_TOGGLE.button -> TurboHelper.toggleTurbo(true)

            Hotkey.TURBO_HOLD.button -> TurboHelper.setTurboEnabled(true, false)

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

            Hotkey.COMBO_BUTTON.button -> {
                ComboHelper.comboActivate(NativeLibrary.ButtonState.PRESSED)
            }

            else -> {}
        }
        hotkeyIsPressed = true
        return true
    }

    /** takes a list of OUTPUT axes (outaxis, outdir) that are associated with an input key and tracks them */
    private fun updateAxisStateForKeyPress(axisSet: List<Pair<Int, Int>>, pressed: Boolean) {
        if (pressed) {
            heldKeyAxes.addAll(axisSet)
        } else {
            heldKeyAxes.removeAll(axisSet)
        }
    }

    /** Takes an INPUT axis (inAxis, inDir) and magnitude and adjusts the state of any associated output buttons */
    private fun updateButtonsForAxisMovement(
        axisPair: Pair<Int, Int>,
        magnitude: Float,
        descriptor: String
    ) {
        val buttons = InputMappingManager.getOutButtonsForAxis(axisPair)
        if (buttons.isEmpty()) return
        // for now, assume all buttons mapped to the same axis have the same threshold.
        val threshold = InputMappingManager.getMappingForButton(buttons[0])?.threshold ?: 0.5f
        if (magnitude > threshold && heldAxisButtons.add(axisPair)) {
            handleOutButtonsPress(buttons, descriptor)
        } else if (magnitude <= threshold && heldAxisButtons.remove(axisPair)) {
            handleOutButtonsRelease(buttons, descriptor)
        }
    }

    /**
     * Update axis state on the 3DS based on currently pressed inputs
     */
    private fun sendAxisState(descriptor: String, affectedAxes: List<Pair<Int, Int>>): Boolean {
        val stickAccumulator = HashMap<Int, Pair<Float, Float>>()
        // to make sure that when both directions are released, we still get a return to 0, but only for
        // axes that have been touched by buttons

        // note that if one side of an axis is analog and the other is a button, this will cause issues
        // that is a weird edge case not worth handling I think

        affectedAxes.forEach { (outAxis, _) ->
            val component = GamepadHelper.getJoystickComponent(outAxis) ?: return@forEach
            stickAccumulator.putIfAbsent(component.joystickType, Pair(0f, 0f))
        }

        heldKeyAxes.forEach { (outAxis, outDir) ->
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
            handled =
                NativeLibrary.onGamePadMoveEvent(
                    descriptor,
                    joystickType,
                    value.first,
                    value.second
                ) ||
                handled
        }
        return handled
    }

    fun handleMotionEvent(event: MotionEvent): Boolean {
        if (event.actionMasked == MotionEvent.ACTION_CANCEL) {
            reset()
            return true
        }
        val device = event.device ?: return false
        val descriptor = device.descriptor
        val stickAccumulator = HashMap<Int, Pair<Float, Float>>()

        for (range in device.motionRanges) {
            val axis = range.axis
            val origValue = event.getAxisValue(axis)
            var value = GamepadHelper.scaleAxis(device, axis, origValue)
            val activeDirection = when {
                value > 0.1f -> 1
                value < -0.1f -> -1
                else -> 0
            }

            for (dir in intArrayOf(1, -1)) {
                // treat this movement as TWO movements: one that is the actual movement and
                // one that is a movement to position 0 in the opposite direction. This ensures
                // axis->button combos get correctly released.
                val axisPair = Pair(axis, dir)
                val magnitude = if (dir == activeDirection) abs(value) else 0f

                // Axis to Axis mappings
                InputMappingManager.getOutAxesForAxis(axisPair).forEach { (outAxis, outDir) ->
                    val component = GamepadHelper.getJoystickComponent(outAxis) ?: return@forEach
                    val current =
                        stickAccumulator.getOrDefault(component.joystickType, Pair(0f, 0f))
                    val contribution = magnitude * outDir
                    stickAccumulator[component.joystickType] = if (component.isVertical) {
                        Pair(current.first, contribution + current.second)
                    } else {
                        Pair(contribution + current.first, current.second)
                    }
                }

                updateButtonsForAxisMovement(axisPair, magnitude, descriptor)
            }
        }
        stickAccumulator.forEach { (outAxis, value) ->
            NativeLibrary.onGamePadMoveEvent(
                device.descriptor,
                outAxis,
                value.first,
                value.second
            )
        }

        return true
    }

    // resets the axis->button and button->axis state
    fun reset() {
        heldAxisButtons.forEach {
            handleOutButtonsRelease(InputMappingManager.getOutButtonsForAxis(it), "")
        }
        handleOutButtonsRelease(currentlyPressedButtons.toList(), "")
        currentlyPressedButtons.clear()
        heldAxisButtons.clear()
        if (heldKeyAxes.isNotEmpty()) {
            sendAxisState("", heldKeyAxes.toList())
        }
        heldKeyAxes.clear()
    }
}
