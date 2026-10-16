// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <algorithm>
#include <bit>
#include <cstring>
#include "audio_core/dsp_interface.h"
#include "common/alignment.h"
#include "common/archives.h"
#include "common/bit_field.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/process.h"
#include "core/hle/result.h"
#include "core/hle/service/csnd/csnd_snd.h"

SERVICE_CONSTRUCT_IMPL(Service::CSND::CSND_SND)
SERIALIZE_EXPORT_IMPL(Service::CSND::CSND_SND)
SERIALIZE_EXPORT_IMPL(Service::CSND::ClientData)

namespace Service::CSND {

namespace {

using Hw = AudioCore::Csnd;
using AudioCore::CsndCaptureCnt;
using AudioCore::CsndCnt;

constexpr Result ResultInvalidState(ErrorDescription::InvalidResultValue, ErrorModule::CSND,
                                    ErrorSummary::InvalidState,
                                    ErrorLevel::Status); // 0xC8A0B7FF
constexpr Result ResultAlreadyInitialized(59, ErrorModule::OS, ErrorSummary::InvalidState,
                                          ErrorLevel::Usage); // 0xE0A0183B
constexpr Result ResultInvalidAddress(ErrorDescription::InvalidPointer, ErrorModule::Common,
                                      ErrorSummary::InvalidArgument,
                                      ErrorLevel::Usage); // 0xE0E3FFF6

/// Channels that AcquireSoundChannels gives to clients, 0-7 are used for direct sound.
constexpr u32 ClientChannelMask = 0xFFFFFF00;

constexpr u16 CommandListEnd = 0xFFFF;

/// Volume fades, CSND moves volumes in 40 steps, 4 steps (200 us apart)
/// per tick of a 1.666 ms timer.
constexpr s32 FadeSteps = 40;
constexpr u32 FadeStepsPerTick = 4;
constexpr u64 FadeTickUs = 1666;
constexpr u64 FadeStepUs = 200;
constexpr s32 MaxVolume = 0x8000;

/// ARM11 cycles per audio frame, same as for the DSP (see audio_core/hle/hle.cpp).
constexpr s64 AudioFrameTicks = AudioCore::samples_per_frame * 4096 * 2;

enum class CommandId : u16 {
    SetPlayStateRaw = 0x000,
    SetPlayState = 0x001,
    SetEncoding = 0x002,
    SetLoopBlock = 0x003,
    SetLoopMode = 0x004,
    SetHold = 0x005,
    SetLinearInterpolation = 0x006,
    SetPsgDuty = 0x007,
    SetTimer = 0x008,
    SetVolume = 0x009,
    SetBlock = 0x00A,
    SetAdpcmState = 0x00B,
    SetLoopAdpcmState = 0x00C,
    SetLoopAdpcmReload = 0x00D,
    ConfigureChannel = 0x00E,
    ConfigurePsg = 0x00F,
    ConfigureNoise = 0x010,
    SetCaptureEnable = 0x100,
    SetCaptureOneShot = 0x101,
    SetCaptureFormat = 0x102,
    SetCaptureBit2 = 0x103,
    SetCaptureTimer = 0x104,
    SetCaptureBuffer = 0x105,
    ConfigureCapture = 0x106,
    ApplyMasks = 0x200,
    UpdateState = 0x300,
};

/// Channel state entry written by UpdateState, one per acquired channel.
struct ChannelState {
    u8 active;
    INSERT_PADDING_BYTES(3);
    s16_le adpcm_predictor;
    u8 adpcm_index;
    INSERT_PADDING_BYTES(1);
    u32_le zero;
};
static_assert(sizeof(ChannelState) == 0xC, "ChannelState structure size is wrong");

constexpr u32 CaptureStateSize = 8;

/// First parameter of command 0xE (ConfigureChannel).
union ConfigureChannelFlags {
    u32 raw;
    BitField<0, 5, u32> channel;
    BitField<6, 1, u32> linear_interpolation;
    BitField<10, 2, Hw::LoopMode> loop_mode;
    BitField<12, 2, Hw::Format> format;
    BitField<14, 1, u32> play;
    BitField<16, 16, u32> timer;
};

} // namespace

struct Type0Command {
    u16_le next_command_offset;
    enum_le<CommandId> command_id;
    u8 finished;
    INSERT_PADDING_BYTES(3);
    std::array<u32_le, 6> params;
};
static_assert(sizeof(Type0Command) == 0x20, "Type0Command structure size is wrong");

template <class Archive>
void ClientData::serialize(Archive& ar, const unsigned int) {
    ar& boost::serialization::base_object<Kernel::SessionRequestHandler::SessionDataBase>(*this);
    ar & mutex;
    ar & shared_memory;
    ar & master_state_offset;
    ar & channel_state_offset;
    ar & capture_state_offset;
    ar & direct_sound_offset;
    ar & channel_mask;
    ar & capture_mask;
}
SERIALIZE_IMPL(ClientData)

/// Writes to the client's shared memory, if it fits.
template <typename T>
static void WriteShared(ClientData& client, u32 offset, const T& data) {
    if (!client.shared_memory || offset > client.shared_memory->GetSize() ||
        client.shared_memory->GetSize() - offset < sizeof(T)) {
        LOG_ERROR(Service_CSND, "out of bounds write size=0x{:X} offset=0x{:X}", sizeof(T), offset);
        return;
    }
    std::memcpy(client.shared_memory->GetPointer(offset), &data, sizeof(T));
}

void CSND_SND::UpdateHardware() {
    if (!is_ticking) {
        return;
    }
    const s64 elapsed = system.CoreTiming().GetTicks() - frame_start_ticks;
    const u32 elapsed_scaled = static_cast<u32>(std::clamp<s64>(
        elapsed * AudioCore::samples_per_frame / frame_length_ticks, 0, frame.size()));
    if (elapsed_scaled > frame_samples) {
        csnd_hw.Render(std::span(frame).subspan(frame_samples, elapsed_scaled - frame_samples));
        frame_samples = elapsed_scaled;
    }
}

void CSND_SND::AudioTickCallback(s64 cycles_late) {
    if (frame_samples < frame.size()) {
        csnd_hw.Render(std::span(frame).subspan(frame_samples));
    }

    system.DSP().OutputCsndFrame(frame);
    frame_samples = 0;

    if (initialized_clients == 0 && !csnd_hw.IsAnyChannelBusy()) {
        is_ticking = false;
        return;
    }

    // Keep in sync with the DSP audio frames (see DspHle::Impl::AudioTickCallback).
    const double time_scale = Settings::values.enable_realtime_audio
                                  ? std::max(0.01, system.GetStableFrameTimeScale())
                                  : 1.0;
    frame_length_ticks = std::max<s64>(static_cast<s64>(AudioFrameTicks / time_scale), 1);
    frame_start_ticks = system.CoreTiming().GetTicks() - cycles_late;
    system.CoreTiming().ScheduleEvent(frame_length_ticks - cycles_late, audio_tick_event);
}

void CSND_SND::StartTicking() {
    if (is_ticking) {
        return;
    }
    is_ticking = true;
    frame_samples = 0;
    frame_length_ticks = AudioFrameTicks;
    frame_start_ticks = system.CoreTiming().GetTicks();
    system.CoreTiming().ScheduleEvent(frame_length_ticks, audio_tick_event);
}

bool CSND_SND::IsRunning(u32 channel) {
    constexpr CsndCnt running = CsndCnt::Enable | CsndCnt::Run;
    return (csnd_hw.GetCnt(channel) & running) == running;
}

void CSND_SND::WriteCurrentVolume(u32 channel) {
    const auto& volume = volumes[channel];
    csnd_hw.SetVolume(channel, static_cast<u16>(volume.current[0]),
                      static_cast<u16>(volume.current[1]));
}

void CSND_SND::SetRequestedVolume(u32 channel, u16 left, u16 right, bool start_fade) {
    volumes[channel].requested = {left, right};
    SetTargetVolume(channel, left, right, start_fade);
}

void CSND_SND::SetTargetVolume(u32 channel, u16 left, u16 right, bool start_fade) {
    auto& volume = volumes[channel];
    const std::array<u16, 2> target{left, right};
    for (std::size_t side = 0; side < 2; side++) {
        volume.step[side] = (target[side] - volume.current[side]) / FadeSteps;
        volume.target[side] = target[side];
        if (volume.step[side] == 0) {
            volume.current[side] = target[side];
        }
    }
    fading.set(channel);
    if (start_fade) {
        StartFade();
    }
}

void CSND_SND::StartFade() {
    if (fade_scheduled) {
        return;
    }
    fade_scheduled = true;
    // The module's timer has no initial delay, the first tick runs immediately.
    system.CoreTiming().ScheduleEvent(0, fade_event, 0);
}

void CSND_SND::FadeCallback(std::uintptr_t step, s64 cycles_late) {
    const ChannelSet work = (fading | stop_pending | pause_pending) & started;
    if (work.none()) {
        fade_scheduled = false;
        return;
    }

    UpdateHardware();

    const ChannelSet fading_now = fading & started;
    for (u32 channel = 0; channel < Hw::NumChannels; channel++) {
        if (!fading_now[channel]) {
            continue;
        }
        auto& volume = volumes[channel];
        for (std::size_t side = 0; side < 2; side++) {
            if (volume.current[side] != volume.target[side]) {
                volume.current[side] =
                    std::clamp(volume.current[side] + volume.step[side], 0, MaxVolume);
            }
        }
    }

    if (step == FadeStepsPerTick - 1) {
        // Finish the fades that reached their target.
        for (u32 channel = 0; channel < Hw::NumChannels; channel++) {
            if (!work[channel]) {
                continue;
            }
            auto& volume = volumes[channel];
            bool reached = true;
            for (std::size_t side = 0; side < 2; side++) {
                const bool done = volume.step[side] > 0
                                      ? volume.current[side] >= volume.target[side]
                                      : volume.current[side] <= volume.target[side];
                if (done) {
                    volume.current[side] = volume.target[side];
                } else {
                    reached = false;
                }
            }
            if (!reached) {
                continue;
            }
            fading.reset(channel);
            if (stop_pending[channel]) {
                StopChannel(channel);
                stop_pending.reset(channel);
                started.reset(channel);
            }
            if (pause_pending[channel]) {
                csnd_hw.SetCnt(channel, csnd_hw.GetCnt(channel) & ~CsndCnt::Run);
                pause_pending.reset(channel);
                started.reset(channel);
            }
        }
    }

    for (u32 channel = 0; channel < Hw::NumChannels; channel++) {
        if (fading_now[channel]) {
            WriteCurrentVolume(channel);
        }
    }

    if (((fading | stop_pending | pause_pending) & started).none()) {
        fade_scheduled = false;
        return;
    }
    if (step + 1 < FadeStepsPerTick) {
        system.CoreTiming().ScheduleEvent(usToCycles(FadeStepUs) - cycles_late, fade_event,
                                          step + 1);
    } else {
        const u64 rest = FadeTickUs - FadeStepUs * (FadeStepsPerTick - 1);
        system.CoreTiming().ScheduleEvent(usToCycles(rest) - cycles_late, fade_event, 0);
    }
}

void CSND_SND::StopChannel(u32 channel) {
    // Real hardware stops the channel by starting a non-looping short silent sound.
    // We do not need that and can just clear the channel.
    csnd_hw.SetCnt(channel,
                   csnd_hw.GetCnt(channel) & ~(CsndCnt::Enable | CsndCnt::Run |
                                               CsndCnt::LinearInterpolation | CsndCnt::Hold));
}

void CSND_SND::SetPlayStateRaw(u32 channel, bool play) {
    if (play) {
        csnd_hw.SetCnt(channel, csnd_hw.GetCnt(channel) | CsndCnt::Enable);
    } else if (IsRunning(channel)) {
        stop_pending.set(channel);
        SetTargetVolume(channel, 0, 0, true);
    } else {
        StopChannel(channel);
        volumes[channel].current = {};
        started.reset(channel);
    }
}

void CSND_SND::SetPlayState(u32 channel, bool play) {
    auto& volume = volumes[channel];
    if (play) {
        started.set(channel);
        volume.current = {volume.requested[0], volume.requested[1]};
        WriteCurrentVolume(channel);
        csnd_hw.SetCnt(channel, csnd_hw.GetCnt(channel) | CsndCnt::Enable | CsndCnt::Run);
    } else if (IsRunning(channel)) {
        pause_pending.set(channel);
        SetTargetVolume(channel, 0, 0, true);
    } else {
        csnd_hw.SetCnt(channel, csnd_hw.GetCnt(channel) & ~CsndCnt::Run);
        volume.current = {};
        started.reset(channel);
    }
}

void CSND_SND::UpdateState(ClientData& client) {
    const u32 mask_register = csnd_hw.GetChannelMaskRegister();
    WriteShared(client, client.master_state_offset,
                std::array<u32_le, 2>{mask_register & client.channel_mask,
                                      mask_register & client.capture_mask});

    u32 index = 0;
    for (u32 channel = 0; channel < Hw::NumChannels; channel++) {
        if (!(client.channel_mask & (1u << channel))) {
            continue;
        }
        const u32 adpcm = csnd_hw.GetAdpcmState(channel);
        const u32 offset = client.channel_state_offset + index * sizeof(ChannelState);
        WriteShared(client, offset, static_cast<u8>(IsRunning(channel)));
        WriteShared(client, offset + offsetof(ChannelState, adpcm_predictor),
                    s16_le{static_cast<s16>(adpcm & 0xFFFF)});
        WriteShared(client, offset + offsetof(ChannelState, adpcm_index),
                    static_cast<u8>((adpcm >> 16) & 0x7F));
        WriteShared(client, offset + offsetof(ChannelState, zero), u32_le{0});
        index++;
    }

    for (u32 unit = 0; unit < Hw::NumCaptureUnits; unit++) {
        if (!(client.capture_mask & (1u << unit))) {
            continue;
        }
        // Capture is not emulated: a capture unit never runs.
        const u32 offset = client.capture_state_offset + unit * CaptureStateSize;
        WriteShared(client, offset, u8{0});
        WriteShared(client, offset + 4, u32_le{0});
    }
}

void CSND_SND::ExecuteCommand(ClientData& client, const Type0Command& command) {
    const CommandId id = command.command_id;
    const auto& params = command.params;
    const u32 channel = params[0] & 0x1F;
    const u32 value = params[1];

    if (static_cast<u16>(id) < 0x100) {
        // Channel commands only reach the channels the client acquired.
        if (!(client.channel_mask & (1u << channel))) {
            return;
        }
    } else if (static_cast<u16>(id) < 0x200) {
        if (channel >= Hw::NumCaptureUnits) {
            return;
        }
        LOG_WARNING(Service_CSND, "capture is not emulated (command 0x{:03X})",
                    static_cast<u16>(id));
    }

    const auto clear_channel_state = [&] {
        const u32 index = std::popcount(client.channel_mask & ((1u << channel) - 1));
        WriteShared(client, client.channel_state_offset + index * sizeof(ChannelState) + 8,
                    u32_le{0});
    };
    const auto clear_capture_state = [&] {
        WriteShared(client, client.capture_state_offset + channel * CaptureStateSize + 4,
                    u32_le{0});
    };
    const auto set_cnt_bits = [&](u32 mask, CsndCnt bits) {
        csnd_hw.SetCnt(channel, (csnd_hw.GetCnt(channel) & ~static_cast<CsndCnt>(mask)) | bits);
    };
    const auto set_capture_cnt_bits = [&](CsndCaptureCnt mask, CsndCaptureCnt bits) {
        csnd_hw.SetCaptureCnt(channel, (csnd_hw.GetCaptureCnt(channel) & ~mask) | bits);
    };
    const bool busy = True(csnd_hw.GetCnt(channel) & CsndCnt::Enable);

    switch (id) {
    case CommandId::SetPlayStateRaw:
        SetPlayStateRaw(channel, value != 0);
        break;
    case CommandId::SetPlayState:
        SetPlayState(channel, value != 0);
        break;
    case CommandId::SetEncoding:
        // The value is not masked to the field, as in real hardware.
        if (!busy) {
            set_cnt_bits(AudioCore::Csnd::CntFormatMask,
                         static_cast<CsndCnt>((value & 0xFF) << Hw::CntFormatShift));
        }
        break;
    case CommandId::SetLoopBlock:
        csnd_hw.SetLoopAddress(channel, value);
        csnd_hw.SetSize(channel, params[2]);
        break;
    case CommandId::SetLoopMode:
        if (!busy) {
            set_cnt_bits(AudioCore::Csnd::CntLoopModeMask,
                         static_cast<CsndCnt>((value & 0xFF) << Hw::CntLoopModeShift));
        }
        break;
    case CommandId::SetHold:
        set_cnt_bits(static_cast<u32>(CsndCnt::Hold), value != 0 ? CsndCnt::Hold : CsndCnt::None);
        break;
    case CommandId::SetLinearInterpolation:
        if (!busy) {
            set_cnt_bits(static_cast<u32>(CsndCnt::LinearInterpolation),
                         value != 0 ? CsndCnt::LinearInterpolation : CsndCnt::None);
        }
        break;
    case CommandId::SetTimer:
        csnd_hw.SetTimer(channel, static_cast<u16>(0x10000 - value));
        break;
    case CommandId::SetVolume:
        SetRequestedVolume(channel, value & 0xFFFF, value >> 16, true);
        csnd_hw.SetCaptureVolume(channel, params[2] & 0xFFFF, params[2] >> 16);
        break;
    case CommandId::SetBlock:
        csnd_hw.SetStartAddress(channel, value);
        csnd_hw.SetSize(channel, params[2]);
        clear_channel_state();
        break;
    case CommandId::SetAdpcmState:
        csnd_hw.SetAdpcmState(channel, (value & 0xFFFF) | ((params[2] & 0xFF) << 16));
        csnd_hw.SetLoopAdpcmState(channel, 0);
        break;
    case CommandId::SetLoopAdpcmState:
        csnd_hw.SetLoopAdpcmState(channel, (value & 0xFFFF) | ((params[2] & 0xFF) << 16) |
                                               Hw::LoopAdpcmReload);
        break;
    case CommandId::SetLoopAdpcmReload: {
        const u32 state = csnd_hw.GetLoopAdpcmState(channel) & ~Hw::LoopAdpcmReload;
        csnd_hw.SetLoopAdpcmState(channel, state | (value != 0 ? Hw::LoopAdpcmReload : 0));
        break;
    }
    case CommandId::ConfigureChannel: {
        const ConfigureChannelFlags flags{params[0]};
        csnd_hw.SetCnt(channel, CsndCnt::None);
        csnd_hw.SetTimer(channel, static_cast<u16>(0x10000 - flags.timer));
        csnd_hw.SetVolume(channel, 0, 0);
        csnd_hw.SetCaptureVolume(channel, params[2] & 0xFFFF, params[2] >> 16);
        csnd_hw.SetLoopAddress(channel, params[4]);
        csnd_hw.SetSize(channel, params[5] & 0x7FFFFFF);
        csnd_hw.SetStartAddress(channel, params[3]);
        if (flags.format != Hw::Format::ImaAdpcm) {
            csnd_hw.SetAdpcmState(channel, 0);
            csnd_hw.SetLoopAdpcmState(channel, 0);
        }
        csnd_hw.SetCnt(channel, Hw::CntFormat(flags.format) | Hw::CntLoopMode(flags.loop_mode) |
                                    (flags.linear_interpolation ? CsndCnt::LinearInterpolation
                                                                : CsndCnt::None));
        SetRequestedVolume(channel, value & 0xFFFF, value >> 16, false);
        SetPlayState(channel, flags.play != 0);
        clear_channel_state();
        break;
    }
    case CommandId::SetPsgDuty:
    case CommandId::ConfigurePsg:
    case CommandId::ConfigureNoise:
        // TODO(PabloMK7): PSG and noise channels.
        LOG_WARNING(Service_CSND, "(STUBBED) PSG/noise command 0x{:03X}, channel {}",
                    static_cast<u16>(id), channel);
        break;
    case CommandId::SetCaptureEnable:
        set_capture_cnt_bits(CsndCaptureCnt::Enable,
                             value != 0 ? CsndCaptureCnt::Enable : CsndCaptureCnt::None);
        break;
    case CommandId::SetCaptureOneShot:
        // The value is not masked to the field, as in real hardware.
        set_capture_cnt_bits(CsndCaptureCnt::OneShot, static_cast<CsndCaptureCnt>(value & 0xFF));
        break;
    case CommandId::SetCaptureFormat:
        set_capture_cnt_bits(CsndCaptureCnt::Pcm8,
                             static_cast<CsndCaptureCnt>((value & 0xFF) << 1));
        break;
    case CommandId::SetCaptureBit2:
        set_capture_cnt_bits(CsndCaptureCnt::Unknown2,
                             value != 0 ? CsndCaptureCnt::Unknown2 : CsndCaptureCnt::None);
        break;
    case CommandId::SetCaptureTimer:
        csnd_hw.SetCaptureTimer(channel, static_cast<u16>(0x10000 - value));
        break;
    case CommandId::SetCaptureBuffer:
        csnd_hw.SetCaptureAddress(channel, value, params[2]);
        clear_capture_state();
        break;
    case CommandId::ConfigureCapture:
        csnd_hw.SetCaptureCnt(channel, CsndCaptureCnt::None);
        csnd_hw.SetCaptureTimer(channel, static_cast<u16>(0x10000 - (value >> 16)));
        csnd_hw.SetCaptureAddress(channel, params[2], params[3]);
        csnd_hw.SetCaptureCnt(channel, static_cast<CsndCaptureCnt>(value) &
                                           (CsndCaptureCnt::Enable | CsndCaptureCnt::OneShot |
                                            CsndCaptureCnt::Pcm8 | CsndCaptureCnt::Unknown2));
        clear_capture_state();
        break;
    case CommandId::ApplyMasks:
        csnd_hw.SetChannelMaskRegister(client.channel_mask);
        csnd_hw.SetCaptureMaskRegister(client.capture_mask);
        break;
    case CommandId::UpdateState:
        UpdateState(client);
        break;
    default:
        LOG_DEBUG(Service_CSND, "unknown command 0x{:X}", static_cast<u16>(id));
        break;
    }
}

void CSND_SND::Initialize(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 size = rp.Pop<u32>();
    const u32 master_state_offset = rp.Pop<u32>();
    const u32 channel_state_offset = rp.Pop<u32>();
    const u32 capture_state_offset = rp.Pop<u32>();
    const u32 direct_sound_offset = rp.Pop<u32>();

    LOG_DEBUG(Service_CSND,
              "called, size=0x{:08X} master_state_offset=0x{:08X} "
              "channel_state_offset=0x{:08X} capture_state_offset=0x{:08X} "
              "direct_sound_offset=0x{:08X}",
              size, master_state_offset, channel_state_offset, capture_state_offset,
              direct_sound_offset);

    auto* client = GetSessionData(ctx.Session());
    if (client->mutex) {
        // Real hardware panics, we are more gentle and return an error instead.
        LOG_ERROR(Service_CSND, "session already initialized");
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultAlreadyInitialized);
        return;
    }

