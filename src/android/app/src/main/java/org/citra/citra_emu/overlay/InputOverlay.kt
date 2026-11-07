// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.overlay

import android.app.Activity
import android.content.Context
import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.Rect
import android.graphics.drawable.VectorDrawable
import android.os.Build
import android.util.AttributeSet
import android.view.InputDevice
import android.view.MotionEvent
import android.view.SurfaceView
import android.view.View
import android.view.View.OnTouchListener
import androidx.core.content.ContextCompat
import androidx.core.graphics.scale
import androidx.core.view.ViewCompat
import kotlin.math.min
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.features.input.Hotkey
import org.citra.citra_emu.features.input.InputOverlayElement
import org.citra.citra_emu.features.input.InputOverlaySpec
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.CitraSettings
import org.citra.citra_emu.features.settings.model.InputOverlaySetting
import org.citra.citra_emu.features.settings.utils.SettingsFile
import org.citra.citra_emu.utils.ComboHelper
import org.citra.citra_emu.utils.EmulationMenuSettings
import org.citra.citra_emu.utils.TurboHelper

/**
 * Draws the interactive input overlay on top of the
 * [SurfaceView] that is rendering emulation.
 *
 * @param context The current [Context].
 * @param attrs   [AttributeSet] for parsing XML attributes.
 */
