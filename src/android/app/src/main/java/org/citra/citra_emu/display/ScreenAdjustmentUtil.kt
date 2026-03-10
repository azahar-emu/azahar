// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.display

import android.content.Context
import android.content.pm.ActivityInfo
import android.app.Activity
import android.view.WindowManager
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.features.settings.model.IntListSetting
import org.citra.citra_emu.features.settings.model.Settings
import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.utils.EmulationMenuSettings

class ScreenAdjustmentUtil(
    private val context: Context,
    private val windowManager: WindowManager,
    private val settings: Settings,
) {
    fun swapScreen() {
        val isEnabled = !EmulationMenuSettings.swapScreens
        EmulationMenuSettings.swapScreens = isEnabled
        NativeLibrary.swapScreens(
            isEnabled,
            windowManager.defaultDisplay.rotation
        )
        settings.update(BooleanSetting.SWAP_SCREEN, isEnabled)
        SettingsFile.saveSetting(BooleanSetting.SWAP_SCREEN, settings)
    }

    fun cycleLayouts() {

        val landscapeLayoutsToCycle = settings.get(IntListSetting.LAYOUTS_TO_CYCLE)
        val landscapeValues =
            if (landscapeLayoutsToCycle.isNotEmpty())
                landscapeLayoutsToCycle.toIntArray()
            else context.resources.getIntArray(
                R.array.landscapeValues
            )
        val portraitValues = context.resources.getIntArray(R.array.portraitValues)

        if (NativeLibrary.isPortraitMode) {
            val currentLayout = settings.get(IntSetting.PORTRAIT_SCREEN_LAYOUT)
            val pos = portraitValues.indexOf(currentLayout)
            val layoutOption = portraitValues[(pos + 1) % portraitValues.size]
            changePortraitOrientation(layoutOption)
        } else {
            val currentLayout = settings.get(IntSetting.SCREEN_LAYOUT)
            val pos = landscapeValues.indexOf(currentLayout)
            val layoutOption = landscapeValues[(pos + 1) % landscapeValues.size]
            changeScreenOrientation(layoutOption)
        }
    }

    fun changePortraitOrientation(layoutOption: Int) {
        settings.update(IntSetting.PORTRAIT_SCREEN_LAYOUT, layoutOption)
        SettingsFile.saveSetting(IntSetting.PORTRAIT_SCREEN_LAYOUT, settings)
        NativeLibrary.reloadSettings()
        NativeLibrary.updateFramebuffer(NativeLibrary.isPortraitMode)
    }

    fun changeScreenOrientation(layoutOption: Int) {
        settings.update(IntSetting.SCREEN_LAYOUT, layoutOption)
        SettingsFile.saveSetting(IntSetting.SCREEN_LAYOUT, settings)
        NativeLibrary.reloadSettings()
        NativeLibrary.updateFramebuffer(NativeLibrary.isPortraitMode)
    }

    fun changeActivityOrientation(orientationOption: Int) {
        val activity = context as? Activity ?: return
        settings.update(IntSetting.ORIENTATION_OPTION, orientationOption)
        SettingsFile.saveSetting(IntSetting.ORIENTATION_OPTION, settings)
        activity.requestedOrientation = orientationOption
    }

    fun toggleScreenUpright() {
        val uprightBoolean = settings.get(BooleanSetting.UPRIGHT_SCREEN)
        settings.update(BooleanSetting.UPRIGHT_SCREEN, !uprightBoolean)
        SettingsFile.saveSetting(BooleanSetting.UPRIGHT_SCREEN, settings)
        NativeLibrary.reloadSettings()
        NativeLibrary.updateFramebuffer(NativeLibrary.isPortraitMode)

    }
}
