// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.fragments

import android.content.DialogInterface
import android.os.Bundle
import android.text.method.LinkMovementMethod
import android.view.Gravity
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import androidx.appcompat.app.AlertDialog
import androidx.core.content.ContextCompat
import androidx.core.text.HtmlCompat
import androidx.core.widget.doOnTextChanged
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.navigation.findNavController
import androidx.preference.PreferenceManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.progressindicator.CircularProgressIndicator
import com.google.android.material.textview.MaterialTextView
import com.google.android.material.transition.MaterialSharedAxis
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.HomeNavigationDirections
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.databinding.DialogSoftwareKeyboardBinding
import org.citra.citra_emu.databinding.FragmentSystemFilesBinding
import org.citra.citra_emu.features.settings.model.Settings
import org.citra.citra_emu.model.Game
import org.citra.citra_emu.utils.SystemSaveGame
import org.citra.citra_emu.viewmodel.GamesViewModel
import org.citra.citra_emu.viewmodel.HomeViewModel

class SystemFilesFragment : Fragment() {
    private var _binding: FragmentSystemFilesBinding? = null
    private val binding get() = _binding!!

    private val homeViewModel: HomeViewModel by activityViewModels()
    private val gamesViewModel: GamesViewModel by activityViewModels()

    private val REGION_START = "RegionStart"

    private val homeMenuMap: MutableMap<String, String> = mutableMapOf()
    private var setupStateCached: BooleanArray? = null
    private lateinit var regionValues: IntArray

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enterTransition = MaterialSharedAxis(MaterialSharedAxis.X, true)
        returnTransition = MaterialSharedAxis(MaterialSharedAxis.X, false)
        SystemSaveGame.load()
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentSystemFilesBinding.inflate(layoutInflater)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        homeViewModel.setNavigationVisibility(visible = false, animated = true)
        homeViewModel.setStatusBarShadeVisibility(visible = false)

        // TODO: Remove workaround for text filtering issue in material components when fixed
        // https://github.com/material-components/material-components-android/issues/1464
        binding.dropdownSystemRegionStart.isSaveEnabled = false

