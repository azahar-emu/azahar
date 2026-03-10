// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.display

import android.content.Context
import android.os.Build
import android.view.WindowManager
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.IntListSetting
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.features.settings.model.Settings
import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.utils.EmulationMenuSettings

class ScreenAdjustmentUtil(private val context: Context, private val windowManager: WindowManager) {
    fun swapScreen() {
        val isEnabled = !EmulationMenuSettings.swapScreens
        val displayRotation = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            context.display.rotation
        } else {
            @Suppress("DEPRECATION")
            windowManager.defaultDisplay.rotation
        }
        EmulationMenuSettings.swapScreens = isEnabled
        NativeLibrary.swapScreens(
            isEnabled,
            displayRotation
        )
        Settings.activeSettings.update(BooleanSetting.SWAP_SCREEN, isEnabled)
        SettingsFile.saveSetting(BooleanSetting.SWAP_SCREEN, Settings.activeSettings)
    }

    fun cycleLayouts() {
        val landscapeLayoutsToCycle = Settings.activeSettings.get(IntListSetting.LAYOUTS_TO_CYCLE)
        val landscapeValues =
            if (landscapeLayoutsToCycle.isNotEmpty()) {
                landscapeLayoutsToCycle.toIntArray()
            } else {
                context.resources.getIntArray(
                    R.array.landscapeValues
                )
            }
        val portraitValues = context.resources.getIntArray(R.array.portraitValues)

        if (NativeLibrary.isPortraitMode()) {
            val currentLayout = Settings.activeSettings.get(IntSetting.PORTRAIT_SCREEN_LAYOUT)
            val pos = portraitValues.indexOf(currentLayout)
            val layoutOption = portraitValues[(pos + 1) % portraitValues.size]
            changePortraitOrientation(layoutOption)
        } else {
            val currentLayout = Settings.activeSettings.get(IntSetting.SCREEN_LAYOUT)
            val pos = landscapeValues.indexOf(currentLayout)
            val layoutOption = landscapeValues[(pos + 1) % landscapeValues.size]
            changeScreenOrientation(layoutOption)
        }
    }

    fun changePortraitOrientation(layoutOption: Int) {
        Settings.activeSettings.update(IntSetting.PORTRAIT_SCREEN_LAYOUT, layoutOption)
        SettingsFile.saveSetting(IntSetting.PORTRAIT_SCREEN_LAYOUT, Settings.activeSettings)
        NativeLibrary.reloadSettings()
        NativeLibrary.updateFramebuffer(NativeLibrary.isPortraitMode())
    }

    fun changeScreenOrientation(layoutOption: Int) {
        Settings.activeSettings.update(IntSetting.SCREEN_LAYOUT, layoutOption)
        SettingsFile.saveSetting(IntSetting.SCREEN_LAYOUT, Settings.activeSettings)
        NativeLibrary.reloadSettings()
        NativeLibrary.updateFramebuffer(NativeLibrary.isPortraitMode())
    }

    fun changeSecondaryOrientation(layoutOption: Int) {
        Settings.activeSettings.update(IntSetting.SECONDARY_DISPLAY_LAYOUT, layoutOption)
        SettingsFile.saveSetting(IntSetting.SECONDARY_DISPLAY_LAYOUT, Settings.activeSettings)
        NativeLibrary.reloadSettings()
        NativeLibrary.updateFramebuffer(NativeLibrary.isPortraitMode())
    }

    fun enableSecondaryDisplay(layoutOption: Int) {
        Settings.activeSettings.update(BooleanSetting.ENABLE_SECONDARY_DISPLAY, true)
        SettingsFile.saveSetting(BooleanSetting.ENABLE_SECONDARY_DISPLAY, Settings.activeSettings)
        changeSecondaryOrientation(layoutOption)
    }

    fun disableSecondaryDisplay() {
        Settings.activeSettings.update(BooleanSetting.ENABLE_SECONDARY_DISPLAY, false)
        SettingsFile.saveSetting(BooleanSetting.ENABLE_SECONDARY_DISPLAY, Settings.activeSettings)
    }

    fun toggleScreenUpright() {
        val uprightBoolean = Settings.activeSettings.get(BooleanSetting.UPRIGHT_SCREEN)
        Settings.activeSettings.update(BooleanSetting.UPRIGHT_SCREEN, !uprightBoolean)
        SettingsFile.saveSetting(BooleanSetting.UPRIGHT_SCREEN, Settings.activeSettings)
        NativeLibrary.reloadSettings()
        NativeLibrary.updateFramebuffer(NativeLibrary.isPortraitMode())
    }
}
