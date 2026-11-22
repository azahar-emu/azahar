// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <cerrno>
#include <cstdint>
#include <iterator>
#include <map>
#include <mutex>
#include <mach/mach.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include "common/apple_jit.h"
#include "common/logging/log.h"

extern "C" int csops(pid_t pid, unsigned int ops, void* useraddr, size_t usersize);

#if defined(__aarch64__)
extern "C" {
// Debugger "syscalls" of StikDebug's universal JIT script. The script handles the brk, advances
// the PC past it and places the result in x0.
__attribute__((noinline, naked)) void* JIT26PrepareRegion(void* /*addr*/, size_t /*len*/) {
    asm("mov x16, #1 \n"
        "brk #0xf00d \n"
        "ret");
}

__attribute__((noinline, naked)) void JIT26Detach(void) {
    asm("mov x16, #0 \n"
        "brk #0xf00d \n"
        "ret");
}
}
#endif

namespace Common::AppleJIT {

namespace {

constexpr unsigned int CS_OPS_STATUS = 0;
constexpr std::uint32_t CS_DEBUGGED = 0x10000000;

/// Size of the executable arena requested from the debugger. Every dynarmic instance (one per
/// emulated core and process) takes its code cache from here, as do the shader JIT programs.
constexpr std::size_t ArenaSize = 512 * 1024 * 1024;

/// Size of the arena of the JIT helper route. The helper writes one byte per 16 KiB page of it,
/// so a smaller arena is ready sooner (256 MiB took about 2.5 s; it ran out for a game that
/// asks for several 32 MiB blocks, so it is the size of the debugger route now).
constexpr std::size_t HelperArenaSize = 512 * 1024 * 1024;

bool IsDebugged() {
    std::uint32_t flags = 0;
    return csops(getpid(), CS_OPS_STATUS, &flags, sizeof(flags)) == 0 && (flags & CS_DEBUGGED);
}

/// Returns true while a debugger is attached. The brk based protocol must only be used then,
/// without a debugger the brk raises SIGTRAP and kills the app.
bool IsTraced() {
    kinfo_proc info{};
    std::size_t size = sizeof(info);
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0) {
        return false;
    }
    return (info.kp_proc.p_flag & P_TRACED) != 0;
}

std::size_t RoundUp(std::size_t size) {
    const std::size_t page = vm_page_size;
    return (size + page - 1) & ~(page - 1);
}

class State {
public:
    Mode Initialize(bool has_txm) {
        std::scoped_lock lock{mutex};
        if (mode != Mode::Unavailable || gave_up) {
            return mode;
        }

        if (!IsDebugged()) {
            if (!reported_not_debugged) {
                reported_not_debugged = true;
                LOG_INFO(Common, "JIT: the process is not debugged (CS_DEBUGGED is not set)");
            }
            return mode;
        }

        if (!has_txm) {
            LOG_INFO(Common, "JIT: device without TXM, using mprotect for code memory");
            mode = Mode::SingleMapping;
            return mode;
        }

        if (helper_rx != nullptr && helper_prepared) {
            // The JIT helper (an app extension acting as the debugger) prepared an arena that
            // was reserved with ReserveHelperArena, and detached again.
            arena_size = HelperArenaSize;
            rx_base = static_cast<std::uint8_t*>(helper_rx);
            rw_base = static_cast<std::uint8_t*>(helper_rw);
            free_blocks.emplace(0, arena_size);
            mode = Mode::DualMapping;
            LOG_INFO(Common, "JIT: executable arena prepared by the helper at {}, writable alias "
                             "at {}", fmt::ptr(rx_base), fmt::ptr(rw_base));
            return mode;
        }

        if (!IsTraced()) {
            if (!reported_not_traced) {
                reported_not_traced = true;
                LOG_WARNING(Common, "JIT: this device has TXM and the debugger already detached. "
                                    "Launch with StikDebug using the universal JIT script, the "
                                    "interpreters will be used instead");
            }
            return mode;
        }

#if defined(__aarch64__)
        // Whatever happens next, the debugger is detached afterwards and can not be asked again.
        gave_up = true;

        LOG_INFO(Common, "JIT: requesting a {} MiB executable region from the debugger",
                 ArenaSize / (1024 * 1024));
        arena_size = ArenaSize;
        void* const rx = JIT26PrepareRegion(nullptr, ArenaSize);
        if (rx == nullptr) {
            LOG_ERROR(Common, "JIT: the debugger did not provide executable memory, make sure "
                              "the universal JIT script is used");
            JIT26Detach();
            return mode;
        }

        vm_address_t rw = 0;
        vm_prot_t cur_prot = VM_PROT_NONE;
        vm_prot_t max_prot = VM_PROT_NONE;
        kern_return_t kr = vm_remap(mach_task_self(), &rw, ArenaSize, 0, VM_FLAGS_ANYWHERE,
                                    mach_task_self(), reinterpret_cast<vm_address_t>(rx), FALSE,
                                    &cur_prot, &max_prot, VM_INHERIT_NONE);
        if (kr != KERN_SUCCESS) {
            LOG_ERROR(Common, "JIT: vm_remap of the executable region failed ({})", kr);
            JIT26Detach();
            return mode;
        }
        kr = vm_protect(mach_task_self(), rw, ArenaSize, FALSE, VM_PROT_READ | VM_PROT_WRITE);
        if (kr != KERN_SUCCESS) {
            LOG_ERROR(Common, "JIT: making the writable alias writable failed ({})", kr);
            vm_deallocate(mach_task_self(), rw, ArenaSize);
            JIT26Detach();
            return mode;
        }

        rx_base = static_cast<std::uint8_t*>(rx);
        rw_base = reinterpret_cast<std::uint8_t*>(rw);
        free_blocks.emplace(0, ArenaSize);
        mode = Mode::DualMapping;
        LOG_INFO(Common, "JIT: executable arena at {}, writable alias at {}", fmt::ptr(rx_base),
                 fmt::ptr(rw_base));

        LOG_INFO(Common, "JIT: detaching the debugger");
        JIT26Detach();
#endif
        return mode;
    }

