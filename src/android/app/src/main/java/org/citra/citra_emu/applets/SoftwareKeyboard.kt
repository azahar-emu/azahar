// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.applets

import android.text.InputFilter
import android.text.Spanned
import androidx.annotation.Keep
import java.io.Serializable
import org.citra.citra_emu.CitraApplication.Companion.appContext
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.fragments.KeyboardDialogFragment
import org.citra.citra_emu.fragments.MessageDialogFragment
import org.citra.citra_emu.utils.Log

@Keep
object SoftwareKeyboard {
    lateinit var data: KeyboardData
    val finishLock = Object()

    private fun executeImpl(config: KeyboardConfig) {
        val emulationActivity = NativeLibrary.sEmulationActivity.get()
        data = KeyboardData(0, "")
        KeyboardDialogFragment.newInstance(config)
            .show(emulationActivity!!.supportFragmentManager, KeyboardDialogFragment.TAG)
    }

    fun handleValidationError(config: KeyboardConfig, error: ValidationError) {
        val emulationActivity = NativeLibrary.sEmulationActivity.get()!!
        val message: String = when (error) {
            ValidationError.FixedLengthRequired -> emulationActivity.getString(
                R.string.fixed_length_required,
                config.maxTextLength
            )

            ValidationError.MaxLengthExceeded ->
                emulationActivity.getString(R.string.max_length_exceeded, config.maxTextLength)

            ValidationError.BlankInputNotAllowed ->
                emulationActivity.getString(R.string.blank_input_not_allowed)

            ValidationError.EmptyInputNotAllowed ->
                emulationActivity.getString(R.string.empty_input_not_allowed)

            else -> emulationActivity.getString(R.string.invalid_input)
        }

        MessageDialogFragment.newInstance(R.string.software_keyboard, message).show(
            NativeLibrary.sEmulationActivity.get()!!.supportFragmentManager,
            MessageDialogFragment.TAG
        )
    }

    @JvmStatic
    fun execute(config: KeyboardConfig): KeyboardData {
        if (config.buttonConfig == ButtonConfig.NONE) {
            Log.error("Unexpected button config None")
            return KeyboardData(0, "")
        }
        NativeLibrary.sEmulationActivity.get()!!.runOnUiThread { executeImpl(config) }
        synchronized(finishLock) {
            try {
                finishLock.wait()
            } catch (ignored: Exception) {
            }
        }
        return data
    }

    @Suppress("unused")
    @JvmStatic
    fun showError(error: String) {
        NativeLibrary.displayAlertMsg(
            appContext.resources.getString(R.string.software_keyboard),
            error,
            false
        )
    }

    @Suppress("FunctionName")
    private external fun ValidateFilters(text: String): ValidationError

    @Suppress("FunctionName")
    external fun ValidateInput(text: String): ValidationError

    // / Corresponds to Frontend::ButtonConfig
    interface ButtonConfig {
        companion object {
            const val SINGLE = 0 // / Ok button
            const val DUAL = 1 // / Cancel | Ok buttons
            const val TRIPLE = 2 // / Cancel | I Forgot | Ok buttons
            const val NONE = 3 // / No button (returned by swkbdInputText in special cases)
        }
    }

    // / Corresponds to Frontend::ValidationError
    enum class ValidationError {
        None,

        // Button Selection
        ButtonOutOfRange,

        // Configured Filters
        MaxDigitsExceeded,
        AtSignNotAllowed,
        PercentNotAllowed,
        BackslashNotAllowed,
        ProfanityNotAllowed,
        CallbackFailed,

        // Allowed Input Type
        FixedLengthRequired,
        MaxLengthExceeded,
        BlankInputNotAllowed,
        EmptyInputNotAllowed
    }

    @Keep
    class KeyboardConfig : Serializable {
        var buttonConfig = 0
        var maxTextLength = 0

        // True if the keyboard accepts multiple lines of input
        var multilineMode = false

        // Displayed in the field as a hint before
        var hintText: String? = null

        // Contains the button text that the caller provides
        lateinit var buttonText: Array<String>
    }

    // / Corresponds to Frontend::KeyboardData
    class KeyboardData(var button: Int, var text: String)
    class Filter : InputFilter {
        override fun filter(
            source: CharSequence,
            start: Int,
            end: Int,
            dest: Spanned,
            dstart: Int,
            dend: Int
        ): CharSequence? {
            val text = StringBuilder(dest)
                .replace(dstart, dend, source.subSequence(start, end).toString())
                .toString()
            return if (ValidateFilters(text) == ValidationError.None) {
                null // Accept replacement
            } else {
                dest.subSequence(dstart, dend) // Request the subsequence to be unchanged
            }
        }
    }
}
