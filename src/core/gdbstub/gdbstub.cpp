// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

// Originally written by Sven Peter <sven@fail0verflow.com> for anergistic.

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <numeric>
#include <fcntl.h>
#include <fmt/format.h>

#ifdef _WIN32
#include <winsock2.h>
// winsock2.h needs to be included first to prevent winsock.h being included by other includes
#include <io.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#define SHUT_RDWR 2
#else
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#include "common/logging/log.h"
#include "core/arm/arm_interface.h"
#include "core/core.h"
#include "core/gdbstub/gdbstub.h"
#include "core/gdbstub/hio.h"
#include "core/hle/kernel/process.h"
#include "core/loader/loader.h"
#include "core/memory.h"

#pragma optimize("", off)

// Uncomment to log all GDB traffic
#define PRINT_GDB_TRAFFIC

namespace GDBStub {
namespace {
constexpr int GDB_BUFFER_SIZE = 10000;

constexpr char GDB_STUB_START = '$';
constexpr char GDB_STUB_END = '#';
constexpr char GDB_STUB_ACK = '+';
constexpr char GDB_STUB_NACK = '-';

#ifndef SIGTRAP
constexpr u32 SIGTRAP = 5;
#endif

#ifndef SIGTERM
constexpr u32 SIGTERM = 15;
#endif

#ifndef MSG_WAITALL
constexpr u32 MSG_WAITALL = 8;
#endif

#ifndef SIGSEGV
constexpr u32 SIGSEGV = 11;
#endif

constexpr u32 SP_REGISTER = 13;
constexpr u32 LR_REGISTER = 14;
constexpr u32 PC_REGISTER = 15;
constexpr u32 CPSR_REGISTER = 25;
constexpr u32 D0_REGISTER = 26;
constexpr u32 FPSCR_REGISTER = 42;

// For sample XML files see the GDB source /gdb/features
// GDB also wants the l character at the start
// This XML defines what the registers are for this specific ARM device
constexpr char target_xml[] =
    R"(l<?xml version="1.0"?>
<!DOCTYPE target SYSTEM "gdb-target.dtd">
<target version="1.0">
  <architecture>arm</architecture>
  <osabi>3DS</osabi>
  <feature name="org.gnu.gdb.arm.core">
    <reg name="r0" bitsize="32"/>
    <reg name="r1" bitsize="32"/>
    <reg name="r2" bitsize="32"/>
    <reg name="r3" bitsize="32"/>
    <reg name="r4" bitsize="32"/>
    <reg name="r5" bitsize="32"/>
    <reg name="r6" bitsize="32"/>
    <reg name="r7" bitsize="32"/>
    <reg name="r8" bitsize="32"/>
    <reg name="r9" bitsize="32"/>
    <reg name="r10" bitsize="32"/>
    <reg name="r11" bitsize="32"/>
    <reg name="r12" bitsize="32"/>
    <reg name="sp" bitsize="32" type="data_ptr"/>
    <reg name="lr" bitsize="32"/>
    <reg name="pc" bitsize="32" type="code_ptr"/>

    <!-- The CPSR is register 25, rather than register 16, because
         the FPA registers historically were placed between the PC
         and the CPSR in the "g" packet.  -->

    <reg name="cpsr" bitsize="32" regnum="25"/>
  </feature>
  <feature name="org.gnu.gdb.arm.vfp">
    <reg name="d0" bitsize="64" type="float"/>
    <reg name="d1" bitsize="64" type="float"/>
    <reg name="d2" bitsize="64" type="float"/>
    <reg name="d3" bitsize="64" type="float"/>
    <reg name="d4" bitsize="64" type="float"/>
    <reg name="d5" bitsize="64" type="float"/>
    <reg name="d6" bitsize="64" type="float"/>
    <reg name="d7" bitsize="64" type="float"/>
    <reg name="d8" bitsize="64" type="float"/>
    <reg name="d9" bitsize="64" type="float"/>
    <reg name="d10" bitsize="64" type="float"/>
    <reg name="d11" bitsize="64" type="float"/>
    <reg name="d12" bitsize="64" type="float"/>
    <reg name="d13" bitsize="64" type="float"/>
    <reg name="d14" bitsize="64" type="float"/>
    <reg name="d15" bitsize="64" type="float"/>
    <reg name="fpscr" bitsize="32" type="int" group="float"/>
  </feature>
</target>
)";

int gdbserver_socket = -1;
bool defer_start = false;

u8 command_buffer[GDB_BUFFER_SIZE];
u32 command_length;

u32 latest_signal = 0;

static Kernel::Thread* current_thread = nullptr;
static Kernel::Process* current_process = nullptr;

// Binding to a port within the reserved ports range (0-1023) requires root permissions,
// so default to a port outside of that range.
u16 gdbstub_port = 24689;

constexpr bool supports_extended_mode = true;
bool is_extended_mode = false;

// If set to false, the server will never be started and no
// gdbstub-related functions will be executed.
std::atomic<bool> server_enabled(false);
int accept_socket = -1;
int continue_thread = -1;

static Kernel::Thread* break_thread = nullptr;
static int break_signal = 0;

#ifdef _WIN32
WSADATA InitData;
#endif

struct Breakpoint {
    bool active;
    VAddr addr;
    u32 len;
    std::array<u8, 4> inst;
};

using BreakpointMap = std::map<VAddr, Breakpoint>;
BreakpointMap breakpoints_execute;
BreakpointMap breakpoints_read;
BreakpointMap breakpoints_write;
} // Anonymous namespace

static Kernel::Thread* FindThreadById(int id) {
    if (!current_process) {
        return nullptr;
    }

    auto thread_list = current_process->GetThreadList();
    for (auto& thread : thread_list) {
        if (thread->GetThreadId() == static_cast<u32>(id)) {
            return thread.get();
        }
    }

    return nullptr;
}

static u32 RegRead(std::size_t id, Kernel::Thread* thread = nullptr) {
    if (!thread) {
        return 0;
    }

    if (id <= PC_REGISTER) {
        return thread->context.cpu_registers[id];
    } else if (id == CPSR_REGISTER) {
        return thread->context.cpsr;
    } else {
        return 0;
    }
}

