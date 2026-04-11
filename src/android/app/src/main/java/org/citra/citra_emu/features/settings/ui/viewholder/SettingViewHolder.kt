// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.settings.ui.viewholder

import android.view.View
import androidx.recyclerview.widget.RecyclerView
import org.citra.citra_emu.features.settings.model.view.SettingsItem
import org.citra.citra_emu.features.settings.ui.SettingsAdapter

abstract class SettingViewHolder<out T: SettingsItem>(itemView: View, protected val adapter: SettingsAdapter) :
    RecyclerView.ViewHolder(itemView), View.OnClickListener, View.OnLongClickListener {
    init {
        itemView.setOnClickListener(this)
        itemView.setOnLongClickListener(this)
    }

    /**
     * The SettingsItem we are holding
     */
    abstract val setting: T?
    /**
     * Called by the adapter to set this ViewHolder's child views to display the list item
     * it must now represent.
     *
     * @param item The list item that should be represented by this ViewHolder.
     */
    abstract fun bind(item: SettingsItem)

    /**
     * Called when this ViewHolder's view is clicked on. Implementations should usually pass
     * this event up to the adapter.
     *
     * @param clicked The view that was clicked on.
     */
    abstract override fun onClick(clicked: View)

    abstract override fun onLongClick(clicked: View): Boolean

    fun showGlobalButtonIfNeeded(buttonUseGlobal: View, position: Int) {
        setting ?: return
        // Show "Revert to global" button in Custom Settings if applicable.
        val settings = adapter.fragmentView.activityView?.settings
        val showGlobal = settings?.isPerGame() == true
                && setting?.setting != null
                && settings.hasOverride(setting!!.setting!!)

        buttonUseGlobal.visibility = if (showGlobal) View.VISIBLE else View.GONE
        if (showGlobal) {
            buttonUseGlobal.setOnClickListener {
                setting?.setting?.let { descriptor ->
                    adapter.resetSettingToGlobal(descriptor, bindingAdapterPosition)
                }
            }
        }
    }
}
