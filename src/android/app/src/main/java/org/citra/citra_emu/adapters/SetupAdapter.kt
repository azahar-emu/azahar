// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.adapters

import android.content.res.ColorStateList
import android.text.Html
import android.text.method.LinkMovementMethod
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.res.ResourcesCompat
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.button.MaterialButton
import org.citra.citra_emu.databinding.PageSetupBinding
import org.citra.citra_emu.model.ButtonState
import org.citra.citra_emu.model.PageState
import org.citra.citra_emu.model.SetupCallback
import org.citra.citra_emu.model.SetupPage
import org.citra.citra_emu.R
import org.citra.citra_emu.utils.ViewUtils

class SetupAdapter(val activity: AppCompatActivity, val pages: List<SetupPage>) :
    RecyclerView.Adapter<SetupAdapter.SetupPageViewHolder>() {
    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): SetupPageViewHolder {
        val binding = PageSetupBinding.inflate(LayoutInflater.from(parent.context), parent, false)
        return SetupPageViewHolder(binding)
    }

    override fun getItemCount(): Int = pages.size

    override fun onBindViewHolder(holder: SetupPageViewHolder, position: Int) =
        holder.bind(pages[position])

    inner class SetupPageViewHolder(val binding: PageSetupBinding) :
        RecyclerView.ViewHolder(binding.root), SetupCallback {
        lateinit var page: SetupPage

        init {
            itemView.tag = this
        }

        fun bind(page: SetupPage) {
            this.page = page

            if (page.pageSteps.invoke() == PageState.PAGE_STEPS_COMPLETE) {
                onStepCompleted(0, pageFullyCompleted = true)
            }

            if (page.pageButtons != null && page.pageSteps.invoke() != PageState.PAGE_STEPS_COMPLETE) {
                for (pageButton in page.pageButtons) {
                    val pageButtonView = LayoutInflater.from(activity)
                        .inflate(
                            R.layout.page_button,
                            binding.pageButtonContainer,
                            false
                        ) as MaterialButton

                    pageButtonView.apply {
                        id = pageButton.titleId
                        icon = ResourcesCompat.getDrawable(
                            activity.resources,
                            pageButton.iconId,
                            activity.theme
                        )
                        text = activity.resources.getString(pageButton.titleId)
                    }

                    pageButtonView.setOnClickListener {
                        pageButton.buttonAction.invoke(this@SetupPageViewHolder)
                    }

                    binding.pageButtonContainer.addView(pageButtonView)

                    // Disable buton add if its already completed
                    if (pageButton.buttonState.invoke() == ButtonState.BUTTON_ACTION_COMPLETE) {
                        onStepCompleted(pageButton.titleId, pageFullyCompleted = false)
                    }
                }
            }

            binding.icon.setImageDrawable(
                ResourcesCompat.getDrawable(
                    activity.resources,
                    page.iconId,
                    activity.theme
                )
            )
            binding.textTitle.text = activity.resources.getString(page.titleId)
            binding.textDescription.text =
                Html.fromHtml(activity.resources.getString(page.descriptionId), 0)
            binding.textDescription.movementMethod = LinkMovementMethod.getInstance()
        }

        override fun onStepCompleted(pageButtonId: Int, pageFullyCompleted: Boolean) {
            val button = binding.pageButtonContainer.findViewById<MaterialButton>(pageButtonId)

            if (pageFullyCompleted) {
                ViewUtils.hideView(binding.pageButtonContainer, 200)
                ViewUtils.showView(binding.textConfirmation, 200)
            }

            if (button != null) {
                button.isEnabled = false
                button.animate()
                    .alpha(0.38f)
                    .setDuration(200)
                    .start()
                button.setTextColor(button.context.getColor(com.google.android.material.R.color.material_on_surface_disabled))
                button.iconTint =
                    ColorStateList.valueOf(button.context.getColor(com.google.android.material.R.color.material_on_surface_disabled))
            }
        }
    }
}