static void RegWrite(std::size_t id, u32 val, Kernel::Thread* thread = nullptr) {
    if (!thread) {
        return;
    }

    if (id <= PC_REGISTER) {
        thread->context.cpu_registers[id] = val;
    } else if (id == CPSR_REGISTER) {
        thread->context.cpsr = val;
    }
}

static u64 FpuRead(std::size_t id, Kernel::Thread* thread = nullptr) {
    if (!thread) {
        return 0;
    }

    if (id >= D0_REGISTER && id < FPSCR_REGISTER) {
        u64 ret = thread->context.fpu_registers[2 * (id - D0_REGISTER)];
        ret |= static_cast<u64>(thread->context.fpu_registers[2 * (id - D0_REGISTER) + 1]) << 32;
        return ret;
    } else if (id == FPSCR_REGISTER) {
        return thread->context.fpscr;
    } else {
        return 0;
    }
}

static void FpuWrite(std::size_t id, u64 val, Kernel::Thread* thread = nullptr) {
    if (!thread) {
        return;
    }

    if (id >= D0_REGISTER && id < FPSCR_REGISTER) {
        thread->context.fpu_registers[2 * (id - D0_REGISTER)] = static_cast<u32>(val);
        thread->context.fpu_registers[2 * (id - D0_REGISTER) + 1] = static_cast<u32>(val >> 32);
    } else if (id == FPSCR_REGISTER) {
        thread->context.fpscr = static_cast<u32>(val);
    }
}

// Clear instruction cache for all cores.
static void ClearAllInstructionCache() {
    for (int i = 0; i < Core::GetNumCores(); i++) {
        Core::GetCore(i).ClearInstructionCache();
    }
}

/**
 * Turns hex string character into the equivalent byte.
 *
 * @param hex Input hex character to be turned into byte.
 */
static u8 HexCharToValue(u8 hex) {
    if (hex >= '0' && hex <= '9') {
        return hex - '0';
    } else if (hex >= 'a' && hex <= 'f') {
        return hex - 'a' + 0xA;
    } else if (hex >= 'A' && hex <= 'F') {
        return hex - 'A' + 0xA;
    }

    LOG_ERROR(Debug_GDBStub, "Invalid nibble: {:c} {:02x}\n", hex, hex);
    return 0;
}

/**
 * Turn nibble of byte into hex string character.
 *
 * @param n Nibble to be turned into hex character.
 */
static u8 NibbleToHex(u8 n) {
    n &= 0xF;
    if (n < 0xA) {
        return '0' + n;
    } else {
        return 'a' + n - 0xA;
    }
}

u32 HexToInt(const u8* src, std::size_t len) {
    u32 output = 0;
    while (len-- > 0) {
        output = (output << 4) | HexCharToValue(src[0]);
        src++;
    }
    return output;
}

/**
 * Converts input array of u8 bytes into their equivalent hex string characters.
 *
 * @param dest Pointer to buffer to store output hex string characters.
 * @param src Pointer to array of u8 bytes.
 * @param len Length of src array.
 */
static void MemToGdbHex(u8* dest, const u8* src, std::size_t len) {
    while (len-- > 0) {
        u8 tmp = *src++;
        *dest++ = NibbleToHex(tmp >> 4);
        *dest++ = NibbleToHex(tmp);
    }
}

/**
 * Converts input gdb-formatted hex string characters into an array of equivalent of u8 bytes.
 *
 * @param dest Pointer to buffer to store u8 bytes.
 * @param src Pointer to array of output hex string characters.
 * @param len Length of src array.
 */
static void GdbHexToMem(u8* dest, const u8* src, std::size_t len) {
    while (len-- > 0) {
        *dest++ = (HexCharToValue(src[0]) << 4) | HexCharToValue(src[1]);
        src += 2;
    }
}

/**
 * Convert a u32 into a gdb-formatted hex string.
 *
 * @param dest Pointer to buffer to store output hex string characters.
 * @param v    Value to convert.
 */
static void IntToGdbHex(u8* dest, u32 v) {
    for (int i = 0; i < 8; i += 2) {
        dest[i + 1] = NibbleToHex(v >> (4 * i));
        dest[i] = NibbleToHex(v >> (4 * (i + 1)));
    }
}

/**
 * Convert a gdb-formatted hex string into a u32.
 *
 * @param src Pointer to hex string.
 */
static u32 GdbHexToInt(const u8* src) {
    u32 output = 0;

    for (int i = 0; i < 8; i += 2) {
        output = (output << 4) | HexCharToValue(src[7 - i - 1]);
        output = (output << 4) | HexCharToValue(src[7 - i]);
    }

    return output;
}

/**
 * Convert a u64 into a gdb-formatted hex string.
 *
 * @param dest Pointer to buffer to store output hex string characters.
 * @param v    Value to convert.
 */
static void LongToGdbHex(u8* dest, u64 v) {
    for (int i = 0; i < 16; i += 2) {
        dest[i + 1] = NibbleToHex(static_cast<u8>(v >> (4 * i)));
        dest[i] = NibbleToHex(static_cast<u8>(v >> (4 * (i + 1))));
    }
}

/**
 * Convert a gdb-formatted hex string into a u64.
 *
 * @param src Pointer to hex string.
 */
static u64 GdbHexToLong(const u8* src) {
    u64 output = 0;

    for (int i = 0; i < 16; i += 2) {
        output = (output << 4) | HexCharToValue(src[15 - i - 1]);
        output = (output << 4) | HexCharToValue(src[15 - i]);
    }

    return output;
}

static int GetErrno() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static bool SetNonBlock(int socket, bool nonblock) {
#ifdef _WIN32
    unsigned long nonblocking = nonblock ? 1 : 0;
    int ret = ioctlsocket(socket, FIONBIO, &nonblocking);
    if (ret < 0) {
        LOG_ERROR(Debug_GDBStub, "Failed to set non-blocking gdb socket");
        return false;
    }
#else
    int flags = nonblock ? O_NONBLOCK : 0;

    const int ret = ::fcntl(socket, F_SETFL, flags);
    if (ret < 0) {
        LOG_ERROR(Debug_GDBStub, "Failed to set non-blocking gdb socket");
        return false;
    }
#endif
    return true;
}

