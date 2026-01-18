// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <condition_variable>
#include <mutex>
#include <thread>
#include "common/logging/log.h"
#include "core/frontend/emu_window.h"
#include "video_core/gpu.h"
#include "video_core/gpu_command_queue.h"
#include "video_core/gpu_impl.h"

namespace VideoCore {

GPUCommandQueue::GPUCommandQueue(GPU& gpu, std::unique_ptr<Frontend::GraphicsContext> context)
    : gpu{gpu}, graphics_context{std::move(context)} {
    worker_thread = std::make_unique<std::thread>([this] { ProcessCommandQueue(); });
}

GPUCommandQueue::~GPUCommandQueue() {
    Shutdown();
}

void GPUCommandQueue::QueueCommand(const Service::GSP::Command& command) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        command_queue.push(command);
        is_idle = false;
    }
    queue_cv.notify_one();
}

void GPUCommandQueue::WaitForIdle() {
    std::unique_lock<std::mutex> lock(queue_mutex);
    idle_cv.wait(lock, [this] { return is_idle; });
}

void GPUCommandQueue::SignalFlush() {
    // Non-blocking signal that GPU should flush pending work
    // Used at frame boundaries where we can't block the timing thread
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        // Just ensure the worker is awake to process any remaining commands
        // Don't wait for completion
    }
    queue_cv.notify_one();
}

void GPUCommandQueue::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        shutdown_requested = true;
    }
    queue_cv.notify_one();

    if (worker_thread && worker_thread->joinable()) {
        worker_thread->join();
    }
}

bool GPUCommandQueue::IsIdle() const {
    std::lock_guard<std::mutex> lock(queue_mutex);
    return is_idle;
}

void GPUCommandQueue::ProcessCommandQueue() {
    // Execute queued commands on a dedicated worker thread.
    // Rasterizer access is protected by a mutex to ensure thread safety.

    while (true) {
        Service::GSP::Command command;
        bool has_command = false;

        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            // Wait for commands or shutdown - no timeout, no artificial delays
            queue_cv.wait(lock, [this] { return !command_queue.empty() || shutdown_requested; });

            if (shutdown_requested && command_queue.empty()) {
                break;
            }

            if (!command_queue.empty()) {
                command = command_queue.front();
                command_queue.pop();
                has_command = true;
            }
        }

        // Process the command outside the queue lock but with rasterizer lock held
        if (has_command) {
            // Hold rasterizer mutex while executing to prevent races with main thread
            std::lock_guard<std::mutex> rasterizer_lock(gpu.impl->rasterizer_mutex);
            gpu.ExecuteCommand(command);

            // Check if queue is now idle after processing this command
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                if (command_queue.empty()) {
                    is_idle = true;
                    idle_cv.notify_all();
                }
            }
        }
    }
}

} // namespace VideoCore
