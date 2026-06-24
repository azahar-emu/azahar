// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.ui.viewholder

import android.view.View
import androidx.core.content.res.ResourcesCompat
import org.citra.citra_emu.databinding.ListItemSettingBinding
import org.citra.citra_emu.features.settings.model.view.SettingsItem
import org.citra.citra_emu.features.settings.model.view.SubmenuSetting
import org.citra.citra_emu.features.settings.ui.SettingsAdapter

class SubmenuViewHolder(val binding: ListItemSettingBinding, adapter: SettingsAdapter) :
    SettingViewHolder<SubmenuSetting>(binding.root, adapter) {
    override lateinit var setting: SubmenuSetting

    override fun bind(item: SettingsItem) {
        setting = item as SubmenuSetting
        if (item.iconId == 0) {
            binding.icon.visibility = View.GONE
        } else {
            binding.icon.visibility = View.VISIBLE
            binding.icon.setImageDrawable(
                ResourcesCompat.getDrawable(
                    binding.icon.resources,
                    item.iconId,
                    binding.icon.context.theme
                )
            )
        }
        binding.textSettingName.setText(item.nameId)
        if (item.descriptionId != 0) {
            binding.textSettingDescription.setText(item.descriptionId)
            binding.textSettingDescription.visibility = View.VISIBLE
        } else {
            binding.textSettingDescription.visibility = View.GONE
        }
        binding.textSettingValue.visibility = View.GONE
    }

    override fun onClick(clicked: View) {
        adapter.onSubmenuClick(setting)
    }

    override fun onLongClick(clicked: View): Boolean {
        // no-op
        return true
    }
}
