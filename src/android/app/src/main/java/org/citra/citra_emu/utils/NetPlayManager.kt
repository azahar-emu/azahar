// Copyright 2024 Mandarine Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.utils

import android.app.Activity
import android.content.Context
import android.net.wifi.WifiManager
import android.os.Handler
import android.os.Looper
import android.text.format.Formatter
import android.widget.Toast
import androidx.preference.PreferenceManager
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.R
import org.citra.citra_emu.dialogs.ChatMessage

object NetPlayManager {
    external fun netPlayCreateRoom(ipAddress: String, port: Int, username: String, password: String, roomName: String, maxPlayers: Int): Int
    external fun netPlayJoinRoom(ipAddress: String, port: Int, username: String, password: String): Int
    external fun netPlayRoomInfo(): Array<String>
    external fun netPlayIsJoined(): Boolean
    external fun netPlayIsHostedRoom(): Boolean
    external fun netPlaySendMessage(msg: String)
    external fun netPlayKickUser(username: String)
    external fun netPlayLeaveRoom()
    external fun netPlayIsModerator(): Boolean
    external fun netPlayGetBanList(): Array<String>
    external fun netPlayBanUser(username: String)
    external fun netPlayUnbanUser(username: String)

    private var messageListener: ((Int, String) -> Unit)? = null
    private var adapterRefreshListener: ((Int, String) -> Unit)? = null

    fun setOnMessageReceivedListener(listener: (Int, String) -> Unit) {
        messageListener = listener
    }

    fun setOnAdapterRefreshListener(listener: (Int, String) -> Unit) {
        adapterRefreshListener = listener
    }

    fun getUsername(activity: Context): String {        val prefs = PreferenceManager.getDefaultSharedPreferences(activity)
        val name = "Azahar${(Math.random() * 100).toInt()}"
        return prefs.getString("NetPlayUsername", name) ?: name
    }

    fun setUsername(activity: Activity, name: String) {
        val prefs = PreferenceManager.getDefaultSharedPreferences(activity)
        prefs.edit().putString("NetPlayUsername", name).apply()
    }

    fun getRoomAddress(activity: Activity): String {
        val prefs = PreferenceManager.getDefaultSharedPreferences(activity)
        val address = getIpAddressByWifi(activity)
        return prefs.getString("NetPlayRoomAddress", address) ?: address
    }

    fun setRoomAddress(activity: Activity, address: String) {
        val prefs = PreferenceManager.getDefaultSharedPreferences(activity)
        prefs.edit().putString("NetPlayRoomAddress", address).apply()
    }

    fun getRoomPort(activity: Activity): String {
        val prefs = PreferenceManager.getDefaultSharedPreferences(activity)
        return prefs.getString("NetPlayRoomPort", "24872") ?: "24872"
    }

    fun setRoomPort(activity: Activity, port: String) {
        val prefs = PreferenceManager.getDefaultSharedPreferences(activity)
        prefs.edit().putString("NetPlayRoomPort", port).apply()
    }

    private val chatMessages = mutableListOf<ChatMessage>()
    private var isChatOpen = false

    fun addChatMessage(message: ChatMessage) {
        chatMessages.add(message)
    }

    fun getChatMessages(): List<ChatMessage> = chatMessages

    fun clearChat() {
        chatMessages.clear()
    }

    fun setChatOpen(isOpen: Boolean) {
        isChatOpen = isOpen
    }

    fun addNetPlayMessage(type: Int, msg: String) {
        val context = CitraApplication.appContext
        val message = formatNetPlayStatus(context, type, msg)

        when (type) {
            NetPlayStatus.CHAT_MESSAGE -> {
                val parts = msg.split(":", limit = 2)
                if (parts.size == 2) {
                    val nickname = parts[0].trim()
                    val chatMessage = parts[1].trim()
                    addChatMessage(
                        ChatMessage(
                        nickname = nickname,
                        username = "",
                        message = chatMessage
                    )
                    )
                }
            }
            NetPlayStatus.MEMBER_JOIN,
            NetPlayStatus.MEMBER_LEAVE,
            NetPlayStatus.MEMBER_KICKED,
            NetPlayStatus.MEMBER_BANNED -> {
                addChatMessage(
                    ChatMessage(
                    nickname = "System",
                    username = "",
                    message = message
                )
                )
            }
        }


            Handler(Looper.getMainLooper()).post {
                if (!isChatOpen) {
                    Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
                }
            }


        messageListener?.invoke(type, msg)
        adapterRefreshListener?.invoke(type, msg)
    }