/// Read a byte from the gdb client.
static u8 ReadByte() {
    u8 c;
    std::size_t received_size = recv(gdbserver_socket, reinterpret_cast<char*>(&c), 1, MSG_WAITALL);
    if (received_size != 1) {
        LOG_ERROR(Debug_GDBStub, "recv failed : {}", GetErrno());
        Shutdown();
    }

    return c;
}

/// Calculate the checksum of the current command buffer.
static u8 CalculateChecksum(const u8* buffer, std::size_t length) {
    return static_cast<u8>(std::accumulate(buffer, buffer + length, 0, std::plus<u8>()));
}

/**
 * Get the map of breakpoints for a given breakpoint type.
 *
 * @param type Type of breakpoint map.
 */
static BreakpointMap& GetBreakpointMap(BreakpointType type) {
    switch (type) {
    case BreakpointType::Execute:
        return breakpoints_execute;
    case BreakpointType::Read:
        return breakpoints_read;
    case BreakpointType::Write:
        return breakpoints_write;
    default:
        return breakpoints_read;
    }
}

/**
 * Remove the breakpoint from the given address of the specified type.
 *
 * @param type Type of breakpoint.
 * @param addr Address of breakpoint.
 */
static void RemoveBreakpoint(BreakpointType type, VAddr addr) {
    BreakpointMap& p = GetBreakpointMap(type);

    const auto bp = p.find(addr);
    if (bp == p.end()) {
        return;
    }

    LOG_DEBUG(Debug_GDBStub, "gdb: removed a breakpoint: {:08x} bytes at {:08x} of type {}",
              bp->second.len, bp->second.addr, type);

    if (type == BreakpointType::Execute) {
        Core::System::GetInstance().Memory().WriteBlock(*current_process, bp->second.addr,
                                                        bp->second.inst.data(), bp->second.len);
        u32 num_cores = Core::GetNumCores();
        for (u32 i = 0; i < num_cores; ++i) {
            Core::GetCore(i).ClearInstructionCache();
        }
    } else {
        Core::System::GetInstance().Memory().UnregisterWatchpoint(*current_process, bp->second.addr,
                                                                  bp->second.len);
    }
    p.erase(addr);
}

BreakpointAddress GetNextBreakpointFromAddress(VAddr addr, BreakpointType type) {
    const BreakpointMap& p = GetBreakpointMap(type);
    const auto next_breakpoint = p.lower_bound(addr);
    BreakpointAddress breakpoint;

    if (next_breakpoint != p.end()) {
        breakpoint.address = next_breakpoint->first;
        breakpoint.type = type;
    } else {
        breakpoint.address = 0;
        breakpoint.type = BreakpointType::None;
    }

    return breakpoint;
}

bool CheckBreakpoint(VAddr addr, u32 access_len, BreakpointType type) {
    if (!IsConnected()) {
        return false;
    }

    const BreakpointMap& p = GetBreakpointMap(type);

    // Access range: [addr, access_end)
    const VAddr access_end = addr + access_len;

    for (const auto& [base_addr, bp] : p) {
        if (!bp.active) {
            continue;
        }

        u32 bp_len = bp.len;

        // IDA Pro defaults to 4-byte breakpoints for all non-hardware breakpoints
        // no matter if it's a 4-byte or 2-byte instruction. When you execute a
        // Thumb instruction with a 4-byte breakpoint set, it will set a breakpoint on
        // two instructions instead of the single instruction you placed the breakpoint
        // on. So, as a way to make sure that execution breakpoints are only breaking
        // on the instruction that was specified, set the length of an execution
        // breakpoint to 1. This should be fine since the CPU should never begin executing
        // an instruction anywhere except the beginning of the instruction.
        if (type == BreakpointType::Execute) {
            bp_len = 1;
        }

        // Breakpoint/watchpoint range: [bp.addr, bp_end)
        const VAddr bp_end = bp.addr + bp_len;

        bool hit = false;

        if (type == BreakpointType::Execute) {
            // Execute breakpoints should only trigger on exact PC match.
            hit = (addr == bp.addr);
        } else {
            // Range overlap test:
            // [addr, access_end) overlaps [bp.addr, bp_end)
            hit = (addr < bp_end) && (bp.addr < access_end);
        }

        if (hit) {
            LOG_DEBUG(Debug_GDBStub,
                      "Found breakpoint type {}, "
                      "access range: {:08x} - {:08x}, "
                      "breakpoint range: {:08x} - {:08x}",
                      type, addr, addr, access_end, bp.addr, bp_end);

            return true;
        }
    }

    return false;
}

/**
 * Send packet to gdb client.
 *
 * @param packet Packet to be sent to client.
 */
static void SendPacket(const char packet) {
    std::size_t sent_size = send(gdbserver_socket, &packet, 1, 0);
    if (sent_size != 1) {
        LOG_ERROR(Debug_GDBStub, "send failed");
    }
}

void SendReply(const char* reply) {
    if (!IsConnected()) {
        return;
    }

    std::memset(command_buffer, 0, sizeof(command_buffer));

    command_length = static_cast<u32>(strlen(reply));

#ifdef PRINT_GDB_TRAFFIC
    LOG_INFO(Debug_GDBStub, "Res: {}", reply);
#endif

    if (command_length + 4 > sizeof(command_buffer)) {
        LOG_ERROR(Debug_GDBStub, "command_buffer overflow in SendReply");
        return;
    }

    std::memcpy(command_buffer + 1, reply, command_length);

    u8 checksum = CalculateChecksum(command_buffer, command_length + 1);
    command_buffer[0] = GDB_STUB_START;
    command_buffer[command_length + 1] = GDB_STUB_END;
    command_buffer[command_length + 2] = NibbleToHex(checksum >> 4);
    command_buffer[command_length + 3] = NibbleToHex(checksum);

    u8* ptr = command_buffer;
    u32 left = command_length + 4;
    while (left > 0) {
        s32 sent_size =
            static_cast<s32>(send(gdbserver_socket, reinterpret_cast<char*>(ptr), left, 0));
        if (sent_size < 0) {
            LOG_ERROR(Debug_GDBStub, "gdb: send failed");
            return Shutdown();
        }

        left -= sent_size;
        ptr += sent_size;
    }
}

