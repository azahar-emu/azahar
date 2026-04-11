// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import android.widget.Toast
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.features.settings.model.Settings

object TurboHelper {
    private var turboSpeedEnabled = false

    fun isTurboSpeedEnabled(): Boolean {
        return turboSpeedEnabled
    }

    fun reloadTurbo(showToast: Boolean, settings: Settings) {
        val context = CitraApplication.appContext
        val toastMessage: String

        if (turboSpeedEnabled) {
            NativeLibrary.setTemporaryFrameLimit(settings.get(IntSetting.TURBO_LIMIT).toDouble())
            toastMessage = context.getString(R.string.turbo_enabled_toast)
        } else {
            NativeLibrary.disableTemporaryFrameLimit()
            toastMessage = context.getString(R.string.turbo_disabled_toast)
        }

        if (showToast) {
            Toast.makeText(context, toastMessage, Toast.LENGTH_SHORT).show()
        }
    }

    fun setTurboEnabled(state: Boolean, showToast: Boolean, settings: Settings) {
        turboSpeedEnabled = state
        reloadTurbo(showToast, settings)
    }

    fun toggleTurbo(showToast: Boolean, settings: Settings) {
        setTurboEnabled(!TurboHelper.isTurboSpeedEnabled(), showToast, settings)
    }
}
