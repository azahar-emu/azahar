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
import kotlinx.coroutines.flow.distinctUntilChanged
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
    private var presentationSession: WindowAreaSessionPresenter? = null
    private var targetAreaInfo: WindowAreaInfo? = null
    private var coverArtwork: Bitmap? = null

    fun init() {
        scope.launch {
            windowAreaController.windowAreaInfos
                .distinctUntilChanged()
                .collect { areaInfos ->
                    // Find folding cover screen area
                    targetAreaInfo = areaInfos.firstOrNull { info ->
                        val capability = info.getCapability(WindowAreaCapability.Operation.OPERATION_PRESENT_ON_AREA)
                        capability.status == WindowAreaCapability.Status.WINDOW_AREA_STATUS_AVAILABLE
                    }

                    if (targetAreaInfo != null && presentationSession == null) {
                        startCoverControls()
                    }
                }
        }
    }

    fun setCoverArtwork(bitmap: Bitmap?) {
        coverArtwork = bitmap
        // Update live if session is already running
        (presentationSession as? WindowAreaSessionPresenter)?.let { session ->
            val iv = session.context.resources
        }
    }

    fun startCoverControls() {
        val token: Binder? = targetAreaInfo?.token
        if (token != null && presentationSession == null) {
            windowAreaController.presentContentOnWindowArea(
                token = token,
                activity = activity,
                executor = executor,
                windowAreaPresentationSessionCallback = this
            )
        }
    }

    fun stopCoverControls() {
        presentationSession?.close()
        presentationSession = null
    }

    override fun onSessionStarted(session: WindowAreaSessionPresenter) {
        presentationSession = session

        val coverView = LayoutInflater.from(session.context).inflate(R.layout.fold_cover_controls, null)

        val artworkImageView = coverView.findViewById<ImageView>(R.id.cover_artwork_view)
        if (coverArtwork != null) {
            artworkImageView.setImageBitmap(coverArtwork)
        } else {
            // fallback to default drawable in res/drawable folder
            // artworkImageView.setImageResource(R.drawable.default_3ds_shell)
        }

        bindShoulderTouchZones(coverView)
        session.setContentView(coverView)
    }

    override fun onSessionEnded(t: Throwable?) {
        presentationSession = null
    }

    override fun onContainerVisibilityChanged(isVisible: Boolean) {
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
        view?.setOnTouchListener { _, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                    onButtonEvent(buttonType, NativeLibrary.ButtonState.PRESSED)
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                    onButtonEvent(buttonType, NativeLibrary.ButtonState.RELEASED)
                    true
                }
                else -> false
            }
        }
    }
}