/// Handle query command from gdb client.
static void HandleQuery() {
    const char* query = reinterpret_cast<const char*>(command_buffer + 1);
    LOG_DEBUG(Debug_GDBStub, "gdb: query '{}'\n", query);

    if (strcmp(query, "TStatus") == 0) {
        SendReply("T0");
    } else if (strncmp(query, "Attached", strlen("Attached")) == 0) {
        SendReply("1");
    } else if (strncmp(query, "Supported", strlen("Supported")) == 0) {
        // PacketSize needs to be large enough for target xml
        SendReply("PacketSize=2000;qXfer:features:read+;qXfer:osdata:read+;qXfer:threads:read+;"
                  "vContSupported+");
    } else if (strncmp(query, "Xfer:features:read:target.xml:",
                       strlen("Xfer:features:read:target.xml:")) == 0) {
        SendReply(target_xml);
    } else if (strncmp(query, "fThreadInfo", strlen("fThreadInfo")) == 0) {
        if (!current_process) {
            SendReply("E01");
            return;
        }

        auto thread_list = current_process->GetThreadList();
        std::string val = "m";
        for (const auto& thread : thread_list) {
            val += fmt::format("{:x},", thread->GetThreadId());
        }

        val.pop_back();
        SendReply(val.c_str());
    } else if (strncmp(query, "sThreadInfo", strlen("sThreadInfo")) == 0) {
        SendReply("l");
    } else if (strncmp(query, "Xfer:osdata:read:processes", strlen("Xfer:osdata:read:processes")) ==
               0) {
        std::string buffer;
        buffer += "l<osdata type=\"processes\">";
        auto process_list = Core::System::GetInstance().Kernel().GetProcessList();
        for (const auto& p : process_list) {
            buffer += fmt::format("<item>"
                                  "<column name=\"pid\">{}</column>"
                                  "<column name=\"command\">{}</column>"
                                  "</item>",
                                  p->process_id, p->codeset->name);
        }
        buffer += "</osdata>";
        SendReply(buffer.c_str());
    } else if (strncmp(query, "Xfer:threads:read", strlen("Xfer:threads:read")) == 0) {
        if (!current_process) {
            SendReply("E01");
            return;
        }

        std::string buffer;
        buffer += "l<?xml version=\"1.0\"?>";
        buffer += "<threads>";
        auto thread_list = current_process->GetThreadList();
        for (const auto& thread : thread_list) {
            buffer += fmt::format(R"*(<thread id="{:x}" name="Thread {:x}"></thread>)*",
                                  thread->GetThreadId(), thread->GetThreadId());
        }
        buffer += "</threads>";

        SendReply(buffer.c_str());
    } else {
        SendReply("");
    }
}
static bool SetThread(int thread_id) {
    if (!current_process) {
        // The process has not been selected yet
        return false;
    }

    if (thread_id >= 1) {
        current_thread = FindThreadById(thread_id);
    }
    if (!current_thread) {
        auto thread_list = current_process->GetThreadList();
        if (thread_list.size() > 0) {
            // Select the lowest thread ID, which is the main thread
            std::sort(thread_list.begin(), thread_list.end(),
                      [](const std::shared_ptr<Kernel::Thread>& a,
                         const std::shared_ptr<Kernel::Thread>& b) {
                          return a->thread_id < b->thread_id;
                      });
            current_thread = thread_list[0].get();
        }
    }
    return current_thread != nullptr;
}
/// Handle set thread command from gdb client.
static void HandleSetThread() {
    int thread_id = -1;
    if (command_buffer[2] != '-') {
        thread_id = static_cast<int>(HexToInt(command_buffer + 2, command_length - 2));
    }

    if (command_buffer[1] == 'c') {
        continue_thread = thread_id;
        SendReply("OK");
        return;
    }

    if (SetThread(thread_id)) {
        SendReply("OK");
        return;
    }
    SendReply("E01");
}

/// Handle thread alive command from gdb client.
static void HandleThreadAlive() {
    if (!current_process) {
        SendReply("E01");
        return;
    }

    int thread_id = static_cast<int>(HexToInt(command_buffer + 1, command_length - 1));
    if (thread_id == 0) {
        thread_id = 1;
    }
    if (FindThreadById(thread_id)) {
        SendReply("OK");
        return;
    }
    SendReply("E01");
}

static void HandleExtendedMode() {
    if (supports_extended_mode) {
        is_extended_mode = true;
        SendReply("OK");
    } else {
        SendReply("");
    }
}

/**
 * Send signal packet to client.
 *
 * @param signal Signal to be sent to client.
 */
static void SendSignal(Kernel::Thread* thread, u32 signal, bool full = true) {
    if (gdbserver_socket == -1) {
        return;
    }

    latest_signal = signal;

    if (!thread) {
        full = false;
    }

    std::string buffer;
    if (full) {
        Core::ARM_Interface::ThreadContext ctx{};
        if (thread) {
            ctx = thread->context;
        } else {
            Core::GetRunningCore().SaveContext(ctx);
        }
        buffer = fmt::format("T{:02x}{:02x}:{:08x};{:02x}:{:08x};{:02x}:{:08x}", latest_signal,
                             PC_REGISTER, htonl(ctx.cpu_registers[PC_REGISTER]), SP_REGISTER,
                             htonl(ctx.cpu_registers[SP_REGISTER]), LR_REGISTER,
                             htonl(ctx.cpu_registers[LR_REGISTER]));
    } else {
        buffer = fmt::format("T{:02x}", latest_signal);
    }

    if (thread) {
        buffer += fmt::format(";thread:{:x};", thread->GetThreadId());
    }

    LOG_DEBUG(Debug_GDBStub, "Response: {}", buffer);
    SendReply(buffer.c_str());
}

