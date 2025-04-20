// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import android.widget.Toast
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.features.settings.model.IntSetting

object TurboHelper {
    private var turboSpeedEnabled = false

    fun isTurboSpeedEnabled(): Boolean {
        return turboSpeedEnabled
    }

    fun setTurboEnabled(state: Boolean) {
        turboSpeedEnabled = state
        reloadTurbo()
    }

    fun reloadTurbo() {
        val context = CitraApplication.appContext
        val toastMessage: String

        if (turboSpeedEnabled) {
            NativeLibrary.setTemporaryFrameLimit(IntSetting.TURBO_LIMIT.int.toDouble())
            toastMessage = context.getString(R.string.turbo_enabled_toast)
        } else {
            NativeLibrary.disableTemporaryFrameLimit()
            toastMessage = context.getString(R.string.turbo_disabled_toast)
        }

        Toast.makeText(context, toastMessage, Toast.LENGTH_SHORT).show()
    }
}
