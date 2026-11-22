// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.features.settings.ui.viewholder

import android.view.View
import org.citra.citra_emu.databinding.ListItemSettingBinding
import org.citra.citra_emu.features.settings.model.InputMappingSetting
import org.citra.citra_emu.features.settings.model.view.InputBindingSetting
import org.citra.citra_emu.features.settings.model.view.SettingsItem
import org.citra.citra_emu.features.settings.ui.SettingsAdapter

class InputBindingSettingViewHolder(val binding: ListItemSettingBinding, adapter: SettingsAdapter) :
    SettingViewHolder<InputBindingSetting>(binding.root, adapter) {
    override lateinit var setting: InputBindingSetting

    override fun bind(item: SettingsItem) {
        setting = item as InputBindingSetting
        binding.textSettingName.setText(item.nameId)
        val uiString = setting.value
        if (uiString.isNotEmpty()) {
            binding.textSettingDescription.visibility = View.GONE
            binding.textSettingValue.visibility = View.VISIBLE
            binding.textSettingValue.text = uiString
        } else {
            binding.textSettingDescription.visibility = View.GONE
            binding.textSettingValue.visibility = View.GONE
        }

        if (setting.isEditable) {
            binding.textSettingName.alpha = 1f
            binding.textSettingDescription.alpha = 1f
            binding.textSettingValue.alpha = 1f
        } else {
            binding.textSettingName.alpha = 0.5f
            binding.textSettingDescription.alpha = 0.5f
            binding.textSettingValue.alpha = 0.5f
        }

        showGlobalMessageIfNeeded(binding.messageUseGlobal)
    }

    override fun onClick(clicked: View) {
        if (setting.isEditable) {
            adapter.onInputBindingClick(setting, bindingAdapterPosition)
        } else {
            adapter.onClickDisabledSetting(!setting.isEditable, setting.disabledMessage)
        }
    }

    override fun onLongClick(clicked: View): Boolean {
        if (setting.isEditable) {
            adapter.onInputBindingLongClick(
                setting.setting as InputMappingSetting,
                bindingAdapterPosition
            )
        } else {
            adapter.onClickDisabledSetting(!setting.isEditable, setting.disabledMessage)
        }
        return false
    }
}
