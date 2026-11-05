// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

package org.citra.citra_emu.features.input

/**
 * Stores information about a particular input overlay button
 */
data class InputOverlayElement(
    val visible: Boolean = false,

    /** From 0 to 1000. O is all the way left, 1000 is all the way right. */
    val portraitX: Int,
    /** From 0 to 1000. O is all the way up, 1000 is all the way down. */
    val portraitY: Int,

    /** From 0 to 1000. O is all the way left, 1000 is all the way right. */
    val landscapeX: Int,
    /** From 0 to 1000. O is all the way up, 1000 is all the way down. */
    val landscapeY: Int,

    /** From 0 to 1000 where 1000 is the NARROWER (Portrait width) dimension */
    val size: Int,

    // button-specific values
    /** The key code (3DS or hotkey) that will be sent on  bpress */
    val button: Int? = null,
    /** ID of the default (unpressed) drawable  */
    val buttonResID: Int? = null,
    /** ID of the pressed drawable */
    val pressedButtonResID: Int? = null,

    // joystick-specific values
    val joystick: Int? = null,
    /** ID of the inner drawable for joystick **/
    val defaultInnerResId: Int? = null,
    /** ID of the pressed-inner drawable for joystick **/
    val pressedInnerResId: Int? = null,
    /** ID of the outer drawable for joystick **/
    val outerResId: Int? = null,

    // dpad-specific values
    val upButton: Int? = null,
    val downButton: Int? = null,
    val leftButton: Int? = null,
    val rightButton: Int? = null,
    val dpadResId: Int? = null,
    val dpadPressedOneDirectionResId: Int? = null,
    val dpadPressedTwoDirectionsRedId: Int? = null
)

enum class InputOverlayType {
    BUTTON,
    DPAD,
    JOYSTICK
}
