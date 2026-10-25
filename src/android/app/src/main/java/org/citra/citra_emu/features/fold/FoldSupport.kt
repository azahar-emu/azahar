// Copyright 2026 tahirwrth
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.features.fold

import androidx.activity.ComponentActivity
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.window.area.WindowAreaCapability
import androidx.window.area.WindowAreaController
import androidx.window.area.WindowAreaInfo
import androidx.window.core.ExperimentalWindowApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch

@OptIn(ExperimentalWindowApi::class)
object FoldSupport {
    private val _status = MutableStateFlow(
        WindowAreaCapability.Status.WINDOW_AREA_STATUS_UNSUPPORTED
    )
    val status: StateFlow<WindowAreaCapability.Status> = _status.asStateFlow()

    // checks if device is a foldable
    val isSupported: Boolean
        get() = _status.value != WindowAreaCapability.Status.WINDOW_AREA_STATUS_UNSUPPORTED

    // checks if device can also present i.e., it is unfolded

    val canPresent: Boolean
        get() = _status.value == WindowAreaCapability.Status.WINDOW_AREA_STATUS_AVAILABLE ||
            _status.value == WindowAreaCapability.Status.WINDOW_AREA_STATUS_ACTIVE

    fun observe(activity: ComponentActivity) {
        activity.lifecycleScope.launch {
            activity.repeatOnLifecycle(Lifecycle.State.STARTED) {
                WindowAreaController.getOrCreate().windowAreaInfos
                    .map { infos ->
                        infos.firstOrNull { it.type == WindowAreaInfo.Type.TYPE_REAR_FACING }
                    }
                    .map { info ->
                        info?.getCapability(
                            WindowAreaCapability.Operation.OPERATION_PRESENT_ON_AREA
                        )
                            ?.status
                            ?: WindowAreaCapability.Status.WINDOW_AREA_STATUS_UNSUPPORTED
                    }
                    .distinctUntilChanged()
                    .collect { _status.value = it }
            }
        }
    }
}
