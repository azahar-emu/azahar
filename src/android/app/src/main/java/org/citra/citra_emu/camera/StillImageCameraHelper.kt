// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.camera

import android.graphics.Bitmap
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.Keep
import androidx.core.graphics.drawable.toBitmap
import coil.executeBlocking
import coil.imageLoader
import coil.request.ImageRequest
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary

// Used in native code.
object StillImageCameraHelper {
    private val filePickerLock = Object()
    private var filePickerPath: String? = null

    // Opens file picker for camera.
    @Keep
    @JvmStatic
    fun OpenFilePicker(): String? {
        val emulationActivity = NativeLibrary.sEmulationActivity.get()

        // At this point, we are assuming that we already have permissions as they are
        // needed to launch a game
        emulationActivity!!.runOnUiThread {
            val request = PickVisualMediaRequest.Builder()
                .setMediaType(ActivityResultContracts.PickVisualMedia.ImageOnly).build()
            emulationActivity.openImageLauncher.launch(request)
        }
        synchronized(filePickerLock) {
            try {
                filePickerLock.wait()
            } catch (ignored: InterruptedException) {
            }
        }
        return filePickerPath
    }

    // Called from EmulationActivity.
    @JvmStatic
    fun OnFilePickerResult(result: String) {
        filePickerPath = result
        synchronized(filePickerLock) { filePickerLock.notifyAll() }
    }

    // Blocking call. Load image from file and crop/resize it to fit in width x height.
    @Keep
    @JvmStatic
    fun LoadImageFromFile(uri: String?, width: Int, height: Int): Bitmap? {
        val context = CitraApplication.appContext
        val request = ImageRequest.Builder(context)
            .data(uri)
            .size(width, height)
            .allowHardware(false)
            .build()
        return context.imageLoader.executeBlocking(request).drawable?.toBitmap(
            width,
            height,
            Bitmap.Config.ARGB_8888
        )
    }
}
