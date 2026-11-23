// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.display

import android.app.Activity
import android.app.Application
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Rect
import android.os.Looper
import android.widget.FrameLayout
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], manifest = Config.NONE, application = Application::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class EmulationBackgroundTest {
    private lateinit var context: Application
    private lateinit var store: EmulationBackgroundStore

    @Before fun setUp() = runBlocking {
        context = RuntimeEnvironment.getApplication()
        store = EmulationBackgroundStore(context)
        store.remove()
        EmulationBackgroundLayouts.update(false, 0, 0, null)
        EmulationBackgroundLayouts.update(true, 0, 0, null)
    }

    private fun png(width: Int = 8, height: Int = 8, color: Int = Color.RED): ByteArray {
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        bitmap.eraseColor(color)
        return ByteArrayOutputStream().use {
            check(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it))
            bitmap.recycle()
            it.toByteArray()
        }
    }

    private fun assertOnlyUnusedPixelsChange(width: Int, height: Int, screens: List<Rect>) {
        val original = IntArray(width * height) {
            0xff000000.toInt() or
                ((it * 31337) and 0x00ffffff)
        }
        val output = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        output.setPixels(original, 0, width, 0, 0, width, height)
        val background = Bitmap.createBitmap(2, 2, Bitmap.Config.ARGB_8888).apply {
            eraseColor(Color.WHITE)
        }
        EmulationBackgroundPainter.draw(
            Canvas(output),
            background,
            EmulationBackgroundLayout(width, height, screens),
            width,
            height,
            Paint()
        )
        val actual = IntArray(width * height)
        output.getPixels(actual, 0, width, 0, 0, width, height)
        for (y in 0 until height) {
            for (x in 0 until width) {
                val expected = if (screens.any {
                        it.contains(x, y)
                    }
                ) {
                    original[y * width + x]
                } else {
                    Color.WHITE
                }
                if (actual[y * width + x] !=
                    expected
                ) {
                    fail("Pixel ($x,$y) changed inside a game screen or was not filled outside it")
                }
            }
        }
    }

    @Test fun duoMarginsPreserveEveryGamePixel() {
        assertOnlyUnusedPixelsChange(1920, 1080, listOf(Rect(60, 0, 1860, 1080)))
    }

    @Test fun attachedViewsUseSeparateLayoutsAndReleaseOnDetach() = runBlocking {
        val shared = EmulationBackgroundStore.get(context)
        shared.remove()
        shared.importImage(ByteArrayInputStream(png()))
        val activity = Robolectric.buildActivity(Activity::class.java).setup()
        val main = EmulationBackgroundView(activity.get())
        val second = EmulationBackgroundView(activity.get()).apply { secondaryDisplay = true }
        val content = FrameLayout(activity.get()).apply {
            addView(main, FrameLayout.LayoutParams(80, 60))
            addView(second, FrameLayout.LayoutParams(80, 60))
        }
        activity.get().setContentView(content)
        main.layout(0, 0, 80, 60)
        second.layout(0, 0, 80, 60)
        EmulationBackgroundLayouts.update(false, 80, 60, intArrayOf(10, 0, 70, 60))
        EmulationBackgroundLayouts.update(true, 80, 60, intArrayOf(0, 0, 80, 60))
        Shadows.shadowOf(Looper.getMainLooper()).idle()
        fun render(view: EmulationBackgroundView): Bitmap =
            Bitmap.createBitmap(80, 60, Bitmap.Config.ARGB_8888).apply {
                eraseColor(Color.BLUE)
                view.draw(Canvas(this))
            }
        assertEquals(Color.RED, render(main).getPixel(0, 0))
        assertEquals(Color.BLUE, render(main).getPixel(40, 30))
        assertEquals(Color.BLUE, render(second).getPixel(0, 0))
        activity.get().setContentView(FrameLayout(activity.get()))
        assertEquals(Color.BLUE, render(main).getPixel(0, 0))
        activity.pause().stop().destroy()
        shared.remove()
    }

    @Test fun viewRespondsToLayoutChangesAndImageRemoval() = runBlocking<Unit> {
        val shared = EmulationBackgroundStore.get(context)
        shared.remove()
        shared.importImage(ByteArrayInputStream(png()))
        val activity = Robolectric.buildActivity(Activity::class.java).setup()
        val view = EmulationBackgroundView(activity.get())
        activity.get().setContentView(view, FrameLayout.LayoutParams(80, 60))
        view.layout(0, 0, 80, 60)
        fun render(): Bitmap = Bitmap.createBitmap(80, 60, Bitmap.Config.ARGB_8888).apply {
            eraseColor(Color.BLUE)
            view.draw(Canvas(this))
        }
        EmulationBackgroundLayouts.update(false, 80, 60, intArrayOf(10, 0, 70, 60))
        Shadows.shadowOf(Looper.getMainLooper()).idle()
        assertEquals(Color.RED, render().getPixel(0, 0))
        EmulationBackgroundLayouts.update(false, 80, 60, intArrayOf(0, 0, 80, 60))
        Shadows.shadowOf(Looper.getMainLooper()).idle()
        assertEquals(Color.BLUE, render().getPixel(0, 0))
        EmulationBackgroundLayouts.update(false, 80, 60, intArrayOf(10, 0, 70, 60))
        shared.remove()
        Shadows.shadowOf(Looper.getMainLooper()).idle()
        assertEquals(Color.BLUE, render().getPixel(0, 0))
        activity.pause().stop().destroy()
    }

    @Test fun dualScreenCustomAndHybridRegionsRemainUntouched() {
        assertOnlyUnusedPixelsChange(
            160,
            120,
            listOf(Rect(0, 0, 80, 48), Rect(90, 60, 150, 105), Rect(20, 25, 105, 90))
        )
    }

    @Test fun rotatedAndSwappedLayoutsUseTheirOwnCoordinates() {
        assertOnlyUnusedPixelsChange(90, 160, listOf(Rect(15, 5, 75, 85), Rect(3, 95, 87, 145)))
        assertOnlyUnusedPixelsChange(90, 160, listOf(Rect(3, 5, 87, 55), Rect(15, 75, 75, 155)))
    }

    @Test fun stretchedScreenAndStereoFailClosedPayloadDrawNothingOverGame() {
        assertOnlyUnusedPixelsChange(80, 60, listOf(Rect(0, 0, 80, 60)))
    }

    @Test fun unknownOrOutdatedSurfaceLayoutDoesNotDraw() {
        val output = Bitmap.createBitmap(80, 60, Bitmap.Config.ARGB_8888).apply {
            eraseColor(Color.BLUE)
        }
        val image = Bitmap.createBitmap(4, 4, Bitmap.Config.ARGB_8888).apply {
            eraseColor(Color.WHITE)
        }
        EmulationBackgroundPainter.draw(Canvas(output), image, null, 80, 60, Paint())
        EmulationBackgroundPainter.draw(
            Canvas(output),
            image,
            EmulationBackgroundLayout(60, 80, listOf(Rect(10, 0, 50, 80))),
            80,
            60,
            Paint()
        )
        assertEquals(Color.BLUE, output.getPixel(0, 0))
        assertEquals(Color.BLUE, output.getPixel(79, 59))
    }

    @Test fun displayLayoutsAreIndependentAndDestroyClearsOnlyThatDisplay() {
        EmulationBackgroundLayouts.update(false, 1920, 1080, intArrayOf(60, 0, 1860, 1080))
        EmulationBackgroundLayouts.update(true, 1280, 960, intArrayOf(0, 0, 1280, 960))
        assertEquals(
            Rect(60, 0, 1860, 1080),
            EmulationBackgroundLayouts.forDisplay(false).value!!.screens.single()
        )
        EmulationBackgroundLayouts.update(true, 0, 0, null)
        assertNull(EmulationBackgroundLayouts.forDisplay(true).value)
        assertNotNull(EmulationBackgroundLayouts.forDisplay(false).value)
    }

    @Test fun malformedAndEmptyNativeLayoutsAreNotUsed() {
        for (payload in listOf(intArrayOf(), intArrayOf(1, 2, 3), intArrayOf(20, 20, 10, 10))) {
            EmulationBackgroundLayouts.update(false, 80, 60, payload)
            assertNull(EmulationBackgroundLayouts.forDisplay(false).value)
        }
    }

    @Test fun outOfBoundsCustomScreensAreClippedToTheSurface() {
        EmulationBackgroundLayouts.update(false, 80, 60, intArrayOf(-20, -10, 100, 70))
        assertEquals(
            Rect(0, 0, 80, 60),
            EmulationBackgroundLayouts.forDisplay(false).value!!.screens.single()
        )
    }

    @Test fun importOwnsItsImageAndReloadsWithoutTheOriginalDocument() = runBlocking {
        store.importImage(ByteArrayInputStream(png()))
        assertEquals(Color.RED, store.image.value!!.getPixel(0, 0))
        val reopened = EmulationBackgroundStore(context)
        reopened.load()
        assertEquals(Color.RED, reopened.image.value!!.getPixel(0, 0))
        assertTrue(reopened.hasImage())
    }

    @Test fun invalidImportKeepsTheLastValidImageAndFile() = runBlocking {
        store.importImage(ByteArrayInputStream(png()))
        val saved = java.io.File(context.filesDir, "emulation-background.png").readBytes()
        try {
            store.importImage(ByteArrayInputStream(byteArrayOf(1, 2, 3)))
            fail("Invalid image accepted")
        } catch (_: IOException) { }
        assertEquals(Color.RED, store.image.value!!.getPixel(0, 0))
        assertArrayEquals(
            saved,
            java.io.File(context.filesDir, "emulation-background.png").readBytes()
        )
    }

    @Test fun overLimitStreamIsRejectedWithoutReplacingTheImageOrLeavingTempFiles() = runBlocking {
        store.importImage(ByteArrayInputStream(png()))
        val oversized = object : InputStream() {
            var remaining = EmulationBackgroundStore.MAX_INPUT_BYTES + 1
            override fun read(): Int = if (remaining-- > 0) 0 else -1
            override fun read(b: ByteArray, off: Int, len: Int): Int {
                if (remaining <= 0) return -1
                val count = minOf(len.toLong(), remaining).toInt()
                b.fill(0, off, off + count)
                remaining -= count
                return count
            }
        }
        try {
            store.importImage(oversized)
            fail("Oversized image accepted")
        } catch (_: IOException) { }
        assertEquals(Color.RED, store.image.value!!.getPixel(0, 0))
        assertTrue(
            context.cacheDir.listFiles()!!.none {
                it.name.startsWith("emulation-background-")
            }
        )
    }

    @Test fun largeImagesAreDownsampledBeforeAllocation() = runBlocking {
        store.importImage(ByteArrayInputStream(png(5000, 2)))
        assertTrue(store.image.value!!.width <= 4096)
        assertTrue(store.image.value!!.height >= 1)
    }

    @Test fun removeClearsBothTheCurrentImageAndThePersistentCopy() = runBlocking {
        store.importImage(ByteArrayInputStream(png()))
        store.remove()
        assertNull(store.image.value)
        assertFalse(store.hasImage())
        val reopened = EmulationBackgroundStore(context)
        reopened.load()
        assertNull(reopened.image.value)
    }
}
