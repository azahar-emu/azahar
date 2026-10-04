// Copyright 2018-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <span>
#include <vector>
#include "common/polyfill_thread.h"
#include "common/thread.h"
#include "common/threadsafe_queue.h"
#include "core/rpc/packet.h"

namespace Core {
class System;
}

namespace Core::RPC {

class Packet;
struct PacketHeader;
enum class PacketType : u32;

/**
 * Handles RPC requests on a dedicated thread.
 *
 * When built with ENABLE_SCRIPTING_SYNC, every request that touches the emulated system is
 * forwarded to the emulation thread, which runs it from ProcessCoreRequests() at a point
 * where neither the CPU nor the renderer is running. Only ReadScreenshot, which just reads the
 * server's own screenshot buffer stays on the RPC thread.
 */
class RPCServer {
public:
    explicit RPCServer(Core::System& system);
    ~RPCServer();

    void QueueRequest(std::unique_ptr<RPC::Packet> request);

    /// Handles the requests that must run on the emulation thread. Must be called from the
    /// emulation thread. Does nothing unless built with ENABLE_SCRIPTING_SYNC.
    void ProcessCoreRequests();

    /// Blocks until a request for the emulation thread is queued (returns true) or the deadline
    /// passes (returns false).
    bool WaitForCoreRequests(std::chrono::steady_clock::time_point deadline);

    /// Stops the request handler and screenshot threads. No replies are sent after this returns.
    void Stop();

private:
    void HandleReadMemory(Packet& packet, u32 address, u32 data_size);
    void HandleWriteMemory(Packet& packet, u32 address, std::span<const u8> data);
    void HandleProcessList(Packet& packet, u32 start_index, u32 max_amount);
    void HandleSetGetProcess(Packet& packet, u32 operation, u32 process_id);
    void HandleTakeScreenshot(std::unique_ptr<Packet> packet, u32 res_scale, ScreenshotFlags flags);
    void HandleReadScreenshot(Packet& packet, u32 offset, u32 data_size);
    void HandleGetPerfStats(Packet& packet, PerfStatsMode mode);
    bool ValidatePacket(const PacketHeader& packet_header);
    static bool IsCoreRequest(PacketType packet_type);
    void HandleSingleRequest(std::unique_ptr<Packet> request);
    void DispatchRequest(std::unique_ptr<Packet> request);
    void HandleRequestsLoop(std::stop_token stop_token);

private:
    Core::System& system;
    u32 selected_pid = 0xFFFFFFFF;
    Common::SPSCQueue<std::unique_ptr<Packet>, true> request_queue;
    /// Requests forwarded from the RPC thread to the emulation thread (sync mode only)
    Common::SPSCQueue<std::unique_ptr<Packet>> core_request_queue;
    /// Signaled when a request is pushed to core_request_queue
    Common::Event core_request_event;

    std::mutex screenshot_mutex;
    /// Set while a screenshot is being captured or encoded
    bool screenshot_in_progress = false;
    /// Image data of the last screenshot taken, to be read in chunks by the client
    std::vector<u8> screenshot_data;

    // Declared last so they are joined before the state they use is destroyed
    std::jthread screenshot_thread;
    std::jthread request_handler_thread;
};

} // namespace Core::RPC
