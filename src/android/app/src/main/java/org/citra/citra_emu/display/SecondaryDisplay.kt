// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.display

import android.app.Presentation
import android.content.Context
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Display
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.NativeLibrary

class SecondaryDisplay(val context: Context) : DisplayManager.DisplayListener {
    private var pres: SecondaryDisplayPresentation? = null
    private val displayManager = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
    private val vd: VirtualDisplay
    // Track DP displays separately for profile matching (e.g., XREAL via USB-C)
    @Volatile private var lastDPDisplayName: String? = null
    // Handler for debouncing display profile changes
    private val handler = Handler(Looper.getMainLooper())
    private var pendingProfileChange: Runnable? = null
    private companion object {
        const val PROFILE_CHANGE_DEBOUNCE_MS = 300L
    }

    init {
        vd = displayManager.createVirtualDisplay(
            "HiddenDisplay",
            1920,
            1080,
            320,
            null,
            DisplayManager.VIRTUAL_DISPLAY_FLAG_PRESENTATION
        )
        displayManager.registerDisplayListener(this, null)

        // Initialize the display profile manager
        DisplayProfileManager.initialize()

        // Check for already connected DP display at init
        val currentDPDisplay = getDPDisplayForProfileMatching()
        if (currentDPDisplay != null) {
            lastDPDisplayName = null  // Force the "connected" path
            handleDisplayProfileChange(currentDPDisplay)
        }
    }

    fun updateSurface() {
        pres?.let {
            NativeLibrary.secondarySurfaceChanged(it.getSurfaceHolder().surface)
        }
    }

    fun destroySurface() {
        NativeLibrary.secondarySurfaceDestroyed()
    }

    private fun getExternalDisplay(context: Context): Display? {
        val dm = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
        val currentDisplayId = context.display?.displayId ?: Display.DEFAULT_DISPLAY
        val displays = dm.displays
        val presDisplays = dm.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)

        val extDisplays = displays.filter {
            val isPresentable = presDisplays.any { pd -> pd.displayId == it.displayId }
            val isExternal = it.displayId != Display.DEFAULT_DISPLAY && it.displayId != currentDisplayId
            val isUsable = it.name != "HiddenDisplay" && it.state != Display.STATE_OFF
            // EXCLUDE DP/USB-C displays - those are for mirroring the main screen, not for SecondaryDisplay
            val isDPDisplay = it.name.contains("DP", true)

            (isPresentable || isExternal) && isUsable && !isDPDisplay
        }

