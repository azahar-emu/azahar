// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <algorithm>
#include <deque>
#include <unordered_set>
#include <utility>
#include <vector>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include "citra_ios/Bridge/jit_warmup.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/core.h"
#if defined(__aarch64__) && defined(__APPLE__) && TARGET_OS_IPHONE
#include "core/arm/dynarmic/arm_dynarmic.h"
#define AZAHAR_JIT_WARMUP 1
#endif

namespace JitWarmup {

namespace {

using Clock = std::chrono::steady_clock;

constexpr u32 FILE_MAGIC = 0x504A574E; // "NWJP"
constexpr u32 FILE_VERSION = 1;
/// A profile never grows beyond this many blocks (8 bytes each).
constexpr std::size_t MAX_ENTRIES = 400'000;
/// The code cache of a core is 32 MiB (see ARM_Dynarmic::MakeJit). Compiling ahead of time stops
/// while less than this is left, so it never fills the cache: a full cache is thrown away as a
/// whole, which would cost far more than it saves.
constexpr std::size_t KEEP_FREE = 12 * 1024 * 1024;
/// Compiling ahead of time stops this long before the frame limiter's deadline, so the next frame
/// starts on time even when a block takes long.
constexpr auto DEADLINE_MARGIN = std::chrono::microseconds{2000};
/// Loads of code smaller than this (patched words, relocations) do not queue the profile again.
constexpr std::size_t MIN_REQUEUE_RANGE = 4096;

struct State {
    Core::System* system = nullptr;
    bool active = false;  ///< Between Start and Stop
    bool enabled = false; ///< Recording and compiling ahead of time
    std::string path;

    std::unordered_set<u64> known; ///< Everything in the file or recorded since
    std::vector<u64> fresh;        ///< Recorded since the last save
    /// The blocks of the profile file by guest address, for queueing a range of them again.
    std::vector<std::pair<u32, u64>> by_address;
    std::deque<u64> queue; ///< Blocks to compile ahead of time
    bool cache_low = false;
    bool precompiling = false;

