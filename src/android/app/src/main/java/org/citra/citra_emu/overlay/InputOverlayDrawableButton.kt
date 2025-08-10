// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.overlay

import android.content.res.Resources
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Rect
import android.graphics.drawable.BitmapDrawable
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.utils.EmulationMenuSettings

enum class ButtonSlidingMode(val int: Int) {
    // Disabled, buttons can only be triggered by pressing them directly.
    Disabled(0),

    // Additionally to pressing buttons directly, they can be activated and released by sliding into
    // and out of their area.
    Enabled(1),

    // The first button is kept activated until released, further buttons use the simple button
    // sliding method.
    Alternative(2)
}

/**
 * Custom [BitmapDrawable] that is capable
 * of storing it's own ID.
 *
 * @param res                [Resources] instance.
 * @param defaultStateBitmap [Bitmap] to use with the default state Drawable.
 * @param pressedStateBitmap [Bitmap] to use with the pressed state Drawable.
 * @param id                 Identifier for this type of button.
 */
class InputOverlayDrawableButton(
    res: Resources,
    defaultStateBitmap: Bitmap,
    pressedStateBitmap: Bitmap,
    val id: Int,
    val opacity: Int
) {
    var trackId: Int

    private var isMotionFirstButton = false // mark the first activated button with the current motion

    private var previousTouchX = 0
    private var previousTouchY = 0
    private var controlPositionX = 0
    private var controlPositionY = 0
    val width: Int
    val height: Int
    private val defaultStateBitmap: BitmapDrawable
    private val pressedStateBitmap: BitmapDrawable
    private var pressedState = false

    init {
        this.defaultStateBitmap = BitmapDrawable(res, defaultStateBitmap)
        this.pressedStateBitmap = BitmapDrawable(res, pressedStateBitmap)
        trackId = -1
        width = this.defaultStateBitmap.intrinsicWidth
        height = this.defaultStateBitmap.intrinsicHeight
    }

    /**
     * Updates button status based on the motion event.
     *
     * @return true if value was changed
     */
    fun updateStatus(event: MotionEvent, hasActiveButtons: Boolean, overlay: InputOverlay): Boolean {
        val buttonSliding = EmulationMenuSettings.buttonSlide
        val pointerIndex = event.actionIndex
        val xPosition = event.getX(pointerIndex).toInt()
        val yPosition = event.getY(pointerIndex).toInt()
        val pointerId = event.getPointerId(pointerIndex)
        val motionEvent = event.action and MotionEvent.ACTION_MASK
        val isActionDown =
            motionEvent == MotionEvent.ACTION_DOWN || motionEvent == MotionEvent.ACTION_POINTER_DOWN
        val isActionUp =
            motionEvent == MotionEvent.ACTION_UP || motionEvent == MotionEvent.ACTION_POINTER_UP
        if (isActionDown) {
            if (!bounds.contains(xPosition, yPosition)) {
                return false
            }
            buttonDown(true, pointerId, overlay)
            return true
        }
        if (isActionUp) {
            if (trackId != pointerId) {
                return false
            }
            buttonUp(overlay)
            return true
        }

        val isActionMoving = motionEvent == MotionEvent.ACTION_MOVE
        if (buttonSliding != ButtonSlidingMode.Disabled.int && isActionMoving) {
            val inside = bounds.contains(xPosition, yPosition)
            if (pressedState) {
                // button is already pressed
                // check whether we moved out of the button area to update the state
                if (inside || trackId != pointerId) {
                    return false
                }
                // prevent the first (directly pressed) button to deactivate when sliding off
                if (buttonSliding == ButtonSlidingMode.Alternative.int && isMotionFirstButton) {
                    return false
                }
                buttonUp(overlay)
                return true
            } else {
                // button was not yet pressed
                // check whether we moved into the button area to update the state
                if (!inside) {
                    return false
                }
                buttonDown(!hasActiveButtons, pointerId, overlay)
                return true
            }
        }

        return false
    }

    private fun buttonDown(firstBtn: Boolean, pointerId: Int, overlay: InputOverlay) {
        pressedState = true
        isMotionFirstButton = firstBtn
        trackId = pointerId
        overlay.hapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
    }

    private fun buttonUp(overlay: InputOverlay) {
        pressedState = false
        isMotionFirstButton = false
        trackId = -1
        overlay.hapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY_RELEASE)
    }

    fun onConfigureTouch(event: MotionEvent): Boolean {
        val pointerIndex = event.actionIndex
        val fingerPositionX = event.getX(pointerIndex).toInt()
        val fingerPositionY = event.getY(pointerIndex).toInt()
        when (event.action) {
            MotionEvent.ACTION_DOWN -> {
                previousTouchX = fingerPositionX
                previousTouchY = fingerPositionY
            }

            MotionEvent.ACTION_MOVE -> {
                controlPositionX += fingerPositionX - previousTouchX
                controlPositionY += fingerPositionY - previousTouchY
                setBounds(
                    controlPositionX,
                    controlPositionY,
                    width + controlPositionX,
                    height + controlPositionY
                )
                previousTouchX = fingerPositionX
                previousTouchY = fingerPositionY
            }
        }
        return true
    }

    fun setPosition(x: Int, y: Int) {
        controlPositionX = x
        controlPositionY = y
    }

    fun draw(canvas: Canvas) {
        val bitmapDrawable: BitmapDrawable = currentStateBitmapDrawable
        bitmapDrawable.alpha = opacity
        bitmapDrawable.draw(canvas)
    }

    private val currentStateBitmapDrawable: BitmapDrawable
        get() = if (pressedState) pressedStateBitmap else defaultStateBitmap

    fun setBounds(left: Int, top: Int, right: Int, bottom: Int) {
        defaultStateBitmap.setBounds(left, top, right, bottom)
        pressedStateBitmap.setBounds(left, top, right, bottom)
    }

    val status: Int
        get() = if (pressedState) NativeLibrary.ButtonState.PRESSED else NativeLibrary.ButtonState.RELEASED
    val bounds: Rect
        get() = defaultStateBitmap.bounds
}