static void HandleGetStopReason() {
    if (is_extended_mode) {
        // In extended mode, tell the debugger that there is no selected process yet.
        // That way the debugger can ask for the process list and attack to the right PID.
        if (!current_process) {
            // The process has not been selected yet.
            SendReply("W00");
        } else {
            SendSignal(current_thread, latest_signal);
        }
    } else {
        // In non extended mode, select the process corresponding to the "main"
        // launched application.
        u64 program_id = 0;
        Core::System::GetInstance().GetAppLoader().ReadProgramId(program_id);
        auto process_list = Core::System::GetInstance().Kernel().GetProcessList();
        for (const auto& process : process_list) {
            if (process->codeset->program_id == program_id) {
                current_process = process.get();
                current_process->SetDebugBreak(true);
                if (SetThread(0)) {
                    SendSignal(current_thread, 0);
                } else {
                    // Should never happen
                    SendReply("W00");
                }
                return;
            }
        }

        // No process, should never happen
        SendReply("W00");
    }
}

static void BreakImpl(int signal) {
    if (signal == SIGSEGV) {
        LOG_WARNING(Debug_GDBStub, "Segmentation fault signals may not be fully accurate");
    }

    current_process->SetDebugBreak(true);

    latest_signal = signal;

    SendSignal(current_thread, signal);
}

/// Read command from gdb client.
static void ReadCommand() {
    command_length = 0;
    std::memset(command_buffer, 0, sizeof(command_buffer));

    u8 c = ReadByte();
    if (c == GDB_STUB_ACK) {
        // ignore ack
        return;
    } else if (c == 0x03) {
        LOG_INFO(Debug_GDBStub, "gdb: found break command\n");
        BreakImpl(SIGTRAP);
        return;
    } else if (c != GDB_STUB_START) {
        LOG_DEBUG(Debug_GDBStub, "gdb: read invalid byte {:02x}\n", c);
        return;
    }

    while ((c = ReadByte()) != GDB_STUB_END) {
        if (command_length >= sizeof(command_buffer)) {
            LOG_ERROR(Debug_GDBStub, "gdb: command_buffer overflow\n");
            SendPacket(GDB_STUB_NACK);
            return;
        }
        command_buffer[command_length++] = c;
    }

    u8 checksum_received = HexCharToValue(ReadByte()) << 4;
    checksum_received |= HexCharToValue(ReadByte());

    u8 checksum_calculated = CalculateChecksum(command_buffer, command_length);

    if (checksum_received != checksum_calculated) {
        LOG_ERROR(
            Debug_GDBStub,
            "gdb: invalid checksum: calculated {:02x} and read {:02x} for ${}# (length: {})\n",
            checksum_calculated, checksum_received, reinterpret_cast<const char*>(command_buffer),
            command_length);

        command_length = 0;

        SendPacket(GDB_STUB_NACK);
        return;
    }

    SendPacket(GDB_STUB_ACK);
}

/// Check if there is data to be read from the gdb client.
static bool IsDataAvailable() {
    if (!IsConnected()) {
        return false;
    }

    fd_set fd_socket;

    FD_ZERO(&fd_socket);
    FD_SET(static_cast<u32>(gdbserver_socket), &fd_socket);

    struct timeval t;
    t.tv_sec = 0;
    t.tv_usec = 0;

    if (select(gdbserver_socket + 1, &fd_socket, nullptr, nullptr, &t) < 0) {
        LOG_ERROR(Debug_GDBStub, "select failed");
        return false;
    }

    return FD_ISSET(gdbserver_socket, &fd_socket) != 0;
}

/// Send requested register to gdb client.
static void ReadRegister() {
    if (!current_process || !current_thread) {
        SendReply("E01");
        return;
    }

    static u8 reply[64];
    std::memset(reply, 0, sizeof(reply));

    u32 id = HexCharToValue(command_buffer[1]);
    if (command_buffer[2] != '\0') {
        id <<= 4;
        id |= HexCharToValue(command_buffer[2]);
    }

    if (id <= PC_REGISTER) {
        IntToGdbHex(reply, RegRead(id, current_thread));
    } else if (id == CPSR_REGISTER) {
        IntToGdbHex(reply, RegRead(id, current_thread));
    } else if (id >= D0_REGISTER && id < FPSCR_REGISTER) {
        LongToGdbHex(reply, FpuRead(id, current_thread));
    } else if (id == FPSCR_REGISTER) {
        IntToGdbHex(reply, static_cast<u32>(FpuRead(id, current_thread)));
    } else {
        return SendReply("E01");
    }

    SendReply(reinterpret_cast<char*>(reply));
}

/// Send all registers to the gdb client.
static void ReadRegisters() {
    if (!current_process || !current_thread) {
        SendReply("E01");
        return;
    }

    static u8 buffer[GDB_BUFFER_SIZE - 4];
    std::memset(buffer, 0, sizeof(buffer));

    u8* bufptr = buffer;

    for (u32 reg = 0; reg <= PC_REGISTER; reg++) {
        IntToGdbHex(bufptr + reg * 8, RegRead(reg, current_thread));
    }

    bufptr += 16 * 8;

    IntToGdbHex(bufptr, RegRead(CPSR_REGISTER, current_thread));

    bufptr += 8;

    for (u32 reg = D0_REGISTER; reg < FPSCR_REGISTER; reg++) {
        LongToGdbHex(bufptr + reg * 16, FpuRead(reg, current_thread));
    }

    bufptr += 16 * 16;

    IntToGdbHex(bufptr, static_cast<u32>(FpuRead(FPSCR_REGISTER, current_thread)));

    SendReply(reinterpret_cast<char*>(buffer));
}

static void UpdateCPUThreadContext() {
    u32 core_id = current_thread->core_id;
    auto& system = Core::System::GetInstance();
    auto& thread_manager = system.Kernel().GetThreadManager(core_id);
    if (thread_manager.GetCurrentThread() == current_thread) {
        // Only update CPU context if current thread is active,
        // otherwise it will be updated when the thread is selected
        Core::GetCore(current_thread->core_id).LoadContext(current_thread->context);
    }
}

