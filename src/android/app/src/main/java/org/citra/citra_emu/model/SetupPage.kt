// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.model

data class SetupPage(
    val iconId: Int,
    val titleId: Int,
    val descriptionId: Int,
    val buttonIconId: Int,
    val leftAlignedIcon: Boolean,
    val buttonTextId: Int,
    val pageButtons: List<PageButton>? = null,
    val pageSteps: () -> PageState = { PageState.PAGE_STEPS_UNDEFINED },
)

data class PageButton(
    val iconId: Int,
    val titleId: Int,
    val descriptionId: Int,
    val buttonAction: (callback: SetupCallback) -> Unit,
    val buttonState: () -> ButtonState = { ButtonState.BUTTON_ACTION_UNDEFINED },
    val isUnskippable: Boolean = false,
    val hasWarning: Boolean = false,
    val warningTitleId: Int = 0,
    val warningDescriptionId: Int = 0,
    val warningHelpLinkId: Int = 0
)

interface SetupCallback {
    fun onStepCompleted(pageButtonId : Int, pageFullyCompleted: Boolean)
}

enum class PageState {
    PAGE_STEPS_COMPLETE,
    PAGE_STEPS_INCOMPLETE,
    PAGE_STEPS_UNDEFINED
}

enum class ButtonState {
    BUTTON_ACTION_COMPLETE,
    BUTTON_ACTION_INCOMPLETE,
    BUTTON_ACTION_UNDEFINED
}
