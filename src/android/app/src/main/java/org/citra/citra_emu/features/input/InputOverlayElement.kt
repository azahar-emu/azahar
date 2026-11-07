// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.input

/**
 * The user-editable, persisted part of an overlay control. This is the setting's value type.
 * Positions are 0..1000 of the overlay view's width (X) / height (Y) in the given orientation.
 */
data class InputOverlayElement(
    val visible: Boolean = false,
    val portraitX: Int,
    val portraitY: Int,
    val landscapeX: Int,
    val landscapeY: Int,
    /** Side length of the (square) control, as 0..1000 of the view's shorter dimension.
     *  For joysticks, this is the outer ring. */
    val size: Int,
    /** 0..100 */
    val opacity: Int = 50
)

/** Fixed identity of a control: what it sends and what it looks like. Never persisted. */
sealed interface InputOverlaySpec {
    data class Button(val button: Int, val buttonResId: Int, val pressedButtonResId: Int) :
        InputOverlaySpec

    data class Dpad(
        val up: Int,
        val down: Int,
        val left: Int,
        val right: Int,
        val dpadResId: Int,
        val pressedOneDirectionResId: Int,
        val pressedTwoDirectionsResId: Int
    ) : InputOverlaySpec

    data class Joystick(
        val joystick: Int,
        val outerResId: Int,
        val defaultInnerResId: Int,
        val pressedInnerResId: Int
    ) : InputOverlaySpec
}
