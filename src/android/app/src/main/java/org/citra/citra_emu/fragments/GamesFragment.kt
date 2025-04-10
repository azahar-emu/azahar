// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.fragments

import android.annotation.SuppressLint
import android.net.Uri
import android.os.Bundle
import android.text.method.LinkMovementMethod
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.ViewGroup.MarginLayoutParams
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.edit
import androidx.core.text.HtmlCompat
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.updatePadding
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import androidx.preference.PreferenceManager
import androidx.recyclerview.widget.GridLayoutManager
import com.google.android.material.color.MaterialColors
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.transition.MaterialFadeThrough
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.R
import org.citra.citra_emu.adapters.GameAdapter
import org.citra.citra_emu.databinding.FragmentGamesBinding
import org.citra.citra_emu.features.settings.model.Settings
import org.citra.citra_emu.model.Game
import org.citra.citra_emu.viewmodel.GamesViewModel
import org.citra.citra_emu.viewmodel.HomeViewModel

class GamesFragment : Fragment() {
    private var _binding: FragmentGamesBinding? = null
    private val binding get() = _binding!!

    private val gamesViewModel: GamesViewModel by activityViewModels()
    private val homeViewModel: HomeViewModel by activityViewModels()
    private var show3DSFileWarning: Boolean = true
    private lateinit var gameAdapter: GameAdapter

    private val openImageLauncher = registerForActivityResult(
        ActivityResultContracts.GetContent()
    ) { uri: Uri? ->
        gameAdapter.handleShortcutImageResult(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enterTransition = MaterialFadeThrough()
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?
    ): View {
        _binding = FragmentGamesBinding.inflate(inflater)
        return binding.root
    }

    // This is using the correct scope, lint is just acting up
    @SuppressLint("UnsafeRepeatOnLifecycleDetector")
    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        homeViewModel.setNavigationVisibility(visible = true, animated = true)
        homeViewModel.setStatusBarShadeVisibility(visible = true)

        val inflater = LayoutInflater.from(requireContext())

        gameAdapter = GameAdapter(
            requireActivity() as AppCompatActivity,
            inflater,
            openImageLauncher
        )

        binding.gridGames.apply {
            layoutManager = GridLayoutManager(
                requireContext(),
                resources.getInteger(R.integer.game_grid_columns)
            )
            adapter = this@GamesFragment.gameAdapter
        }

        binding.swipeRefresh.apply {
            // Add swipe down to refresh gesture
            setOnRefreshListener {
                gamesViewModel.reloadGames(false)
            }

            // Set theme color to the refresh animation's background
            setProgressBackgroundColorSchemeColor(
                MaterialColors.getColor(
                    binding.swipeRefresh,
                    com.google.android.material.R.attr.colorPrimary
                )
            )
            setColorSchemeColors(
                MaterialColors.getColor(
                    binding.swipeRefresh,
                    com.google.android.material.R.attr.colorOnPrimary
                )
            )

            // Make sure the loading indicator appears even if the layout is told to refresh before being fully drawn
            post {
                if (_binding == null) {
                    return@post
                }
                binding.swipeRefresh.isRefreshing = gamesViewModel.isReloading.value
            }
        }

        viewLifecycleOwner.lifecycleScope.apply {
            launch {
                repeatOnLifecycle(Lifecycle.State.RESUMED) {
                    gamesViewModel.isReloading.collect { isReloading ->
                        binding.swipeRefresh.isRefreshing = isReloading
                        if (gamesViewModel.games.value.isEmpty() && !isReloading) {
                            binding.noticeText.visibility = View.VISIBLE
                        } else {
                            binding.noticeText.visibility = View.INVISIBLE
                        }
                    }
                }
            }
            launch {
                repeatOnLifecycle(Lifecycle.State.RESUMED) {
                    gamesViewModel.games.collectLatest { setAdapter(it) }
                }
            }
            launch {
                repeatOnLifecycle(Lifecycle.State.RESUMED) {
                    gamesViewModel.shouldSwapData.collect {
                        if (it) {
                            setAdapter(gamesViewModel.games.value)
                            gamesViewModel.setShouldSwapData(false)
                        }
                    }
                }
            }
            launch {
                repeatOnLifecycle(Lifecycle.State.RESUMED) {
                    gamesViewModel.shouldScrollToTop.collect {
                        if (it) {
                            scrollToTop()
                            gamesViewModel.setShouldScrollToTop(false)
                        }
                    }
                }
            }
        }

        setInsets()
    }

    override fun onResume() {
        super.onResume()

        if (show3DSFileWarning &&
            !PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
                .getBoolean("show_3ds_files_warning", false)) {
            val message = HtmlCompat.fromHtml(getString(R.string.warning_3ds_files),
                HtmlCompat.FROM_HTML_MODE_LEGACY)

            context?.let {
                val alert = MaterialAlertDialogBuilder(it)
                    .setTitle(getString(R.string.important))
                    .setMessage(message)
                    .setPositiveButton(R.string.dont_show_again) { _, _ ->
                        PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
                            .edit() {
                            putBoolean("show_3ds_files_warning", true)
                        }
                    }
                    .show()

                val alertMessage = alert.findViewById<View>(android.R.id.message) as TextView
                alertMessage.movementMethod = LinkMovementMethod.getInstance()
            }
        }
        show3DSFileWarning = false
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    private fun setAdapter(games: List<Game>) {
        val preferences = PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
        if (preferences.getBoolean(Settings.PREF_SHOW_HOME_APPS, false)) {
            (binding.gridGames.adapter as GameAdapter).submitList(games)
        } else {
            val filteredList = games.filter { !it.isSystemTitle }
            (binding.gridGames.adapter as GameAdapter).submitList(filteredList)
        }
    }

    private fun scrollToTop() {
        if (_binding != null) {
            binding.gridGames.smoothScrollToPosition(0)
        }
    }

    private fun setInsets() =
        ViewCompat.setOnApplyWindowInsetsListener(
            binding.root
        ) { view: View, windowInsets: WindowInsetsCompat ->
            val barInsets = windowInsets.getInsets(WindowInsetsCompat.Type.systemBars())
            val cutoutInsets = windowInsets.getInsets(WindowInsetsCompat.Type.displayCutout())
            val extraListSpacing = resources.getDimensionPixelSize(R.dimen.spacing_large)
            val spacingNavigation = resources.getDimensionPixelSize(R.dimen.spacing_navigation)
            val spacingNavigationRail =
                resources.getDimensionPixelSize(R.dimen.spacing_navigation_rail)

            binding.gridGames.updatePadding(
                top = barInsets.top + extraListSpacing,
                bottom = barInsets.bottom + spacingNavigation + extraListSpacing
            )

            binding.swipeRefresh.setProgressViewEndTarget(
                false,
                barInsets.top + resources.getDimensionPixelSize(R.dimen.spacing_refresh_end)
            )

            val leftInsets = barInsets.left + cutoutInsets.left
            val rightInsets = barInsets.right + cutoutInsets.right
            val mlpSwipe = binding.swipeRefresh.layoutParams as MarginLayoutParams
            if (ViewCompat.getLayoutDirection(view) == ViewCompat.LAYOUT_DIRECTION_LTR) {
                mlpSwipe.leftMargin = leftInsets + spacingNavigationRail
                mlpSwipe.rightMargin = rightInsets
            } else {
                mlpSwipe.leftMargin = leftInsets
                mlpSwipe.rightMargin = rightInsets + spacingNavigationRail
            }
            binding.swipeRefresh.layoutParams = mlpSwipe

            binding.noticeText.updatePadding(bottom = spacingNavigation)

            windowInsets
        }
}