/// Modify data of register specified by gdb client.
static void WriteRegister() {
    if (!current_process || !current_thread) {
        SendReply("E01");
        return;
    }

    const u8* buffer_ptr = command_buffer + 3;

    u32 id = HexCharToValue(command_buffer[1]);
    if (command_buffer[2] != '=') {
        ++buffer_ptr;
        id <<= 4;
        id |= HexCharToValue(command_buffer[2]);
    }

    if (id <= PC_REGISTER) {
        RegWrite(id, GdbHexToInt(buffer_ptr), current_thread);
    } else if (id == CPSR_REGISTER) {
        RegWrite(id, GdbHexToInt(buffer_ptr), current_thread);
    } else if (id >= D0_REGISTER && id < FPSCR_REGISTER) {
        FpuWrite(id, GdbHexToLong(buffer_ptr), current_thread);
    } else if (id == FPSCR_REGISTER) {
        FpuWrite(id, GdbHexToInt(buffer_ptr), current_thread);
    } else {
        return SendReply("E01");
    }

    UpdateCPUThreadContext();

    SendReply("OK");
}

/// Modify all registers with data received from the client.
static void WriteRegisters() {
    if (!current_process || !current_thread) {
        SendReply("E01");
        return;
    }

    const u8* buffer_ptr = command_buffer + 1;

    if (command_buffer[0] != 'G')
        return SendReply("E01");

    for (u32 i = 0, reg = 0; reg <= FPSCR_REGISTER; i++, reg++) {
        if (reg <= PC_REGISTER) {
            RegWrite(reg, GdbHexToInt(buffer_ptr + i * 8), current_thread);
        } else if (reg == CPSR_REGISTER) {
            RegWrite(reg, GdbHexToInt(buffer_ptr + i * 8), current_thread);
        } else if (reg == CPSR_REGISTER - 1) {
            // Dummy FPA register, ignore
        } else if (reg < CPSR_REGISTER) {
            // Dummy FPA registers, ignore
            i += 2;
        } else if (reg >= D0_REGISTER && reg < FPSCR_REGISTER) {
            FpuWrite(reg, GdbHexToLong(buffer_ptr + i * 16), current_thread);
            i++; // Skip padding
        } else if (reg == FPSCR_REGISTER) {
            FpuWrite(reg, GdbHexToInt(buffer_ptr + i * 8), current_thread);
        }
    }

    UpdateCPUThreadContext();

    SendReply("OK");
}

/// Read location in memory specified by gdb client.
static void ReadMemory() {
    if (!current_process) {
        SendReply("E01");
        return;
    }

    static u8 reply[GDB_BUFFER_SIZE - 4];

    auto start_offset = command_buffer + 1;
    auto addr_pos = std::find(start_offset, command_buffer + command_length, ',');
    VAddr addr = HexToInt(start_offset, static_cast<u32>(addr_pos - start_offset));

    start_offset = addr_pos + 1;
    u32 len =
        HexToInt(start_offset, static_cast<u32>((command_buffer + command_length) - start_offset));

    LOG_DEBUG(Debug_GDBStub, "ReadMemory addr: {:08x} len: {:08x}", addr, len);

    if (len * 2 > sizeof(reply)) {
        SendReply("E01");
    }

    auto& memory = Core::System::GetInstance().Memory();
    if (!memory.IsValidVirtualAddress(*current_process, addr)) {
        return SendReply("E14");
    }

    std::vector<u8> data(len);
    memory.ReadBlock(*current_process, addr, data.data(), len);

    MemToGdbHex(reply, data.data(), len);
    reply[len * 2] = '\0';

    auto reply_str = reinterpret_cast<char*>(reply);

    LOG_DEBUG(Debug_GDBStub, "ReadMemory result: {}", reply_str);
    SendReply(reply_str);
}

/// Modify location in memory with data received from the gdb client.
static void WriteMemory() {
    if (!current_process) {
        SendReply("E01");
        return;
    }

    auto start_offset = command_buffer + 1;
    auto addr_pos = std::find(start_offset, command_buffer + command_length, ',');
    VAddr addr = HexToInt(start_offset, static_cast<u32>(addr_pos - start_offset));

    start_offset = addr_pos + 1;
    auto len_pos = std::find(start_offset, command_buffer + command_length, ':');
    u32 len = HexToInt(start_offset, static_cast<u32>(len_pos - start_offset));

    auto& memory = Core::System::GetInstance().Memory();
    if (!memory.IsValidVirtualAddress(*current_process, addr)) {
        return SendReply("E00");
    }

    std::vector<u8> data(len);

    GdbHexToMem(data.data(), len_pos + 1, len);
    memory.WriteBlock(*current_process, addr, data.data(), len);
    ClearAllInstructionCache();
    SendReply("OK");
}

void Break(int signal) {
    if (!IsConnected() || !current_process ||
        Core::System::GetInstance().Kernel().GetCurrentProcess().get() != current_process) {
        LOG_ERROR(Debug_GDBStub, "Got signal for un-attached process, ignoring...");
        return;
    }

    if (break_thread) {
        LOG_ERROR(Debug_GDBStub,
                  "Got multiple break signals in quick succession, latest may be lost");
        return;
    }

    break_thread =
        Core::System::GetInstance().Kernel().GetCurrentThreadManager().GetCurrentThread();
    if (break_thread) {
        break_signal = signal;
    }

    // Try to break CPU asap
    Core::GetRunningCore().SetBreakFlag();
    Core::GetRunningCore().ClearInstructionCache();
    Core::GetRunningCore().PrepareReschedule();
}

/// Tell the CPU to continue executing.
static void Continue() {
    if (!current_process) {
        return;
    }

    u32 thread_id = -1;
    if (continue_thread == 0) {
        thread_id = current_process->GetThreadList()[0]->thread_id;
    } else {
        thread_id = continue_thread;
    }

    // There is no documentation anywhere if continue should
    // reset the continue thread value. Luma3DS implementation
    // does this, and looks like IDA Pro expects it that way too.
    continue_thread = -1;

    std::vector<u32> continue_list{};
    if (thread_id != -1) {
        continue_list.push_back(thread_id);
    }

    current_process->SetDebugBreak(false, continue_list);
    ClearAllInstructionCache();
}

/**
 * Commit breakpoint to list of breakpoints.
 *
 * @param type Type of breakpoint.
 * @param addr Address of breakpoint.
 * @param len Length of breakpoint.
 */
