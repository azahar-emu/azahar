package org.citra.citra_emu.features.fold

import android.app.Activity
import android.graphics.Bitmap
import android.os.Binder
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
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.launch
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.utils.Log

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
        Log.info("[FoldDualScreenController] Initializing collector...")
        collectorJob?.cancel()
        collectorJob = scope.launch {
            windowAreaController.windowAreaInfos
                .distinctUntilChanged()
                .collect { areaInfos ->
                    Log.info("[FoldDualScreenController] WindowAreaInfos updated, count = ${areaInfos.size}")

                    targetAreaInfo = areaInfos.firstOrNull { info ->
                        val capability = info.getCapability(WindowAreaCapability.Operation.OPERATION_PRESENT_ON_AREA)
                        Log.info("[FoldDualScreenController] Area ${info.type} capability status: ${capability.status}")
                        capability.status == WindowAreaCapability.Status.WINDOW_AREA_STATUS_AVAILABLE
                    }

                    if (targetAreaInfo != null) {
                        Log.info("[FoldDualScreenController] Target area available! Trying to start controls...")
                        if (presentationSession == null && !isStartingSession) {
                            activity.runOnUiThread {
                                startCoverControls()
                            }
                        }
                    } else {
                        Log.info("[FoldDualScreenController] No available window area for presentation currently.")
                    }
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
            Log.error("[FoldDualScreenController] Cannot start: targetAreaInfo is null")
            return
        }

        val token: Binder? = area.token
        if (token == null) {
            Log.error("[FoldDualScreenController] Cannot start: Area token is null")
            return
        }

        if (presentationSession != null || isStartingSession) {
            Log.info("[FoldDualScreenController] Session already starting or active.")
            return
        }

        try {
            isStartingSession = true
            Log.info("[FoldDualScreenController] Calling presentContentOnWindowArea...")
            windowAreaController.presentContentOnWindowArea(
                token = token,
                activity = activity,
                executor = executor,
                windowAreaPresentationSessionCallback = this
            )
        } catch (e: Exception) {
            Log.error("[FoldDualScreenController] Exception starting session: ${e.message}")
            isStartingSession = false
        }
    }

    @Synchronized
    fun stopCoverControls() {
        try {
            Log.info("[FoldDualScreenController] Stopping cover controls...")
            presentationSession?.close()
        } catch (e: Exception) {
            Log.error("[FoldDualScreenController] Error closing session: ${e.message}")
        } finally {
            presentationSession = null
            isStartingSession = false
        }
    }

    override fun onSessionStarted(session: WindowAreaSessionPresenter) {
        Log.info("[FoldDualScreenController] onSessionStarted triggered!")
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
            Log.info("[FoldDualScreenController] Cover view successfully attached to session!")
        } catch (e: Exception) {
            Log.error("[FoldDualScreenController] Failed inflating cover content: ${e.message}")
        }
    }

    override fun onSessionEnded(t: Throwable?) {
        Log.info("[FoldDualScreenController] onSessionEnded. Reason: ${t?.message}")
        presentationSession = null
        isStartingSession = false
    }

    override fun onContainerVisibilityChanged(isVisible: Boolean) {
        Log.info("[FoldDualScreenController] onContainerVisibilityChanged: isVisible = $isVisible")
        if (!isVisible) {
            onButtonEvent(NativeLibrary.ButtonType.TRIGGER_L, NativeLibrary.ButtonState.RELEASED)
            onButtonEvent(NativeLibrary.ButtonType.TRIGGER_R, NativeLibrary.ButtonState.RELEASED)
        }
    }

    private fun bindShoulderTouchZones(rootView: View) {
        setupTouch(rootView.findViewById(R.id.touch_zone_l), NativeLibrary.ButtonType.TRIGGER_L)
        setupTouch(rootView.findViewById(R.id.touch_zone_r), NativeLibrary.ButtonType.TRIGGER_R)
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
