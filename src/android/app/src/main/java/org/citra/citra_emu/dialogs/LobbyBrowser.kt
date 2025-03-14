// Copyright 2025 Azahar Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included

package org.citra.citra_emu.dialogs

import android.content.Context
import android.content.res.Configuration
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.core.widget.doOnTextChanged
import androidx.recyclerview.widget.DividerItemDecoration
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.textfield.TextInputEditText
import info.debatty.java.stringsimilarity.Jaccard
import info.debatty.java.stringsimilarity.JaroWinkler
import org.citra.citra_emu.R
import org.citra.citra_emu.databinding.DialogLobbyBrowserBinding
import org.citra.citra_emu.databinding.ItemLobbyRoomBinding
import org.citra.citra_emu.utils.CompatUtils
import org.citra.citra_emu.utils.NetPlayManager
import java.util.Locale

class LobbyBrowser(context: Context) : BottomSheetDialog(context) {
    private lateinit var binding: DialogLobbyBrowserBinding
    private lateinit var adapter: LobbyRoomAdapter
    private val handler = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        behavior.state = BottomSheetBehavior.STATE_EXPANDED
        behavior.skipCollapsed =
            context.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE

        binding = DialogLobbyBrowserBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.emptyRefreshButton.setOnClickListener {
            binding.progressBar.visibility = View.VISIBLE
            refreshRoomList()
        }