static bool CommitBreakpoint(BreakpointType type, VAddr addr, u32 len) {
    BreakpointMap& p = GetBreakpointMap(type);

    if (type == BreakpointType::Execute && len != 2 && len != 4) {
        return false;
    }

    Breakpoint breakpoint;
    breakpoint.active = true;
    breakpoint.addr = addr;
    breakpoint.len = len;
    Core::System::GetInstance().Memory().ReadBlock(*current_process, addr, breakpoint.inst.data(),
                                                   len);

    static constexpr std::array<u8, 4> btrap{0x70, 0x00, 0x20, 0xe1};
    static constexpr std::array<u8, 2> btrap_thumb{0x00, 0xBE};

    if (type == BreakpointType::Execute) {
        Core::System::GetInstance().Memory().WriteBlock(
            *current_process, addr, (len == 2) ? btrap_thumb.data() : btrap.data(), len);
        ClearAllInstructionCache();
    } else {
        Core::System::GetInstance().Memory().RegisterWatchpoint(*current_process, addr, len);
    }
    p.insert({addr, breakpoint});

    LOG_DEBUG(Debug_GDBStub, "gdb: added {} breakpoint: {:08x} bytes at {:08x}\n", type,
              breakpoint.len, breakpoint.addr);

    return true;
}

/// Handle add breakpoint command from gdb client.
static void AddBreakpoint() {
    if (!current_process) {
        SendReply("E01");
        return;
    }

    BreakpointType type;

    u8 type_id = HexCharToValue(command_buffer[1]);
    switch (type_id) {
    case 0:
    case 1:
        type = BreakpointType::Execute;
        break;
    case 2:
        type = BreakpointType::Write;
        break;
    case 3:
        type = BreakpointType::Read;
        break;
    case 4:
        type = BreakpointType::Access;
        break;
    default:
        return SendReply("E01");
    }

    auto start_offset = command_buffer + 3;
    auto addr_pos = std::find(start_offset, command_buffer + command_length, ',');
    VAddr addr = HexToInt(start_offset, static_cast<u32>(addr_pos - start_offset));

    start_offset = addr_pos + 1;
    u32 len =
        HexToInt(start_offset, static_cast<u32>((command_buffer + command_length) - start_offset));

    if (type == BreakpointType::Access) {
        // Access is made up of Read and Write types, so add both breakpoints
        type = BreakpointType::Read;

        if (!CommitBreakpoint(type, addr, len)) {
            return SendReply("E02");
        }

        type = BreakpointType::Write;
    }

    if (!CommitBreakpoint(type, addr, len)) {
        return SendReply("E02");
    }

    SendReply("OK");
}

/// Handle remove breakpoint command from gdb client.
static void RemoveBreakpoint() {
    if (!current_process) {
        SendReply("E01");
        return;
    }

    BreakpointType type;

    u8 type_id = HexCharToValue(command_buffer[1]);
    switch (type_id) {
    case 0:
    case 1:
        type = BreakpointType::Execute;
        break;
    case 2:
        type = BreakpointType::Write;
        break;
    case 3:
        type = BreakpointType::Read;
        break;
    case 4:
        type = BreakpointType::Access;
        break;
    default:
        return SendReply("E01");
    }

    auto start_offset = command_buffer + 3;
    auto addr_pos = std::find(start_offset, command_buffer + command_length, ',');
    VAddr addr = HexToInt(start_offset, static_cast<u32>(addr_pos - start_offset));

    if (type == BreakpointType::Access) {
        // Access is made up of Read and Write types, so add both breakpoints
        type = BreakpointType::Read;
        RemoveBreakpoint(type, addr);

        type = BreakpointType::Write;
    }

    RemoveBreakpoint(type, addr);
    SendReply("OK");
}

void HandleVCommand() {
    std::string_view cmd_view((const char*)command_buffer, command_length);

    if (cmd_view.size() <= 1) {
        SendReply("E01");
        return;
    }
    size_t delimiter_pos = cmd_view.find(';');
    std::string_view command =
        cmd_view.substr(1, delimiter_pos == cmd_view.npos ? cmd_view.npos : delimiter_pos);
    if (command == "Attach;") {
        if (!is_extended_mode) {
            SendReply("E01");
            return;
        }

        std::string_view arg = cmd_view.substr(delimiter_pos + 1);
        u32 pid = HexToInt(reinterpret_cast<const u8*>(arg.data()), arg.size());
        auto process = Core::System::GetInstance().Kernel().GetProcessById(pid);
        if (!process) {
            SendReply("E02");
        } else {
            current_process = process.get();
            current_process->SetDebugBreak(true);
            if (SetThread(0)) {
                SendSignal(current_thread, 0);
            } else {
                // Should never happen
                SendReply("W00");
            }
        }
        return;
    } else if (command == "Cont?") {
        SendReply("vCont;c;C");
    } else if (command == "Cont;") {
        if (!current_process) {
            SendReply("E01");
            return;
        }

        std::string_view arg = cmd_view.substr(delimiter_pos + 1);
        auto actions = Common::SplitString(arg, ';');

        if (actions.empty()) {
            SendReply("E01");
            return;
        }
        for (auto& action : actions) {
            auto threads = Common::SplitString(action, ':');
            if (threads.empty()) {
                SendReply("E01");
                return;
            }
            char action_type = threads[0][0];
            if (action_type != 'c') {
                SendReply("E01");
                return;
            }
            std::vector<u32> thread_ids;
            for (size_t i = 1; i < threads.size(); i++) {
                thread_ids.push_back(
                    HexToInt(reinterpret_cast<const u8*>(threads[i].c_str()), threads[i].size()));
            }

            current_process->SetDebugBreak(false, thread_ids);
        }
    } else {
        SendReply("");
    }
}

