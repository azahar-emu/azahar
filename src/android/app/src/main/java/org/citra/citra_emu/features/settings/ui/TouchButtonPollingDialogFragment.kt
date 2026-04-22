// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.ui

import android.os.Bundle
import android.view.InputDevice
import android.view.KeyEvent
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import org.citra.citra_emu.databinding.DialogTouchButtonPollingBinding
import kotlin.math.abs

/**
 * Result of a button polling operation.
 * For key events: keyEvent is set, axisId is -1.
 * For axis events: keyEvent is null, axisId is the motion axis.
 */
data class PollingResult(val keyEvent: KeyEvent?, val axisId: Int)

class TouchButtonPollingDialogFragment : BottomSheetDialogFragment() {
    private var _binding: DialogTouchButtonPollingBinding? = null
    private val binding get() = _binding!!

    private var onButtonCaptured: ((PollingResult) -> Unit)? = null
    private var onCancelled: (() -> Unit)? = null

    private val previousValues = ArrayList<Float>()
    private var prevDeviceId = 0

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (onButtonCaptured == null) {
            dismiss()
        }
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = DialogTouchButtonPollingBinding.inflate(inflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        BottomSheetBehavior.from<View>(view.parent as View).state =
            BottomSheetBehavior.STATE_EXPANDED

        isCancelable = false
        view.requestFocus()
        view.setOnFocusChangeListener { v, hasFocus -> if (!hasFocus) v.requestFocus() }

        // Intercept key events at the dialog level — before view navigation
        dialog?.setOnKeyListener { _, _, event -> onKeyEvent(event) }

        // Intercept axis/motion events on the root view
        binding.root.setOnGenericMotionListener { _, event -> onMotionEvent(event) }

        binding.buttonCancel.setOnClickListener {
            onCancelled?.invoke()
            dismiss()
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    private fun onKeyEvent(event: KeyEvent): Boolean {
        if (event.action == KeyEvent.ACTION_DOWN && isGamepadButton(event.keyCode)) {
            onButtonCaptured?.invoke(PollingResult(event, -1))
            dismiss()
            return true
        }
        return false
    }

    private fun onMotionEvent(event: MotionEvent): Boolean {
        if (event.source and InputDevice.SOURCE_CLASS_JOYSTICK == 0 &&
            event.source and InputDevice.SOURCE_GAMEPAD == 0) return false
        if (event.action != MotionEvent.ACTION_MOVE) return false

        val input = event.device ?: return false
        val motionRanges = input.motionRanges

        if (input.id != prevDeviceId) {
            previousValues.clear()
        }
        prevDeviceId = input.id
        val firstEvent = previousValues.isEmpty()

        for (i in motionRanges.indices) {
            val range = motionRanges[i]
            val axis = range.axis
            val origValue = event.getAxisValue(axis)
            if (firstEvent) {
                previousValues.add(origValue)
            } else {
                val previousValue = previousValues[i]
                if (abs(origValue) > 0.5f && origValue != previousValue) {
                    // Significant axis movement — capture this axis
                    onButtonCaptured?.invoke(PollingResult(null, axis))
                    dismiss()
                    return true
                }
            }
            if (i < previousValues.size) {
                previousValues[i] = origValue
            } else {
                previousValues.add(origValue)
            }
        }
        return true
    }

    private fun isGamepadButton(keyCode: Int): Boolean {
        return keyCode in KeyEvent.KEYCODE_BUTTON_A..KeyEvent.KEYCODE_BUTTON_MODE ||
                keyCode == KeyEvent.KEYCODE_DPAD_UP ||
                keyCode == KeyEvent.KEYCODE_DPAD_DOWN ||
                keyCode == KeyEvent.KEYCODE_DPAD_LEFT ||
                keyCode == KeyEvent.KEYCODE_DPAD_RIGHT
    }

    companion object {
        const val TAG = "TouchButtonPollingDialogFragment"

        fun newInstance(
            onButtonCaptured: (PollingResult) -> Unit,
            onCancelled: () -> Unit
        ): TouchButtonPollingDialogFragment {
            return TouchButtonPollingDialogFragment().apply {
                this.onButtonCaptured = onButtonCaptured
                this.onCancelled = onCancelled
            }
        }
    }
}