    auto shared_memory = system.Kernel().CreateSharedMemory(
        nullptr, Common::AlignUp(std::max<u32>(size, 1), Memory::CITRA_PAGE_SIZE),
        Kernel::MemoryPermission::ReadWrite, Kernel::MemoryPermission::ReadWrite, 0,
        Kernel::MemoryRegion::BASE, "CSND:SharedMemory");
    if (shared_memory.Failed()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(shared_memory.Code());
        return;
    }

    client->mutex = system.Kernel().CreateMutex(false, "CSND:mutex");
    client->shared_memory = shared_memory.Unwrap();
    std::memset(client->shared_memory->GetPointer(), 0, client->shared_memory->GetSize());
    client->master_state_offset = master_state_offset;
    client->channel_state_offset = channel_state_offset;
    client->capture_state_offset = capture_state_offset;
    client->direct_sound_offset = direct_sound_offset;
    initialized_clients++;
    StartTicking();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 3);
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(client->mutex, client->shared_memory);
}

void CSND_SND::Shutdown(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    LOG_DEBUG(Service_CSND, "called");

    // Channels keep playing and stay acquired, as in real hardware.
    auto* client = GetSessionData(ctx.Session());
    if (client->mutex) {
        initialized_clients--;
    }
    client->mutex = nullptr;
    client->shared_memory = nullptr;

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void CSND_SND::ExecuteCommands(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 first_offset = rp.Pop<u32>();
    LOG_TRACE(Service_CSND, "called, offset=0x{:08X}", first_offset);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    auto* client = GetSessionData(ctx.Session());
    if (!client->shared_memory) {
        LOG_ERROR(Service_CSND, "called before Initialize");
        rb.Push(ResultInvalidState);
        return;
    }

    // Real hardware defers this to a worker thread.
    UpdateHardware();
    const u64 size = client->shared_memory->GetSize();
    const u64 max_commands = size / sizeof(Type0Command);
    u64 executed = 0;
    for (u32 offset = first_offset; offset != CommandListEnd;) {
        if (offset + sizeof(Type0Command) > size || executed++ >= max_commands) {
            LOG_ERROR(Service_CSND, "bad command list (offset 0x{:X})", offset);
            break;
        }
        Type0Command command;
        std::memcpy(&command, client->shared_memory->GetPointer(offset), sizeof(command));
        ExecuteCommand(*client, command);
        offset = command.next_command_offset;
    }

    if (first_offset + sizeof(Type0Command) <= size) {
        *client->shared_memory->GetPointer(first_offset + offsetof(Type0Command, finished)) = 1;
    }

    rb.Push(ResultSuccess);
}

void CSND_SND::AcquireSoundChannels(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    // There is no real channel allocation per client, all clients get the same channels.
    auto* client = GetSessionData(ctx.Session());
    client->channel_mask = ClientChannelMask;

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(client->channel_mask);
}

void CSND_SND::ReleaseSoundChannels(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    GetSessionData(ctx.Session())->channel_mask = 0;

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void CSND_SND::AcquireCapUnit(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    auto* client = GetSessionData(ctx.Session());

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    const auto free_unit =
        std::find(capture_units_in_use.begin(), capture_units_in_use.end(), false);
    if (free_unit == capture_units_in_use.end()) {
        LOG_WARNING(Service_CSND, "No more capture units available");
        rb.Push(ResultInvalidState);
        rb.Push<u32>(0xFFFFFFFF);
        return;
    }

    const u32 unit = static_cast<u32>(free_unit - capture_units_in_use.begin());
    *free_unit = true;
    client->capture_mask |= 1u << unit;
    WriteShared(*client, client->capture_state_offset + unit * CaptureStateSize, u8{0});
    WriteShared(*client, client->capture_state_offset + unit * CaptureStateSize + 4, u32_le{0});

    rb.Push(ResultSuccess);
    rb.Push(unit);
}

void CSND_SND::ReleaseCapUnit(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 unit = rp.Pop<u32>();
    auto* client = GetSessionData(ctx.Session());

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    if (unit >= Hw::NumCaptureUnits || !(client->capture_mask & (1u << unit))) {
        LOG_ERROR(Service_CSND, "capture unit {} not acquired", unit);
        rb.Push(ResultInvalidState);
        return;
    }

    client->capture_mask &= ~(1u << unit);
    capture_units_in_use[unit] = false;
    rb.Push(ResultSuccess);
}

void CSND_SND::CacheOperation(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const VAddr address = rp.Pop<u32>();
    const u32 size = rp.Pop<u32>();
    [[maybe_unused]] const auto process = rp.PopObject<Kernel::Process>();

    // The CPU cache is not emulated, only the range check is.
    const auto in_range = [&](u32 low, u32 high) {
        const u32 end = address + size;
        return low <= address && address <= high && low <= end && end <= high;
    };
    const bool valid = in_range(0x14000000, 0x1C000000) || in_range(0x30000000, 0x40000000);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(valid ? ResultSuccess : ResultInvalidAddress);

    LOG_TRACE(Service_CSND, "called address=0x{:08X}, size=0x{:08X}", address, size);
}

void CSND_SND::FlushDataCache(Kernel::HLERequestContext& ctx) {
    CacheOperation(ctx);
}

void CSND_SND::StoreDataCache(Kernel::HLERequestContext& ctx) {
    CacheOperation(ctx);
}

void CSND_SND::InvalidateDataCache(Kernel::HLERequestContext& ctx) {
    CacheOperation(ctx);
}

void CSND_SND::Reset(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    LOG_DEBUG(Service_CSND, "called");

    // Stops all channels immediately, without fading.
    UpdateHardware();
    for (u32 channel = 0; channel < Hw::NumChannels; channel++) {
        StopChannel(channel);
        volumes[channel].current = {};
        WriteCurrentVolume(channel);
    }
    started.reset();
    fading.reset();
    stop_pending.reset();
    pause_pending.reset();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void CSND_SND::ClientDisconnected(std::shared_ptr<Kernel::ServerSession> server_session) {
    if (auto* client = GetSessionData(server_session)) {
        if (client->mutex) {
            initialized_clients--;
        }
        for (u32 unit = 0; unit < Hw::NumCaptureUnits; unit++) {
            if (client->capture_mask & (1u << unit)) {
                capture_units_in_use[unit] = false;
            }
        }
    }
    SessionRequestHandler::ClientDisconnected(server_session);
}

CSND_SND::CSND_SND(Core::System& system)
    : ServiceFramework("csnd:SND", 4), system(system), csnd_hw(system.Memory()) {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x0001, &CSND_SND::Initialize, "Initialize"},
        {0x0002, &CSND_SND::Shutdown, "Shutdown"},
        {0x0003, &CSND_SND::ExecuteCommands, "ExecuteCommands"},
        {0x0004, nullptr, "PlaySoundDirectly"},
        {0x0005, &CSND_SND::AcquireSoundChannels, "AcquireSoundChannels"},
        {0x0006, &CSND_SND::ReleaseSoundChannels, "ReleaseSoundChannels"},
        {0x0007, &CSND_SND::AcquireCapUnit, "AcquireCapUnit"},
        {0x0008, &CSND_SND::ReleaseCapUnit, "ReleaseCapUnit"},
        {0x0009, &CSND_SND::FlushDataCache, "FlushDataCache"},
        {0x000A, &CSND_SND::StoreDataCache, "StoreDataCache"},
        {0x000B, &CSND_SND::InvalidateDataCache, "InvalidateDataCache"},
        {0x000C, &CSND_SND::Reset, "Reset"},
        // clang-format on
    };
    RegisterHandlers(functions);

    Core::Timing& timing = system.CoreTiming();
    audio_tick_event =
        timing.RegisterEvent("CSND::AudioTickCallback", [this](std::uintptr_t, s64 cycles_late) {
            AudioTickCallback(cycles_late);
        });
    fade_event =
        timing.RegisterEvent("CSND::FadeCallback", [this](std::uintptr_t step, s64 cycles_late) {
            FadeCallback(step, cycles_late);
        });
}

CSND_SND::~CSND_SND() = default;

template <class Archive>
void CSND_SND::serialize(Archive& ar, const unsigned int) {
    DEBUG_SERIALIZATION_POINT;
    ar& boost::serialization::base_object<Kernel::SessionRequestHandler>(*this);
    ar & csnd_hw;
    ar & volumes;
    ar & started;
    ar & fading;
    ar & stop_pending;
    ar & pause_pending;
    ar & fade_scheduled;
    ar & capture_units_in_use;
    ar & is_ticking;
    ar & initialized_clients;
    ar & frame;
    ar & frame_samples;
    ar & frame_start_ticks;
    ar & frame_length_ticks;
}
SERIALIZE_IMPL(CSND_SND)

void InstallInterfaces(Core::System& system) {
    auto& service_manager = system.ServiceManager();
    std::make_shared<CSND_SND>(system)->InstallAsService(service_manager);
}

} // namespace Service::CSND
