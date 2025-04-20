// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.hotkeys

import android.content.Context
import android.widget.Toast
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.utils.EmulationLifecycleUtil
import org.citra.citra_emu.utils.TurboHelper
import org.citra.citra_emu.display.ScreenAdjustmentUtil

class HotkeyUtility(
    private val screenAdjustmentUtil: ScreenAdjustmentUtil,
    private val context: Context) {

    private val hotkeyButtons = Hotkey.entries.map { it.button }

    fun handleHotkey(bindedButton: Int): Boolean {
        if(hotkeyButtons.contains(bindedButton)) {
            when (bindedButton) {
                Hotkey.SWAP_SCREEN.button -> screenAdjustmentUtil.swapScreen()
                Hotkey.CYCLE_LAYOUT.button -> screenAdjustmentUtil.cycleLayouts()
                Hotkey.CLOSE_GAME.button -> EmulationLifecycleUtil.closeGame()
                Hotkey.PAUSE_OR_RESUME.button -> EmulationLifecycleUtil.pauseOrResume()
                Hotkey.TURBO_SPEED.button -> TurboHelper.setTurboEnabled(!TurboHelper.isTurboSpeedEnabled())
                Hotkey.QUICKSAVE.button -> {
                    NativeLibrary.saveState(NativeLibrary.QUICKSAVE_SLOT)
                    Toast.makeText(context,
                        context.getString(R.string.saving),
                        Toast.LENGTH_SHORT).show()
                }
                Hotkey.QUICKLOAD.button -> {
                    val wasLoaded = NativeLibrary.loadStateIfAvailable(NativeLibrary.QUICKSAVE_SLOT)
                    val stringRes = if(wasLoaded) {
                        R.string.loading
                    } else {
                        R.string.quickload_not_found
                    }
                    Toast.makeText(context,
                        context.getString(stringRes),
                        Toast.LENGTH_SHORT).show()
                }
                else -> {}
            }
            return true
        }
        return false
    }
}
