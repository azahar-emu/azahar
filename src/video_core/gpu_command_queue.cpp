// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <condition_variable>
#include <mutex>
#include <thread>
#include "common/logging/log.h"
#include "video_core/gpu.h"
#include "video_core/gpu_command_queue.h"

namespace VideoCore {

GPUCommandQueue::GPUCommandQueue(GPU& gpu) : gpu{gpu} {
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

        // Process the command outside the lock - no artificial delays
        if (has_command) {
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
