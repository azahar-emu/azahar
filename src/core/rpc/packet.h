// Copyright 2018-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <array>
#include <functional>
#include <span>
#include "common/common_funcs.h"
#include "common/common_types.h"

namespace Core::RPC {

enum class PacketType : u32 {
    Undefined = 0,
    ReadMemory = 1,
    WriteMemory = 2,
    ProcessList = 3,
    SetGetProcess = 4,
    TakeScreenshot = 5,
    ReadScreenshot = 6,
    GetPerfStats = 7,
    SetFreeCam = 8,
    SaveState = 9,
    LoadState = 10,
    SetInput = 11,
};

namespace FreeCamFlags {
enum : u32 {
    /// Do not transform vertices that are already in (w == ~1), keeping most 2D HUDs in place.
    Skip2D = 1 << 0,
};
} // namespace FreeCamFlags

namespace InputFlags {
enum : u32 {
    Home = 1 << 0,
    Power = 1 << 1,
    Touch = 1 << 2,     ///< The bottom screen is touched at (touch_x, touch_y)
    CirclePad = 1 << 3, ///< The circle pad is at (circle_pad_x, circle_pad_y)
};
} // namespace InputFlags

/// Input applied at every HID update until the next SetInput request, combined with the
/// configured input devices. Send it with every field zero to release everything.
struct InputInfo {
    u32 buttons;      ///< HID pad bits (A = bit 0 ... ZR = bit 15)
    u32 flags;        ///< InputFlags
    u16 touch_x;      ///< Bottom screen pixels, 0 to 319
    u16 touch_y;      ///< Bottom screen pixels, 0 to 239
    s16 circle_pad_x; ///< Raw HID units, -0x9A to 0x9A
    s16 circle_pad_y;
};
static_assert(sizeof(InputInfo) == 0x10, "Incorrect InputInfo size");

struct PacketHeader {
    u32 version;
    u32 id;
    PacketType packet_type;
    u32 packet_size;
};

#pragma pack(push, 1)
struct ProcessInfo {
    u32 process_id;
    u64 title_id;
    std::array<u8, 8> process_name;
};
static_assert(sizeof(ProcessInfo) == 0x14, "Incorrect ProcessInfo size");
#pragma pack(pop)

enum class ScreenshotResult : u32 {
    Success = 0,
    Unsupported = 1,     ///< The active renderer does not support screenshots
    Busy = 2,            ///< Another screenshot request is still in progress
    Timeout = 3,         ///< No frame was rendered in time
    EncodeFailed = 4,    ///< The captured frame could not be encoded as PNG
    InvalidArgument = 5, ///< Invalid resolution scale or flags requested
};

#pragma pack(push, 1)
/// Reply data of a TakeScreenshot request.
struct ScreenshotInfo {
    ScreenshotResult result;
    u32 width;
    u32 height;
    u32 data_size;
};
static_assert(sizeof(ScreenshotInfo) == 0x10, "Incorrect ScreenshotInfo size");

/// Reply data of a GetPerfStats request. Times are in seconds,
/// rates are per second.
struct PerfStatsInfo {
    f64 system_fps;
    f64 game_fps;
    f64 time_vblank_interval;
    f64 time_hle_svc;
    f64 time_hle_ipc;
    f64 time_gpu;
    f64 time_swap;
    f64 time_remaining;
    f64 emulation_speed;
    f64 artic_transmitted;
    u32 artic_events;
};
static_assert(sizeof(PerfStatsInfo) == 0x54, "Incorrect PerfStatsInfo size");
#pragma pack(pop)

constexpr u32 MAX_SCREENSHOT_RES_SCALE = 10;

enum class ScreenshotFlags : u32 {
    SecondaryWindow = (1 << 0),
    RawRGB = (1 << 1),
};
DECLARE_ENUM_FLAG_OPERATORS(ScreenshotFlags);

enum class PerfStatsMode : u32 {
    Last = 0,     ///< Last stats computed by the frontend, no reset
    GetReset = 1, ///< Compute stats since the last reset, then reset
};

constexpr u32 CURRENT_VERSION = 2;
constexpr u32 MIN_PACKET_SIZE = sizeof(PacketHeader);
constexpr u32 MAX_PACKET_DATA_SIZE = 32 * 1024;
constexpr u32 MAX_PACKET_SIZE = MIN_PACKET_SIZE + MAX_PACKET_DATA_SIZE;
constexpr u32 MAX_READ_SIZE = MAX_PACKET_DATA_SIZE;

constexpr u32 MAX_PROCESSES_IN_LIST = (MAX_PACKET_DATA_SIZE - sizeof(u32)) / sizeof(ProcessInfo);

/// SetFreeCam payload: u32 flags, u32 reserved, then a 4x4 float matrix.
constexpr u32 FREECAM_MATRIX_SIZE = sizeof(float) * 16;
constexpr u32 FREECAM_PACKET_SIZE = (sizeof(u32) * 2) + FREECAM_MATRIX_SIZE;

class Packet {
public:
    explicit Packet(const PacketHeader& header, u8* data,
                    std::function<void(Packet&)> send_reply_callback);
    ~Packet();

    u32 GetVersion() const {
        return header.version;
    }

    u32 GetId() const {
        return header.id;
    }

    PacketType GetPacketType() const {
        return header.packet_type;
    }

    u32 GetPacketDataSize() const {
        return header.packet_size;
    }

    const PacketHeader& GetHeader() const {
        return header;
    }

    std::span<u8, MAX_PACKET_DATA_SIZE> GetPacketData() {
        return packet_data;
    }

    void SetPacketDataSize(u32 size) {
        header.packet_size = size;
    }

    void SendReply() {
        send_reply_callback(*this);
    }

private:
    struct PacketHeader header;
    std::array<u8, MAX_PACKET_DATA_SIZE> packet_data;

    std::function<void(Packet&)> send_reply_callback;
};

} // namespace Core::RPC