class InputOverlay(context: Context?, attrs: AttributeSet?) :
    SurfaceView(context, attrs),
    OnTouchListener {
    private val overlayButtons: MutableSet<InputOverlayDrawableButton> = HashSet()
    private val overlayDpads: MutableSet<InputOverlayDrawableDpad> = HashSet()
    private val overlayJoysticks: MutableSet<InputOverlayDrawableJoystick> = HashSet()
    private var isInEditMode = false
    private var buttonBeingConfigured: InputOverlayDrawableButton? = null
    private var dpadBeingConfigured: InputOverlayDrawableDpad? = null
    private var joystickBeingConfigured: InputOverlayDrawableJoystick? = null

    // Stores the ID of the pointer that interacted with the 3DS touchscreen.
    private var touchscreenPointerId = -1

    init {

        // Reset 3ds touchscreen pointer ID
        touchscreenPointerId = -1

        // Set the on touch listener.
        setOnTouchListener(this)

        // Force draw
        setWillNotDraw(false)

        // Request focus for the overlay so it has priority on presses.
        requestFocus()

        // Deliver controller axis events as they arrive instead of batching them to the next
        // frame.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            requestUnbufferedDispatch(InputDevice.SOURCE_CLASS_JOYSTICK)
        }
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        if (w > 0 && h > 0) refreshControls()
    }

    override fun draw(canvas: Canvas) {
        super.draw(canvas)
        overlayButtons.forEach { it.draw(canvas) }
        overlayDpads.forEach { it.draw(canvas) }
        overlayJoysticks.forEach { it.draw(canvas) }
    }

    private fun swapScreen() {
        val isEnabled = !EmulationMenuSettings.swapScreens
        EmulationMenuSettings.swapScreens = isEnabled
        val displayRotation = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            context.display.rotation
        } else {
            @Suppress("DEPRECATION")
            (context as Activity).windowManager.defaultDisplay.rotation
        }
        NativeLibrary.swapScreens(
            isEnabled,
            displayRotation
        )
    }

    fun hapticFeedback(type: Int) {
        if (EmulationMenuSettings.hapticFeedback) {
            performHapticFeedback(type)
        }
    }

    override fun onTouch(v: View, event: MotionEvent): Boolean {
        if (isInEditMode) {
            return onTouchWhileEditing(event)
        }

        // Deliver the touch events as they arrive instead of batching them to the next
        // frame.
        if (event.actionMasked == MotionEvent.ACTION_DOWN) {
            requestUnbufferedDispatch(event)
        }

        val motionEvent = event.action and MotionEvent.ACTION_MASK
        val isActionDown =
            motionEvent == MotionEvent.ACTION_DOWN || motionEvent == MotionEvent.ACTION_POINTER_DOWN
        val isActionMove = motionEvent == MotionEvent.ACTION_MOVE
        val isActionUp =
            motionEvent == MotionEvent.ACTION_UP || motionEvent == MotionEvent.ACTION_POINTER_UP

        val pointerList = (0 until event.pointerCount).toMutableList()
        // Move the pointer that triggered the most recent event to the front
        // of the list so that it is processed first
        val currentActionPointer = event.actionIndex
        pointerList.remove(pointerList.indexOf(currentActionPointer))
        pointerList.add(0, currentActionPointer)

        // Set up a loop for if we need to check touches other than the most recent one
        // (Only happens if we're dragging the touch)
        for (pointerIndex in pointerList) {
            val pointerId = event.getPointerId(pointerIndex)

            var xPosition = event.getX(pointerIndex).toInt()
            var yPosition = event.getY(pointerIndex).toInt()

            if (CitraSettings.activeSettings.get(BooleanSetting.EXPAND_TO_CUTOUT_AREA)) {
                val cutout = ViewCompat.getRootWindowInsets(this)?.displayCutout
                val marginX = cutout?.safeInsetLeft ?: 0
                val marginY = cutout?.safeInsetTop ?: 0
                xPosition += marginX
                yPosition += marginY
            }

            var hasActiveButtons = false
            for (button in overlayButtons) {
                if (button.trackId == pointerId) {
                    hasActiveButtons = true
                    break
                }
            }

            var hasActiveDpad = false
            if (!hasActiveButtons) {
                for (dpad in overlayDpads) {
                    if (dpad.trackId == pointerId) {
                        hasActiveDpad = true
                        break
                    }
                }
            }

            var hasActiveJoystick = false
            if (!hasActiveButtons && !hasActiveDpad) {
                for (joystick in overlayJoysticks) {
                    if (joystick.trackId == pointerId) {
                        hasActiveJoystick = true
                        break
                    }
                }
            }

            val hasActiveOverlay = hasActiveButtons || hasActiveDpad || hasActiveJoystick

            if (!hasActiveOverlay) {
                if (isActionMove) {
                    NativeLibrary.onTouchMoved(xPosition.toFloat(), yPosition.toFloat())
                    continue
                } else if (isActionUp) {
                    NativeLibrary.onTouchEvent(0f, 0f, false)
                    break // Up and down actions shouldn't loop
                }
            }

            var anyOverlayStateChanged = false
            var shouldUpdateView = false
            if (!hasActiveDpad && !hasActiveJoystick) {
                for (button in overlayButtons) {
                    val stateChanged = button.updateStatus(
                        event,
                        pointerIndex,
                        hasActiveButtons,
                        this
                    )
                    if (!stateChanged) {
                        continue
                    }
                    anyOverlayStateChanged = true
                    val id = (button.setting.spec as InputOverlaySpec.Button).button
                    if (id == Hotkey.SWAP_SCREEN.button &&
                        button.status == NativeLibrary.ButtonState.PRESSED
                    ) {
                        swapScreen()
                    } else if (id == Hotkey.TURBO_TOGGLE.button &&
                        button.status == NativeLibrary.ButtonState.PRESSED
                    ) {
                        TurboHelper.toggleTurbo(true)
                    } else if (id == Hotkey.COMBO_BUTTON.button) {
                        ComboHelper.comboActivate(button.status)
                    }

                    NativeLibrary.onGamePadEvent(
                        NativeLibrary.TOUCHSCREEN_DEVICE,
                        id,
                        button.status
                    )

                    shouldUpdateView = true
                }
            }

            if (!hasActiveButtons && !hasActiveJoystick) {
                for (dpad in overlayDpads) {
                    val stateChanged = dpad.updateStatus(
                        event,
                        pointerIndex,
                        hasActiveDpad,
                        EmulationMenuSettings.dpadSlide,
                        this
                    )
                    if (!stateChanged) {
                        continue
                    }
                    anyOverlayStateChanged = true
                    val spec = dpad.setting.spec as InputOverlaySpec.Dpad
                    NativeLibrary.onGamePadEvent(
                        NativeLibrary.TOUCHSCREEN_DEVICE,
                        spec.up,
                        dpad.upStatus
                    )
                    NativeLibrary.onGamePadEvent(
                        NativeLibrary.TOUCHSCREEN_DEVICE,
                        spec.down,
                        dpad.downStatus
                    )
                    NativeLibrary.onGamePadEvent(
                        NativeLibrary.TOUCHSCREEN_DEVICE,
                        spec.left,
                        dpad.leftStatus
                    )
                    NativeLibrary.onGamePadEvent(
                        NativeLibrary.TOUCHSCREEN_DEVICE,
                        spec.right,
                        dpad.rightStatus
                    )

                    shouldUpdateView = true
                }
            }

            if (!hasActiveDpad && !hasActiveButtons) {
                for (joystick in overlayJoysticks) {
                    val stateChanged = joystick.updateStatus(
                        event,
                        pointerIndex,
                        hasActiveJoystick,
                        this
                    )
                    if (!stateChanged) {
                        continue
                    }
                    anyOverlayStateChanged = true

                    val axisID = (joystick.setting.spec as InputOverlaySpec.Joystick).joystick
                    NativeLibrary.onGamePadMoveEvent(
                        NativeLibrary.TOUCHSCREEN_DEVICE,
                        axisID,
                        joystick.xAxis,
                        joystick.yAxis
                    )

                    shouldUpdateView = true
                }
            }

            if (shouldUpdateView) {
                invalidate()
            }

            if (isActionDown && !anyOverlayStateChanged) {
                // These need to be recalculated because touching the area
                // right in the middle of the dpad (between the "buttons") or
                // tapping a joystick in a certain way both don't cause
                // `anyOverlayStateChanged` to be set to true
                var isDpadPressed = false
                for (dpad in overlayDpads) {
                    if (dpad.trackId == pointerId) {
                        isDpadPressed = true
                        break
                    }
                }
                var isJoystickPressed = false
                for (joystick in overlayJoysticks) {
                    if (joystick.trackId == pointerId) {
                        isJoystickPressed = true
                        break
                    }
                }

                if (!isDpadPressed && !isJoystickPressed) {
                    NativeLibrary.onTouchEvent(xPosition.toFloat(), yPosition.toFloat(), true)
                }
            }

            // We should only loop here if touch is being dragged
            if (!isActionMove) {
                break
            }
        }
        return true
    }

    fun onTouchWhileEditing(event: MotionEvent): Boolean {
        val pointerIndex = event.actionIndex
        val fingerPositionX = event.getX(pointerIndex).toInt()
        val fingerPositionY = event.getY(pointerIndex).toInt()

        // Maybe combine Button and Joystick as subclasses of the same parent?
        // Or maybe create an interface like IMoveableHUDControl?
        overlayButtons.forEach {
            // Determine the button state to apply based on the MotionEvent action flag.
            when (event.action and MotionEvent.ACTION_MASK) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN ->
                    // If no button is being moved now, remember the currently touched button to move.
                    if (buttonBeingConfigured == null &&
                        it.bounds.contains(fingerPositionX, fingerPositionY)
                    ) {
                        buttonBeingConfigured = it
                        buttonBeingConfigured!!.onConfigureTouch(event)
                    }

                MotionEvent.ACTION_MOVE -> if (buttonBeingConfigured != null) {
                    buttonBeingConfigured!!.onConfigureTouch(event)
                    invalidate()
                    return true
                }

                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> if (buttonBeingConfigured ==
                    it
                ) {
                    // Persist button position by saving new place.
                    saveControlPosition(
                        buttonBeingConfigured!!.setting,
                        buttonBeingConfigured!!.bounds.left,
                        buttonBeingConfigured!!.bounds.top
                    )
                    buttonBeingConfigured = null
                }
            }
        }
        overlayDpads.forEach {
            // Determine the button state to apply based on the MotionEvent action flag.
            when (event.action and MotionEvent.ACTION_MASK) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN ->
                    // If no button is being moved now, remember the currently touched button to move.
                    if (buttonBeingConfigured == null &&
                        it.bounds.contains(fingerPositionX, fingerPositionY)
                    ) {
                        dpadBeingConfigured = it
                        dpadBeingConfigured!!.onConfigureTouch(event)
                    }

                MotionEvent.ACTION_MOVE -> if (dpadBeingConfigured != null) {
                    dpadBeingConfigured!!.onConfigureTouch(event)
                    invalidate()
                    return true
                }

                MotionEvent.ACTION_UP,
                MotionEvent.ACTION_POINTER_UP -> if (dpadBeingConfigured == it) {
                    // Persist button position by saving new place.
                    saveControlPosition(
                        dpadBeingConfigured!!.setting,
                        dpadBeingConfigured!!.bounds.left,
                        dpadBeingConfigured!!.bounds.top
                    )
                    dpadBeingConfigured = null
                }
            }
        }
        overlayJoysticks.forEach {
            when (event.action) {
                MotionEvent.ACTION_DOWN,
                MotionEvent.ACTION_POINTER_DOWN -> if (joystickBeingConfigured == null &&
                    it.bounds.contains(fingerPositionX, fingerPositionY)
                ) {
                    joystickBeingConfigured = it
                    joystickBeingConfigured!!.onConfigureTouch(event)
                }

                MotionEvent.ACTION_MOVE -> if (joystickBeingConfigured != null) {
                    joystickBeingConfigured!!.onConfigureTouch(event)
                    invalidate()
                }

                MotionEvent.ACTION_UP,
                MotionEvent.ACTION_POINTER_UP -> if (joystickBeingConfigured != null) {
                    saveControlPosition(
                        joystickBeingConfigured!!.setting,
                        joystickBeingConfigured!!.bounds.left,
                        joystickBeingConfigured!!.bounds.top
                    )
                    joystickBeingConfigured = null
                }
            }
        }
        return true
    }

    private fun addOverlayControls() {
        InputOverlaySetting.entries.forEach {
            val element = CitraSettings.activeSettings.get(it) ?: it.defaultValue
            if (element.visible) {
                when (it.spec) {
                    is InputOverlaySpec.Button -> {
                        overlayButtons.add(
                            initializeOverlayButton(it)
                        )
                    }

                    is InputOverlaySpec.Dpad -> {
                        overlayDpads.add(
                            initializeOverlayDpad(it)
                        )
                    }

                    else -> {
                        overlayJoysticks.add(
                            initializeOverlayJoystick(it)
                        )
                    }
                }
            }
        }
    }

    fun refreshControls() {
        // Remove all the overlay buttons from the HashSet.
        overlayButtons.clear()
        overlayDpads.clear()
        overlayJoysticks.clear()
        if (width == 0 || height == 0) return
        // Add all the enabled overlay items back to the HashSet.
        if (EmulationMenuSettings.showOverlay) {
            addOverlayControls()
        }
        invalidate()
    }

    /** x and y here are in the view, so this converts to milles and saves the setting */
    private fun saveControlPosition(setting: InputOverlaySetting, x: Int, y: Int) {
        val currentValue = CitraSettings.activeSettings.get(setting) ?: setting.defaultValue
        val xMilles = ((x.toFloat() / width) * 1000).toInt()
        val yMilles = ((y.toFloat() / height) * 1000).toInt()
        val newValue = if (isPortrait) {
            currentValue.copy(portraitX = xMilles, portraitY = yMilles)
        } else {
            currentValue.copy(landscapeX = xMilles, landscapeY = yMilles)
        }
        CitraSettings.activeSettings.setByCurrentState(setting, newValue)
    }

    fun setIsInEditMode(isInEditMode: Boolean) {
        val wasEditing = this.isInEditMode
        this.isInEditMode = isInEditMode
        // if we are leaving edit mode, save the settings to the file
        if (wasEditing && !isInEditMode) {
            SettingsFile.saveGlobalFile(CitraSettings.activeSettings)
            SettingsFile.saveCustomFile(CitraSettings.activeSettings)
        }
    }

    fun resetButtonPlacement() {
        InputOverlaySetting.entries.forEach {
            CitraSettings.activeSettings.setByCurrentState(it, it.defaultValue)
        }
        refreshControls()
    }

    override fun isInEditMode(): Boolean = isInEditMode

    /**
     * Resizes a [Bitmap] at a specific square size
     * @param drawableId    The ID of the drawable to scale.
     * @param pxSize         The size of the desired bitmap in pixels.
     * @return The scaled [Bitmap]
     */
    private fun getBitmap(drawableId: Int, pxSize: Int): Bitmap {
        var bitmap = BitmapFactory.decodeResource(
            resources,
            drawableId
        )?.scale(pxSize, pxSize, true)
        if (bitmap != null) return bitmap

        val vectorDrawable = ContextCompat.getDrawable(context, drawableId) as VectorDrawable

        bitmap = Bitmap.createBitmap(pxSize, pxSize, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        vectorDrawable.setBounds(0, 0, pxSize, pxSize)
        vectorDrawable.draw(canvas)
        return bitmap
    }

    /** Returns the size in pixels for this device, given the scaled size out of 1000 */
    private fun sizeInPixels(size: Int): Int = (size / 1000f * min(width, height)).toInt()

    private val isPortrait get() = resources.configuration.orientation ==
        Configuration.ORIENTATION_PORTRAIT

    private fun pixelPosition(element: InputOverlayElement): Pair<Int, Int> {
        val x = if (isPortrait) element.portraitX else element.landscapeX
        val y = if (isPortrait) element.portraitY else element.landscapeY
        return Pair((x / 1000f * width).toInt(), (y / 1000f * height).toInt())
    }

    /**
     * Initializes an InputOverlayDrawableButton, given by resId, with all of the
     * parameters set for it to be properly shown on the InputOverlay.
     *
     * Technically no modifications should need to be performed on the returned
     * InputOverlayDrawableButton. Simply add it to the HashSet of overlay items and wait
     * for Android to call the onDraw method.
     *
     * @param setting       The InputOverlaySetting that stores the informatino about the overlay element
     * @return An [InputOverlayDrawableButton] with the correct drawing bounds set.
     */
    private fun initializeOverlayButton(setting: InputOverlaySetting): InputOverlayDrawableButton {
        val values =
            CitraSettings.activeSettings.get(setting) ?: setting.defaultValue
        val spec = setting.spec as InputOverlaySpec.Button
        val pxSize = sizeInPixels(values.size)
        // Initialize the InputOverlayDrawableButton.
        val defaultStateBitmap =
            getBitmap(spec.buttonResId, pxSize)
        val pressedStateBitmap =
            getBitmap(spec.pressedButtonResId, pxSize)
        val overlayDrawable =
            InputOverlayDrawableButton(
                resources,
                defaultStateBitmap,
                pressedStateBitmap,
                setting
            )

        // The X and Y coordinates of the InputOverlayDrawableButton on the InputOverlay.
        // These were set in the input overlay configuration menu.
        val (drawableX, drawableY) = pixelPosition(values)

        // Now set the bounds for the InputOverlayDrawableButton.
        // This will dictate where on the screen (and the what the size) the InputOverlayDrawableButton will be.
        overlayDrawable.setBounds(
            drawableX,
            drawableY,
            drawableX + pxSize,
            drawableY + pxSize
        )

        // Need to set the image's position
        overlayDrawable.setPosition(drawableX, drawableY)
        return overlayDrawable
    }

    /**
     * Initializes an [InputOverlayDrawableDpad]
     *
     * @param setting                   The InputOverlaySetting used for data.
     * @return the initialized [InputOverlayDrawableDpad]
     */
    private fun initializeOverlayDpad(setting: InputOverlaySetting): InputOverlayDrawableDpad {
        val values = CitraSettings.activeSettings.get(setting) ?: setting.defaultValue
        val spec = setting.spec as InputOverlaySpec.Dpad
        val pxSize = sizeInPixels(values.size)

        val defaultStateBitmap = getBitmap(spec.dpadResId, pxSize)
        val pressedOneDirectionStateBitmap =
            getBitmap(spec.pressedOneDirectionResId, pxSize)
        val pressedTwoDirectionsStateBitmap =
            getBitmap(spec.pressedTwoDirectionsResId, pxSize)
        val overlayDrawable = InputOverlayDrawableDpad(
            resources,
            defaultStateBitmap,
            pressedOneDirectionStateBitmap,
            pressedTwoDirectionsStateBitmap,
            setting
        )

        // The X and Y coordinates of the InputOverlayDrawableDpad on the InputOverlay.
        // These were set in the input overlay configuration menu.
        val (drawableX, drawableY) = pixelPosition(values)

        // Now set the bounds for the InputOverlayDrawableDpad.
        // This will dictate where on the screen (and the what the size) the InputOverlayDrawableDpad will be.
        overlayDrawable.setBounds(drawableX, drawableY, drawableX + pxSize, drawableY + pxSize)

        // Need to set the image's position
        overlayDrawable.setPosition(drawableX, drawableY)
        return overlayDrawable
    }

    /**
     * Initializes an [InputOverlayDrawableJoystick]
     *
     * @param setting        The InputOverlaySetting for the joystick
     * @return the initialized [InputOverlayDrawableJoystick].
     */
    private fun initializeOverlayJoystick(
        setting: InputOverlaySetting
    ): InputOverlayDrawableJoystick {
        val values = CitraSettings.activeSettings.get(setting) ?: setting.defaultValue
        val spec = setting.spec as InputOverlaySpec.Joystick
        val pxSize = sizeInPixels(values.size)

        val bitmapOuter = getBitmap(spec.outerResId, pxSize)
        val bitmapInnerDefault = getBitmap(spec.defaultInnerResId, pxSize)
        val bitmapInnerPressed = getBitmap(spec.pressedInnerResId, pxSize)

        val (drawableX, drawableY) = pixelPosition(values)

        // Now set the bounds for the InputOverlayDrawableJoystick.
        // This will dictate where on the screen (and the what the size) the InputOverlayDrawableJoystick will be.
        val outerSize = bitmapOuter.width
        val outerRect = Rect(
            drawableX,
            drawableY,
            drawableX + pxSize,
            drawableY + pxSize
        )
        val innerRect = Rect(0, 0, pxSize, pxSize)

        // Send the drawableId to the joystick so it can be referenced when saving control position.
        val overlayDrawable = InputOverlayDrawableJoystick(
            resources,
            bitmapOuter,
            bitmapInnerDefault,
            bitmapInnerPressed,
            outerRect,
            innerRect,
            setting
        )

        // Need to set the image's position
        overlayDrawable.setPosition(drawableX, drawableY)
        return overlayDrawable
    }
}
