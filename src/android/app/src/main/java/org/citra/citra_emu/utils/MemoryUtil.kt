// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// SPDX-FileCopyrightText: 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

package org.citra.citra_emu.utils

import android.app.ActivityManager
import android.content.Context
import android.os.Build
import java.util.Locale
import kotlin.math.ceil
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.R

object MemoryUtil {
    private val context get() = CitraApplication.appContext

    private val Float.hundredths: String
        get() = String.format(Locale.ROOT, "%.2f", this)

    const val KB: Float = 1024F
    const val MB = KB * 1024
    const val GB = MB * 1024
    const val TB = GB * 1024
    const val PB = TB * 1024
    const val EB = PB * 1024

    fun bytesToSizeUnit(size: Float, roundUp: Boolean = false): String = when {
        size < KB -> {
            context.getString(
                R.string.memory_formatted,
                size.hundredths,
                context.getString(R.string.memory_byte_shorthand)
            )
        }

        size < MB -> {
            context.getString(
                R.string.memory_formatted,
                if (roundUp) ceil(size / KB) else (size / KB).hundredths,
                context.getString(R.string.memory_kilobyte)
            )
        }

        size < GB -> {
            context.getString(
                R.string.memory_formatted,
                if (roundUp) ceil(size / MB) else (size / MB).hundredths,
                context.getString(R.string.memory_megabyte)
            )
        }

        size < TB -> {
            context.getString(
                R.string.memory_formatted,
                if (roundUp) ceil(size / GB) else (size / GB).hundredths,
                context.getString(R.string.memory_gigabyte)
            )
        }

        size < PB -> {
            context.getString(
                R.string.memory_formatted,
                if (roundUp) ceil(size / TB) else (size / TB).hundredths,
                context.getString(R.string.memory_terabyte)
            )
        }

        size < EB -> {
            context.getString(
                R.string.memory_formatted,
                if (roundUp) ceil(size / PB) else (size / PB).hundredths,
                context.getString(R.string.memory_petabyte)
            )
        }

        else -> {
            context.getString(
                R.string.memory_formatted,
                if (roundUp) ceil(size / EB) else (size / EB).hundredths,
                context.getString(R.string.memory_exabyte)
            )
        }
    }

    val totalMemory: Float
        get() {
            val memInfo = ActivityManager.MemoryInfo()
            with(context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager) {
                getMemoryInfo(memInfo)
            }

            return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                memInfo.advertisedMem.toFloat()
            } else {
                memInfo.totalMem.toFloat()
            }
        }

    fun isLessThan(minimum: Int, size: Float): Boolean = when (size) {
        KB -> totalMemory < MB && totalMemory < minimum
        MB -> totalMemory < GB && (totalMemory / MB) < minimum
        GB -> totalMemory < TB && (totalMemory / GB) < minimum
        TB -> totalMemory < PB && (totalMemory / TB) < minimum
        PB -> totalMemory < EB && (totalMemory / PB) < minimum
        EB -> totalMemory / EB < minimum
        else -> totalMemory < KB && totalMemory < minimum
    }

    // Devices are unlikely to have 0.5GB increments of memory so we'll just round up to account for
    // the potential error created by memInfo.totalMem
    fun getDeviceRAM(): String = bytesToSizeUnit(totalMemory, true)
}
