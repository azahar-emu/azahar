// Copyright Citra Emulator Project / Lime3DS Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.adapters

import android.graphics.drawable.Icon
import android.net.Uri
import android.os.SystemClock
import android.text.TextUtils
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.content.Context
import android.widget.TextView
import android.widget.ImageView
import android.widget.Toast
import android.graphics.drawable.BitmapDrawable
import android.graphics.Bitmap
import android.content.pm.ShortcutInfo
import android.content.pm.ShortcutManager
import android.graphics.BitmapFactory
import androidx.activity.result.ActivityResultLauncher
import androidx.appcompat.app.AppCompatActivity
import androidx.documentfile.provider.DocumentFile
import androidx.lifecycle.ViewModelProvider
import androidx.navigation.findNavController
import androidx.preference.PreferenceManager
import androidx.recyclerview.widget.AsyncDifferConfig
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.google.android.material.button.MaterialButton
import com.google.android.material.color.MaterialColors
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.launch
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.CoroutineScope
import org.citra.citra_emu.HomeNavigationDirections
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.R
import org.citra.citra_emu.adapters.GameAdapter.GameViewHolder
import org.citra.citra_emu.databinding.CardGameBinding
import org.citra.citra_emu.databinding.DialogShortcutBinding
import org.citra.citra_emu.features.cheats.ui.CheatsFragmentDirections
import org.citra.citra_emu.model.Game
import org.citra.citra_emu.utils.GameIconUtils
import org.citra.citra_emu.viewmodel.GamesViewModel
import androidx.core.net.toUri
import androidx.core.graphics.scale
import androidx.core.content.edit

