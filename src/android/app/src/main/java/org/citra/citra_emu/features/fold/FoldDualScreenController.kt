package org.citra.citra_emu.features.fold

import android.app.Activity
import android.graphics.Bitmap
import android.os.Binder
import android.util.Log
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.widget.ImageView
import androidx.core.content.ContextCompat
import androidx.window.area.WindowAreaCapability
import androidx.window.area.WindowAreaController
import androidx.window.area.WindowAreaInfo
import androidx.window.area.WindowAreaPresentationSessionCallback
import androidx.window.area.WindowAreaSessionPresenter
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R

class FoldDualScreenController(
    private val activity: Activity,
    private val scope: CoroutineScope,
    private val onButtonEvent: (buttonType: Int, state: Int) -> Unit
) : WindowAreaPresentationSessionCallback {

    private val windowAreaController = WindowAreaController.getOrCreate()
    private val executor = ContextCompat.getMainExecutor(activity)

    @Volatile
    private var presentationSession: WindowAreaSessionPresenter? = null
    private var targetAreaInfo: WindowAreaInfo? = null
    private var coverArtwork: Bitmap? = null
    private var collectorJob: Job? = null
    private var isStartingSession = false

    fun init() {
        Log.e("FoldController", "Initialising activity: $activity")

        collectorJob?.cancel()
        collectorJob = scope.launch {
            try {
                windowAreaController.windowAreaInfos.collectLatest { areaInfos ->
                    Log.e("FoldController", "WindowAreaInfos received. Count = ${areaInfos.size}")

                    for (info in areaInfos) {
                        val capability = info.getCapability(WindowAreaCapability.Operation.OPERATION_PRESENT_ON_AREA)
                        Log.e(
                            "FoldController",
                            "-> Area type: ${info.type}, Capability Status: ${capability.status}"
                        )
                    }

                    targetAreaInfo = areaInfos.firstOrNull { info ->
                        val capability = info.getCapability(WindowAreaCapability.Operation.OPERATION_PRESENT_ON_AREA)
                        capability.status == WindowAreaCapability.Status.WINDOW_AREA_STATUS_AVAILABLE
                    }

                    if (targetAreaInfo != null) {
                        Log.e("FoldController", "Target Area is available, requesting start")
                        if (presentationSession == null && !isStartingSession) {
                            activity.runOnUiThread {
                                startCoverControls()
                            }
                        }
                    } else {
                        Log.e("FoldController", "No available window area for presentation currently")
                    }
                }
            } catch (t: Throwable) {
                Log.e("FoldController", "Exception in windowAreaInfos flow", t)
            }
        }
    }

    fun setCoverArtwork(bitmap: Bitmap?) {
        coverArtwork = bitmap
    }

    @Synchronized
    fun startCoverControls() {
        val area = targetAreaInfo
        if (area == null) {
            Log.e("FoldController", "startCoverControls aborted: targetAreaInfo is null")
            return
        }

        val token: Binder? = area.token
        if (token == null) {
            Log.e("FoldController", "startCoverControls aborted: token is null")
            return
        }

        if (presentationSession != null || isStartingSession) {
            Log.e("FoldController", "Session already running or starting. Skipping.")
            return
        }

        try {
            isStartingSession = true
            Log.e("FoldController", "Calling presentContentOnWindowArea with token: $token")
            windowAreaController.presentContentOnWindowArea(
                token = token,
                activity = activity,
                executor = executor,
                windowAreaPresentationSessionCallback = this
            )
        } catch (t: Throwable) {
            Log.e("FoldController", "Failed to start window area presentation", t)
            isStartingSession = false
        }
    }

    @Synchronized
    fun stopCoverControls() {
        try {
            Log.e("FoldController", "Closing presentation session...")
            presentationSession?.close()
        } catch (t: Throwable) {
            Log.e("FoldController", "Error closing session", t)
        } finally {
            presentationSession = null
            isStartingSession = false
        }
    }

    override fun onSessionStarted(session: WindowAreaSessionPresenter) {
        Log.e("FoldController", ">>> onSessionStarted RECEIVED! <<<")
        presentationSession = session
        isStartingSession = false

        try {
            val coverView = LayoutInflater.from(session.context).inflate(R.layout.fold_cover_controls, null)

            val artworkImageView = coverView.findViewById<ImageView>(R.id.cover_artwork_view)
            if (coverArtwork != null) {
                artworkImageView?.setImageBitmap(coverArtwork)
            }

            bindShoulderTouchZones(coverView)
            session.setContentView(coverView)
            Log.e("FoldController", "setContentView applied successfully to Cover Screen!")
        } catch (t: Throwable) {
            Log.e("FoldController", "Error in onSessionStarted layout setup", t)
        }
    }

    override fun onSessionEnded(t: Throwable?) {
        Log.e("FoldController", "onSessionEnded called. Throwable: ${t?.message}")
        presentationSession = null
        isStartingSession = false
    }

    override fun onContainerVisibilityChanged(isVisible: Boolean) {
        Log.e("FoldController", "onContainerVisibilityChanged: isVisible = $isVisible")
        if (!isVisible) {
            onButtonEvent(NativeLibrary.ButtonType.TRIGGER_L, NativeLibrary.ButtonState.RELEASED)
            onButtonEvent(NativeLibrary.ButtonType.TRIGGER_R, NativeLibrary.ButtonState.RELEASED)
        }
    }

    private fun bindShoulderTouchZones(rootView: View) {
        setupTouch(rootView.findViewById(R.id.touch_zone_l), NativeLibrary.ButtonType.TRIGGER_R)
        setupTouch(rootView.findViewById(R.id.touch_zone_r), NativeLibrary.ButtonType.TRIGGER_L)
    }

    private fun setupTouch(view: View?, buttonType: Int) {
        view?.setOnTouchListener { v, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                    v.setBackgroundColor(0x88FFFFFF.toInt())
                    onButtonEvent(buttonType, NativeLibrary.ButtonState.PRESSED)
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                    v.setBackgroundColor(0x33FFFFFF)
                    onButtonEvent(buttonType, NativeLibrary.ButtonState.RELEASED)
                    true
                }
                else -> false
            }
        }
    }
}