void HandlePacket(Core::System& system) {

    if (!IsConnected()) {
        if (defer_start) {
            ToggleServer(true);
            defer_start = false;
        }

        // Handle accept new GDB connection
        if (accept_socket != -1) {
            sockaddr_in saddr_client;
            sockaddr* client_addr = reinterpret_cast<sockaddr*>(&saddr_client);
            socklen_t client_addrlen = sizeof(saddr_client);
            gdbserver_socket =
                static_cast<int>(accept(accept_socket, client_addr, &client_addrlen));
            if (gdbserver_socket < 0) {
#ifdef _WIN32
                if (GetErrno() == WSAEWOULDBLOCK) {
                    // Nothing connected yet
                    return;
                }
#else
                if (GetErrno() == EAGAIN || GetErrno() == EWOULDBLOCK) {
                    // Nothing connected yet
                    return;
                }
#endif
                LOG_ERROR(Debug_GDBStub, "Failed to accept gdb client");
            } else {
                LOG_INFO(Debug_GDBStub, "Client connected.\n");
                SetNonBlock(gdbserver_socket, false);
            }

            shutdown(accept_socket, SHUT_RDWR);
            accept_socket = -1;
        }
        return;
    }

    if (break_thread) {
        current_thread = break_thread;
        break_thread = nullptr;
        int signal = break_signal;
        break_signal = 0;
        BreakImpl(signal);
        return;
    }

    if (HandlePendingHioRequestPacket()) {
        // Don't do anything else while we wait for the client to respond
        return;
    }

    if (!IsDataAvailable()) {
        return;
    }

    ReadCommand();
    if (command_length == 0) {
        return;
    }

#ifdef PRINT_GDB_TRAFFIC
    std::string cmd_str(command_buffer + 1, command_buffer + command_length);
    LOG_INFO(Debug_GDBStub, "Req: {:c} {}", command_buffer[0], cmd_str);
#endif

    LOG_DEBUG(Debug_GDBStub, "Packet: {0:d} ('{0:c}')", command_buffer[0]);

    switch (command_buffer[0]) {
    case 'q':
        HandleQuery();
        break;
    case 'H':
        HandleSetThread();
        break;
    case '?':
        HandleGetStopReason();
        break;
    case '!':
        HandleExtendedMode();
        break;
    case 'D':
        SendReply("OK");
        ToggleServer(false);
        // Continue execution
        continue_thread = -1;
        Continue();
        break;
    case 'k':
        LOG_INFO(Debug_GDBStub, "killed by gdb");
        ToggleServer(false);
        // Continue execution and stop emulation
        continue_thread = -1;
        Continue();
        system.RequestShutdown();
        return;
    case 'F':
        HandleHioReply(system, current_process, command_buffer, command_length);
        break;
    case 'g':
        ReadRegisters();
        break;
    case 'G':
        WriteRegisters();
        break;
    case 'p':
        ReadRegister();
        break;
    case 'P':
        WriteRegister();
        break;
    case 'm':
        ReadMemory();
        break;
    case 'M':
        WriteMemory();
        break;
    case 's':
        // Single step not supported, return ENOTSUP
        SendReply("E5F");
        return;
    case 'C':
    case 'c':
        Continue();
        return;
    case 'z':
        RemoveBreakpoint();
        break;
    case 'Z':
        AddBreakpoint();
        break;
    case 'T':
        HandleThreadAlive();
        break;
    case 'v':
        HandleVCommand();
        break;
    default:
        SendReply("");
        break;
    }
}

void SetServerPort(u16 port) {
    gdbstub_port = port;
}

void ToggleServer(bool status) {
    if (status) {
        server_enabled = status;

        // Start server
        if (!IsInitialized() && Core::System::GetInstance().IsPoweredOn()) {
            Init();
        }
    } else {
        // Stop server
        if (IsInitialized()) {
            Shutdown();
        }

        server_enabled = status;
    }
}

void DeferStart() {
    defer_start = true;
}

static void Init(u16 port) {
    if (!server_enabled) {
        return;
    }

    breakpoints_execute.clear();
    breakpoints_read.clear();
    breakpoints_write.clear();

    // Start gdb server
    LOG_INFO(Debug_GDBStub, "Starting GDB server on port {}...", port);

    sockaddr_in saddr_server = {};
    saddr_server.sin_family = AF_INET;
    saddr_server.sin_port = htons(port);
    saddr_server.sin_addr.s_addr = INADDR_ANY;

#ifdef _WIN32
    WSAStartup(MAKEWORD(2, 2), &InitData);
#endif

    accept_socket = static_cast<int>(socket(PF_INET, SOCK_STREAM, 0));
    if (accept_socket == -1) {
        LOG_ERROR(Debug_GDBStub, "Failed to create gdb socket");
    }

    // Set socket to SO_REUSEADDR so it can always bind on the same port
    int reuse_enabled = 1;
    if (setsockopt(accept_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse_enabled,
                   sizeof(reuse_enabled)) < 0) {
        LOG_ERROR(Debug_GDBStub, "Failed to set gdb socket option");
    }

    const sockaddr* server_addr = reinterpret_cast<const sockaddr*>(&saddr_server);
    socklen_t server_addrlen = sizeof(saddr_server);
    if (bind(accept_socket, server_addr, server_addrlen) < 0) {
        LOG_ERROR(Debug_GDBStub, "Failed to bind gdb socket");
    }

    if (listen(accept_socket, 1) < 0) {
        LOG_ERROR(Debug_GDBStub, "Failed to listen to gdb socket");
    }

    SetNonBlock(accept_socket, true);

    // Wait for gdb to connect
    LOG_INFO(Debug_GDBStub, "Waiting for gdb to connect...\n");
}

void Init() {
    Init(gdbstub_port);
}

void Shutdown() {
    if (!server_enabled) {
        return;
    }
    defer_start = false;
    is_extended_mode = false;

    LOG_INFO(Debug_GDBStub, "Stopping GDB ...");
    if (gdbserver_socket != -1) {
        shutdown(gdbserver_socket, SHUT_RDWR);
        gdbserver_socket = -1;
    }

    if (accept_socket != -1) {
        shutdown(accept_socket, SHUT_RDWR);
        accept_socket = -1;
    }

#ifdef _WIN32
    WSACleanup();
#endif

    LOG_INFO(Debug_GDBStub, "GDB stopped.");
}

bool IsServerEnabled() {
    return server_enabled;
}

bool IsInitialized() {
    return IsServerEnabled() && (accept_socket != -1 || gdbserver_socket != -1);
}

bool IsConnected() {
    return IsServerEnabled() && gdbserver_socket != -1;
}
}; // namespace GDBStub
