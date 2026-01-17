// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <queue>

#include "common/common_types.h"
#include "core/hle/service/gsp/gsp_gpu.h"

namespace VideoCore {

class GPU;

/**
 * GPU Command Queue for asynchronous GPU command processing.
 * Processes GPU commands on a dedicated worker thread, similar to real 3DS hardware.
 *
 * Design principles:
 * - No artificial delays or busy-waiting
 * - Worker thread sleeps when queue is empty (OS scheduler handles CPU allocation)
 * - Logic thread not blocked when rendering
 * - Efficient synchronization with condition variables
 */
class GPUCommandQueue {
public:
    explicit GPUCommandQueue(GPU& gpu);
    ~GPUCommandQueue();

    /// Queue a GPU command for processing
    void QueueCommand(const Service::GSP::Command& command);

    /// Wait for all queued commands to be processed
    void WaitForIdle();

    /// Shutdown the command queue and worker thread
    void Shutdown();

    /// Check if the queue is idle
    [[nodiscard]] bool IsIdle() const;

private:
    /// Worker thread function - processes commands without artificial delays
    void ProcessCommandQueue();

    GPU& gpu;
    std::queue<Service::GSP::Command> command_queue;
    mutable std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::condition_variable idle_cv;
    std::unique_ptr<std::thread> worker_thread;
    bool shutdown_requested{false};
    bool is_idle{true};
};

} // namespace VideoCore
