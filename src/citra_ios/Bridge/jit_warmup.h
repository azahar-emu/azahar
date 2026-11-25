// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace Core {
class System;
}

/**
 * JIT warm-up. The CPU recompiler (dynarmic) compiles each block of game code the first time it
 * runs, on the emulation thread, which shows as a stutter when a game reaches a lot of new code at
 * once (a new scene). Every run of a game writes down which blocks were compiled (their location
 * descriptors, in the order they were first needed) to a file next to the shader cache. The next
 * run compiles those blocks ahead of time while the emulation thread waits for the next frame (the
 * frame limiter), a few at a time, so the game finds them ready.
 *
 * The blocks are compiled from the game's memory as it is then, so a block of code that is not
 * loaded yet (a module loaded later) is compiled again once it is: loading code invalidates the
 * range (System::InvalidateCacheRange), and the profile's blocks in a range that was loaded are
 * queued again. Code seen for the first time is compiled as before.
 *
 * Everything runs on the emulation thread.
 */
namespace JitWarmup {

/// Starts for a game: loads its profile from `path` and registers the idle work. With `enabled`
/// false it only reports the compile times (for the slow frame log), nothing is recorded or
/// compiled ahead of time.
void Start(Core::System& system, const std::string& path, bool enabled);

/// Saves what was recorded and unregisters. Called before the system shuts down.
void Stop();

/// A block was compiled (the dynarmic hook). True when it was compiled ahead of time by the warm-up
/// itself, whose time does not belong to the frame.
bool OnBlockCompiled(std::uint64_t descriptor, std::uint64_t nanoseconds);

/// Logs what happened and saves new entries now and then. Called about once a second.
void Tick();

} // namespace JitWarmup