        reloadUi()
        if (savedInstanceState != null) {
            binding.dropdownSystemRegionStart
                .setText(savedInstanceState.getString(REGION_START), false)
        }
    }

    override fun onResume() {
        super.onResume()
        SystemSaveGame.load()
    }

    override fun onPause() {
        super.onPause()
        SystemSaveGame.save()
    }

    private fun showProgressDialog(
        main_title: CharSequence,
        main_text: CharSequence
    ): AlertDialog? {
        val context = requireContext()
        val progressIndicator = CircularProgressIndicator(context).apply {
            isIndeterminate = true
            layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.CENTER // Center the progress indicator
            ).apply {
                setMargins(50, 50, 50, 50) // Add margins (left, top, right, bottom)
            }
        }

        val pleaseWaitText = MaterialTextView(context).apply {
            text = main_text
        }

        val container = LinearLayout(context).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setPadding(40, 40, 40, 40) // Optional: Add padding to the entire layout
            addView(pleaseWaitText)
            addView(progressIndicator)
        }

        return MaterialAlertDialogBuilder(context)
            .setTitle(main_title)
            .setView(container)
            .setCancelable(false)
            .show()
    }

    private fun reloadUi() {
        val preferences = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)

        binding.switchRunSystemSetup.isChecked = SystemSaveGame.getIsSystemSetupNeeded()
        binding.switchRunSystemSetup.setOnCheckedChangeListener { _, isChecked ->
            SystemSaveGame.setSystemSetupNeeded(isChecked)
        }

        val showHomeApps = preferences.getBoolean(Settings.PREF_SHOW_HOME_APPS, false)
        binding.switchShowApps.isChecked = showHomeApps
        binding.switchShowApps.setOnCheckedChangeListener { _, isChecked ->
            preferences.edit()
                .putBoolean(Settings.PREF_SHOW_HOME_APPS, isChecked)
                .apply()
            gamesViewModel.setShouldSwapData(true)
        }

        binding.setupSystemFilesDescription?.apply {
            text = HtmlCompat.fromHtml(
                context.getString(R.string.setup_system_files_preamble),
                HtmlCompat.FROM_HTML_MODE_COMPACT
            )
            movementMethod = LinkMovementMethod.getInstance()
        }

        binding.buttonUnlinkConsoleData.isEnabled = NativeLibrary.isFullConsoleLinked()
        binding.buttonUnlinkConsoleData.setOnClickListener {
            MaterialAlertDialogBuilder(requireContext())
                .setTitle(R.string.delete_system_files)
                .setMessage(HtmlCompat.fromHtml(
                    requireContext().getString(R.string.delete_system_files_description),
                    HtmlCompat.FROM_HTML_MODE_COMPACT
                ))
                .setPositiveButton(android.R.string.ok) { _: DialogInterface, _: Int ->
                    NativeLibrary.unlinkConsole()
                    binding.buttonUnlinkConsoleData.isEnabled = NativeLibrary.isFullConsoleLinked()
                }
                .setNegativeButton(android.R.string.cancel, null)
                .show()
        }

        binding.buttonSetUpSystemFiles.setOnClickListener {
            val inflater = LayoutInflater.from(context)
            val inputBinding = DialogSoftwareKeyboardBinding.inflate(inflater)
            var textInputValue: String = preferences.getString("last_artic_base_addr", "")!!

            val progressDialog = showProgressDialog(
                getText(R.string.setup_system_files),
                getString(R.string.setup_system_files_detect)
            )

            CoroutineScope(Dispatchers.IO).launch {
                val setupState = setupStateCached ?: NativeLibrary.areSystemTitlesInstalled().also {
                    setupStateCached = it
                }

                withContext(Dispatchers.Main) {
                    progressDialog?.dismiss()

                    inputBinding.editTextInput.setText(textInputValue)
                    inputBinding.editTextInput.doOnTextChanged { text, _, _, _ ->
                        textInputValue = text.toString()
                    }

                    val buttonGroup = context?.let { it1 -> RadioGroup(it1) }!!

                    val buttonO3ds = context?.let { it1 ->
                        RadioButton(it1).apply {
                            text = context.getString(R.string.setup_system_files_o3ds)
                            isChecked = false
                        }
                    }!!

                    val buttonN3ds = context?.let { it1 ->
                        RadioButton(it1).apply {
                            text = context.getString(R.string.setup_system_files_n3ds)
                            isChecked = false
                        }
                    }!!

                    val textO3ds: String
                    val textN3ds: String

                    val colorO3ds: Int
                    val colorN3ds: Int

                    if (!setupStateCached!![0]) {
                        textO3ds = getString(R.string.setup_system_files_possible)
                        colorO3ds = R.color.citra_primary_blue

                        textN3ds = getString(R.string.setup_system_files_o3ds_needed)
                        colorN3ds = R.color.citra_primary_yellow

                        buttonN3ds.isEnabled = false
                    } else {
                        textO3ds = getString(R.string.setup_system_files_completed)
                        colorO3ds = R.color.citra_primary_green

                        if (!setupStateCached!![1]) {
                            textN3ds = getString(R.string.setup_system_files_possible)
                            colorN3ds = R.color.citra_primary_blue
                        } else {
                            textN3ds = getString(R.string.setup_system_files_completed)
                            colorN3ds = R.color.citra_primary_green
                        }
                    }

                    val tooltipO3ds = context?.let { it1 ->
                        MaterialTextView(it1).apply {
                            text = textO3ds
                            textSize = 12f
                            setTextColor(ContextCompat.getColor(requireContext(), colorO3ds))
                        }
                    }

                    val tooltipN3ds = context?.let { it1 ->
                        MaterialTextView(it1).apply {
                            text = textN3ds
                            textSize = 12f
                            setTextColor(ContextCompat.getColor(requireContext(), colorN3ds))
                        }
                    }

                    buttonGroup.apply {
                        addView(buttonO3ds)
                        addView(tooltipO3ds)
                        addView(buttonN3ds)
                        addView(tooltipN3ds)
                    }

                    inputBinding.root.apply {
                        addView(buttonGroup)
                    }

                    val dialog = context?.let {
                        MaterialAlertDialogBuilder(it)
                            .setView(inputBinding.root)
                            .setTitle(getString(R.string.setup_system_files_enter_address))
                            .setPositiveButton(android.R.string.ok) { diag, _ ->
                                if (textInputValue.isNotEmpty() && !(!buttonO3ds.isChecked && !buttonN3ds.isChecked)) {
                                    preferences.edit()
                                        .putString("last_artic_base_addr", textInputValue)
                                        .apply()
                                    val menu = Game(
                                        title = getString(R.string.artic_base),
                                        path = if (buttonO3ds.isChecked) {
                                            "articinio://$textInputValue"
                                        } else {
                                            "articinin://$textInputValue"
                                        },
                                        filename = ""
                                    )
                                    val progressDialog2 = showProgressDialog(
                                        getText(R.string.setup_system_files),
                                        getString(
                                            R.string.setup_system_files_preparing
                                        )
                                    )

                                    CoroutineScope(Dispatchers.IO).launch {
                                        NativeLibrary.uninstallSystemFiles(buttonO3ds.isChecked)
                                        withContext(Dispatchers.Main) {
                                            setupStateCached = null
                                            progressDialog2?.dismiss()
                                            val action =
                                                HomeNavigationDirections.actionGlobalEmulationActivity(
                                                    menu
                                                )
                                            binding.root.findNavController().navigate(action)
                                        }
                                    }
                                }
                            }
                            .setNegativeButton(android.R.string.cancel) { _, _ -> }
                            .show()
                    }
                }
            }
        }

        populateHomeMenuOptions()
        binding.buttonStartHomeMenu.setOnClickListener {
            val menuPath = homeMenuMap[binding.dropdownSystemRegionStart.text.toString()]!!
            val menu = Game(
                title = getString(R.string.home_menu),
                path = menuPath,
                filename = ""
            )
            val action = HomeNavigationDirections.actionGlobalEmulationActivity(menu)
            binding.root.findNavController().navigate(action)
        }
    }

    private fun populateHomeMenuOptions() {
        regionValues = resources.getIntArray(R.array.systemFileRegionValues)
        val regionEntries = resources.getStringArray(R.array.systemFileRegions)
        regionValues.forEachIndexed { i: Int, region: Int ->
            val regionString = regionEntries[i]
            val regionPath = NativeLibrary.getHomeMenuPath(region)
            homeMenuMap[regionString] = regionPath
        }

        val availableMenus = homeMenuMap.filter { it.value != "" }
        if (availableMenus.isNotEmpty()) {
            binding.systemRegionStart.isEnabled = true
            binding.buttonStartHomeMenu.isEnabled = true

            binding.dropdownSystemRegionStart.setAdapter(
                ArrayAdapter(
                    requireContext(),
                    R.layout.support_simple_spinner_dropdown_item,
                    availableMenus.keys.toList()
                )
            )
            binding.dropdownSystemRegionStart.setText(availableMenus.keys.first(), false)
        }
    }
}
