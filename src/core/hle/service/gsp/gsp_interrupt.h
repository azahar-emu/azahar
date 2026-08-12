// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include "common/bit_field.h"
#include "common/common_types.h"

namespace Service::GSP {

/// GSP interrupt ID
enum class InterruptId : u8 {
    PSC0 = 0x00,
    PSC1 = 0x01,
    PDC0 = 0x02,
    PDC1 = 0x03,
    PPF = 0x04,
    P3D = 0x05,
    DMA = 0x06,

    COUNT,
};

/// GSP thread interrupt relay queue
struct InterruptRelayQueue {
    static constexpr size_t max_slots = 0x34;
    static constexpr size_t stop_queuing_pdc_threeshold = 0x20;
    static constexpr u8 queue_full_error = 0x1;

    // Index of last interrupt in the queue
    u8 index;
    // Number of interrupts remaining to be processed by the userland code
    u8 number_interrupts;
    // Error code - zero on success, otherwise an error has occurred
    u8 error_code;

    union {
        u8 config;
        BitField<0, 1, u8> ignore_pdc;
    };

    u32 missed_PDC0;
    u32 missed_PDC1;

    InterruptId slot[max_slots]; ///< Interrupt ID slots
};
static_assert(sizeof(InterruptRelayQueue) == 0x40, "InterruptRelayQueue struct has incorrect size");

using InterruptHandler = std::function<void(InterruptId, u64)>;

} // namespace Service::GSP
