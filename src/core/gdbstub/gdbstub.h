// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

// Originally written by Sven Peter <sven@fail0verflow.com> for anergistic.

#pragma once

#include <span>
#include "common/common_types.h"
#include "core/hle/kernel/thread.h"

#ifndef ENABLE_GDBSTUB
#error "File was included with GDB stub support disabled"
#endif

namespace Core {
class System;
}

namespace GDBStub {

/// Breakpoint Method
enum class BreakpointType {
    None,    ///< None
    Execute, ///< Execution Breakpoint
    Read,    ///< Read Breakpoint
    Write,   ///< Write Breakpoint
    Access   ///< Access (R/W) Breakpoint
};

struct BreakpointAddress {
    VAddr address;
    BreakpointType type;
};

/**
 * Set the port the gdbstub should use to listen for connections.
 *
 * @param port Port to listen for connection
 */
void SetServerPort(u16 port);

/**
 * Starts or stops the server if possible.
 *
 * @param status Set the server to enabled or disabled.
 */
void ToggleServer(bool status);

/// Start the gdbstub server.
void Init();

/**
 * Defer initialization of the gdbstub to the first packet processing functions.
 * This avoids a case where the gdbstub thread is frozen after initialization
 * and fails to respond in time to packets.
 */
void DeferStart();

/// Stop gdbstub server.
void Shutdown();

/// Checks if the gdbstub server is enabled.
bool IsServerEnabled();

/// Returns true if the GDB server is initialized
bool IsInitialized();

/// Returns true if there is an active socket connection.
bool IsConnected();

/**
 * Signal to the gdbstub server that it should halt CPU execution.
 *
 * @param signal Signal that produced the break (SIGTRAP by default)
 */
void Break(int signal);

/**
 * Signal to the GDB stub that the specified process ID is exiting
 */
void OnProcessExit(u32 process_id);

/**
 * Signal to the GDB stub that the specified thread ID is exiting
 */
void OnThreadExit(u32 thread_id);

/// Read and handle packet from gdb client.
void HandlePacket(Core::System& system);

/**
 * Get the nearest breakpoint of the specified type at the given address.
 *
 * @param addr Address to search from.
 * @param type Type of breakpoint.
 */
BreakpointAddress GetNextBreakpointFromAddress(VAddr addr, GDBStub::BreakpointType type);

/**
 * Check if a breakpoint of the specified type exists at the given address.
 *
 * @param addr Address of breakpoint.
 * @param access_len Access size in bytes.
 * @param type Type of breakpoint.
 */
bool CheckBreakpoint(VAddr addr, u32 access_len, BreakpointType type);

/**
 * Send reply to gdb client.
 *
 * @param reply Reply to be sent to client.
 */
void SendReply(const char* reply);

/**
 * Converts input hex string characters into an array of equivalent of u8 bytes.
 *
 * @param src Pointer to array of output hex string characters.
 * @param len Length of src array.
 */
u32 HexToInt(const u8* src, std::size_t len);
} // namespace GDBStub
