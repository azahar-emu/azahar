// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model.view

import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.features.settings.model.AbstractSetting

/**
 * ViewModel abstraction for an Item in the RecyclerView powering SettingsFragments.
 * Each one corresponds to a [AbstractSetting] object, so this class's subclasses
 * should vaguely correspond to those subclasses. There are a few with multiple analogues
 * and a few with none (Headers, for example, do not correspond to anything in the ini
 * file.)
 */
abstract class SettingsItem(
    var setting: AbstractSetting?,
    val nameId: Int,
    val descriptionId: Int
) {
    abstract val type: Int

    val isEditable: Boolean
        get() {
            if (!EmulationActivity.isRunning()) return true
            return setting?.isRuntimeEditable ?: false
        }

    open val isEnabled: Boolean = true

    open val requiredSettingName: String? = null

    val isActive: Boolean
        get() {
            return this.isEditable && this.isEnabled
        }

    companion object {
        const val TYPE_HEADER = 0
        const val TYPE_SWITCH = 1
        const val TYPE_SINGLE_CHOICE = 2
        const val TYPE_SLIDER = 3
        const val TYPE_SUBMENU = 4
        const val TYPE_STRING_SINGLE_CHOICE = 5
        const val TYPE_DATETIME_SETTING = 6
        const val TYPE_RUNNABLE = 7
        const val TYPE_INPUT_BINDING = 8
        const val TYPE_STRING_INPUT = 9
        const val TYPE_FLOAT_INPUT = 10
    }
}
