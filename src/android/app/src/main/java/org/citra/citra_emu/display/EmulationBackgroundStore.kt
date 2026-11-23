// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.display

import android.content.Context
import android.graphics.Bitmap
import android.graphics.ColorSpace
import android.graphics.ImageDecoder
import android.net.Uri
import android.util.AtomicFile
import java.io.File
import java.io.IOException
import java.io.InputStream
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sqrt
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

/** Owns a bounded, private copy of the selected image; no persistent document permission is needed. */
internal class EmulationBackgroundStore(private val context: Context) {
    private val file = AtomicFile(File(context.filesDir, "emulation-background.png"))
    private val mutex = Mutex()
    private var loaded = false
    private val currentImage = MutableStateFlow<Bitmap?>(null)
    val image = currentImage.asStateFlow()

    fun hasImage(): Boolean = file.baseFile.exists()

    suspend fun load() = withContext(Dispatchers.IO) {
        mutex.withLock {
            if (!loaded) {
                currentImage.value = try {
                    // openRead also recovers an interrupted AtomicFile replacement.
                    file.openRead().use { input -> decodeCopy(input) }
                } catch (_: IOException) {
                    null
                } catch (_: IllegalArgumentException) {
                    null
                }
                loaded = true
            }
        }
    }

    suspend fun importImage(uri: Uri) = withContext(Dispatchers.IO) {
        context.contentResolver.openInputStream(uri)?.use { importImage(it) }
            ?: throw IOException("Cannot open background image")
    }

    internal suspend fun importImage(input: InputStream) = withContext(Dispatchers.IO) {
        mutex.withLock {
            val bitmap = decodeCopy(input)
            val output = file.startWrite()
            try {
                if (!bitmap.compress(Bitmap.CompressFormat.PNG, 100, output)) {
                    throw IOException("Cannot save background image")
                }
                file.finishWrite(output)
            } catch (error: Exception) {
                file.failWrite(output)
                throw error
            }
            loaded = true
            currentImage.value = bitmap
        }
    }

    suspend fun remove() = withContext(Dispatchers.IO) {
        mutex.withLock {
            file.delete()
            if (file.baseFile.exists()) throw IOException("Cannot remove background image")
            loaded = true
            currentImage.value = null
        }
    }

    private fun decodeCopy(input: InputStream): Bitmap {
        val temporary = File.createTempFile("emulation-background-", null, context.cacheDir)
        try {
            temporary.outputStream().use { output ->
                val buffer = ByteArray(8192)
                var total = 0L
                while (true) {
                    val count = input.read(buffer)
                    if (count < 0) break
                    total += count
                    if (total >
                        MAX_INPUT_BYTES
                    ) {
                        throw IOException("Background image exceeds 32 MiB")
                    }
                    output.write(buffer, 0, count)
                }
            }
            return ImageDecoder.decodeBitmap(ImageDecoder.createSource(temporary)) {
                    decoder,
                    info,
                    _
                ->
                if (info.isAnimated || info.mimeType !in SUPPORTED_TYPES) {
                    throw IOException("Select a static PNG, JPEG or WebP image")
                }
                val width = info.size.width
                val height = info.size.height
                val scale = min(
                    1.0,
                    min(
                        MAX_DIMENSION.toDouble() / max(width, height),
                        sqrt(MAX_PIXELS / (width.toDouble() * height))
                    )
                )
                decoder.setTargetSize(
                    max(1, (width * scale).toInt()),
                    max(1, (height * scale).toInt())
                )
                decoder.allocator = ImageDecoder.ALLOCATOR_SOFTWARE
                decoder.setTargetColorSpace(ColorSpace.get(ColorSpace.Named.SRGB))
            }
        } finally {
            temporary.delete()
        }
    }

    companion object {
        internal const val MAX_INPUT_BYTES = 32L * 1024 * 1024
        private const val MAX_DIMENSION = 4096
        private const val MAX_PIXELS = 8_000_000.0
        val SUPPORTED_TYPES = setOf("image/png", "image/jpeg", "image/webp")

        @Volatile private var instance: EmulationBackgroundStore? = null

        fun get(context: Context): EmulationBackgroundStore = instance ?: synchronized(this) {
            instance ?: EmulationBackgroundStore(context.applicationContext).also { instance = it }
        }
    }
}
