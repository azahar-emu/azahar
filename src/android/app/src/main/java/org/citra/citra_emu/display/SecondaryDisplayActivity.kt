// Copyright 2025-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.display

import android.app.ActivityOptions
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import androidx.activity.OnBackPressedCallback
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.utils.Log

/** Reports secondary surface events back to SecondaryDisplay regardless of the host used. */
interface SecondaryDisplayCallback {
    fun onSurfaceChanged(surface: Surface)
    fun onSurfaceDestroyed()
}

/**
 * Hosts the secondary 3DS screen on displays that do not support Presentation (e.g. the LG G8X
 * Dual Screen). A real Activity is launched onto the target display via
 * ActivityOptions.setLaunchDisplayId(), which needs android:resizeableActivity="true".
 * SecondaryDisplay owns the lifecycle and calls finishActive() before each new launch.
 */
class SecondaryDisplayActivity : AppCompatActivity() {
    companion object {
        private const val EXTRA_DISPLAY_ID = "display_id"

        private var activeInstance: SecondaryDisplayActivity? = null
        private var callback: SecondaryDisplayCallback? = null

        var currentDisplayId: Int = -1
            private set

        // Set between launch() and the host's onCreate so a display event racing the launch treats
        // it as already hosting instead of relaunching a duplicate.
        @Volatile
        private var launchPending: Boolean = false

        /**
         * True while a live host activity owns [displayId] (or one is being launched onto it). It
         * survives a surface cycle such as rotation (the activity is kept via configChanges) and
         * turns false only once the host is finishing or gone, so the manager can tell a working
         * host from one the system tore down on another display (e.g. after a Recents round-trip).
         */
        fun isHosting(displayId: Int): Boolean {
            if (currentDisplayId != displayId) return false
            if (launchPending) return true
            val instance = activeInstance ?: return false
            return !instance.isFinishing
        }

        fun launch(context: Context, displayId: Int, cb: SecondaryDisplayCallback) {
            // Set before starting so overlapping updateDisplay() calls dedupe on currentDisplayId
            // instead of launching duplicate activities.
            currentDisplayId = displayId
            callback = cb
            launchPending = true
            val options = ActivityOptions.makeBasic().apply { launchDisplayId = displayId }
            val intent = Intent(context, SecondaryDisplayActivity::class.java).apply {
                putExtra(EXTRA_DISPLAY_ID, displayId)
                addFlags(
                    Intent.FLAG_ACTIVITY_NEW_TASK or
                        Intent.FLAG_ACTIVITY_MULTIPLE_TASK or
                        Intent.FLAG_ACTIVITY_NO_ANIMATION
                )
            }
            try {
                context.startActivity(intent, options.toBundle())
                Log.info("SecondaryDisplayActivity launched on display $displayId")
            } catch (e: SecurityException) {
                // Some OEMs restrict which UID may launch onto a given display.
                Log.error("SecondaryDisplayActivity failed to launch on display $displayId: ${e.message}")
                callback = null
                currentDisplayId = -1
                launchPending = false
            }
        }

        fun finishActive() {
            activeInstance?.finish()
            activeInstance = null
            callback = null
            currentDisplayId = -1
            launchPending = false
        }

        fun sendToBack() {
            activeInstance?.moveTaskToBack(true)
        }

        fun bringToFront(context: Context) {
            val instance = activeInstance ?: return
            val displayId = currentDisplayId
            if (displayId < 0 || instance.isForeground) return
            val intent = Intent(context, SecondaryDisplayActivity::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT or Intent.FLAG_ACTIVITY_NO_ANIMATION)
            }
            val options = ActivityOptions.makeBasic().apply { launchDisplayId = displayId }
            context.startActivity(intent, options.toBundle())
        }
    }

    private lateinit var surfaceView: SurfaceView
    private var isForeground = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        activeInstance?.finish()
        activeInstance = this
        launchPending = false
        currentDisplayId = intent.getIntExtra(EXTRA_DISPLAY_ID, -1)
        Log.info("SecondaryDisplayActivity created for display $currentDisplayId")

        // This window may hold input focus on multi-display devices (its display can become the
        // focused one after a background/foreground switch). Keep it focusable and forward any
        // key/gamepad events it receives to EmulationActivity in dispatchKeyEvent()/
        // dispatchGenericMotionEvent() so the game keeps working without a tap on the main screen.
        window.setFlags(
            WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
            WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
        )
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowInsetsControllerCompat(window, window.decorView).let { controller ->
            controller.hide(WindowInsetsCompat.Type.systemBars())
            controller.systemBarsBehavior =
                WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }

        surfaceView = SurfaceView(this)
        surfaceView.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {}

            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                val surface = holder.surface
                if (surface != null && surface.isValid) {
                    callback?.onSurfaceChanged(surface)
                }
            }

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                callback?.onSurfaceDestroyed()
            }
        })

        // Top screen is not a touchscreen; consume without action.
        surfaceView.setOnTouchListener { _, _ -> true }

        // Only EmulationActivity's teardown finishes this activity; ignore back here.
        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() = Unit
        })

        setContentView(surfaceView)
    }

    override fun onPause() {
        isForeground = false
        super.onPause()
    }

    override fun onResume() {
        super.onResume()
        isForeground = true
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean =
        EmulationActivity.runningInstance()?.dispatchKeyEvent(event)
            ?: super.dispatchKeyEvent(event)

    // Forward joystick axes only; pointer/mouse events are not forwarded.
    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        if (event.source and InputDevice.SOURCE_CLASS_JOYSTICK == 0) {
            return super.dispatchGenericMotionEvent(event)
        }
        return EmulationActivity.runningInstance()?.dispatchGenericMotionEvent(event)
            ?: super.dispatchGenericMotionEvent(event)
    }

    override fun onDestroy() {
        Log.info("SecondaryDisplayActivity destroyed (display $currentDisplayId)")
        if (activeInstance == this) {
            activeInstance = null
            callback = null
            currentDisplayId = -1
            launchPending = false
        }
        super.onDestroy()
    }
}
