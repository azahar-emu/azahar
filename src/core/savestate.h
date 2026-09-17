// Copyright 2020-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <vector>
#include "common/common_types.h"

namespace Core {

struct SaveStateInfo {
    u32 slot;
    u64 time;
    enum class ValidationStatus {
        OK,
        RevisionMismatch,
        BuildMismatch,
    } status;
    std::string build_name;
    std::string build_version;
};

constexpr u32 SaveStateSlotCount = 11; // Maximum count of savestate slots

std::vector<SaveStateInfo> ListSaveStates(u64 program_id, u64 movie_id);

SaveStateInfo GetSaveStateInfo(u64 program_id, u64 movie_id, u32 slot);

constexpr u32 QuicksaveSlot = 0;                     // Reserved for quick save/load
constexpr u32 FirstUserSlot = 1;                     // First cursor-addressable slot
constexpr u32 LastUserSlot = SaveStateSlotCount - 1; // Last cursor-addressable slot

/// Returns the slot the save/load-current hotkeys act on. Never the quicksave slot.
u32 GetCurrentSlot();

/// Sets the current slot, clamped to [FirstUserSlot, LastUserSlot].
void SetCurrentSlot(u32 slot);

/// Moves the current slot by delta, wrapping within the user slot range.
/// Returns the new slot.
u32 AdvanceSlot(int delta);

/// Returns the user slot holding the most recent state, or FirstUserSlot when none does.
/// The quicksave slot, out-of-range slots and ValidationStatus are all ignored.
u32 PickInitialSlot(const std::vector<SaveStateInfo>& states);

/// Sets the current slot from the states that exist on disk for this title.
void InitCurrentSlot(u64 program_id, u64 movie_id);

} // namespace Core
