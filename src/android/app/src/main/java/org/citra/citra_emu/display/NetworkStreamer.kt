// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.display

import android.content.Context
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.net.wifi.WifiManager
import android.os.Build
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.features.settings.model.BooleanSetting
import org.citra.citra_emu.features.settings.model.IntSetting
import org.citra.citra_emu.utils.Log
import java.io.DataInputStream
import java.io.DataOutputStream
import java.net.ServerSocket
import java.net.Socket

/**
 * Streams the emulated bottom screen to another Android device on the same network.
 *
 * Reuses Azahar's existing "secondary display" render path (the same one used to pop the
 * bottom screen out to a real second monitor): the core already knows how to render a chosen
 * screen layout into any Surface handed to it via [NativeLibrary.secondarySurfaceChanged].
 * Here that Surface is the input Surface of a hardware H.264 encoder instead of a SurfaceView,
 * so the GPU never has to round-trip through the CPU.
 */
class NetworkStreamer(private val context: Context) {
    companion object {
        private const val SERVICE_TYPE = "_azahar._tcp."
        private const val PORT = 27500
        private const val WIDTH = 320
        private const val HEIGHT = 240
        private const val BIT_RATE = 4_000_000
        private const val FRAME_RATE = 60
        private const val I_FRAME_INTERVAL = 1
    }

    private var encoder: MediaCodec? = null
    private var serverSocket: ServerSocket? = null
    private var clientSocket: Socket? = null
    private var serverThread: Thread? = null
    private var drainThread: Thread? = null
    private var multicastLock: WifiManager.MulticastLock? = null
    private var previousAspectRatio: Int = 0
    private val nsdManager by lazy {
        context.applicationContext.getSystemService(Context.NSD_SERVICE) as NsdManager
    }

    @Volatile
    private var running = false

    val isRunning: Boolean
        get() = running

