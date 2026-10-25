// Copyright 2026 tahirwrth
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.features.fold

import android.widget.Toast
import androidx.lifecycle.lifecycleScope
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.activities.EmulationActivity

// owns everything about the foldable cover-screen controls so EmulationActivity only needs to forward a few lifecycle calls

class FoldCoverManager(private val activity: EmulationActivity) {
    private var controller: FoldDualScreenController? = null

    @Volatile
    var isActive = false
        private set

    fun attach() {
        FoldSupport.observe(activity)
    }

    // true when menu entry should be shown
    val isAvailable: Boolean
        get() = FoldSupport.canPresent

    fun toggle() {
        if (isActive) {
            stop()
            Toast.makeText(activity, R.string.fold_cover_disabled, Toast.LENGTH_SHORT).show()
        } else if (FoldSupport.canPresent) {
            start()
            Toast.makeText(activity, R.string.fold_cover_enabled, Toast.LENGTH_SHORT).show()
        }
    }

    fun onStop() = stop()

    fun onDestroy() {
        stop()
        controller = null
    }

    private fun start() {
        isActive = true
        // keep emulator's secondary-display surface off the real cover screen
        activity.secondaryDisplayManager.forceHiddenDisplay = true
        activity.secondaryDisplayManager.updateDisplay()

        val c = controller ?: FoldDualScreenController(
            activity,
            activity.lifecycleScope,
            onButtonEvent = { type, state ->
                NativeLibrary.onGamePadEvent(NativeLibrary.TOUCHSCREEN_DEVICE, type, state)
            },
            onSessionClosed = ::onSessionClosed
        ).also { controller = it }
        c.start()
    }

    private fun stop() {
        controller?.stop()
        onSessionClosed()
    }

    // called from stop() and when system ends session
    private fun onSessionClosed() {
        if (!isActive) return
        isActive = false
        activity.secondaryDisplayManager.forceHiddenDisplay = false
        activity.secondaryDisplayManager.updateDisplay()
    }
}
