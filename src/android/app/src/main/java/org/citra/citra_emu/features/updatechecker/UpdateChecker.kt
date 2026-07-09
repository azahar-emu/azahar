// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.features.updatechecker

import java.net.HttpURLConnection
import java.net.URL
import kotlin.time.Duration.Companion.seconds
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.jsonObject
import org.citra.citra_emu.utils.Log

object UpdateChecker {

    private fun getResponse(urlString: String): String? {
        var connection: HttpURLConnection? = null
        try {
            val url = URL(urlString)
            connection = url.openConnection() as HttpURLConnection
            connection.requestMethod = "GET"
            connection.connectTimeout = 15.seconds.inWholeMilliseconds.toInt()
            connection.readTimeout = 15.seconds.inWholeMilliseconds.toInt()

            if (connection.responseCode == HttpURLConnection.HTTP_OK) {
                return connection.inputStream.bufferedReader().use { it.readText() }
            } else {
                Log.error(
                    "[UpdateChecker] Failed to get HTTP response with HTTP response code ${connection.responseCode}"
                )
                return null
            }
        } catch (e: Exception) {
            Log.error(
                "[UpdateChecker] Failed to get HTTP response with Kotlin exception:\n" +
                    "Type: $e\n" +
                    "Message: ${e.message}"
            )
            return null
        } finally {
            connection?.disconnect()
        }
    }

    fun getLatestRelease(includePrereleases: Boolean): String? {
        val updateCheckUrl = "https://api.github.com"
        var updateCheckPath = "/repos/azahar-emu/azahar"
        if (includePrereleases) { // This can return either a prerelease or a stable release,
            // whichever is more recent.
            val updateCheckTagsPath = "$updateCheckPath/tags"
            val updateCheckReleasesPath = "$updateCheckPath/releases"

            val tagsResponse = getResponse(updateCheckUrl + updateCheckTagsPath)
            val releasesResponse = getResponse(updateCheckUrl + updateCheckReleasesPath)

            if (tagsResponse.isNullOrEmpty() || releasesResponse.isNullOrEmpty()) {
                return null
            }

            val latestTag = Json.decodeFromString<JsonArray>(
                tagsResponse
            )[0].jsonObject["name"].toString()
                .drop(1).dropLast(1) // Remove quotation marks
            val latestTagHasRelease = releasesResponse.contains("\"$latestTag\"")
            // If there is a newer tag, but that tag has no associated release, don't prompt the
            // user to update.
            if (!latestTagHasRelease) {
                return null
            }

            return latestTag
        } else { // Stable releases only
            updateCheckPath += "/releases/latest"
            val response = getResponse(updateCheckUrl + updateCheckPath)

            if (response.isNullOrEmpty()) {
                return null
            }

            val latestTag = Json.decodeFromString<JsonElement>(
                response
            ).jsonObject["tag_name"].toString()
                .drop(1).dropLast(1) // Remove quotation mark
            return latestTag
        }
    }
}