    private fun formatNetPlayStatus(context: Context, type: Int, msg: String): String {
        return when (type) {
            NetPlayStatus.NETWORK_ERROR -> context.getString(R.string.multiplayer_network_error)
            NetPlayStatus.LOST_CONNECTION -> context.getString(R.string.multiplayer_lost_connection)
            NetPlayStatus.NAME_COLLISION -> context.getString(R.string.multiplayer_name_collision)
            NetPlayStatus.MAC_COLLISION -> context.getString(R.string.multiplayer_mac_collision)
            NetPlayStatus.CONSOLE_ID_COLLISION -> context.getString(R.string.multiplayer_console_id_collision)
            NetPlayStatus.WRONG_VERSION -> context.getString(R.string.multiplayer_wrong_version)
            NetPlayStatus.WRONG_PASSWORD -> context.getString(R.string.multiplayer_wrong_password)
            NetPlayStatus.COULD_NOT_CONNECT -> context.getString(R.string.multiplayer_could_not_connect)
            NetPlayStatus.ROOM_IS_FULL -> context.getString(R.string.multiplayer_room_is_full)
            NetPlayStatus.HOST_BANNED -> context.getString(R.string.multiplayer_host_banned)
            NetPlayStatus.PERMISSION_DENIED -> context.getString(R.string.multiplayer_permission_denied)
            NetPlayStatus.NO_SUCH_USER -> context.getString(R.string.multiplayer_no_such_user)
            NetPlayStatus.ALREADY_IN_ROOM -> context.getString(R.string.multiplayer_already_in_room)
            NetPlayStatus.CREATE_ROOM_ERROR -> context.getString(R.string.multiplayer_create_room_error)
            NetPlayStatus.HOST_KICKED -> context.getString(R.string.multiplayer_host_kicked)
            NetPlayStatus.UNKNOWN_ERROR -> context.getString(R.string.multiplayer_unknown_error)
            NetPlayStatus.ROOM_UNINITIALIZED -> context.getString(R.string.multiplayer_room_uninitialized)
            NetPlayStatus.ROOM_IDLE -> context.getString(R.string.multiplayer_room_idle)
            NetPlayStatus.ROOM_JOINING -> context.getString(R.string.multiplayer_room_joining)
            NetPlayStatus.ROOM_JOINED -> context.getString(R.string.multiplayer_room_joined)
            NetPlayStatus.ROOM_MODERATOR -> context.getString(R.string.multiplayer_room_moderator)
            NetPlayStatus.MEMBER_JOIN -> context.getString(R.string.multiplayer_member_join, msg)
            NetPlayStatus.MEMBER_LEAVE -> context.getString(R.string.multiplayer_member_leave, msg)
            NetPlayStatus.MEMBER_KICKED -> context.getString(R.string.multiplayer_member_kicked, msg)
            NetPlayStatus.MEMBER_BANNED -> context.getString(R.string.multiplayer_member_banned, msg)
            NetPlayStatus.ADDRESS_UNBANNED -> context.getString(R.string.multiplayer_address_unbanned)
            NetPlayStatus.CHAT_MESSAGE -> msg
            else -> ""
        }
    }

    fun getIpAddressByWifi(activity: Activity): String {
        var ipAddress = 0
        val wifiManager = activity.getSystemService(WifiManager::class.java)
        val wifiInfo = wifiManager.connectionInfo
        if (wifiInfo != null) {
            ipAddress = wifiInfo.ipAddress
        }

        if (ipAddress == 0) {
            val dhcpInfo = wifiManager.dhcpInfo
            if (dhcpInfo != null) {
                ipAddress = dhcpInfo.ipAddress
            }
        }

        return if (ipAddress == 0) {
            "192.168.0.1"
        } else {
            Formatter.formatIpAddress(ipAddress)
        }
    }

    fun getBanList(): List<String> {
        Log.info("Netplay Ban ${netPlayGetBanList()}.toList()")
        return netPlayGetBanList().toList()
    }

    object NetPlayStatus {
        const val NO_ERROR = 0
        const val NETWORK_ERROR = 1
        const val LOST_CONNECTION = 2
        const val NAME_COLLISION = 3
        const val MAC_COLLISION = 4
        const val CONSOLE_ID_COLLISION = 5
        const val WRONG_VERSION = 6
        const val WRONG_PASSWORD = 7
        const val COULD_NOT_CONNECT = 8
        const val ROOM_IS_FULL = 9
        const val HOST_BANNED = 10
        const val PERMISSION_DENIED = 11
        const val NO_SUCH_USER = 12
        const val ALREADY_IN_ROOM = 13
        const val CREATE_ROOM_ERROR = 14
        const val HOST_KICKED = 15
        const val UNKNOWN_ERROR = 16
        const val ROOM_UNINITIALIZED = 17
        const val ROOM_IDLE = 18
        const val ROOM_JOINING = 19
        const val ROOM_JOINED = 20
        const val ROOM_MODERATOR = 21
        const val MEMBER_JOIN = 22
        const val MEMBER_LEAVE = 23
        const val MEMBER_KICKED = 24
        const val MEMBER_BANNED = 25
        const val ADDRESS_UNBANNED = 26
        const val CHAT_MESSAGE = 27
    }
}