    /** Starts encoding + advertising. Safe to call again after [stop]. */
    fun start() {
        if (running) return
        running = true

        // Make sure the core is actually rendering the bottom screen to the secondary window.
        IntSetting.SECONDARY_DISPLAY_LAYOUT.int = SecondaryDisplayLayout.BOTTOM_SCREEN.int
        BooleanSetting.ENABLE_SECONDARY_DISPLAY.boolean = true
        // Touch coordinates below are mapped assuming the bottom screen fills the encoder's
        // 320x240 surface exactly with no letterboxing, which only holds for the Default
        // aspect ratio (any other ratio insets bottom_screen and every touch lands wrong).
        previousAspectRatio = IntSetting.ASPECT_RATIO.int
        IntSetting.ASPECT_RATIO.int = 0
        NativeLibrary.reloadSettings()
        NativeLibrary.updateFramebuffer(NativeLibrary.isPortraitMode())

        val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, WIDTH, HEIGHT).apply {
            setInteger(
                MediaFormat.KEY_COLOR_FORMAT,
                MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface
            )
            setInteger(MediaFormat.KEY_BIT_RATE, BIT_RATE)
            setInteger(MediaFormat.KEY_FRAME_RATE, FRAME_RATE)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, I_FRAME_INTERVAL)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                setInteger(MediaFormat.KEY_LATENCY, 0)
            }
        }

        val codec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
        codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
        val inputSurface = codec.createInputSurface()
        codec.start()
        encoder = codec

        // EmulationActivity keeps its own hidden-virtual-display Presentation alive as a
        // placeholder for the secondary window. Its SurfaceView's surfaceDestroyed callback
        // calls NativeLibrary.secondarySurfaceDestroyed() unconditionally, which nulls out
        // whatever Surface is currently active natively - so it must be torn down BEFORE we
        // hand over the encoder's Surface, never after, or it wipes out our own assignment.
        // suppressUpdates also stops it from being recreated later by unrelated system display
        // events, which would otherwise silently steal the secondary window back from us.
        val secondaryDisplayManager = (context as? EmulationActivity)?.secondaryDisplayManager
        secondaryDisplayManager?.suppressUpdates = true
        secondaryDisplayManager?.releasePresentation()

        // This is the whole trick: hand the encoder's Surface to the same JNI entry point
        // the pop-out secondary display already uses.
        NativeLibrary.secondarySurfaceChanged(inputSurface)

        val wifi = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
        multicastLock = wifi.createMulticastLock("azahar-stream").apply {
            setReferenceCounted(true)
            acquire()
        }
        registerService()

        serverThread = Thread({ acceptLoop() }, "AzaharStream-Accept").apply { start() }
        drainThread = Thread({ drainLoop(codec) }, "AzaharStream-Drain").apply { start() }
    }

    fun stop() {
        if (!running) return
        running = false

        unregisterService()
        try {
            multicastLock?.release()
        } catch (_: Exception) {
        }
        multicastLock = null

        try {
            serverSocket?.close()
        } catch (_: Exception) {
        }
        try {
            clientSocket?.close()
        } catch (_: Exception) {
        }
        serverThread?.interrupt()
        drainThread?.interrupt()
        serverSocket = null
        clientSocket = null

        try {
            encoder?.stop()
            encoder?.release()
        } catch (_: Exception) {
        }
        encoder = null

        NativeLibrary.secondarySurfaceDestroyed()

        IntSetting.ASPECT_RATIO.int = previousAspectRatio
        NativeLibrary.reloadSettings()

        // Hand the secondary window back to the normal hidden-display placeholder so the
        // rest of the app behaves as if streaming had never touched it.
        val secondaryDisplayManager = (context as? EmulationActivity)?.secondaryDisplayManager
        secondaryDisplayManager?.suppressUpdates = false
        secondaryDisplayManager?.updateDisplay()
    }

    private var registrationListener: NsdManager.RegistrationListener? = null

    private fun registerService() {
        val info = NsdServiceInfo().apply {
            serviceName = "Azahar-${Build.MODEL}".take(63)
            serviceType = SERVICE_TYPE
            port = PORT
        }
        val listener = object : NsdManager.RegistrationListener {
            override fun onServiceRegistered(nsdServiceInfo: NsdServiceInfo) {
                Log.info("NetworkStreamer: service registered as ${nsdServiceInfo.serviceName}")
            }

            override fun onRegistrationFailed(nsdServiceInfo: NsdServiceInfo, errorCode: Int) {
                Log.error("NetworkStreamer: NSD registration failed ($errorCode)")
            }

            override fun onServiceUnregistered(nsdServiceInfo: NsdServiceInfo) {}
            override fun onUnregistrationFailed(nsdServiceInfo: NsdServiceInfo, errorCode: Int) {}
        }
        registrationListener = listener
        nsdManager.registerService(info, NsdManager.PROTOCOL_DNS_SD, listener)
    }

    private fun unregisterService() {
        val listener = registrationListener ?: return
        registrationListener = null
        try {
            nsdManager.unregisterService(listener)
        } catch (_: Exception) {
        }
    }

    private fun acceptLoop() {
        try {
            val server = ServerSocket(PORT)
            serverSocket = server
            while (running) {
                val socket = server.accept()
                Log.info("NetworkStreamer: viewer connected from ${socket.inetAddress}")
                try {
                    clientSocket?.close()
                } catch (_: Exception) {
                }
                clientSocket = socket
                // A viewer connecting after the encoder's one-time format-changed event
                // would otherwise never see the SPS/PPS and could never decode anything,
                // so replay the cached codec config to every newly (re)connected client.
                csd0?.let { sendChunk(socket, it, MediaCodec.BUFFER_FLAG_CODEC_CONFIG) }
                csd1?.let { sendChunk(socket, it, MediaCodec.BUFFER_FLAG_CODEC_CONFIG) }
                Thread({ touchLoop(socket) }, "AzaharStream-Touch").apply { start() }
            }
        } catch (_: Exception) {
            // Expected when stop() closes the socket.
        }
    }

    /**
     * Reads touch events the viewer sends back over the same socket and feeds them into
     * the same secondary-window touch hooks Azahar's own pop-out display already uses.
     * Wire format per event: 1 byte action (0=down, 1=move, 2=up) + 2 big-endian floats
     * with X/Y normalized to [0,1] - the viewer doesn't need to know our pixel resolution.
     */
    private fun touchLoop(socket: Socket) {
        val input = DataInputStream(socket.getInputStream())
        try {
            while (running && clientSocket === socket) {
                val action = input.readByte().toInt()
                val normX = input.readFloat()
                val normY = input.readFloat()
                val x = normX * WIDTH
                val y = normY * HEIGHT
                when (action) {
                    0 -> NativeLibrary.onSecondaryTouchEvent(x, y, true)
                    1 -> NativeLibrary.onSecondaryTouchMoved(x, y)
                    2 -> NativeLibrary.onSecondaryTouchEvent(0f, 0f, false)
                }
            }
        } catch (_: Exception) {
            // Expected once this client disconnects or a new one replaces it.
        }
    }

    private var csd0: ByteArray? = null
    private var csd1: ByteArray? = null

    // Synchronized: acceptLoop (codec-config replay) and drainLoop (frames) both call this
    // on the same socket from different threads, and writes must not interleave.
    @Synchronized
    private fun sendChunk(socket: Socket, bytes: ByteArray, flags: Int) {
        if (socket.isClosed) return
        try {
            // 4-byte big-endian length prefix so the receiver can split the TCP byte
            // stream back into the original access units, followed by the MediaCodec
            // buffer flags (BUFFER_FLAG_CODEC_CONFIG etc.) - hardware decoders need the
            // SPS/PPS chunk explicitly flagged as codec config to decode anything at all.
            val out = DataOutputStream(socket.getOutputStream())
            out.writeInt(bytes.size)
            out.writeInt(flags)
            out.write(bytes)
        } catch (_: Exception) {
            try {
                socket.close()
            } catch (_: Exception) {
            }
            if (clientSocket === socket) clientSocket = null
        }
    }

    // ponytail: single-client, best-effort delivery, no retry/backpressure handling.
    // Add framing + multi-client fan-out if more than one viewer needs to connect at once.
    private fun drainLoop(codec: MediaCodec) {
        val bufferInfo = MediaCodec.BufferInfo()
        while (running) {
            val outputIndex = try {
                codec.dequeueOutputBuffer(bufferInfo, 10_000)
            } catch (_: IllegalStateException) {
                break
            }

            if (outputIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                // Surface-input encoders deliver SPS/PPS via this one-time event, not as
                // a regular output buffer - it never shows up in dequeueOutputBuffer data.
                val format = codec.outputFormat
                csd0 = format.tryGetByteArray("csd-0")
                csd1 = format.tryGetByteArray("csd-1")
                val socket = clientSocket
                if (socket != null) {
                    csd0?.let { sendChunk(socket, it, MediaCodec.BUFFER_FLAG_CODEC_CONFIG) }
                    csd1?.let { sendChunk(socket, it, MediaCodec.BUFFER_FLAG_CODEC_CONFIG) }
                }
                continue
            }
            if (outputIndex < 0) continue

            val outputBuffer = codec.getOutputBuffer(outputIndex)
            if (outputBuffer != null && bufferInfo.size > 0) {
                val bytes = ByteArray(bufferInfo.size)
                outputBuffer.position(bufferInfo.offset)
                outputBuffer.get(bytes)
                clientSocket?.let { sendChunk(it, bytes, bufferInfo.flags) }
            }
            codec.releaseOutputBuffer(outputIndex, false)
        }
    }
}

private fun MediaFormat.tryGetByteArray(key: String): ByteArray? {
    val buffer = try {
        getByteBuffer(key)
    } catch (_: Exception) {
        null
    } ?: return null
    val bytes = ByteArray(buffer.remaining())
    buffer.get(bytes)
    return bytes
}