    Mode GetMode() {
        std::scoped_lock lock{mutex};
        return mode;
    }

    /// Reserves the arena of the helper route: executable memory (it never becomes executable
    /// by itself on TXM devices, the debugger has to write every page) and the writable alias of
    /// the same memory. Returns the executable address, nullptr when it could not be reserved.
    void* ReserveHelperArena(std::size_t* size) {
        std::scoped_lock lock{mutex};
        *size = HelperArenaSize;
        if (helper_rx != nullptr) {
            return helper_rx;
        }
        // Created with execute: a block without it could never get execute added later.
        void* const rx = mmap(nullptr, HelperArenaSize, PROT_READ | PROT_EXEC,
                              MAP_ANON | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
        if (rx == MAP_FAILED) {
            LOG_ERROR(Common, "JIT: reserving executable memory failed (errno {})", errno);
            return nullptr;
        }
        vm_address_t rw = 0;
        vm_prot_t cur_prot = VM_PROT_NONE;
        vm_prot_t max_prot = VM_PROT_NONE;
        kern_return_t kr = vm_remap(mach_task_self(), &rw, HelperArenaSize, 0, VM_FLAGS_ANYWHERE,
                                    mach_task_self(), reinterpret_cast<vm_address_t>(rx), FALSE,
                                    &cur_prot, &max_prot, VM_INHERIT_NONE);
        if (kr != KERN_SUCCESS) {
            LOG_ERROR(Common, "JIT: vm_remap of the reserved memory failed ({})", kr);
            munmap(rx, HelperArenaSize);
            return nullptr;
        }
        kr = vm_protect(mach_task_self(), rw, HelperArenaSize, FALSE,
                        VM_PROT_READ | VM_PROT_WRITE);
        if (kr != KERN_SUCCESS) {
            LOG_ERROR(Common, "JIT: making the writable alias writable failed ({})", kr);
            vm_deallocate(mach_task_self(), rw, HelperArenaSize);
            munmap(rx, HelperArenaSize);
            return nullptr;
        }
        helper_rx = rx;
        helper_rw = reinterpret_cast<void*>(rw);
        LOG_INFO(Common, "JIT: reserved a {} MiB arena for the helper at {}, alias at {}",
                 HelperArenaSize / (1024 * 1024), fmt::ptr(helper_rx), fmt::ptr(helper_rw));
        return helper_rx;
    }

    void MarkHelperArenaPrepared() {
        std::scoped_lock lock{mutex};
        helper_prepared = helper_rx != nullptr;
    }

    bool Allocate(std::size_t size, void** wmem, void** xmem) {
        std::scoped_lock lock{mutex};
        if (mode == Mode::Unavailable) {
            // Memory mapped by the app itself never becomes executable without a debugger, fail
            // here instead of crashing later when the generated code is run.
            LOG_ERROR(Common, "JIT: code memory requested while JIT is not available");
            return false;
        }
        if (mode == Mode::SingleMapping) {
            // Same allocation as upstream oaknut, the code block flips it to RW while writing.
            void* const mem =
                mmap(nullptr, size, PROT_READ | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
            if (mem == MAP_FAILED) {
                return false;
            }
            *wmem = mem;
            *xmem = mem;
            return true;
        }

        size = RoundUp(size);
        for (auto it = free_blocks.begin(); it != free_blocks.end(); ++it) {
            const auto [offset, block_size] = *it;
            if (block_size < size) {
                continue;
            }
            free_blocks.erase(it);
            if (block_size > size) {
                free_blocks.emplace(offset + size, block_size - size);
            }
            *wmem = rw_base + offset;
            *xmem = rx_base + offset;
            return true;
        }
        LOG_CRITICAL(Common, "JIT: the executable arena is exhausted ({} bytes requested)", size);
        return false;
    }

    void Free(void* wmem, void* xmem, std::size_t size) {
        std::scoped_lock lock{mutex};
        auto* const x = static_cast<std::uint8_t*>(xmem);
        if (mode != Mode::DualMapping || x < rx_base || x >= rx_base + arena_size) {
            munmap(wmem, size);
            return;
        }

        std::size_t offset = static_cast<std::size_t>(x - rx_base);
        size = RoundUp(size);
        // Merge with the neighbouring free blocks.
        auto next = free_blocks.lower_bound(offset);
        if (next != free_blocks.end() && offset + size == next->first) {
            size += next->second;
            next = free_blocks.erase(next);
        }
        if (next != free_blocks.begin()) {
            auto prev = std::prev(next);
            if (prev->first + prev->second == offset) {
                offset = prev->first;
                size += prev->second;
                free_blocks.erase(prev);
            }
        }
        free_blocks.emplace(offset, size);
    }

private:
    std::mutex mutex;
    Mode mode = Mode::Unavailable;
    bool gave_up = false;
    bool reported_not_debugged = false;
    bool reported_not_traced = false;
    std::uint8_t* rx_base = nullptr;
    std::uint8_t* rw_base = nullptr;
    std::size_t arena_size = ArenaSize;
    void* helper_rx = nullptr;
    void* helper_rw = nullptr;
    bool helper_prepared = false;
    /// Free ranges of the arena, offset -> size.
    std::map<std::size_t, std::size_t> free_blocks;
};

State& GetState() {
    static State state;
    return state;
}

} // Anonymous namespace

Mode Initialize(bool has_txm) {
    return GetState().Initialize(has_txm);
}

Mode GetMode() {
    return GetState().GetMode();
}

void* ReserveHelperArena(std::size_t* size) {
    return GetState().ReserveHelperArena(size);
}

void MarkHelperArenaPrepared() {
    GetState().MarkHelperArenaPrepared();
}

const char* GetModeName(Mode mode) {
    switch (mode) {
    case Mode::Unavailable:
        return "unavailable";
    case Mode::SingleMapping:
        return "single mapping";
    case Mode::DualMapping:
        return "dual mapping (TXM)";
    }
    return "unknown";
}

} // namespace Common::AppleJIT

extern "C" bool oaknut_ios_jit_alloc(std::size_t size, void** wmem, void** xmem) {
    return Common::AppleJIT::GetState().Allocate(size, wmem, xmem);
}

extern "C" void oaknut_ios_jit_free(void* wmem, void* xmem, std::size_t size) {
    Common::AppleJIT::GetState().Free(wmem, xmem, size);
}
