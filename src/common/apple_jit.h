// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <cstddef>

/**
 * Executable memory for the CPU and shader JITs on iOS.
 *
 * iOS only lets a process generate code while a debugger has marked it CS_DEBUGGED. On devices
 * with the Trusted Execution Monitor (TXM, A15/M2 and newer on iOS 26, nearly everything on
 * iOS 27) that is not enough: pages the app maps itself never become executable, no matter what
 * mprotect reports. The debugger has to allocate the executable region, which the app then
 * writes through a second, writable mapping of the same memory.
 *
 * This follows the protocol of StikDebug's universal JIT script: `brk #0xf00d` with x16 = 1
 * asks the debugger for a region (x0 = address or 0, x1 = size, returns the address in x0), and
 * x16 = 0 detaches it. One arena is requested up front, then the debugger is detached and code
 * buffers are carved out of the arena.
 *
 * oaknut's CodeBlock is patched on iOS (externals/patches) to allocate through
 * oaknut_ios_jit_alloc, and dynarmic to emit through the writable alias.
 */
namespace Common::AppleJIT {

enum class Mode {
    /// The process can not generate code, use the interpreters.
    Unavailable,
    /// Pages are flipped between writable and executable with mprotect (devices without TXM).
    SingleMapping,
    /// Code is written through a writable alias of a debugger-provided executable arena.
    DualMapping,
};

/**
 * Sets up executable memory if a debugger enabled JIT for this process. Can be called repeatedly,
 * so JIT enabled after launch is still picked up; once memory is available the mode is fixed.
 * @param has_txm Whether the device has TXM and needs the debugger to map executable memory.
 */
Mode Initialize(bool has_txm);

/**
 * The route through the JIT helper (an app extension that acts as the debugger, see
 * src/citra_ios/JITHelper): the app reserves an arena, the helper attaches and writes one byte
 * to every page of it, which is what makes a TXM device accept the pages as executable, and
 * detaches. Initialize then uses the arena in dual mapping mode.
 *
 * ReserveHelperArena returns the executable address of the arena (and its size), nullptr when it
 * could not be reserved. MarkHelperArenaPrepared tells that the helper is done.
 */
void* ReserveHelperArena(std::size_t* size);
void MarkHelperArenaPrepared();

/// Returns the mode chosen by the last Initialize call.
Mode GetMode();

const char* GetModeName(Mode mode);

} // namespace Common::AppleJIT

extern "C" {
/// Allocates code memory. On success *wmem is the writable and *xmem the executable view, which
/// are the same pointer unless dual mapping is in use.
bool oaknut_ios_jit_alloc(std::size_t size, void** wmem, void** xmem);
void oaknut_ios_jit_free(void* wmem, void* xmem, std::size_t size);
}
