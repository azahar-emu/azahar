// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"
#include "core/rpc/packet.h"
#include "core/rpc/rpc_server.h"

namespace Core::RPC {

RPCServer::RPCServer(Core::System& system_) : system{system_} {
    LOG_INFO(RPC_Server, "Starting RPC server.");
    request_handler_thread =
        std::jthread([this](std::stop_token stop_token) { HandleRequestsLoop(stop_token); });
}

RPCServer::~RPCServer() = default;

void RPCServer::HandleReadMemory(Packet& packet, u32 address, u32 data_size) {
    if (data_size > MAX_READ_SIZE) {
        return;
    }
    u32 read_size = data_size;

    // Note: Memory read occurs asynchronously from the state of the emulator
    if (selected_pid == 0xFFFFFFFF) {
        LOG_ERROR(RPC_Server, "No target process selected, memory access may be invalid.");
        system.Memory().ReadBlock(address, packet.GetPacketData().data(), data_size);
    } else {
        auto process = system.Kernel().GetProcessById(selected_pid);
        if (process) {
            system.Memory().ReadBlock(*process, address, packet.GetPacketData().data(), data_size);
        } else {
            LOG_ERROR(RPC_Server, "Selected process does not exist.");
            read_size = 0;
        }
    }

    packet.SetPacketDataSize(read_size);
    packet.SendReply();
}

void RPCServer::HandleWriteMemory(Packet& packet, u32 address, std::span<const u8> data) {
    // Only allow writing to certain memory regions
    if ((address >= Memory::PROCESS_IMAGE_VADDR && address <= Memory::PROCESS_IMAGE_VADDR_END) ||
        (address >= Memory::HEAP_VADDR && address <= Memory::HEAP_VADDR_END) ||
        (address >= Memory::LINEAR_HEAP_VADDR && address <= Memory::LINEAR_HEAP_VADDR_END) ||
        (address >= Memory::N3DS_EXTRA_RAM_VADDR && address <= Memory::N3DS_EXTRA_RAM_VADDR_END)) {
        // Note: Memory write occurs asynchronously from the state of the emulator
        if (selected_pid == 0xFFFFFFFF) {
            LOG_ERROR(RPC_Server, "No target process selected, memory access may be invalid.");
            system.Memory().WriteBlock(address, data.data(), data.size());
        } else {
            auto process = system.Kernel().GetProcessById(selected_pid);
            if (process) {
                system.Memory().WriteBlock(*process, address, data.data(), data.size());
            } else {
                LOG_ERROR(RPC_Server, "Selected process does not exist.");
            }
        }

        // If the memory happens to be executable code, make sure the changes become visible

        // Is current core correct here?
        system.InvalidateCacheRange(address, data.size());
    }
    packet.SetPacketDataSize(0);
    packet.SendReply();
}

void RPCServer::HandleProcessList(Packet& packet, u32 start_index, u32 max_amount) {
    const auto process_list = system.Kernel().GetProcessList();
    const u32 start = std::min(start_index, static_cast<u32>(process_list.size()));
    const u32 end = std::min(start + max_amount, static_cast<u32>(process_list.size()));
    const u32 count = std::min(end - start, MAX_PROCESSES_IN_LIST);

    u8* out_data = packet.GetPacketData().data();
    u32 written_bytes = 0;

    memcpy(out_data + written_bytes, &count, sizeof(count));
    written_bytes += sizeof(count);

    for (u32 i = start; i < start + count; i++) {
        ProcessInfo info{};
        info.process_id = process_list[i]->process_id;
        info.title_id = process_list[i]->codeset->program_id;
        memcpy(info.process_name.data(), process_list[i]->codeset->name.data(),
               std::min(process_list[i]->codeset->name.size(), info.process_name.size()));

        memcpy(out_data + written_bytes, &info, sizeof(ProcessInfo));
        written_bytes += sizeof(ProcessInfo);
    }

    packet.SetPacketDataSize(written_bytes);
    packet.SendReply();
}

void RPCServer::HandleSetGetProcess(Packet& packet, u32 operation, u32 process_id) {
    u8* out_data = packet.GetPacketData().data();
    u32 written_bytes = 0;

    if (operation == 0) {
        // Get
        memcpy(out_data + written_bytes, &selected_pid, sizeof(selected_pid));
        written_bytes += sizeof(selected_pid);
    } else {
        // Set
        selected_pid = process_id;
    }

    packet.SetPacketDataSize(written_bytes);
    packet.SendReply();
}

bool RPCServer::ValidatePacket(const PacketHeader& packet_header) {
    if (packet_header.version <= CURRENT_VERSION) {
        switch (packet_header.packet_type) {
        case PacketType::ReadMemory:
        case PacketType::WriteMemory:
        case PacketType::ProcessList:
        case PacketType::SetGetProcess:
            if (packet_header.packet_size >= (sizeof(u32) * 2)) {
                return true;
            }
            break;
        default:
            break;
        }
    }
    return false;
}

void RPCServer::HandleSingleRequest(std::unique_ptr<Packet> request_packet) {
    bool success = false;
    const auto packet_data = request_packet->GetPacketData();

    if (ValidatePacket(request_packet->GetHeader())) {
        // Currently, all request types use to arguments
        u32 arg1 = 0;
        u32 arg2 = 0;
        std::memcpy(&arg1, packet_data.data(), sizeof(arg1));
        std::memcpy(&arg2, packet_data.data() + sizeof(arg1), sizeof(arg2));

        switch (request_packet->GetPacketType()) {
        case PacketType::ReadMemory:
            if (arg2 > 0 && arg2 <= MAX_READ_SIZE) {
                HandleReadMemory(*request_packet, arg1, arg2);
                success = true;
            }
            break;
        case PacketType::WriteMemory:
            if (arg2 > 0 && arg2 <= MAX_PACKET_DATA_SIZE - (sizeof(u32) * 2)) {
                const auto data = packet_data.subspan(sizeof(u32) * 2, arg2);
                HandleWriteMemory(*request_packet, arg1, data);
                success = true;
            }
            break;
        case PacketType::ProcessList:
            HandleProcessList(*request_packet, arg1, arg2);
            success = true;
            break;
        case PacketType::SetGetProcess:
            HandleSetGetProcess(*request_packet, arg1, arg2);
            success = true;
            break;
        default:
            break;
        }
    }

    if (!success) {
        // Send an empty reply, so as not to hang the client
        request_packet->SetPacketDataSize(0);
        request_packet->SendReply();
    }
}

void RPCServer::HandleRequestsLoop(std::stop_token stop_token) {
    std::unique_ptr<RPC::Packet> request_packet;

    LOG_INFO(RPC_Server, "Request handler started.");

    while ((request_packet = request_queue.PopWait(stop_token))) {
        HandleSingleRequest(std::move(request_packet));
    }
}

void RPCServer::QueueRequest(std::unique_ptr<RPC::Packet> request) {
    request_queue.Push(std::move(request));
}

}; // namespace Core::RPC
