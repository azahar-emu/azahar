// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.model.view

import androidx.annotation.DrawableRes
import org.citra.citra_emu.activities.EmulationActivity

class RunnableSetting(
    titleId: Int,
    descriptionId: Int,
    val isRuntimeRunnable: Boolean,
    @DrawableRes val iconId: Int = 0,
    val runnable: () -> Unit,
    val value: (() -> String)? = null,
    val onLongClick: (() -> Boolean)? = null
) : SettingsItem(null, titleId, descriptionId) {
    override val type = TYPE_RUNNABLE

    override val isEditable: Boolean
        get() = if (EmulationActivity.isRunning()) isRuntimeRunnable else true
}