class GameAdapter(private val activity: AppCompatActivity, private val inflater: LayoutInflater,  private val openImageLauncher: ActivityResultLauncher<String>?) :
    ListAdapter<Game, GameViewHolder>(AsyncDifferConfig.Builder(DiffCallback()).build()),
    View.OnClickListener, View.OnLongClickListener {
    private var lastClickTime = 0L
    private var imagePath: String? = null
    private var dialogShortcutBinding: DialogShortcutBinding? = null

    fun handleShortcutImageResult(uri: Uri?) {
        val path = uri?.toString()
        if (path != null) {
            imagePath = path
            dialogShortcutBinding!!.imageScaleSwitch.isEnabled = imagePath != null
            refreshShortcutDialogIcon()
        }
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): GameViewHolder {
        // Create a new view.
        val binding = CardGameBinding.inflate(LayoutInflater.from(parent.context), parent, false)
        binding.cardGame.setOnClickListener(this)
        binding.cardGame.setOnLongClickListener(this)

        // Use that view to create a ViewHolder.
        return GameViewHolder(binding)
    }

    override fun onBindViewHolder(holder: GameViewHolder, position: Int) {
        holder.bind(currentList[position])
    }

    override fun getItemCount(): Int = currentList.size

    /**
     * Launches the game that was clicked on.
     *
     * @param view The card representing the game the user wants to play.
     */
    override fun onClick(view: View) {
        // Double-click prevention, using threshold of 1000 ms
        if (SystemClock.elapsedRealtime() - lastClickTime < 1000) {
            return
        }
        lastClickTime = SystemClock.elapsedRealtime()

        val holder = view.tag as GameViewHolder
        gameExists(holder)

        val preferences =
            PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
        preferences.edit()
            .putLong(
                holder.game.keyLastPlayedTime,
                System.currentTimeMillis()
            )
            .apply()

        val action = HomeNavigationDirections.actionGlobalEmulationActivity(holder.game)
        view.findNavController().navigate(action)
    }

    /**
     * Opens the about game dialog for the game that was clicked on.
     *
     * @param view The view representing the game the user wants to play.
     */
    override fun onLongClick(view: View): Boolean {
        val context = view.context
        val holder = view.tag as GameViewHolder
        gameExists(holder)

        if (holder.game.titleId == 0L) {
            MaterialAlertDialogBuilder(context)
                .setTitle(R.string.properties)
                .setMessage(R.string.properties_not_loaded)
                .setPositiveButton(android.R.string.ok, null)
                .show()
        } else {
            showAboutGameDialog(context, holder.game, holder, view)
        }
        return true
    }

    // Triggers a library refresh if the user clicks on stale data
    private fun gameExists(holder: GameViewHolder): Boolean {
        if (holder.game.isInstalled) {
            return true
        }

        val gameExists = DocumentFile.fromSingleUri(
            CitraApplication.appContext,
            Uri.parse(holder.game.path)
        )?.exists() == true
        return if (!gameExists) {
            Toast.makeText(
                CitraApplication.appContext,
                R.string.loader_error_file_not_found,
                Toast.LENGTH_LONG
            ).show()

            ViewModelProvider(activity)[GamesViewModel::class.java].reloadGames(true)
            false
        } else {
            true
        }
    }

    inner class GameViewHolder(val binding: CardGameBinding) :
        RecyclerView.ViewHolder(binding.root) {
        lateinit var game: Game

        init {
            binding.cardGame.tag = this
        }

        fun bind(game: Game) {
            this.game = game

            binding.imageGameScreen.scaleType = ImageView.ScaleType.CENTER_CROP
            GameIconUtils.loadGameIcon(activity, game, binding.imageGameScreen)

            binding.textGameTitle.visibility = if (game.title.isEmpty()) {
                View.GONE
            } else {
                View.VISIBLE
            }
            binding.textCompany.visibility = if (game.company.isEmpty()) {
                View.GONE
            } else {
                View.VISIBLE
            }

            binding.textGameTitle.text = game.title
            binding.textCompany.text = game.company
            binding.textGameRegion.text = game.regions

            val backgroundColorId =
                if (
                    isValidGame(game.filename.substring(game.filename.lastIndexOf(".") + 1).lowercase())
                ) {
                    R.attr.colorSurface
                } else {
                    R.attr.colorErrorContainer
                }
            binding.cardContents.setBackgroundColor(
                MaterialColors.getColor(
                    binding.cardContents,
                    backgroundColorId
                )
            )

            binding.textGameTitle.postDelayed(
                {
                    binding.textGameTitle.ellipsize = TextUtils.TruncateAt.MARQUEE
                    binding.textGameTitle.isSelected = true

                    binding.textCompany.ellipsize = TextUtils.TruncateAt.MARQUEE
                    binding.textCompany.isSelected = true

                    binding.textGameRegion.ellipsize = TextUtils.TruncateAt.MARQUEE
                    binding.textGameRegion.isSelected = true
                },
                3000
            )
        }
    }

    private fun showAboutGameDialog(context: Context, game: Game, holder: GameViewHolder, view: View) {
        val bottomSheetView = inflater.inflate(R.layout.dialog_about_game, null)

        val bottomSheetDialog = BottomSheetDialog(context)
        bottomSheetDialog.setContentView(bottomSheetView)

        bottomSheetView.findViewById<TextView>(R.id.about_game_title).text = game.title
        bottomSheetView.findViewById<TextView>(R.id.about_game_company).text = game.company
        bottomSheetView.findViewById<TextView>(R.id.about_game_region).text = game.regions
        bottomSheetView.findViewById<TextView>(R.id.about_game_id).text = "ID: " + String.format("%016X", game.titleId)
        bottomSheetView.findViewById<TextView>(R.id.about_game_filename).text = "File: " + game.filename
        GameIconUtils.loadGameIcon(activity, game, bottomSheetView.findViewById(R.id.game_icon))

        bottomSheetView.findViewById<MaterialButton>(R.id.about_game_play).setOnClickListener {
            val action = HomeNavigationDirections.actionGlobalEmulationActivity(holder.game)
            view.findNavController().navigate(action)
        }

        bottomSheetView.findViewById<MaterialButton>(R.id.game_shortcut).setOnClickListener {
            val preferences = PreferenceManager.getDefaultSharedPreferences(context)

            // Default to false for zoomed in shortcut icons
            preferences.edit() {
                putBoolean(
                    "shouldStretchIcon",
                    false
                )
            }

            dialogShortcutBinding = DialogShortcutBinding.inflate(activity.layoutInflater)

            dialogShortcutBinding!!.shortcutNameInput.setText(game.title)
            GameIconUtils.loadGameIcon(activity, game, dialogShortcutBinding!!.shortcutIcon)

            dialogShortcutBinding!!.shortcutIcon.setOnClickListener {
                openImageLauncher?.launch("image/*")
            }

            dialogShortcutBinding!!.imageScaleSwitch.setOnCheckedChangeListener { _, isChecked ->
                preferences.edit {
                    putBoolean(
                        "shouldStretchIcon",
                        isChecked
                    )
                }
                refreshShortcutDialogIcon()
            }

            MaterialAlertDialogBuilder(context)
                .setTitle(R.string.create_shortcut)
                .setView(dialogShortcutBinding!!.root)
                .setPositiveButton(android.R.string.ok) { _, _ ->
                    val shortcutName = dialogShortcutBinding!!.shortcutNameInput.text.toString()
                    if (shortcutName.isEmpty()) {
                        Toast.makeText(context, R.string.shortcut_name_empty, Toast.LENGTH_LONG).show()
                        return@setPositiveButton
                    }
                    val iconBitmap = (dialogShortcutBinding!!.shortcutIcon.drawable as BitmapDrawable).bitmap
                    val shortcutManager = activity.getSystemService(ShortcutManager::class.java)

                    CoroutineScope(Dispatchers.IO).launch {
                        val icon = Icon.createWithBitmap(iconBitmap)
                        val shortcut = ShortcutInfo.Builder(context, shortcutName)
                            .setShortLabel(shortcutName)
                            .setIcon(icon)
                            .setIntent(game.launchIntent.apply {
                                putExtra("launchedFromShortcut", true)
                            })
                            .build()

                        shortcutManager?.requestPinShortcut(shortcut, null)
                        imagePath = null
                    }
                }
                .setNegativeButton(android.R.string.cancel) { _, _ ->
                    imagePath = null
                }
                .show()

            bottomSheetDialog.dismiss()
        }


        bottomSheetView.findViewById<MaterialButton>(R.id.cheats).setOnClickListener {
            val action = CheatsFragmentDirections.actionGlobalCheatsFragment(holder.game.titleId)
            view.findNavController().navigate(action)
            bottomSheetDialog.dismiss()
        }

        val bottomSheetBehavior = bottomSheetDialog.getBehavior()
        bottomSheetBehavior.skipCollapsed = true
        bottomSheetBehavior.state = BottomSheetBehavior.STATE_EXPANDED

        bottomSheetDialog.show()
    }

    private fun refreshShortcutDialogIcon() {
        if (imagePath != null) {
            val originalBitmap = BitmapFactory.decodeStream(
                CitraApplication.appContext.contentResolver.openInputStream(
                    imagePath!!.toUri()
                )
            )
            val scaledBitmap = {
                val preferences =
                    PreferenceManager.getDefaultSharedPreferences(CitraApplication.appContext)
                if (preferences.getBoolean("shouldStretchIcon", true)) {
                    // stretch to fit
                    originalBitmap.scale(108, 108)
                } else {
                    // Zoom in to fit the bitmap while keeping the aspect ratio
                    val width = originalBitmap.width
                    val height = originalBitmap.height
                    val targetSize = 108

                    if (width > height) {
                        // Landscape orientation
                        val scaleFactor = targetSize.toFloat() / height
                        val scaledWidth = (width * scaleFactor).toInt()
                        val scaledBmp = originalBitmap.scale(scaledWidth, targetSize)

                        val startX = (scaledWidth - targetSize) / 2
                        Bitmap.createBitmap(scaledBmp, startX, 0, targetSize, targetSize)
                    } else {
                        val scaleFactor = targetSize.toFloat() / width
                        val scaledHeight = (height * scaleFactor).toInt()
                        val scaledBmp = originalBitmap.scale(targetSize, scaledHeight)

                        val startY = (scaledHeight - targetSize) / 2
                        Bitmap.createBitmap(scaledBmp, 0, startY, targetSize, targetSize)
                    }
                }
            }()
                dialogShortcutBinding!!.shortcutIcon.setImageBitmap(scaledBitmap)
            }
        }


    private fun isValidGame(extension: String): Boolean {
        return Game.badExtensions.stream()
            .noneMatch { extension == it.lowercase() }
    }

    private class DiffCallback : DiffUtil.ItemCallback<Game>() {
        override fun areItemsTheSame(oldItem: Game, newItem: Game): Boolean {
            return oldItem.titleId == newItem.titleId
        }

        override fun areContentsTheSame(oldItem: Game, newItem: Game): Boolean {
            return oldItem == newItem
        }
    }
}
