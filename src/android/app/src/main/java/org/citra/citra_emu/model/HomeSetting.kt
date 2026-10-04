// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

package org.citra.citra_emu.model

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

data class HomeSetting(
    val titleId: Int,
    val descriptionId: Int,
    val iconId: Int,
    val onClick: () -> Unit,
    val isEnabled: () -> Boolean = { true },
    val disabledTitleId: Int = 0,
    val disabledMessageId: Int = 0,
    val details: StateFlow<String> = MutableStateFlow("")
)
