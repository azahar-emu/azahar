// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.GLES20

object GraphicsUtil {
    private const val UNKNOWN_RENDERER = "Unknown"

    private lateinit var openGLRendererStringInternal: String

    val openGLRendererString: String
        @Synchronized
        get() {
            if (!::openGLRendererStringInternal.isInitialized) {
                var result = UNKNOWN_RENDERER
                val thread = Thread {
                    result = retrieveOpenGLRendererString()
                }
                thread.start()
                thread.join()
                openGLRendererStringInternal = result
            }
            return openGLRendererStringInternal
        }

    @Suppress("SimplifyBooleanWithConstants")
    private fun retrieveOpenGLRendererString(): String {
        var result: Boolean

        val glConfigAttribs = intArrayOf(
            EGL14.EGL_SURFACE_TYPE,
            EGL14.EGL_PBUFFER_BIT,
            EGL14.EGL_RENDERABLE_TYPE,
            EGL14.EGL_OPENGL_ES2_BIT,
            EGL14.EGL_NONE
        )
        val glDisplay = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
        if (glDisplay == EGL14.EGL_NO_DISPLAY) {
            return UNKNOWN_RENDERER
        }

        result = EGL14.eglInitialize(glDisplay, null, 0, null, 1)
        if (result == false) {
            return UNKNOWN_RENDERER
        }

        val glConfigs = arrayOfNulls<EGLConfig>(1)
        result =
            EGL14.eglChooseConfig(glDisplay, glConfigAttribs, 0, glConfigs, 0, 1, IntArray(1), 0)
        if (result == false) {
            EGL14.eglTerminate(glDisplay)
            return UNKNOWN_RENDERER
        }

        val glConfig = glConfigs[0]!!
        val glContextAttribs = intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE)
        val glContext = EGL14.eglCreateContext(
            glDisplay,
            glConfig,
            EGL14.EGL_NO_CONTEXT,
            glContextAttribs,
            0
        )
        if (glContext == EGL14.EGL_NO_CONTEXT) {
            EGL14.eglTerminate(glDisplay)
            return UNKNOWN_RENDERER
        }

        val glSurfaceAttribs = intArrayOf(
            EGL14.EGL_WIDTH,
            1,
            EGL14.EGL_HEIGHT,
            1,
            EGL14.EGL_NONE
        )
        val glSurface = EGL14.eglCreatePbufferSurface(glDisplay, glConfig, glSurfaceAttribs, 0)

        result = EGL14.eglMakeCurrent(glDisplay, glSurface, glSurface, glContext)
        if (result == false) {
            EGL14.eglDestroySurface(glDisplay, glSurface)
            EGL14.eglDestroyContext(glDisplay, glContext)
            EGL14.eglTerminate(glDisplay)
            return UNKNOWN_RENDERER
        }

        val rendererString = GLES20.glGetString(GLES20.GL_RENDERER) ?: UNKNOWN_RENDERER

        EGL14.eglMakeCurrent(
            glDisplay,
            EGL14.EGL_NO_SURFACE,
            EGL14.EGL_NO_SURFACE,
            EGL14.EGL_NO_CONTEXT
        )
        EGL14.eglDestroySurface(glDisplay, glSurface)
        EGL14.eglDestroyContext(glDisplay, glContext)
        EGL14.eglTerminate(glDisplay)

        return rendererString
    }

    fun isUsingAngleForOpenGL(): Boolean = (openGLRendererString.contains("ANGLE"))
}
