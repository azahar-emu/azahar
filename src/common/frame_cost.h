// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include "common/common_types.h"

/**
 * Where the emulation thread spends the time of a frame, for finding out what makes a frame slow
 * (the stutters while a game loads). Timers around the expensive steps add their time to the
 * frame being made; the renderer takes the totals at the end of every frame and logs them for a
 * frame that was slow. Only the time spent on the emulation thread counts (MarkEmulationThread).
 */
namespace Common::FrameCost {

enum class Kind : std::size_t {
    TextureUpload, ///< Decoding and uploading textures and surfaces from emulated memory
    ShaderBuild,   ///< Generating shader source and compiling shaders or pipelines in place
    FileRead,      ///< Reading files of the game (RomFS, saves)
    GpuWait,       ///< Waiting for the GPU (readbacks, a full queue)
    JitCompile,    ///< Compiling new game code into host code (dynarmic)
    Count,
};

inline std::array<std::atomic<u64>, static_cast<std::size_t>(Kind::Count)> g_totals{};
inline thread_local bool t_emulation_thread = false;

/// Called once by the emulation thread.
inline void MarkEmulationThread() {
    t_emulation_thread = true;
}

inline void Add(Kind kind, u64 nanoseconds) {
    g_totals[static_cast<std::size_t>(kind)].fetch_add(nanoseconds, std::memory_order_relaxed);
}

/// The totals since the last call, in nanoseconds, and starts over.
inline std::array<u64, static_cast<std::size_t>(Kind::Count)> Take() {
    std::array<u64, static_cast<std::size_t>(Kind::Count)> result{};
    for (std::size_t i = 0; i < result.size(); i++) {
        result[i] = g_totals[i].exchange(0, std::memory_order_relaxed);
    }
    return result;
}

/// Adds the time until the end of the scope, when on the emulation thread.
class Scope {
public:
    explicit Scope(Kind kind_) : kind{kind_}, active{t_emulation_thread} {
        if (active) {
            start = std::chrono::steady_clock::now();
        }
    }
    ~Scope() {
        if (active) {
            Add(kind, static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                           std::chrono::steady_clock::now() - start)
                                           .count()));
        }
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Kind kind;
    bool active;
    std::chrono::steady_clock::time_point start{};
};

} // namespace Common::FrameCost
