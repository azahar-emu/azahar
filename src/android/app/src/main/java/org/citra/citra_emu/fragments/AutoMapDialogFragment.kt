// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.fragments

import android.os.Bundle
import android.view.InputDevice
import android.view.KeyEvent
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import org.citra.citra_emu.R
import org.citra.citra_emu.databinding.DialogAutoMapBinding
import org.citra.citra_emu.features.settings.model.view.InputBindingSetting
import org.citra.citra_emu.utils.Log

/**
 * Captures a single button press to detect controller layout (Xbox vs Nintendo)
 * and d-pad type (axis vs button), then applies the appropriate bindings.
 */
class AutoMapDialogFragment : BottomSheetDialogFragment() {
    private var _binding: DialogAutoMapBinding? = null
    private val binding get() = _binding!!

    private var onComplete: (() -> Unit)? = null

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = DialogAutoMapBinding.inflate(inflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        BottomSheetBehavior.from<View>(view.parent as View).state =
            BottomSheetBehavior.STATE_EXPANDED

        isCancelable = false
        view.requestFocus()
        view.setOnFocusChangeListener { v, hasFocus -> if (!hasFocus) v.requestFocus() }

        binding.textTitle.setText(R.string.controller_auto_map)
        binding.textMessage.setText(R.string.auto_map_prompt)

        binding.imageFaceButtons.setImageResource(R.drawable.automap_face_buttons)

        // Capture button press to detect layout
        dialog?.setOnKeyListener { _, _, event -> onKeyEvent(event) }

        binding.buttonCancel.setOnClickListener {
            dismiss()
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    private fun onKeyEvent(event: KeyEvent): Boolean {
        if (event.action != KeyEvent.ACTION_UP) return false

        val keyCode = event.keyCode

        // Determine layout from which keycode arrives for the east/right position.
        // The user is pressing the button in the "A" (east/right) position on the 3DS diamond.
        // Xbox layout: east position sends KEYCODE_BUTTON_B (97)
        // Nintendo layout: east position sends KEYCODE_BUTTON_A (96)
        val isNintendoLayout = when (keyCode) {
            KeyEvent.KEYCODE_BUTTON_A -> true
            KeyEvent.KEYCODE_BUTTON_B -> false
            else -> {
                // Unrecognized button - ignore and wait for a valid press
                Log.warning("[AutoMap] Ignoring unrecognized keycode $keyCode, waiting for A or B")
                return true
            }
        }

        val layoutName = if (isNintendoLayout) "Nintendo" else "Xbox"
        Log.info("[AutoMap] Detected $layoutName layout (keyCode=$keyCode)")

        // Detect d-pad type from the InputDevice
        val device = event.device
        val useAxisDpad = detectDpadType(device)

        val dpadName = if (useAxisDpad) "axis" else "button"
        Log.info("[AutoMap] Detected $dpadName d-pad (device=${device?.name})")

        // Apply the detected bindings
        InputBindingSetting.clearAllBindings()
        InputBindingSetting.applyAutoMapBindings(isNintendoLayout, useAxisDpad)

        onComplete?.invoke()
        dismiss()
        return true
    }

    companion object {
        const val TAG = "AutoMapDialogFragment"

        fun newInstance(
            onComplete: () -> Unit
        ): AutoMapDialogFragment {
            val dialog = AutoMapDialogFragment()
            dialog.onComplete = onComplete
            return dialog
        }

        // Returns true for axis d-pad, false for button d-pad.
        // Prefers axis when both are present. Defaults to axis if detection fails.
        private fun detectDpadType(device: InputDevice?): Boolean {
            if (device == null) return true

            val hasAxisDpad = device.motionRanges.any {
                it.axis == MotionEvent.AXIS_HAT_X || it.axis == MotionEvent.AXIS_HAT_Y
            }

            if (hasAxisDpad) return true

            val dpadKeyCodes = intArrayOf(
                KeyEvent.KEYCODE_DPAD_UP,
                KeyEvent.KEYCODE_DPAD_DOWN,
                KeyEvent.KEYCODE_DPAD_LEFT,
                KeyEvent.KEYCODE_DPAD_RIGHT
            )
            val hasButtonDpad = device.hasKeys(*dpadKeyCodes).any { it }

            // If button d-pad detected, use button mode. Otherwise default to axis.
            return !hasButtonDpad
        }
    }
}
