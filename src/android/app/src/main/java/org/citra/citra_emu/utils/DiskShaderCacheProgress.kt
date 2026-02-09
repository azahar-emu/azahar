// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import androidx.annotation.Keep
import androidx.lifecycle.ViewModelProvider
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.activities.EmulationActivity
import org.citra.citra_emu.viewmodel.EmulationViewModel

@Keep
object DiskShaderCacheProgress {
    private lateinit var emulationViewModel: EmulationViewModel

    private fun prepareViewModel() {
        emulationViewModel =
            ViewModelProvider(
                NativeLibrary.sEmulationActivity.get() as EmulationActivity
            )[EmulationViewModel::class.java]
    }

    @JvmStatic
    fun loadProgress(stage: LoadCallbackStage, progress: Int, max: Int, obj: String) {
        val emulationActivity = NativeLibrary.sEmulationActivity.get()
        if (emulationActivity == null) {
            Log.error("[DiskShaderCacheProgress] EmulationActivity not present")
            return
        }

        emulationActivity.runOnUiThread {
            when (stage) {
                LoadCallbackStage.Prepare -> prepareViewModel()
                LoadCallbackStage.Decompile -> emulationViewModel.updateProgress(
                    emulationActivity.getString(R.string.preparing_shaders),
                    progress,
                    max
                )

                LoadCallbackStage.Build -> emulationViewModel.updateProgress(
                    emulationActivity.getString(R.string.building_shaders),
                    progress,
                    max
                )

                LoadCallbackStage.Complete -> emulationActivity.onEmulationStarted()
            }
        }
    }

    // Equivalent to VideoCore::LoadCallbackStage
    enum class LoadCallbackStage {
        Prepare,
        Decompile,
        Build,
        Complete
    }
}
