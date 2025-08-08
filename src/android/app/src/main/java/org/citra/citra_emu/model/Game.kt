// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.model

import android.os.Parcelable
import android.content.Intent
import android.net.Uri
import java.util.HashSet
import kotlinx.parcelize.Parcelize
import kotlinx.serialization.Serializable
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.activities.EmulationActivity

@Parcelize
@Serializable
class Game(
    val title: String = "",
    val description: String = "",
    val path: String = "",
    val titleId: Long = 0L,
    val company: String = "",
    val regions: String = "",
    val isInstalled: Boolean = false,
    val isSystemTitle: Boolean = false,
    val isVisibleSystemTitle: Boolean = false,
    val icon: IntArray? = null,
    val fileType: String = "",
    val filename: String,
) : Parcelable {
    val keyAddedToLibraryTime get() = "${filename}_AddedToLibraryTime"
    val keyLastPlayedTime get() = "${filename}_LastPlayed"

    val launchIntent: Intent
        get() = Intent(CitraApplication.appContext, EmulationActivity::class.java).apply {
            action = Intent.ACTION_VIEW
            data = if (isInstalled) {
                CitraApplication.documentsTree.getUri(path)
            } else {
                Uri.parse(path)
            }
        }

    override fun equals(other: Any?): Boolean {
        if (other !is Game) {
            return false
        }

        return hashCode() == other.hashCode()
    }

    override fun hashCode(): Int {
        var result = title.hashCode()
        result = 31 * result + description.hashCode()
        result = 31 * result + regions.hashCode()
        result = 31 * result + path.hashCode()
        result = 31 * result + titleId.hashCode()
        result = 31 * result + company.hashCode()
        return result
    }

    companion object {
        val allExtensions: Set<String> get() = extensions + badExtensions

        val extensions: Set<String> = HashSet(
            listOf("3dsx", "app", "axf", "cci", "cxi", "elf", "z3dsx", "zcci", "zcxi")
        )

        val badExtensions: Set<String> = HashSet(
            listOf("rar", "zip", "7z", "torrent", "tar", "gz")
        )
    }
}
