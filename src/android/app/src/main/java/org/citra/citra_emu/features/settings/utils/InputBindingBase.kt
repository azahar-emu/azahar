package org.citra.citra_emu.features.settings.utils

import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import org.citra.citra_emu.features.settings.model.view.InputBindingSetting
import kotlin.math.abs

abstract class InputBindingBase {
    private val previousValues = ArrayList<Float>()
    private var prevDeviceId = 0
    private var waitingForEvent = true

    abstract fun onButtonCaptured()
    abstract fun onAxisCaptured()
    abstract fun getCurrentSetting(): InputBindingSetting?

    fun reset() {
        previousValues.clear()
        prevDeviceId = 0
        waitingForEvent = true
    }

    fun onMotionEvent(event: MotionEvent): Boolean {
        if ((event.source and InputDevice.SOURCE_CLASS_JOYSTICK == 0) ||
            event.action != MotionEvent.ACTION_MOVE) return false

        val input = event.device
        val motionRanges = input.motionRanges

        if (input.id != prevDeviceId) {
            previousValues.clear()
        }
        prevDeviceId = input.id
        val firstEvent = previousValues.isEmpty()

        var numMovedAxis = 0
        var axisMoveValue = 0.0f
        var lastMovedRange: InputDevice.MotionRange? = null
        var lastMovedDir = '?'

        if (waitingForEvent) {
            for (i in motionRanges.indices) {
                val range = motionRanges[i]
                val axis = range.axis
                val origValue = event.getAxisValue(axis)
                if (firstEvent) {
                    previousValues.add(origValue)
                } else {
                    val previousValue = previousValues[i]

                    if (abs(origValue) > 0.5f && origValue != previousValue) {
                        if (origValue != axisMoveValue) {
                            axisMoveValue = origValue
                            numMovedAxis++
                            lastMovedRange = range
                            lastMovedDir = if (origValue < 0.0f) '-' else '+'
                        }
                    } else if (abs(origValue) < 0.25f && abs(previousValue) > 0.75f) {
                        numMovedAxis++
                        lastMovedRange = range
                        lastMovedDir = if (previousValue < 0.0f) '-' else '+'
                    }
                }
                previousValues[i] = origValue
            }

            if (numMovedAxis == 1) {
                waitingForEvent = false
                getCurrentSetting()?.onMotionInput(input, lastMovedRange!!, lastMovedDir)
                onAxisCaptured()
            }
        }
        return true
    }

    fun onKeyEvent(event: KeyEvent): Boolean {
        return when (event.action) {
            KeyEvent.ACTION_UP -> {
                getCurrentSetting()?.onKeyInput(event)
                onButtonCaptured()
                true
            }
            else -> false
        }
    }
}