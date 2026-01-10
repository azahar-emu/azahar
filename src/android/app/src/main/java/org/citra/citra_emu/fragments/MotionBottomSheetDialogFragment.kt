// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.fragments

import android.content.DialogInterface
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import org.citra.citra_emu.R
import org.citra.citra_emu.databinding.DialogInputBinding
import org.citra.citra_emu.features.settings.model.view.InputBindingSetting
import org.citra.citra_emu.features.settings.utils.InputBindingBase

class MotionBottomSheetDialogFragment : BottomSheetDialogFragment() {

    private val inputHandler = object : InputBindingBase() {
        override fun onButtonCaptured() {
            dismiss()
        }

        override fun onAxisCaptured() {
            dismiss()
        }

        override fun getCurrentSetting() = setting
    }
    private var _binding: DialogInputBinding? = null
    private val binding get() = _binding!!

    private var setting: InputBindingSetting? = null
    private var onCancel: (() -> Unit)? = null
    private var onDismiss: (() -> Unit)? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (setting == null) {
            dismiss()
        }
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = DialogInputBinding.inflate(inflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        BottomSheetBehavior.from<View>(view.parent as View).state =
            BottomSheetBehavior.STATE_EXPANDED

        isCancelable = false
        view.requestFocus()
        view.setOnFocusChangeListener { v, hasFocus -> if (!hasFocus) v.requestFocus() }
        if (setting!!.isButtonMappingSupported()) {
            dialog?.setOnKeyListener { _, _, event -> inputHandler.onKeyEvent(event) }
        }
        if (setting!!.isAxisMappingSupported()) {
            binding.root.setOnGenericMotionListener { _, event -> inputHandler.onMotionEvent(event) }
        }

        val inputTypeId = when {
            setting!!.isCirclePad() -> R.string.controller_circlepad
            setting!!.isCStick() -> R.string.controller_c
            setting!!.isDPad() -> R.string.controller_dpad
            setting!!.isTrigger() -> R.string.controller_trigger
            else -> R.string.button
        }
        binding.textTitle.text =
            String.format(
                getString(R.string.input_dialog_title),
                getString(inputTypeId),
                getString(setting!!.nameId)
            )

        var messageResId: Int = R.string.input_dialog_description
        if (setting!!.isAxisMappingSupported() && !setting!!.isTrigger()) {
            // Use specialized message for axis left/right or up/down
            messageResId = if (setting!!.isHorizontalOrientation()) {
                R.string.input_binding_description_horizontal_axis
            } else {
                R.string.input_binding_description_vertical_axis
            }
        }
        binding.textMessage.text = getString(messageResId)

        binding.buttonClear.setOnClickListener {
            setting?.removeOldMapping()
            dismiss()
        }
        binding.buttonCancel.setOnClickListener {
            onCancel?.invoke()
            dismiss()
        }
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    override fun onDismiss(dialog: DialogInterface) {
        super.onDismiss(dialog)
        onDismiss?.invoke()
    }



    companion object {
        const val TAG = "MotionBottomSheetDialogFragment"

        fun newInstance(
            setting: InputBindingSetting,
            onCancel: () -> Unit,
            onDismiss: () -> Unit
        ): MotionBottomSheetDialogFragment {
            val dialog = MotionBottomSheetDialogFragment()
            dialog.apply {
                this.setting = setting
                this.onCancel = onCancel
                this.onDismiss = onDismiss
            }
            return dialog
        }
    }
}
