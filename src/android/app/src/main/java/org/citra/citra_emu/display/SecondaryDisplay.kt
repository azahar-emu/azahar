// Copyright 2025-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.display

import android.app.Presentation
import android.content.Context
import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.os.Build
import android.os.Bundle
import android.view.Display
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.utils.Log

class SecondaryDisplay(val context: Context) : DisplayManager.DisplayListener,
    SecondaryDisplayCallback {
    private var pres: SecondaryDisplayPresentation? = null
    private var usingActivityFallback = false
    private val displayManager = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
    private val vd: VirtualDisplay
    var preferredDisplayId = -1
    var currentDisplayId = -1

    val availableDisplays: List<Display>
        get() = getSecondaryDisplays()

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
    }

    fun updateSurface() {
        val surface = pres?.getSurfaceHolder()?.surface
        if (surface != null && surface.isValid) {
            NativeLibrary.secondarySurfaceChanged(surface)
        } else {
            Log.warning("SecondaryDisplay Attempted to update null or invalid surface")
        }
    }

    fun destroySurface() {
        NativeLibrary.secondarySurfaceDestroyed()
    }

    // Invoked by SecondaryDisplayActivity when the secondary output is hosted by an Activity.
    override fun onSurfaceChanged(surface: Surface) {
        if (surface.isValid) {
            NativeLibrary.secondarySurfaceChanged(surface)
        } else {
            Log.warning("SecondaryDisplay Attempted to update null or invalid surface")
        }
    }

    override fun onSurfaceDestroyed() {
        NativeLibrary.secondarySurfaceDestroyed()
    }

    private fun getSecondaryDisplays(): List<Display> {
        val currentDisplayId = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            context.display.displayId
        } else {
            @Suppress("DEPRECATION")
            (context.getSystemService(Context.WINDOW_SERVICE) as WindowManager)
                .defaultDisplay.displayId
        }
        val result = displayManager.displays.filter {
            // Do not require DISPLAY_CATEGORY_PRESENTATION: some panels (e.g. LG G8X) are not
            // presentation-capable. updateDisplay() picks Presentation vs Activity per display.
            val kept =
                it.displayId != currentDisplayId &&
                it.name != "HiddenDisplay" &&
                it.state != Display.STATE_OFF &&
                it.isValid
            if (!kept) {
                // Debug-level: this list can change on every display event, so per-display
                // noise does not belong in the regular log.
                Log.debug(
                    "SecondaryDisplay getSecondaryDisplays: excluded ${it.displayId}:${it.name} " +
                        "current=$currentDisplayId state=${it.state} valid=${it.isValid}"
                )
            }
            kept
        }
        Log.debug("SecondaryDisplay getSecondaryDisplays: available=[${result.joinToString { "${it.displayId}:${it.name}" }}]")
        return result
    }

    fun updateDisplay() {
        // return early if the parent context is dead or dying
        if (context is android.app.Activity && (context.isFinishing || context.isDestroyed)) {
            return
        }

        // Snapshot once: avoid re-querying DisplayManager multiple times below, which
        // previously allowed a transient empty result (e.g. mid display-state change) to
        // throw an uncaught IndexOutOfBoundsException on availableDisplays[0].
        val displays = availableDisplays
        Log.info(
            "SecondaryDisplay updateDisplay: available=[${displays.joinToString { "${it.displayId}:${it.name}" }}] " +
                "layout=${IntSetting.SECONDARY_DISPLAY_LAYOUT.int} enabled=${BooleanSetting.ENABLE_SECONDARY_DISPLAY.boolean} " +
                "preferred=$preferredDisplayId"
        )

        val displayToUse: Display? = if (displays.isEmpty() ||
            // Theoretically, the NONE option is no longer selectable, but
            // I am leaving this in for backwards compatibility
            IntSetting.SECONDARY_DISPLAY_LAYOUT.int == SecondaryDisplayLayout.NONE.int ||
            !BooleanSetting.ENABLE_SECONDARY_DISPLAY.boolean
        ) {
            Log.info("SecondaryDisplay updateDisplay: falling back to HiddenDisplay (vd.display id=${vd.display?.displayId})")
            currentDisplayId = -1
            vd.display
        } else if (preferredDisplayId >= 0 &&
            displays.any { it.displayId == preferredDisplayId }
        ) {
            currentDisplayId = preferredDisplayId
            displays.first { it.displayId == preferredDisplayId }
        } else {
            val dm = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
            val default = dm.displays.first { it.displayId == Display.DEFAULT_DISPLAY }
            // prioritize displays that have a different name from the default display, as
            // some devices such as the Odin 2 create a permanent virtual display with the same
            // name as the default display that should be skipped in most cases
            currentDisplayId = displays.firstOrNull {
                it.name != default.name && !it.name.contains("Built", true)
            }?.displayId
                ?: displays.firstOrNull()?.displayId
                ?: -1
            if (currentDisplayId == -1) null else displays.first { it.displayId == currentDisplayId }
        }

        if (displayToUse == null) {
            Log.info("SecondaryDisplay updateDisplay: no display selected, releasing secondary output")
            releaseSecondaryOutput()
            return
        }

        val isPresentationCapable = displayManager
            .getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)
            .any { it.displayId == displayToUse.displayId }

        // if our current output is already on the right display via the right
        // mechanism, ignore
        if (isPresentationCapable && pres?.display == displayToUse) {
            Log.info("SecondaryDisplay updateDisplay: presentation already on display ${displayToUse.displayId}")
            return
        }
        if (!isPresentationCapable && SecondaryDisplayActivity.isHosting(displayToUse.displayId)) {
            Log.info("SecondaryDisplay updateDisplay: activity already hosting display ${displayToUse.displayId}")
            return
        }

        Log.info(
            "SecondaryDisplay updateDisplay: switching secondary output to display " +
                "${displayToUse.displayId} (${displayToUse.name}) " +
                "via ${if (isPresentationCapable) "Presentation" else "SecondaryDisplayActivity (fallback)"}"
        )
        // otherwise, create a new presentation (or activity fallback)
        releaseSecondaryOutput()

        // A real (visible) host will provide a surface, so let the emulator wait for it; the
        // hidden virtual display is not a real host and must not delay game start.
        val hostExpected = displayToUse.displayId != vd.display?.displayId
        NativeLibrary.setSecondaryHostExpected(hostExpected)
        Log.info("SecondaryDisplay updateDisplay: native host expected=$hostExpected")

        if (isPresentationCapable) {
            try {
                pres = SecondaryDisplayPresentation(context, displayToUse, this)
                pres?.show()
                Log.info("SecondaryDisplay updateDisplay: presentation shown on display ${displayToUse.displayId}")
            }
            // catch BadTokenException and InvalidDisplayException,
            // the display became invalid asynchronously, so we can assign to null
            // until onDisplayAdded/Removed/Changed is called and logic retriggered
            catch (_: WindowManager.BadTokenException) {
                pres = null
            } catch (_: WindowManager.InvalidDisplayException) {
                pres = null
            }
        } else {
            usingActivityFallback = true
            SecondaryDisplayActivity.launch(context, displayToUse.displayId, this)
        }
    }

    fun releaseSecondaryOutput() {
        try {
            pres?.dismiss()
        } catch (_: Exception) { }
        pres = null

        if (usingActivityFallback) {
            usingActivityFallback = false
            SecondaryDisplayActivity.finishActive()
        }

        // No secondary host going forward: never make the emulator wait for a surface.
        NativeLibrary.setSecondaryHostExpected(false)
    }

    fun releaseVD() {
        displayManager.unregisterDisplayListener(this)
        vd.release()
    }

    override fun onDisplayAdded(displayId: Int) {
        onDisplayEvent()
    }

    override fun onDisplayRemoved(displayId: Int) {
        onDisplayEvent()
    }

    override fun onDisplayChanged(displayId: Int) {
        onDisplayEvent()
    }

    // Display events fire while the app is backgrounded too (e.g. the cover screen's own launcher
    // takes over during Recents); reconciling then would relaunch the host onto the cover only for
    // the system to tear it down again, thrashing lifecycle state. Reconcile from display events
    // only while the emulation activity is in the foreground -- its onResume re-hosts on the way
    // back.
    private fun onDisplayEvent() {
        if (EmulationActivity.isForeground()) {
            updateDisplay()
        }
    }
}
class SecondaryDisplayPresentation(
    context: Context,
    display: Display,
    val parent: SecondaryDisplay
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
                Log.debug("SecondaryDisplay Surface created on display ${display.displayId}")
            }

            override fun surfaceChanged(
                holder: SurfaceHolder,
                format: Int,
                width: Int,
                height: Int
            ) {
                Log.debug("SecondaryDisplay Surface changed: ${width}x$height on display ${display.displayId}")
                parent.updateSurface()
            }

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                Log.debug("SecondaryDisplay Surface destroyed")
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
    fun getSurfaceHolder(): SurfaceHolder = surfaceView.holder
}
