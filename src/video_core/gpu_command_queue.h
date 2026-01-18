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

namespace Frontend {
class GraphicsContext;
}

namespace VideoCore {

class GPU;

/**
 * GPU Command Queue for asynchronous GPU command processing.
 * Processes GPU commands on a dedicated worker thread with shared OpenGL context.
 *
 * Design principles:
 * - Main thread queues GPU commands without blocking
 * - Worker thread executes with shared GL context (context sharing via frontend)
 * - Rasterizer cache protected by mutex for thread safety
 * - Game logic runs parallel to GPU work, enabling dynamic FPS
 *
 * Why this works:
 * - OpenGL supports context sharing: worker thread gets shared context
 * - GPU objects (shaders, textures) are shared across contexts
 * - Rasterizer mutex prevents cache races
 * - Game logic only waits when explicitly reading GPU results
 */
class GPUCommandQueue {
public:
    explicit GPUCommandQueue(GPU& gpu, std::unique_ptr<Frontend::GraphicsContext> context);
    ~GPUCommandQueue();

    /// Queue a GPU command for processing
    void QueueCommand(const Service::GSP::Command& command);

    /// Wait for all queued commands to be processed (BLOCKING - use sparingly)
    void WaitForIdle();

    /// Non-blocking flush: Signals GPU to complete pending work but doesn't wait
    /// Used for frame boundaries where we can't block the timing thread
    void SignalFlush();

    /// Shutdown the command queue and worker thread
    void Shutdown();

    /// Check if the queue is idle (non-blocking check)
    [[nodiscard]] bool IsIdle() const;

private:
    /// Worker thread function - processes commands without artificial delays
    void ProcessCommandQueue();

    GPU& gpu;
    std::unique_ptr<Frontend::GraphicsContext> graphics_context;
    std::queue<Service::GSP::Command> command_queue;
    mutable std::mutex queue_mutex;
    std::condition_variable queue_cv;
    std::condition_variable idle_cv;
    std::unique_ptr<std::thread> worker_thread;
    bool shutdown_requested{false};
    bool is_idle{true};
};

} // namespace VideoCore
