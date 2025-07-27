// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import android.content.SharedPreferences
import android.net.Uri
import androidx.preference.PreferenceManager
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.NativeLibrary
import org.citra.citra_emu.R
import org.citra.citra_emu.model.CheapDocument
import org.citra.citra_emu.model.Game
import org.citra.citra_emu.model.GameInfo
import java.io.IOException

object GameHelper {
    const val KEY_GAME_PATH = "game_path"
    const val KEY_GAMES = "Games"

    private lateinit var preferences: SharedPreferences

    fun getGames(): List<Game> {
        val games = mutableListOf<Game>()
        val context = CitraApplication.appContext
        preferences = PreferenceManager.getDefaultSharedPreferences(context)
        val gamesDir = preferences.getString(KEY_GAME_PATH, "")
        val gamesUri = Uri.parse(gamesDir)

        addGamesRecursive(games, FileUtil.listFiles(gamesUri), 3)
        NativeLibrary.getInstalledGamePaths().forEach {
            games.add(getGame(Uri.parse(it), isInstalled = true, addedToLibrary = true))
        }

        // Cache list of games found on disk
        val serializedGames = mutableSetOf<String>()
        games.forEach {
            serializedGames.add(Json.encodeToString(it))
        }
        preferences.edit()
            .remove(KEY_GAMES)
            .putStringSet(KEY_GAMES, serializedGames)
            .apply()

        return games.toList()
    }

    private fun addGamesRecursive(
        games: MutableList<Game>,
        files: Array<CheapDocument>,
        depth: Int
    ) {
        if (depth <= 0) {
            return
        }

        files.forEach {
            if (it.isDirectory) {
                addGamesRecursive(games, FileUtil.listFiles(it.uri), depth - 1)
            } else {
                if (Game.allExtensions.contains(FileUtil.getExtension(it.uri))) {
                    games.add(getGame(it.uri, isInstalled = false, addedToLibrary = true))
                }
            }
        }
    }

    fun getGame(uri: Uri, isInstalled: Boolean, addedToLibrary: Boolean): Game {
        val filePath = uri.toString()
        var gameInfo: GameInfo? = GameInfo(filePath)

        if (gameInfo?.isValid() == false) {
            gameInfo = null
        }

        val isEncrypted = gameInfo?.isEncrypted() == true

        val newGame = Game(
            (gameInfo?.getTitle() ?: FileUtil.getFilename(uri)).replace("[\\t\\n\\r]+".toRegex(), " "),
            filePath.replace("\n", " "),
            filePath,
            gameInfo?.getTitleID() ?: 0,
            gameInfo?.getCompany() ?: "",
            if (isEncrypted) { CitraApplication.appContext.getString(R.string.unsupported_encrypted) } else { gameInfo?.getRegions() ?: "" },
            isInstalled,
            gameInfo?.isSystemTitle() ?: false,
            gameInfo?.getIsVisibleSystemTitle() ?: false,
            gameInfo?.getIcon(),
            gameInfo?.getFileType() ?: "",
            if (FileUtil.isNativePath(filePath)) {
                CitraApplication.documentsTree.getFilename(filePath)
            } else {
                FileUtil.getFilename(Uri.parse(filePath))
            }
        )

        if (addedToLibrary) {
            val addedTime = preferences.getLong(newGame.keyAddedToLibraryTime, 0L)
            if (addedTime == 0L) {
                preferences.edit()
                    .putLong(newGame.keyAddedToLibraryTime, System.currentTimeMillis())
                    .apply()
            }
        }

        return newGame
    }
}
