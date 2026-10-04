// Copyright 2019-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <algorithm>
#include <chrono>
#include <cstring>
#include <future>
#include <thread>
#include <lodepng.h>
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"
#include "core/rpc/packet.h"
#include "core/rpc/rpc_server.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"

namespace Core::RPC {

#ifdef ENABLE_SCRIPTING_SYNC
constexpr bool sync_with_core = true;
#else
constexpr bool sync_with_core = false;
#endif

RPCServer::RPCServer(Core::System& system_) : system{system_} {
    LOG_INFO(RPC_Server, "Starting RPC server ({}).",
             sync_with_core ? "synchronous" : "asynchronous");
    request_handler_thread =
        std::jthread([this](std::stop_token stop_token) { HandleRequestsLoop(stop_token); });
}

RPCServer::~RPCServer() {
    Stop();
}

void RPCServer::Stop() {
    if (request_handler_thread.joinable()) {
        request_handler_thread.request_stop();
        request_handler_thread.join();
    }
    if (screenshot_thread.joinable()) {
        screenshot_thread.request_stop();
        screenshot_thread.join();
    }
}

void RPCServer::HandleReadMemory(Packet& packet, u32 address, u32 data_size) {
    if (data_size > MAX_READ_SIZE) {
        return;
    }
    u32 read_size = data_size;

    // Note: Unless built with ENABLE_SCRIPTING_SYNC, memory reads occurs asynchronously from the
    // state of the emulator, which is not safe for some regions.
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
        // Note: Unless built with ENABLE_SCRIPTING_SYNC, memory writes occurs asynchronously from
        // the state of the emulator, which is not safe for some regions.
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

void RPCServer::HandleTakeScreenshot(std::unique_ptr<Packet> packet, u32 res_scale,
                                     ScreenshotFlags flags) {
    const auto reply = [](Packet& reply_packet, const ScreenshotInfo& info) {
        std::memcpy(reply_packet.GetPacketData().data(), &info, sizeof(info));
        reply_packet.SetPacketDataSize(sizeof(info));
        reply_packet.SendReply();
    };
    const auto fail = [&](ScreenshotResult result) {
        ScreenshotInfo info{};
        info.result = result;
        reply(*packet, info);
    };

    constexpr ScreenshotFlags valid_flags =
        ScreenshotFlags::SecondaryWindow | ScreenshotFlags::RawRGB;
    if (True(flags & ~valid_flags) || res_scale > MAX_SCREENSHOT_RES_SCALE) {
        return fail(ScreenshotResult::InvalidArgument);
    }
    if (Settings::values.graphics_api.GetValue() == Settings::GraphicsAPI::Software) {
        return fail(ScreenshotResult::Unsupported);
    }

    auto& renderer = system.GPU().Renderer();
    {
        std::scoped_lock lock{screenshot_mutex};
        if (screenshot_in_progress || renderer.IsScreenshotPending()) {
            return fail(ScreenshotResult::Busy);
        }
        screenshot_in_progress = true;
        screenshot_data.clear();
    }

    if (res_scale == 0) {
        res_scale = renderer.GetResolutionScaleFactor();
    }
    const bool is_secondary = True(flags & ScreenshotFlags::SecondaryWindow);
    const auto layout = Layout::FrameLayoutFromResolutionScale(res_scale, is_secondary);

    struct CaptureState {
        std::vector<u8> pixels;
        std::promise<bool> done;
    };
    auto state = std::make_shared<CaptureState>();
    state->pixels.resize(static_cast<std::size_t>(layout.width) * layout.height * 4);
    auto done_future = state->done.get_future();

    renderer.RequestScreenshot(
        state->pixels.data(), [state](bool invert_y) { state->done.set_value(invert_y); }, layout);

    // Encode the screenshot in a separate thread, to prevent stalling the emulation thread
    // or the RPC thread.
    screenshot_thread = std::jthread([this, state, done_future = std::move(done_future),
                                      packet = std::move(packet), layout, flags,
                                      reply](std::stop_token stop_token) mutable {
        // If emulation is paused no frame is rendered, so time out instead of hanging the client.
        constexpr auto screenshot_timeout = std::chrono::seconds(10);
        constexpr auto poll_interval = std::chrono::milliseconds(50);

        ScreenshotInfo info{};
        std::vector<u8> data;

        const auto deadline = std::chrono::steady_clock::now() + screenshot_timeout;
        while (done_future.wait_for(poll_interval) != std::future_status::ready &&
               !stop_token.stop_requested() && std::chrono::steady_clock::now() < deadline) {
        }

        if (done_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            LOG_ERROR(RPC_Server, "Timed out waiting for screenshot");
            info.result = ScreenshotResult::Timeout;
        } else {
            const bool invert_y = done_future.get();
            const u32 width = layout.width;
            const u32 height = layout.height;

            // Convert to RGB8
            std::vector<u8> rgb(static_cast<std::size_t>(width) * height * 3);
            for (u32 y = 0; y < height; y++) {
                const u32 src_y = invert_y ? (height - 1 - y) : y;
                const u8* src = state->pixels.data() + static_cast<std::size_t>(src_y) * width * 4;
                u8* dst = rgb.data() + static_cast<std::size_t>(y) * width * 3;
                for (u32 x = 0; x < width; x++) {
                    dst[x * 3 + 0] = src[x * 4 + 2];
                    dst[x * 3 + 1] = src[x * 4 + 1];
                    dst[x * 3 + 2] = src[x * 4 + 0];
                }
            }

            info.result = ScreenshotResult::Success;
            if (True(flags & ScreenshotFlags::RawRGB)) {
                data = std::move(rgb);
            } else if (const u32 ret = lodepng::encode(data, rgb, width, height, LCT_RGB, 8)) {
                LOG_ERROR(RPC_Server, "Failed to encode screenshot: {}", lodepng_error_text(ret));
                data.clear();
                info.result = ScreenshotResult::EncodeFailed;
            }
            if (info.result == ScreenshotResult::Success) {
                info.width = width;
                info.height = height;
                info.data_size = static_cast<u32>(data.size());
            }
        }

        {
            std::scoped_lock lock{screenshot_mutex};
            screenshot_data = std::move(data);
            screenshot_in_progress = false;
        }
        reply(*packet, info);
    });
}

void RPCServer::HandleReadScreenshot(Packet& packet, u32 offset, u32 data_size) {
    u32 read_size = 0;
    {
        std::scoped_lock lock{screenshot_mutex};
        if (!screenshot_in_progress && offset < screenshot_data.size()) {
            read_size = std::min(
                {data_size, MAX_READ_SIZE, static_cast<u32>(screenshot_data.size() - offset)});
            std::memcpy(packet.GetPacketData().data(), screenshot_data.data() + offset, read_size);
        }
    }

    packet.SetPacketDataSize(read_size);
    packet.SendReply();
}

void RPCServer::HandleGetPerfStats(Packet& packet, PerfStatsMode mode) {
    u32 written_bytes = 0;

    if (mode == PerfStatsMode::GetReset || mode == PerfStatsMode::Last) {
        const auto results = mode == PerfStatsMode::GetReset ? system.GetAndResetPerfStats()
                                                             : system.GetLastPerfStats();
        PerfStatsInfo info{
            .system_fps = results.system_fps,
            .game_fps = results.game_fps,
            .time_vblank_interval = results.time_vblank_interval,
            .time_hle_svc = results.time_hle_svc,
            .time_hle_ipc = results.time_hle_ipc,
            .time_gpu = results.time_gpu,
            .time_swap = results.time_swap,
            .time_remaining = results.time_remaining,
            .emulation_speed = results.emulation_speed,
            .artic_transmitted = results.artic_transmitted,
            .artic_events = results.artic_events.raw,
        };
        std::memcpy(packet.GetPacketData().data(), &info, sizeof(info));
        written_bytes = sizeof(info);
    }

    packet.SetPacketDataSize(written_bytes);
    packet.SendReply();
}

bool RPCServer::ValidatePacket(const PacketHeader& packet_header) {
    if (packet_header.version == CURRENT_VERSION) {
        switch (packet_header.packet_type) {
        case PacketType::ReadMemory:
        case PacketType::WriteMemory:
        case PacketType::ProcessList:
        case PacketType::SetGetProcess:
        case PacketType::TakeScreenshot:
        case PacketType::ReadScreenshot:
        case PacketType::GetPerfStats:
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

bool RPCServer::IsCoreRequest(PacketType packet_type) {
    switch (packet_type) {
    case PacketType::ReadScreenshot:
        // Only reads the server's own screenshot buffer, which is thread safe
        return false;
    default:
        return true;
    }
}

void RPCServer::DispatchRequest(std::unique_ptr<Packet> request_packet) {
    bool success = false;
    const auto packet_data = request_packet->GetPacketData();

    // Currently, all request types use two arguments
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
        if (arg2 > 0 && arg2 <= MAX_PACKET_DATA_SIZE - (sizeof(u32) * 2) &&
            arg2 <= request_packet->GetPacketDataSize() - (sizeof(u32) * 2)) {
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
    case PacketType::TakeScreenshot:
        // Takes ownership of the packet and always replies
        HandleTakeScreenshot(std::move(request_packet), arg1, (ScreenshotFlags)arg2);
        return;
    case PacketType::ReadScreenshot:
        if (arg2 > 0 && arg2 <= MAX_READ_SIZE) {
            HandleReadScreenshot(*request_packet, arg1, arg2);
            success = true;
        }
        break;
    case PacketType::GetPerfStats:
        HandleGetPerfStats(*request_packet, (PerfStatsMode)arg1);
        success = true;
        break;
    default:
        break;
    }

    if (!success) {
        // Send an empty reply on failure
        request_packet->SetPacketDataSize(0);
        request_packet->SendReply();
    }
}

void RPCServer::HandleSingleRequest(std::unique_ptr<Packet> request_packet) {
    if (!ValidatePacket(request_packet->GetHeader())) {
        // Send an empty reply on failure
        request_packet->SetPacketDataSize(0);
        request_packet->SendReply();
        return;
    }

    if (sync_with_core && IsCoreRequest(request_packet->GetPacketType())) {
        // Defer to the emulation thread (processed by ProcessCoreRequests)
        core_request_queue.Push(std::move(request_packet));
        core_request_event.Set();
        return;
    }

    DispatchRequest(std::move(request_packet));
}

void RPCServer::ProcessCoreRequests() {
    if constexpr (sync_with_core) {
        std::unique_ptr<Packet> request_packet;
        while (core_request_queue.Pop(request_packet)) {
            DispatchRequest(std::move(request_packet));
        }
    }
}

bool RPCServer::WaitForCoreRequests(std::chrono::steady_clock::time_point deadline) {
    return core_request_event.WaitUntil(deadline);
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
