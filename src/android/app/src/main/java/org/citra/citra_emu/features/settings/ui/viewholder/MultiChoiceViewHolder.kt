// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.ui.viewholder

import android.view.View
import org.citra.citra_emu.databinding.ListItemSettingBinding
import org.citra.citra_emu.features.settings.model.view.SettingsItem
import org.citra.citra_emu.features.settings.model.view.MultiChoiceSetting
import org.citra.citra_emu.features.settings.ui.SettingsAdapter

class MultiChoiceViewHolder(val binding: ListItemSettingBinding, adapter: SettingsAdapter) :
    SettingViewHolder(binding.root, adapter) {
    private lateinit var setting: SettingsItem

    override fun bind(item: SettingsItem) {
        setting = item
        binding.textSettingName.setText(item.nameId)
        if (item.descriptionId != 0) {
            binding.textSettingDescription.visibility = View.VISIBLE
            binding.textSettingDescription.setText(item.descriptionId)
        } else {
            binding.textSettingDescription.visibility = View.GONE
        }
        binding.textSettingValue.visibility = View.VISIBLE
        binding.textSettingValue.text = getTextSetting()

        if (setting.isActive) {
            binding.textSettingName.alpha = 1f
            binding.textSettingDescription.alpha = 1f
            binding.textSettingValue.alpha = 1f
        } else {
            binding.textSettingName.alpha = 0.5f
            binding.textSettingDescription.alpha = 0.5f
            binding.textSettingValue.alpha = 0.5f
        }
    }

    private fun getTextSetting(): String {
        when (val item = setting) {
            is MultiChoiceSetting -> {
                val resMgr = binding.textSettingDescription.context.resources
                val values = resMgr.getIntArray(item.valuesId)
                var resList:List<String> = emptyList();
                values.forEachIndexed { i: Int, value: Int ->
                    if ((setting as MultiChoiceSetting).selectedValues.contains(value)) {
                     resList = resList + resMgr.getStringArray(item.choicesId)[i];
                    }
                }
                return resList.joinToString();
            }

            else -> return ""
        }
    }

    override fun onClick(clicked: View) {
        if (!setting.isEditable || !setting.isEnabled) {
            adapter.onClickDisabledSetting(!setting.isEditable)
            return
        }

        if (setting is MultiChoiceSetting) {
            adapter.onMultiChoiceClick(
                (setting as MultiChoiceSetting),
                bindingAdapterPosition
            )
        }
    }

    override fun onLongClick(clicked: View): Boolean {
        if (setting.isActive) {
            return adapter.onLongClick(setting.setting!!, bindingAdapterPosition)
        } else {
            adapter.onClickDisabledSetting(!setting.isEditable)
        }
        return false
    }
}
