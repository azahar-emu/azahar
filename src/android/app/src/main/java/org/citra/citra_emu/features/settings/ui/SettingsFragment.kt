// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

package org.citra.citra_emu.features.settings.ui

import android.content.Context
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.LinearLayoutManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.io.IOException
import kotlin.getValue
import kotlinx.coroutines.launch
import org.citra.citra_emu.R
import org.citra.citra_emu.databinding.FragmentSettingsBinding
import org.citra.citra_emu.display.EmulationBackgroundStore
import org.citra.citra_emu.features.settings.model.SettingsViewModel
import org.citra.citra_emu.features.settings.model.view.SettingsItem

class SettingsFragment :
    Fragment(),
    SettingsFragmentView {
    override var activityView: SettingsActivityView? = null

    private val settingsViewModel: SettingsViewModel by activityViewModels()

    private val fragmentPresenter by lazy {
        SettingsFragmentPresenter(this, settingsViewModel.editingSettings)
    }
    private var settingsAdapter: SettingsAdapter? = null

    private var _binding: FragmentSettingsBinding? = null
    private val binding get() = _binding!!

    private val backgroundPicker =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) {
                val context = requireContext().applicationContext
                lifecycleScope.launch {
                    try {
                        EmulationBackgroundStore.get(context).importImage(uri)
                        if (isAdded) {
                            Toast.makeText(
                                context,
                                R.string.emulation_background_imported,
                                Toast.LENGTH_SHORT
                            ).show()
                        }
                    } catch (_: IOException) {
                        if (isAdded) {
                            Toast.makeText(
                                context,
                                R.string.emulation_background_error,
                                Toast.LENGTH_LONG
                            ).show()
                        }
                    } catch (_: SecurityException) {
                        if (isAdded) {
                            Toast.makeText(
                                context,
                                R.string.emulation_background_error,
                                Toast.LENGTH_LONG
                            ).show()
                        }
                    } catch (_: IllegalArgumentException) {
                        if (isAdded) {
                            Toast.makeText(
                                context,
                                R.string.emulation_background_error,
                                Toast.LENGTH_LONG
                            ).show()
                        }
                    }
                }
            }
        }

    override fun onAttach(context: Context) {
        super.onAttach(context)
        activityView = requireActivity() as SettingsActivityView
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val menuTag = requireArguments().getString(ARGUMENT_MENU_TAG)
        val gameId = requireArguments().getString(ARGUMENT_GAME_ID)
        val gameTitle = requireArguments().getString(ARGUMENT_GAME_TITLE)
        fragmentPresenter.onCreate(menuTag!!, gameId!!, gameTitle!!)
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentSettingsBinding.inflate(layoutInflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        settingsAdapter = SettingsAdapter(this, requireActivity())
        binding.listSettings.apply {
            adapter = settingsAdapter
            layoutManager = LinearLayoutManager(activity)
        }
        fragmentPresenter.onViewCreated(settingsAdapter!!)

        setInsets()
    }

    override fun onDetach() {
        super.onDetach()
        activityView = null
        if (settingsAdapter != null) {
            settingsAdapter!!.closeDialog()
        }
    }

    override fun showSettingsList(settingsList: ArrayList<SettingsItem>) {
        settingsAdapter!!.setSettingsList(settingsList)
    }

    override fun loadSettingsList() {
        fragmentPresenter.loadSettingsList()
    }

    override fun loadSubMenu(menuKey: String) {
        activityView!!.showSettingsFragment(
            menuKey,
            true,
            requireArguments().getString(ARGUMENT_GAME_ID)!!,
            requireArguments().getString(ARGUMENT_GAME_TITLE)!!
        )
    }

    override fun showToastMessage(message: String?, is_long: Boolean) {
        activityView!!.showToastMessage(message!!, is_long)
    }

    override fun onSettingChanged() {
        activityView!!.onSettingChanged()
    }

    override fun showBackgroundImageDialog() {
        val store = EmulationBackgroundStore.get(requireContext())
        val items = if (store.hasImage()) {
            arrayOf(
                getString(R.string.emulation_background_choose),
                getString(R.string.emulation_background_remove)
            )
        } else {
            arrayOf(getString(R.string.emulation_background_choose))
        }
        MaterialAlertDialogBuilder(requireContext())
            .setTitle(R.string.emulation_background_image)
            .setItems(items) { _, which ->
                if (which == 0) {
                    backgroundPicker.launch(EmulationBackgroundStore.SUPPORTED_TYPES.toTypedArray())
                } else {
                    val context = requireContext().applicationContext
                    lifecycleScope.launch {
                        try {
                            store.remove()
                        } catch (_: IOException) {
                            if (isAdded) {
                                Toast.makeText(
                                    context,
                                    R.string.emulation_background_error,
                                    Toast.LENGTH_LONG
                                ).show()
                            }
                        }
                    }
                }
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun setInsets() {
        ViewCompat.setOnApplyWindowInsetsListener(
            binding.root
        ) { _: View, windowInsets: WindowInsetsCompat ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())

            binding.listSettings.updatePadding(
                left = barInsets.left + cutoutInsets.left,
                right = barInsets.right + cutoutInsets.right,
                bottom = barInsets.bottom
            )
            windowInsets
        }
    }

    companion object {
        private const val ARGUMENT_MENU_TAG = "menu_tag"
        private const val ARGUMENT_GAME_ID = "game_id"
        private const val ARGUMENT_GAME_TITLE = "game_title"

        fun newInstance(menuTag: String?, gameId: String?, gameTitle: String?): Fragment {
            val fragment = SettingsFragment()
            val arguments = Bundle()
            arguments.putString(ARGUMENT_MENU_TAG, menuTag)
            arguments.putString(ARGUMENT_GAME_ID, gameId)
            arguments.putString(ARGUMENT_GAME_TITLE, gameTitle)
            fragment.arguments = arguments
            return fragment
        }
    }
}
