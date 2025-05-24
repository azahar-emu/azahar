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
import android.view.ViewGroup
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import org.citra.citra_emu.R
import org.citra.citra_emu.databinding.DialogChatBinding
import org.citra.citra_emu.databinding.ItemChatMessageBinding
import org.citra.citra_emu.utils.NetPlayManager
import java.text.SimpleDateFormat
import java.util.*

class ChatMessage(
    val nickname: String, // This is the common name youll see on private servers
    val username: String, // Username is the community/forum username
    val message: String,
    val timestamp: String = SimpleDateFormat("HH:mm", Locale.getDefault()).format(Date())
) {
}

class ChatDialog(context: Context) : BottomSheetDialog(context) {
    private lateinit var binding: DialogChatBinding
    private lateinit var chatAdapter: ChatAdapter
    private val handler = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = DialogChatBinding.inflate(LayoutInflater.from(context))
        setContentView(binding.root)

        NetPlayManager.setChatOpen(true)
        setupRecyclerView()

        behavior.state = BottomSheetBehavior.STATE_EXPANDED
        behavior.state = BottomSheetBehavior.STATE_EXPANDED
        behavior.skipCollapsed = context.resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE


        handler.post {
            chatAdapter.notifyDataSetChanged()
            binding.chatRecyclerView.post {
                scrollToBottom()
            }
        }

        NetPlayManager.setOnMessageReceivedListener { type, message ->
            handler.post {
                chatAdapter.notifyDataSetChanged()
                scrollToBottom()
            }
        }

        binding.sendButton.setOnClickListener {
            val message = binding.chatInput.text.toString()
            if (message.isNotBlank()) {
                sendMessage(message)
                binding.chatInput.text?.clear()
            }
        }
    }

    override fun dismiss() {
        NetPlayManager.setChatOpen(false)
        super.dismiss()
    }

    private fun sendMessage(message: String) {
        val username = NetPlayManager.getUsername(context)
        NetPlayManager.netPlaySendMessage(message)

        val chatMessage = ChatMessage(
            nickname = username,
            username = "",
            message = message,
            timestamp = SimpleDateFormat("HH:mm", Locale.getDefault()).format(Date())
        )

        NetPlayManager.addChatMessage(chatMessage)
        chatAdapter.notifyDataSetChanged()
        scrollToBottom()
    }

    private fun setupRecyclerView() {
        chatAdapter = ChatAdapter(NetPlayManager.getChatMessages())
        binding.chatRecyclerView.layoutManager = LinearLayoutManager(context).apply {
            stackFromEnd = true
        }
        binding.chatRecyclerView.adapter = chatAdapter
    }

    private fun scrollToBottom() {
        binding.chatRecyclerView.scrollToPosition(chatAdapter.itemCount - 1)
    }
}

class ChatAdapter(private val messages: List<ChatMessage>) :
    RecyclerView.Adapter<ChatAdapter.ChatViewHolder>() {

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ChatViewHolder {
        val binding = ItemChatMessageBinding.inflate(
            LayoutInflater.from(parent.context),
            parent,
            false
        )
        return ChatViewHolder(binding)
    }

    override fun getItemCount(): Int = messages.size

    override fun onBindViewHolder(holder: ChatViewHolder, position: Int) {
        holder.bind(messages[position])
    }

    inner class ChatViewHolder(private val binding: ItemChatMessageBinding) :
        RecyclerView.ViewHolder(binding.root) {
        fun bind(message: ChatMessage) {
            binding.usernameText.text = message.nickname
            binding.messageText.text = message.message
            binding.userIcon.setImageResource(when (message.nickname) {
                "System" -> R.drawable.ic_system
                else -> R.drawable.ic_user
            })
        }
    }
}
