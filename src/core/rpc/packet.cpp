// Copyright 2019 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <algorithm>
#include <cstring>
#include "core/rpc/packet.h"

namespace Core::RPC {

Packet::Packet(const PacketHeader& header_, u8* data,
               std::function<void(Packet&)> send_reply_callback_)
    : header{header_}, send_reply_callback{std::move(send_reply_callback_)} {
    std::memcpy(packet_data.data(), data, std::min(header.packet_size, MAX_PACKET_DATA_SIZE));
}

Packet::~Packet() = default;

}; // namespace Core::RPC