        // Select first non-Built-in display for SecondaryDisplay
        return extDisplays.firstOrNull { !it.name.contains("Built", true) }
            ?: extDisplays.firstOrNull()
    }

    /**
     * Get the product name of a display for profile matching.
     * Uses deviceProductInfo.name (e.g., "XREAL One") when available (API 31+),
     * falls back to display.name (e.g., "DP Screen") on older APIs.
     */
    private fun getDisplayProductName(display: Display): String {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val productInfo = display.deviceProductInfo
            val productName = productInfo?.name
            if (!productName.isNullOrBlank()) {
                return productName
            }
        }
        return display.name
    }

    /**
     * Get the DP/USB-C display for profile matching (separate from SecondaryDisplay selection).
     * Returns the product name of any connected DP display for automatic profile application.
     */
    private fun getDPDisplayForProfileMatching(): String? {
        val dm = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
        val displays = dm.displays

        for (display in displays) {
            if (display.name.contains("DP", true) &&
                display.state != Display.STATE_OFF &&
                display.name != "HiddenDisplay") {
                return getDisplayProductName(display)
            }
        }
        return null
    }

    fun updateDisplay() {
        // return early if the parent context is dead or dying
        if (context is android.app.Activity && (context.isFinishing || context.isDestroyed)) {
            return
        }

        // Check for DP display for profile matching (tracked separately from SecondaryDisplay)
        val dpDisplayName = getDPDisplayForProfileMatching()
        handleDisplayProfileChange(dpDisplayName)

        // decide if we are going to the external display or the internal one
        var display = getExternalDisplay(context)

        if (display == null ||
            IntSetting.SECONDARY_DISPLAY_LAYOUT.int == SecondaryDisplayLayout.NONE.int) {
            display = vd.display
        }

        // if our presentation is already on the right display, ignore
        if (pres?.display == display) return

        // otherwise, make a new presentation
        releasePresentation()

        try {
            pres = SecondaryDisplayPresentation(context, display!!, this)
            pres?.show()
        }
        // catch BadTokenException and InvalidDisplayException,
        // the display became invalid asynchronously, so we can assign to null
        // until onDisplayAdded/Removed/Changed is called and logic retriggered
        catch (_: WindowManager.BadTokenException) {
            pres = null
        } catch (_: WindowManager.InvalidDisplayException) {
            pres = null
        }
    }

    /**
     * Handle display profile changes when DP display connects/disconnects.
     * Only tracks DP displays (like XREAL via USB-C), not regular external displays.
     * Uses debouncing to avoid race conditions from rapid connect/disconnect events.
     */
    private fun handleDisplayProfileChange(currentDPDisplayName: String?) {
        val previousDPDisplayName = lastDPDisplayName

        // Skip if no change
        if (previousDPDisplayName == currentDPDisplayName) return

        // Cancel any pending profile change
        pendingProfileChange?.let { handler.removeCallbacks(it) }

        // Debounce the profile change to avoid race conditions
        pendingProfileChange = Runnable {
            val latestDPDisplayName = getDPDisplayForProfileMatching()
            val storedPreviousName = lastDPDisplayName
            lastDPDisplayName = latestDPDisplayName

            when {
                // DP display connected (was null, now has a name)
                storedPreviousName == null && latestDPDisplayName != null -> {
                    DisplayProfileManager.onDisplayConnected(latestDPDisplayName)
                }
                // DP display disconnected (had a name, now null)
                storedPreviousName != null && latestDPDisplayName == null -> {
                    DisplayProfileManager.onDisplayDisconnected()
                }
                // DP display changed to a different one
                storedPreviousName != null && latestDPDisplayName != null &&
                        storedPreviousName != latestDPDisplayName -> {
                    DisplayProfileManager.onDisplayDisconnected()
                    DisplayProfileManager.onDisplayConnected(latestDPDisplayName)
                }
            }
            pendingProfileChange = null
        }
        handler.postDelayed(pendingProfileChange!!, PROFILE_CHANGE_DEBOUNCE_MS)
    }

    /**
     * Get the name of the currently connected DP display (if any) for profile matching.
     */
    fun getConnectedDisplayName(): String? {
        return getDPDisplayForProfileMatching()
    }

    fun releasePresentation() {
        try {
            pres?.dismiss()
        } catch (_: Exception) { }
        pres = null
    }

    fun releaseVD() {
        displayManager.unregisterDisplayListener(this)
        vd.release()
    }

    override fun onDisplayAdded(displayId: Int) {
        updateDisplay()
    }

    override fun onDisplayRemoved(displayId: Int) {
        updateDisplay()
    }
    override fun onDisplayChanged(displayId: Int) {
        updateDisplay()
    }
}
class SecondaryDisplayPresentation(
    context: Context, display: Display, val parent: SecondaryDisplay
) : Presentation(context, display) {
    private lateinit var surfaceView: SurfaceView
    private var touchscreenPointerId = -1

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window?.setFlags(
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
        )

        // Initialize SurfaceView
        surfaceView = SurfaceView(context)
        surfaceView.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {

            }

            override fun surfaceChanged(
                holder: SurfaceHolder, format: Int, width: Int, height: Int
            ) {
                parent.updateSurface()
            }

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                parent.destroySurface()
            }
        })

        this.surfaceView.setOnTouchListener { _, event ->

            val pointerIndex = event.actionIndex
            val pointerId = event.getPointerId(pointerIndex)
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                    if (touchscreenPointerId == -1) {
                        touchscreenPointerId = pointerId
                        NativeLibrary.onSecondaryTouchEvent(
                            event.getX(pointerIndex),
                            event.getY(pointerIndex),
                            true
                        )
                    }
                }

                MotionEvent.ACTION_MOVE -> {
                    val index = event.findPointerIndex(touchscreenPointerId)
                    if (index != -1) {
                        NativeLibrary.onSecondaryTouchMoved(
                            event.getX(index),
                            event.getY(index)
                        )
                    }
                }

                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                    if (pointerId == touchscreenPointerId) {
                        NativeLibrary.onSecondaryTouchEvent(0f, 0f, false)
                        touchscreenPointerId = -1
                    }
                }
            }
            true
        }

        setContentView(surfaceView) // Set SurfaceView as content
    }

    // Publicly accessible method to get the SurfaceHolder
    fun getSurfaceHolder(): SurfaceHolder {
        return surfaceView.holder
    }
}
