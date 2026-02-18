// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

object EmulationLifecycleUtil {
    private var shutdownHooks: MutableList<Runnable> = ArrayList()
    private var pauseResumeHooks: MutableList<Runnable> = ArrayList()


    fun closeGame() {
        shutdownHooks.forEach(Runnable::run)
    }

    fun pauseOrResume() {
        pauseResumeHooks.forEach(Runnable::run)
    }

    fun addShutdownHook(hook: Runnable) {
        if (shutdownHooks.contains(hook)) {
            Log.warning("[EmulationLifecycleUtil] Tried to add shutdown hook for function that already existed. Skipping.")
        } else {
            shutdownHooks.add(hook)
        }
    }

    fun addPauseResumeHook(hook: Runnable) {
        if (pauseResumeHooks.contains(hook)) {
            Log.warning("[EmulationLifecycleUtil] Tried to add pause resume hook for function that already existed. Skipping.")
        } else {
            pauseResumeHooks.add(hook)
        }
    }

    fun removeHook(hook: Runnable) {
        if (pauseResumeHooks.contains(hook)) {
            pauseResumeHooks.remove(hook)
        }
        if (shutdownHooks.contains(hook)) {
            shutdownHooks.remove(hook)
        }
    }
}