    // Since the last log line.
    u64 compiled = 0;
    u64 compiled_ns = 0;
    u64 ahead = 0;
    u64 ahead_ns = 0;
    u64 requeued = 0;
    Clock::time_point last_log{};
    Clock::time_point last_save{};
};

State g_state;

u32 AddressOf(u64 descriptor) {
    return static_cast<u32>(descriptor);
}

bool IsSingleStep(u64 descriptor) {
    return ((descriptor >> 32) & 4) != 0;
}

void Load() {
    FileUtil::IOFile file(g_state.path, "rb");
    if (!file.IsOpen()) {
        return;
    }
    u32 header[2]{};
    if (file.ReadArray(header, 2) != 2 || header[0] != FILE_MAGIC || header[1] != FILE_VERSION) {
        LOG_WARNING(Frontend, "JIT warm-up: {} is not a profile of this version, starting over",
                    g_state.path);
        file.Close();
        FileUtil::Delete(g_state.path);
        return;
    }
    const std::size_t count =
        std::min<std::size_t>((file.GetSize() - sizeof(header)) / sizeof(u64), MAX_ENTRIES);
    std::vector<u64> entries(count);
    entries.resize(file.ReadArray(entries.data(), count));
    for (const u64 descriptor : entries) {
        if (!IsSingleStep(descriptor) && g_state.known.insert(descriptor).second) {
            g_state.queue.push_back(descriptor);
            g_state.by_address.emplace_back(AddressOf(descriptor), descriptor);
        }
    }
    std::sort(g_state.by_address.begin(), g_state.by_address.end());
}

void Save() {
    if (g_state.fresh.empty()) {
        return;
    }
    const bool exists = FileUtil::Exists(g_state.path);
    if (!exists) {
        FileUtil::CreateFullPath(g_state.path);
    }
    FileUtil::IOFile file(g_state.path, exists ? "ab" : "wb");
    if (!file.IsOpen()) {
        LOG_WARNING(Frontend, "JIT warm-up: could not write {}", g_state.path);
        return;
    }
    if (!exists) {
        const u32 header[2]{FILE_MAGIC, FILE_VERSION};
        file.WriteArray(header, 2);
    }
    file.WriteArray(g_state.fresh.data(), g_state.fresh.size());
    g_state.fresh.clear();
}

#ifdef AZAHAR_JIT_WARMUP
Core::ARM_Dynarmic* AppCore() {
    if (g_state.system == nullptr || g_state.system->GetNumCores() == 0) {
        return nullptr;
    }
    return dynamic_cast<Core::ARM_Dynarmic*>(&g_state.system->GetCore(0));
}

void IdleWork(Clock::time_point deadline) {
    if (!g_state.enabled || g_state.queue.empty() || g_state.cache_low) {
        return;
    }
    Core::ARM_Dynarmic* const core = AppCore();
    if (core == nullptr) {
        return;
    }
    const Clock::time_point stop = deadline - DEADLINE_MARGIN;
    g_state.precompiling = true;
    while (!g_state.queue.empty() && Clock::now() < stop) {
        const int result = core->Precompile(g_state.queue.front(), KEEP_FREE);
        if (result == 2) {
            break; // Executing, try again at the next frame
        }
        if (result == 1) {
            g_state.cache_low = true;
            LOG_INFO(Frontend,
                     "JIT warm-up: the code cache is getting full, {} blocks of earlier runs are "
                     "left to be compiled when the game needs them",
                     g_state.queue.size());
            g_state.queue.clear();
            break;
        }
        g_state.queue.pop_front();
    }
    g_state.precompiling = false;
}

void CodeInvalidated(u32 start, std::size_t length) {
    if (!g_state.enabled || length < MIN_REQUEUE_RANGE || g_state.cache_low) {
        return;
    }
    // New code was loaded there (a module): its blocks from earlier runs are compiled again from
    // it. Blocks compiled from that range before were thrown away by the invalidation.
    const u64 end = static_cast<u64>(start) + length;
    auto it = std::lower_bound(g_state.by_address.begin(), g_state.by_address.end(),
                               std::pair<u32, u64>{start, 0});
    for (; it != g_state.by_address.end() && it->first < end; ++it) {
        g_state.queue.push_back(it->second);
        g_state.requeued++;
    }
}
#endif

} // Anonymous namespace

void Start(Core::System& system, const std::string& path, bool enabled) {
    g_state = State{};
    g_state.system = &system;
    g_state.active = true;
    g_state.path = path;
    g_state.last_log = g_state.last_save = Clock::now();
#ifdef AZAHAR_JIT_WARMUP
    g_state.enabled = enabled;
#else
    g_state.enabled = false;
#endif
    if (!g_state.enabled) {
        return;
    }
    Load();
#ifdef AZAHAR_JIT_WARMUP
    system.RegisterIdleWork(IdleWork);
    system.RegisterCodeInvalidated(CodeInvalidated);
#endif
    LOG_INFO(Frontend,
             "JIT warm-up: {} blocks of game code from earlier runs are compiled ahead of time "
             "while the game waits for the next frame",
             g_state.queue.size());
}

void Stop() {
    if (!g_state.active) {
        return;
    }
    if (g_state.system != nullptr) {
        g_state.system->RegisterIdleWork({});
        g_state.system->RegisterCodeInvalidated({});
    }
    if (g_state.enabled) {
        Save();
    }
    g_state = State{};
}

bool OnBlockCompiled(u64 descriptor, u64 nanoseconds) {
    if (g_state.precompiling) {
        g_state.ahead++;
        g_state.ahead_ns += nanoseconds;
        return true;
    }
    g_state.compiled++;
    g_state.compiled_ns += nanoseconds;
    // The blocks of the core the game runs on (core 0), the warm-up compiles for that one.
    if (g_state.enabled && !IsSingleStep(descriptor) && g_state.system != nullptr &&
        g_state.system->GetRunningCore().GetID() == 0 && g_state.known.size() < MAX_ENTRIES &&
        g_state.known.insert(descriptor).second) {
        // Not added to by_address: a block recorded now is compiled already, and stays so
        // unless its code is loaded again in this run.
        g_state.fresh.push_back(descriptor);
    }
    return false;
}

void Tick() {
    if (!g_state.active) {
        return;
    }
    const Clock::time_point now = Clock::now();
    if (g_state.enabled && now - g_state.last_save > std::chrono::seconds{30}) {
        g_state.last_save = now;
        Save();
    }
    if (now - g_state.last_log < std::chrono::seconds{10} ||
        (g_state.compiled == 0 && g_state.ahead == 0)) {
        return;
    }
    g_state.last_log = now;
    const auto average_us = [](u64 ns, u64 count) {
        return count > 0 ? static_cast<double>(ns) / static_cast<double>(count) / 1000.0 : 0.0;
    };
    LOG_INFO(Frontend,
             "JIT: {} blocks compiled while playing ({:.0f} us each, {:.1f} ms in all), {} ahead "
             "of time ({:.0f} us each); warm-up: {} waiting, {} queued again after code loads, "
             "{} blocks known",
             g_state.compiled, average_us(g_state.compiled_ns, g_state.compiled),
             static_cast<double>(g_state.compiled_ns) / 1e6, g_state.ahead,
             average_us(g_state.ahead_ns, g_state.ahead), g_state.queue.size(), g_state.requeued,
             g_state.known.size());
    g_state.compiled = g_state.compiled_ns = g_state.ahead = g_state.ahead_ns = 0;
    g_state.requeued = 0;
}

} // namespace JitWarmup
