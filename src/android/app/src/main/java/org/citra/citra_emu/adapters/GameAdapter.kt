// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.adapters

import android.graphics.drawable.Icon
import android.content.Intent
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
import androidx.core.content.edit
import androidx.core.graphics.scale
import androidx.core.net.toUri
import androidx.documentfile.provider.DocumentFile
import androidx.lifecycle.ViewModelProvider
import androidx.navigation.findNavController
import androidx.preference.PreferenceManager
import androidx.recyclerview.widget.AsyncDifferConfig
import androidx.recyclerview.widget.DiffUtil
import androidx.recyclerview.widget.ListAdapter
import androidx.recyclerview.widget.RecyclerView
import android.widget.PopupMenu
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
import org.citra.citra_emu.fragments.IndeterminateProgressDialogFragment
import org.citra.citra_emu.model.Game
import org.citra.citra_emu.utils.FileUtil
import org.citra.citra_emu.utils.GameIconUtils
import org.citra.citra_emu.viewmodel.GamesViewModel

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

    private data class GameDirectories(
        val gameDir: String,
        val saveDir: String,
        val modsDir: String,
        val texturesDir: String,
        val appDir: String,
        val dlcDir: String,
        val updatesDir: String,
        val extraDir: String
    )
    private fun getGameDirectories(game: Game): GameDirectories {
        val basePath = "sdmc/Nintendo 3DS/00000000000000000000000000000000/00000000000000000000000000000000"
        return GameDirectories(
            gameDir = game.path.substringBeforeLast("/"),
            saveDir = basePath + "/title/${String.format("%016x", game.titleId).lowercase().substring(0, 8)}/${String.format("%016x", game.titleId).lowercase().substring(8)}/data/00000001",
            modsDir = "load/mods/${String.format("%016X", game.titleId)}",
            texturesDir = "load/textures/${String.format("%016X", game.titleId)}",
            appDir = game.path.substringBeforeLast("/").split("/").filter { it.isNotEmpty() }.joinToString("/"),
            dlcDir = basePath + "/title/0004008c/${String.format("%016x", game.titleId).lowercase().substring(8)}/content",
            updatesDir = basePath + "/title/0004000e/${String.format("%016x", game.titleId).lowercase().substring(8)}/content",
            extraDir = basePath + "/extdata/00000000/${String.format("%016X", game.titleId).substring(8, 14).padStart(8, '0')}"
        )
    }

    private fun showOpenContextMenu(view: View, game: Game) {
        val dirs = getGameDirectories(game)

        val popup = PopupMenu(view.context, view).apply {
            menuInflater.inflate(R.menu.game_context_menu_open, menu)
            listOf(
                R.id.game_context_open_app to dirs.appDir,
                R.id.game_context_open_save_dir to dirs.saveDir,
                R.id.game_context_open_updates to dirs.updatesDir,
                R.id.game_context_open_dlc to dirs.dlcDir,
                R.id.game_context_open_extra to dirs.extraDir
            ).forEach { (id, dir) ->
                menu.findItem(id)?.isEnabled =
                    CitraApplication.documentsTree.folderUriHelper(dir)?.let {
                        DocumentFile.fromTreeUri(view.context, it)?.exists()
                    } ?: false
            }
        }

        popup.setOnMenuItemClickListener { menuItem ->
            val intent = Intent(Intent.ACTION_VIEW)
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                .setType("*/*")

            val uri = when (menuItem.itemId) {
                R.id.game_context_open_app -> CitraApplication.documentsTree.folderUriHelper(dirs.appDir)
                R.id.game_context_open_save_dir -> CitraApplication.documentsTree.folderUriHelper(dirs.saveDir)
                R.id.game_context_open_updates -> CitraApplication.documentsTree.folderUriHelper(dirs.updatesDir)
                R.id.game_context_open_dlc -> CitraApplication.documentsTree.folderUriHelper(dirs.dlcDir)
                R.id.game_context_open_extra -> CitraApplication.documentsTree.folderUriHelper(dirs.extraDir)
                R.id.game_context_open_textures -> CitraApplication.documentsTree.folderUriHelper(dirs.texturesDir, true)
                R.id.game_context_open_mods -> CitraApplication.documentsTree.folderUriHelper(dirs.modsDir, true)
                else -> null
            }

            uri?.let {
                intent.data = it
                view.context.startActivity(intent)
                true
            } ?: false
        }

        popup.show()
    }

    private fun showUninstallContextMenu(view: View, game: Game, bottomSheetDialog: BottomSheetDialog) {
        val dirs = getGameDirectories(game)
        val popup = PopupMenu(view.context, view).apply {
            menuInflater.inflate(R.menu.game_context_menu_uninstall, menu)
            listOf(
                R.id.game_context_uninstall to dirs.gameDir,
                R.id.game_context_uninstall_dlc to dirs.dlcDir,
                R.id.game_context_uninstall_updates to dirs.updatesDir
            ).forEach { (id, dir) ->
                menu.findItem(id)?.isEnabled =
                    CitraApplication.documentsTree.folderUriHelper(dir)?.let {
                        DocumentFile.fromTreeUri(view.context, it)?.exists()
                    } ?: false
            }
        }

        popup.setOnMenuItemClickListener { menuItem ->
            val uninstallAction: () -> Unit = {
                when (menuItem.itemId) {
                    R.id.game_context_uninstall -> CitraApplication.documentsTree.deleteDocument(dirs.gameDir)
                    R.id.game_context_uninstall_dlc -> FileUtil.deleteDocument(CitraApplication.documentsTree.folderUriHelper(dirs.dlcDir)
                        .toString())
                    R.id.game_context_uninstall_updates -> FileUtil.deleteDocument(CitraApplication.documentsTree.folderUriHelper(dirs.updatesDir)
                        .toString())
                }
                ViewModelProvider(activity)[GamesViewModel::class.java].reloadGames(true)
                bottomSheetDialog.dismiss()
            }

            if (menuItem.itemId in listOf(R.id.game_context_uninstall, R.id.game_context_uninstall_dlc, R.id.game_context_uninstall_updates)) {
                IndeterminateProgressDialogFragment.newInstance(activity, R.string.uninstalling, false, uninstallAction)
                    .show(activity.supportFragmentManager, IndeterminateProgressDialogFragment.TAG)
                true
            } else {
                false
            }
        }

        popup.show()
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

        bottomSheetView.findViewById<MaterialButton>(R.id.menu_button_open).setOnClickListener {
            showOpenContextMenu(it, game)
        }

        bottomSheetView.findViewById<MaterialButton>(R.id.menu_button_uninstall).setOnClickListener {
            showUninstallContextMenu(it, game, bottomSheetDialog)
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
