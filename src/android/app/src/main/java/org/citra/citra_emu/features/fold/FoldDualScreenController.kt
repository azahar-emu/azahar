// Copyright 2026 tahirwrth
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.features.fold

import android.annotation.SuppressLint
import android.app.Activity
import android.os.Binder
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import androidx.core.content.ContextCompat
import androidx.window.area.WindowAreaCapability
import androidx.window.area.WindowAreaController
import androidx.window.area.WindowAreaInfo
import androidx.window.area.WindowAreaPresentationSessionCallback
import androidx.window.area.WindowAreaSessionPresenter
import androidx.window.core.ExperimentalWindowApi
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.utils.Log

// shows L/R touch zones on a foldable phone's cover screen
@OptIn(ExperimentalWindowApi::class)
class FoldDualScreenController(
    private val activity: Activity,
    private val scope: CoroutineScope,
    private val onButtonEvent: (buttonType: Int, state: Int) -> Unit,
    private val onSessionClosed: () -> Unit
) : WindowAreaPresentationSessionCallback {

    private val windowAreaController = WindowAreaController.getOrCreate()
    private val executor = ContextCompat.getMainExecutor(activity)

    @Volatile
    private var presentationSession: WindowAreaSessionPresenter? = null
    private var collectorJob: Job? = null
    private var isStartingSession = false

    fun start() {
        collectorJob?.cancel()
        isStartingSession = false

        collectorJob = scope.launch {
            windowAreaController.windowAreaInfos.collectLatest { areaInfos ->
                val availableRearArea = areaInfos.firstOrNull { info ->
                    info.type == WindowAreaInfo.Type.TYPE_REAR_FACING &&
                        info.getCapability(WindowAreaCapability.Operation.OPERATION_PRESENT_ON_AREA)
                            .status == WindowAreaCapability.Status.WINDOW_AREA_STATUS_AVAILABLE
                }
                if (availableRearArea != null) {
                    activity.runOnUiThread { startCoverControls(availableRearArea) }
                }
            }
        }
    }

    fun stop() {
        collectorJob?.cancel()
        collectorJob = null
        try {
            presentationSession?.close()
        } catch (e: Exception) {
            Log.error("[FoldCover] Error closing session: ${e.message}")
        } finally {
            presentationSession = null
            isStartingSession = false
        }
    }

    @Synchronized
    private fun startCoverControls(area: WindowAreaInfo) {
        if (presentationSession != null || isStartingSession) return

        val token: Binder = area.token
        isStartingSession = true
        try {
            windowAreaController.presentContentOnWindowArea(
                token = token,
                activity = activity,
                executor = executor,
                windowAreaPresentationSessionCallback = this
            )
        } catch (e: Exception) {
            Log.error("[FoldCover] Failed to start presentation: ${e.message}")
            isStartingSession = false
        }
    }

    @SuppressLint("InflateParams")
    override fun onSessionStarted(session: WindowAreaSessionPresenter) {
        presentationSession = session
        isStartingSession = false

        try {
            val coverView = LayoutInflater.from(session.context)
                .inflate(R.layout.fold_cover_controls, null)
            coverView.isFocusable = false
            coverView.isFocusableInTouchMode = false
            bindShoulderTouchZones(coverView)
            session.setContentView(coverView)
        } catch (e: Exception) {
            Log.error("[FoldCover] Error setting cover view: ${e.message}")
        }
    }

    override fun onSessionEnded(t: Throwable?) {
        if (t != null) {
            Log.error("[FoldCover] Session ended with error: ${t.message}")
        }
        presentationSession = null
        isStartingSession = false
        onSessionClosed()
    }

    override fun onContainerVisibilityChanged(isVisible: Boolean) {
        if (!isVisible) {
            onButtonEvent(NativeLibrary.ButtonType.TRIGGER_L, NativeLibrary.ButtonState.RELEASED)
            onButtonEvent(NativeLibrary.ButtonType.TRIGGER_R, NativeLibrary.ButtonState.RELEASED)
        }
    }

    private fun bindShoulderTouchZones(rootView: View) {
        setupTouch(rootView.findViewById(R.id.touch_zone_l), NativeLibrary.ButtonType.TRIGGER_R)
        setupTouch(rootView.findViewById(R.id.touch_zone_r), NativeLibrary.ButtonType.TRIGGER_L)
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun setupTouch(view: View?, buttonType: Int) {
        view?.setOnTouchListener { v, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                    v.setBackgroundColor(PRESSED_COLOR)
                    onButtonEvent(buttonType, NativeLibrary.ButtonState.PRESSED)
                    true
                }

                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                    v.setBackgroundColor(IDLE_COLOR)
                    onButtonEvent(buttonType, NativeLibrary.ButtonState.RELEASED)
                    true
                }

                else -> false
            }
        }
    }

    private companion object {
        const val IDLE_COLOR = 0x33FFFFFF
        const val PRESSED_COLOR = 0x88FFFFFF.toInt()
    }
}
