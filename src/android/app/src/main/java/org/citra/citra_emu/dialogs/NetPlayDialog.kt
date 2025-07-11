// Copyright 2024 Mandarine Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

package org.citra.citra_emu.dialogs

import android.content.Context
import android.content.res.Configuration
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.PopupMenu
import android.widget.Toast
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import org.citra.citra_emu.CitraApplication
import org.citra.citra_emu.R
import org.citra.citra_emu.databinding.DialogMultiplayerConnectBinding
import org.citra.citra_emu.databinding.DialogMultiplayerLobbyBinding
import org.citra.citra_emu.databinding.DialogMultiplayerRoomBinding
import org.citra.citra_emu.databinding.ItemBanListBinding
import org.citra.citra_emu.databinding.ItemButtonNetplayBinding
import org.citra.citra_emu.databinding.ItemTextNetplayBinding
import org.citra.citra_emu.dialogs.ChatDialog
import org.citra.citra_emu.utils.CompatUtils
import org.citra.citra_emu.utils.GameHelper
import org.citra.citra_emu.utils.NetPlayManager

class NetPlayDialog(context: Context) : BottomSheetDialog(context) {
    private lateinit var adapter: NetPlayAdapter
    private val gameNameList: MutableList<Array<String>> = mutableListOf()
    private val gameIdList: MutableList<Array<Long>> = mutableListOf()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        behavior.state = BottomSheetBehavior.STATE_EXPANDED
        behavior.state = BottomSheetBehavior.STATE_EXPANDED
        behavior.skipCollapsed = context.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE

        when {
            NetPlayManager.netPlayIsJoined() -> DialogMultiplayerLobbyBinding.inflate(layoutInflater)
                .apply {
                    setContentView(root)
                    adapter = NetPlayAdapter()
                    listMultiplayer.layoutManager = LinearLayoutManager(context)
                    listMultiplayer.adapter = adapter
                    adapter.loadMultiplayerMenu()
                    btnLeave.setOnClickListener {
                        NetPlayManager.netPlayLeaveRoom()
                        dismiss()
                    }
                    btnChat.setOnClickListener {
                        ChatDialog(context).show()
                    }

                    refreshAdapterItems()

                    btnModeration.visibility = if (NetPlayManager.netPlayIsModerator()) View.VISIBLE else View.GONE
                    btnModeration.setOnClickListener {
                        showModerationDialog()
                    }

                }
            else -> {
                DialogMultiplayerConnectBinding.inflate(layoutInflater).apply {
                    setContentView(root)
                    // Prepare the game list in case a user tries to create a room
                    for (game in GameHelper.cachedGameList) {
                        val gameName = game.title
                        if (gameNameList.none { it[0] == gameName }) {
                            gameNameList.add(arrayOf(gameName))
                        }

                        val gameId = game.titleId
                        if (gameIdList.none { it[0] == gameId }) {
                            gameIdList.add((arrayOf(gameId)))
                        }
                    }

                    btnCreate.setOnClickListener {
                        showNetPlayInputDialog(true)
                        dismiss()
                    }
                    btnJoin.setOnClickListener {
                        showNetPlayInputDialog(false)
                        dismiss()
                    }
                    btnLobbyBrowser.setOnClickListener {
                        LobbyBrowser(context).show()
                        dismiss()
                    }
                }
            }
        }
    }

    data class NetPlayItems(
        val option: Int,
        val name: String,
        val type: Int,
        val id: Int = 0
    ) {
        companion object {
            const val MULTIPLAYER_ROOM_TEXT = 1
            const val MULTIPLAYER_ROOM_MEMBER = 2
            const val MULTIPLAYER_SEPARATOR = 3
            const val MULTIPLAYER_ROOM_COUNT = 4
            const val TYPE_BUTTON = 0
            const val TYPE_TEXT = 1
            const val TYPE_SEPARATOR = 2
        }
    }

    inner class NetPlayAdapter : RecyclerView.Adapter<NetPlayAdapter.NetPlayViewHolder>() {
        val netPlayItems = mutableListOf<NetPlayItems>()

        abstract inner class NetPlayViewHolder(itemView: View) : RecyclerView.ViewHolder(itemView), View.OnClickListener {
            init {
                itemView.setOnClickListener(this)
            }
            abstract fun bind(item: NetPlayItems)
        }

        inner class TextViewHolder(private val binding: ItemTextNetplayBinding) : NetPlayViewHolder(binding.root) {
            private lateinit var netPlayItem: NetPlayItems

            override fun onClick(clicked: View) {}

            override fun bind(item: NetPlayItems) {
                netPlayItem = item
                binding.itemTextNetplayName.text = item.name
                binding.itemIcon.apply {
                    val iconRes = when (item.option) {
                        NetPlayItems.MULTIPLAYER_ROOM_TEXT -> R.drawable.ic_system
                        NetPlayItems.MULTIPLAYER_ROOM_COUNT -> R.drawable.ic_joined
                        else -> 0
                    }
                    visibility = if (iconRes != 0) {
                        setImageResource(iconRes)
                        View.VISIBLE
                    } else View.GONE
                }
            }
        }

        inner class ButtonViewHolder(private val binding: ItemButtonNetplayBinding) : NetPlayViewHolder(binding.root) {
            private lateinit var netPlayItems: NetPlayItems
            private val isModerator = NetPlayManager.netPlayIsModerator()

            init {
                binding.itemButtonMore.apply {
                    visibility = View.VISIBLE
                    setOnClickListener { showPopupMenu(it) }
                }
            }

            override fun onClick(clicked: View) {}

            private fun showPopupMenu(view: View) {
                PopupMenu(view.context, view).apply {
                    inflate(R.menu.menu_netplay_member)
                    menu.findItem(R.id.action_kick).isEnabled = isModerator &&
                            netPlayItems.name != NetPlayManager.getUsername(context)
                    menu.findItem(R.id.action_ban).isEnabled = isModerator &&
                            netPlayItems.name != NetPlayManager.getUsername(context)
                    setOnMenuItemClickListener { item ->
                        if (item.itemId == R.id.action_kick) {
                            NetPlayManager.netPlayKickUser(netPlayItems.name)
                            true
                        } else if (item.itemId == R.id.action_ban) {
                            NetPlayManager.netPlayBanUser(netPlayItems.name)
                            true
                        } else false
                    }
                    show()
                }
            }

            override fun bind(item: NetPlayItems) {
                netPlayItems = item
                binding.itemButtonNetplayName.text = netPlayItems.name
            }
        }

        fun loadMultiplayerMenu() {
            val infos = NetPlayManager.netPlayRoomInfo()
            if (infos.isNotEmpty()) {
                val roomInfo = infos[0].split("|")
                netPlayItems.add(NetPlayItems(NetPlayItems.MULTIPLAYER_ROOM_TEXT, roomInfo[0], NetPlayItems.TYPE_TEXT))
                netPlayItems.add(NetPlayItems(NetPlayItems.MULTIPLAYER_ROOM_COUNT, "${infos.size - 1}/${roomInfo[1]}", NetPlayItems.TYPE_TEXT))
                netPlayItems.add(NetPlayItems(NetPlayItems.MULTIPLAYER_SEPARATOR, "", NetPlayItems.TYPE_SEPARATOR))
                for (i in 1 until infos.size) {
                    netPlayItems.add(NetPlayItems(NetPlayItems.MULTIPLAYER_ROOM_MEMBER, infos[i], NetPlayItems.TYPE_BUTTON))
                }
            }
        }

        override fun getItemViewType(position: Int) = netPlayItems[position].type

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): NetPlayViewHolder {
            val inflater = LayoutInflater.from(parent.context)
            return when (viewType) {
                NetPlayItems.TYPE_TEXT -> TextViewHolder(ItemTextNetplayBinding.inflate(inflater, parent, false))
                NetPlayItems.TYPE_BUTTON -> ButtonViewHolder(ItemButtonNetplayBinding.inflate(inflater, parent, false))
                NetPlayItems.TYPE_SEPARATOR -> object : NetPlayViewHolder(inflater.inflate(R.layout.item_separator_netplay, parent, false)) {
                    override fun bind(item: NetPlayItems) {}
                    override fun onClick(clicked: View) {}
                }
                else -> throw IllegalStateException("Unsupported view type")
            }
        }

        override fun onBindViewHolder(holder: NetPlayViewHolder, position: Int) {
            holder.bind(netPlayItems[position])
        }

        override fun getItemCount() = netPlayItems.size
    }

    fun refreshAdapterItems() {
        val handler = Handler(Looper.getMainLooper())

        NetPlayManager.setOnAdapterRefreshListener() { type, msg ->
            handler.post {
                adapter.netPlayItems.clear()
                adapter.loadMultiplayerMenu()
                adapter.notifyDataSetChanged()
            }
        }
    }

    private fun showNetPlayInputDialog(isCreateRoom: Boolean) {
        val activity = CompatUtils.findActivity(context)
        val dialog = BottomSheetDialog(activity)

        dialog.behavior.state = BottomSheetBehavior.STATE_EXPANDED
        dialog.behavior.state = BottomSheetBehavior.STATE_EXPANDED
        dialog.behavior.skipCollapsed = context.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE


        val binding = DialogMultiplayerRoomBinding.inflate(LayoutInflater.from(activity))
        dialog.setContentView(binding.root)

        binding.textTitle.text = activity.getString(
            if (isCreateRoom) R.string.multiplayer_create_room
            else R.string.multiplayer_join_room
        )

        binding.ipAddress.setText(
            if (isCreateRoom) NetPlayManager.getIpAddressByWifi(activity)
            else NetPlayManager.getRoomAddress(activity)
        )
        binding.ipPort.setText(NetPlayManager.getRoomPort(activity))
        binding.username.setText(NetPlayManager.getUsername(activity))

        binding.dropdownPreferedGameName.apply {
            setAdapter(
                ArrayAdapter(
                    activity,
                    R.layout.dropdown_item,
                    gameNameList.map { it[0] }
                )
            )
        }

        binding.preferedGameName.visibility = if (isCreateRoom) View.VISIBLE else View.GONE
        binding.roomName.visibility = if (isCreateRoom) View.VISIBLE else View.GONE
        binding.maxPlayersContainer.visibility = if (isCreateRoom) View.VISIBLE else View.GONE
        binding.maxPlayersLabel.text = context.getString(R.string.multiplayer_max_players_value, binding.maxPlayers.value.toInt())

        binding.maxPlayers.addOnChangeListener { _, value, _ ->
            binding.maxPlayersLabel.text = context.getString(R.string.multiplayer_max_players_value, value.toInt())
        }

        binding.btnConfirm.setOnClickListener {
            binding.btnConfirm.isEnabled = false
            binding.btnConfirm.text = activity.getString(R.string.disabled_button_text)

            val ipAddress = binding.ipAddress.text.toString()
            val username = binding.username.text.toString()
            val portStr = binding.ipPort.text.toString()
            val preferedGameName = binding.dropdownPreferedGameName.text.toString()
            val preferedGameId = gameIdList[gameNameList.indexOfFirst { it[0] == preferedGameName }][0]
            val password = binding.password.text.toString()
            val port = portStr.toIntOrNull() ?: run {
                Toast.makeText(activity, R.string.multiplayer_port_invalid, Toast.LENGTH_LONG).show()
                binding.btnConfirm.isEnabled = true
                binding.btnConfirm.text = activity.getString(R.string.original_button_text)
                return@setOnClickListener
            }
            val roomName = binding.roomName.text.toString()
            val maxPlayers = binding.maxPlayers.value.toInt()

            if (isCreateRoom && (roomName.length !in 3..20)) {
                Toast.makeText(activity, R.string.multiplayer_room_name_invalid, Toast.LENGTH_LONG).show()
                binding.btnConfirm.isEnabled = true
                binding.btnConfirm.text = activity.getString(R.string.original_button_text)
                return@setOnClickListener
            }

            if (isCreateRoom && preferedGameName.isEmpty()) {
                Toast.makeText(activity, R.string.multiplayer_prefered_game_name_invalid, Toast.LENGTH_LONG).show()
                binding.btnConfirm.isEnabled = true
                binding.btnConfirm.text = activity.getString(R.string.original_button_text)
                return@setOnClickListener
            }

            if (ipAddress.length < 7 || username.length < 5) {
                Toast.makeText(activity, R.string.multiplayer_input_invalid, Toast.LENGTH_LONG).show()
                binding.btnConfirm.isEnabled = true
                binding.btnConfirm.text = activity.getString(R.string.original_button_text)
            } else {
                Handler(Looper.getMainLooper()).post {
                    val result = if (isCreateRoom) {
                        NetPlayManager.netPlayCreateRoom(ipAddress, port, username, preferedGameName, preferedGameId, password, roomName, maxPlayers)
                    } else {
                        NetPlayManager.netPlayJoinRoom(ipAddress, port, username, password)
                    }

                    if (result == 0) {
                        NetPlayManager.setUsername(activity, username)
                        NetPlayManager.setRoomPort(activity, portStr)
                        if (!isCreateRoom) NetPlayManager.setRoomAddress(activity, ipAddress)
                        Toast.makeText(
                            CitraApplication.appContext,
                            if (isCreateRoom) R.string.multiplayer_create_room_success
                            else R.string.multiplayer_join_room_success,
                            Toast.LENGTH_LONG
                        ).show()
                        dialog.dismiss()
                    } else {
                        Toast.makeText(activity, R.string.multiplayer_could_not_connect, Toast.LENGTH_LONG).show()
                        binding.btnConfirm.isEnabled = true
                        binding.btnConfirm.text = activity.getString(R.string.original_button_text)
                    }
                }
            }
        }

        dialog.show()
    }

    private fun showModerationDialog() {
        val activity = CompatUtils.findActivity(context)
        val dialog = MaterialAlertDialogBuilder(activity)
        dialog.setTitle(R.string.multiplayer_moderation_title)

        val banList = NetPlayManager.getBanList()
        if (banList.isEmpty()) {
            dialog.setMessage(R.string.multiplayer_no_bans)
            dialog.setPositiveButton(android.R.string.ok, null)
            dialog.show()
            return
        }

        val view = LayoutInflater.from(context).inflate(R.layout.dialog_ban_list, null)
        val recyclerView = view.findViewById<RecyclerView>(R.id.ban_list_recycler)
        recyclerView.layoutManager = LinearLayoutManager(context)

        lateinit var adapter: BanListAdapter

        val onUnban: (String) -> Unit = { bannedItem ->
            MaterialAlertDialogBuilder(activity)
                .setTitle(R.string.multiplayer_unban_title)
                .setMessage(activity.getString(R.string.multiplayer_unban_message, bannedItem))
                .setPositiveButton(R.string.multiplayer_unban) { _, _ ->
                    NetPlayManager.netPlayUnbanUser(bannedItem)
                    adapter.removeBan(bannedItem)
                }
                .setNegativeButton(android.R.string.cancel, null)
                .show()
        }

        adapter = BanListAdapter(banList, onUnban)
        recyclerView.adapter = adapter

        dialog.setView(view)
        dialog.setPositiveButton(android.R.string.ok, null)
        dialog.show()
    }

    private class BanListAdapter(
        banList: List<String>,
        private val onUnban: (String) -> Unit
    ) : RecyclerView.Adapter<BanListAdapter.ViewHolder>() {

        private val usernameBans = banList.filter { !it.contains(".") }.toMutableList()
        private val ipBans = banList.filter { it.contains(".") }.toMutableList()

        class ViewHolder(val binding: ItemBanListBinding) : RecyclerView.ViewHolder(binding.root)

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
            val binding = ItemBanListBinding.inflate(
                LayoutInflater.from(parent.context), parent, false)
            return ViewHolder(binding)
        }

        override fun onBindViewHolder(holder: ViewHolder, position: Int) {
            val isUsername = position < usernameBans.size
            val item = if (isUsername) usernameBans[position] else ipBans[position - usernameBans.size]

            holder.binding.apply {
                banText.text = item
                icon.setImageResource(if (isUsername) R.drawable.ic_user else R.drawable.ic_ip)
                btnUnban.setOnClickListener { onUnban(item) }
            }
        }

        override fun getItemCount() = usernameBans.size + ipBans.size

        fun removeBan(bannedItem: String) {
            val position = if (bannedItem.contains(".")) {
                ipBans.indexOf(bannedItem).let { if (it >= 0) it + usernameBans.size else it }
            } else {
                usernameBans.indexOf(bannedItem)
            }

            if (position >= 0) {
                if (bannedItem.contains(".")) {
                    ipBans.remove(bannedItem)
                } else {
                    usernameBans.remove(bannedItem)
                }
                notifyItemRemoved(position)
            }
        }

    }
}