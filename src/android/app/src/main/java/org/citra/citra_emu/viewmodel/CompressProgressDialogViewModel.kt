// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.viewmodel

import androidx.lifecycle.ViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

object CompressProgressDialogViewModel: ViewModel() {
    private val _progress = MutableStateFlow(0)
    val progress = _progress.asStateFlow()

    private val _total = MutableStateFlow(0)
    val total = _total.asStateFlow()

    private val _message = MutableStateFlow("")
    val message = _message.asStateFlow()

    fun update(totalBytes: Long, currentBytes: Long) {
        val percent = ((currentBytes * 100L) / totalBytes).coerceIn(0L, 100L).toInt()
        _total.value = 100
        _progress.value = percent
        _message.value = ""
    }

    fun reset() {
        _progress.value = 0
        _total.value = 0
        _message.value = ""
    }
}