        setupRecyclerView()
        setupRefreshButton()
        refreshRoomList()
        setupSearchBar()
    }

    private fun setupRecyclerView() {
        adapter = LobbyRoomAdapter { room -> handleRoomSelection(room) }

        binding.roomList.apply {
            layoutManager = LinearLayoutManager(context)
            adapter = this@LobbyBrowser.adapter
            addItemDecoration(DividerItemDecoration(context, DividerItemDecoration.VERTICAL))
        }
    }

    private fun setupRefreshButton() {
        binding.refreshButton.setOnClickListener {
            binding.refreshButton.isEnabled = false
            binding.progressBar.visibility = View.VISIBLE
            refreshRoomList()
        }
    }

    private fun setupSearchBar() {
        binding.chipGroup.setOnCheckedStateChangeListener { _, _ -> adapter.filterAndSearch() }


        binding.searchText.doOnTextChanged { text: CharSequence?, _: Int, _: Int, _: Int ->
            if (text.toString().isNotEmpty()) {
                binding.clearButton.visibility = View.VISIBLE
            } else {
                binding.clearButton.visibility = View.INVISIBLE
            }
            adapter.filterAndSearch()
        }

        binding.clearButton.setOnClickListener {
            binding.searchText.setText("")
            adapter.updateRooms(NetPlayManager.getPublicRooms())
        }
    }

    private fun refreshRoomList() {
        NetPlayManager.refreshRoomListAsync { rooms ->
            binding.emptyView.visibility = if (rooms.isEmpty()) View.VISIBLE else View.GONE
            binding.roomList.visibility = if (rooms.isEmpty()) View.GONE else View.VISIBLE
            binding.appbar.visibility = if (rooms.isEmpty()) View.GONE else View.VISIBLE
            adapter.updateRooms(rooms)
            adapter.filterAndSearch()
            binding.refreshButton.isEnabled = true
            binding.progressBar.visibility = View.GONE
        }
    }

    private fun handleRoomSelection(room: NetPlayManager.RoomInfo) {
        if (room.hasPassword) {
            showPasswordDialog(room)
        } else {
            joinRoom(room, "")
        }
    }

    private fun showPasswordDialog(room: NetPlayManager.RoomInfo) {
        val dialogView = LayoutInflater.from(context).inflate(R.layout.dialog_password_input, null)
        val passwordInput = dialogView.findViewById<TextInputEditText>(R.id.password_input)

        MaterialAlertDialogBuilder(context)
            .setTitle(context.getString(R.string.multiplayer_password_required))
            .setView(dialogView)
            .setPositiveButton(R.string.multiplayer_join_room) { _, _ ->
                joinRoom(room, passwordInput.text.toString())
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun joinRoom(room: NetPlayManager.RoomInfo, password: String) {
        val username = NetPlayManager.getUsername(context)

        Thread {
            val result = NetPlayManager.netPlayJoinRoom(room.ip, room.port, username, password)

            handler.post {
                if (result == 0) {
                    dismiss()
                    NetPlayDialog(context).show()
                }
            }
        }.start()
    }

    inner class LobbyRoomAdapter(private val onRoomSelected: (NetPlayManager.RoomInfo) -> Unit) :
        RecyclerView.Adapter<LobbyRoomAdapter.RoomViewHolder>() {

        private val rooms = mutableListOf<NetPlayManager.RoomInfo>()

        inner class RoomViewHolder(private val binding: ItemLobbyRoomBinding) :
            RecyclerView.ViewHolder(binding.root) {
            fun bind(room: NetPlayManager.RoomInfo) {
                binding.roomName.text = room.name
                binding.roomOwner.text = room.owner
                binding.playerCount.text = context.getString(
                    R.string.multiplayer_player_count,
                    room.members.size,
                    room.maxPlayers
                )

                binding.lockIcon.visibility = if (room.hasPassword) View.VISIBLE else View.GONE

                if (room.preferredGameName.isNotEmpty() && room.preferredGameId != 0L) {
                    binding.gameName.text = room.preferredGameName
                } else {
                    binding.gameName.text = context.getString(R.string.multiplayer_no_game_info)
                }

                itemView.setOnClickListener { onRoomSelected(room) }
            }
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RoomViewHolder {
            val binding = ItemLobbyRoomBinding.inflate(
                LayoutInflater.from(parent.context),
                parent,
                false
            )
            return RoomViewHolder(binding)
        }

        override fun onBindViewHolder(holder: RoomViewHolder, position: Int) {
            holder.bind(rooms[position])
        }

        override fun getItemCount() = rooms.size

        fun updateRooms(newRooms: List<NetPlayManager.RoomInfo>) {
            rooms.clear()
            rooms.addAll(newRooms)
            notifyDataSetChanged()
        }

        fun filterAndSearch() {
            if (binding.searchText.text.toString().isEmpty() &&
                binding.chipGroup.checkedChipId == View.NO_ID
            ) {
                adapter.updateRooms(NetPlayManager.getPublicRooms())
                return
            }

            val baseList = NetPlayManager.getPublicRooms()
            val filteredList: List<NetPlayManager.RoomInfo> =
                when (binding.chipGroup.checkedChipId) {
                    R.id.chip_hide_full -> {
                        baseList.filter { it.members.size < it.maxPlayers }
                    }

                    R.id.chip_hide_empty -> {
                        baseList.filter {
                            it.members.isNotEmpty()
                        }
                    }

                    else -> baseList
                }

            if (binding.searchText.text.toString().isEmpty() &&
                binding.chipGroup.checkedChipId != View.NO_ID
            ) {
                adapter.updateRooms(filteredList)
                return
            }

            val searchTerm = binding.searchText.text.toString().lowercase(Locale.getDefault())
            val searchAlgorithm = if (searchTerm.length > 1) Jaccard(2) else JaroWinkler()
            val sortedList: List<NetPlayManager.RoomInfo> = filteredList.mapNotNull { room ->
                    val roomName = room.name.lowercase(Locale.getDefault())

                    val score = searchAlgorithm.similarity(roomName, searchTerm)
                    if (score > 0.03) {
                        ScoreItem(score, room)
                    } else {
                        null
                    }
                }.sortedByDescending { it ->
                    it.score
                }.map { it.item }
            adapter.updateRooms(sortedList)

        }
    }

    private inner class ScoreItem(val score: Double, val item: NetPlayManager.RoomInfo)
}