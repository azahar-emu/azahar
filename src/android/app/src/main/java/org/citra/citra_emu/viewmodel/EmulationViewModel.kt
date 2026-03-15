// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.viewmodel

import androidx.lifecycle.ViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.citra.citra_emu.features.settings.model.Settings

class EmulationViewModel : ViewModel() {
    val emulationStarted get() = _emulationStarted.asStateFlow()
    val activeSettings = Settings()

    private val _emulationStarted = MutableStateFlow(false)

    val shaderProgress get() = _shaderProgress.asStateFlow()
    private val _shaderProgress = MutableStateFlow(0)

    val totalShaders get() = _totalShaders.asStateFlow()
    private val _totalShaders = MutableStateFlow(0)

    val shaderMessage get() = _shaderMessage.asStateFlow()
    private val _shaderMessage = MutableStateFlow("")

    fun setShaderProgress(progress: Int) {
        _shaderProgress.value = progress
    }

    fun setTotalShaders(max: Int) {
        _totalShaders.value = max
    }

    fun setShaderMessage(msg: String) {
        _shaderMessage.value = msg
    }

    fun updateProgress(msg: String, progress: Int, max: Int) {
        setShaderMessage(msg)
        setShaderProgress(progress)
        setTotalShaders(max)
    }

    fun setEmulationStarted(started: Boolean) {
        _emulationStarted.value = started
    }
}
