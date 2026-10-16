// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <algorithm>
#include <boost/serialization/array.hpp>
#include "audio_core/csnd.h"
#include "common/archives.h"
#include "core/memory.h"

namespace AudioCore {

namespace {

/// The channel timers tick at 67027964 Hz (the ARM11 base clock / 4) and the output runs at
/// 67027964 / 2048 Hz (the native sample rate), so one output sample is 2048 timer ticks.
constexpr u32 TimerTicksPerOutputSample = 2048;
/// Shortest period the hardware supports, faster timer values hang it.
constexpr u32 MinTimerPeriod = 0x42;

constexpr u16 MaxVolume = 0x8000;

constexpr std::array<s16, 89> ImaStepTable{
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
constexpr std::array<s8, 8> ImaIndexTable{-1, -1, -1, -1, 2, 4, 6, 8};

u16 ClampVolume(u16 volume) {
    return std::min(volume, MaxVolume);
}

} // namespace

Csnd::Csnd(Memory::MemorySystem& memory_) : memory(memory_) {}

Csnd::~Csnd() = default;

CsndCnt Csnd::GetCnt(u32 channel) const {
    return channels[channel].cnt;
}

void Csnd::SetCnt(u32 channel, CsndCnt value) {
    Channel& ch = channels[channel];
    const bool was_enabled = True(ch.cnt & CsndCnt::Enable);
    ch.cnt = value;
    if (!was_enabled && True(value & CsndCnt::Enable)) {
        StartChannel(channel);
    } else if (was_enabled && False(value & CsndCnt::Enable)) {
        StopChannel(ch);
        ch.holding = false;
    }
    if (False(value & CsndCnt::Hold)) {
        ch.holding = false;
    }
}

void Csnd::SetTimer(u32 channel, u16 value) {
    channels[channel].timer = value;
}

void Csnd::SetVolume(u32 channel, u16 left, u16 right) {
    channels[channel].volume_left = ClampVolume(left);
    channels[channel].volume_right = ClampVolume(right);
}

void Csnd::SetCaptureVolume(u32 channel, u16 left, u16 right) {
    channels[channel].capture_volume_left = ClampVolume(left);
    channels[channel].capture_volume_right = ClampVolume(right);
}

void Csnd::SetStartAddress(u32 channel, u32 address) {
    channels[channel].start_address = address;
}

void Csnd::SetSize(u32 channel, u32 size) {
    channels[channel].size = size & 0x7FFFFFF;
}

void Csnd::SetLoopAddress(u32 channel, u32 address) {
    channels[channel].loop_address = address;
}

u32 Csnd::GetAdpcmState(u32 channel) const {
    const Channel& ch = channels[channel];
    if (True(ch.cnt & CsndCnt::Enable)) {
        return static_cast<u16>(ch.adpcm_predictor) | (static_cast<u32>(ch.adpcm_index) << 16);
    }
    return ch.adpcm_state;
}

void Csnd::SetAdpcmState(u32 channel, u32 value) {
    channels[channel].adpcm_state = value;
}

u32 Csnd::GetLoopAdpcmState(u32 channel) const {
    return channels[channel].loop_adpcm_state;
}

void Csnd::SetLoopAdpcmState(u32 channel, u32 value) {
    channels[channel].loop_adpcm_state = value;
}

CsndCaptureCnt Csnd::GetCaptureCnt(u32 unit) const {
    return capture_units[unit].cnt;
}

void Csnd::SetCaptureCnt(u32 unit, CsndCaptureCnt value) {
    capture_units[unit].cnt = value;
}

void Csnd::SetCaptureTimer(u32 unit, u16 value) {
    capture_units[unit].timer = value;
}

void Csnd::SetCaptureAddress(u32 unit, u32 address, u32 size) {
    capture_units[unit].address = address;
    capture_units[unit].size = size;
}

void Csnd::SetMasterVolume(u16 value) {
    master_volume = ClampVolume(value);
}

u32 Csnd::GetChannelMaskRegister() const {
    return channel_mask_register;
}

void Csnd::SetChannelMaskRegister(u32 value) {
    channel_mask_register = value;
}

void Csnd::SetCaptureMaskRegister(u8 value) {
    capture_mask_register = value;
}

bool Csnd::IsAnyChannelBusy() const {
    return std::any_of(channels.begin(), channels.end(),
                       [](const Channel& ch) { return True(ch.cnt & CsndCnt::Enable); });
}

void Csnd::StartChannel(u32 index) {
    Channel& ch = channels[index];
    ch.block_address = ch.start_address;
    ch.block_size = ch.size;
    ch.position = 0;
    ch.high_nibble = false;
    ch.holding = false;
    ch.counter = 0;
    ch.previous_sample = 0;
    ch.current_sample = 0;
    ch.adpcm_predictor = static_cast<s16>(ch.adpcm_state & 0xFFFF);
    ch.adpcm_index = std::min<u8>((ch.adpcm_state >> 16) & 0x7F, 88);
}

void Csnd::StopChannel(Channel& ch) {
    // Keep the decoder state consistent after the channel stops.
    ch.adpcm_state =
        static_cast<u16>(ch.adpcm_predictor) | (static_cast<u32>(ch.adpcm_index) << 16);
    ch.cnt &= ~CsndCnt::Enable;
}

bool Csnd::ReachedBlockEnd(const Channel& ch) const {
    if (GetFormat(ch.cnt) == Format::PsgNoise || GetLoopMode(ch.cnt) == LoopMode::Manual) {
        return false;
    }
    return ch.position >= ch.block_size;
}

void Csnd::Step(Channel& ch) {
    if (ReachedBlockEnd(ch)) {
        const LoopMode loop_mode = GetLoopMode(ch.cnt);
        if (loop_mode == LoopMode::OneShot) {
            StopChannel(ch);
            ch.holding = True(ch.cnt & CsndCnt::Hold);
            return;
        }
        ch.block_address = ch.loop_address;
        if (loop_mode == LoopMode::Normal) {
            ch.block_size = ch.size;
        }
        ch.position = 0;
        ch.high_nibble = false;
        if (ch.loop_adpcm_state & LoopAdpcmReload) {
            ch.adpcm_predictor = static_cast<s16>(ch.loop_adpcm_state & 0xFFFF);
            ch.adpcm_index = std::min<u8>((ch.loop_adpcm_state >> 16) & 0x7F, 88);
        }
        if (ch.block_size == 0) {
            // An empty looped block would never advance.
            StopChannel(ch);
            return;
        }
    }

    ch.previous_sample = ch.current_sample;
    ch.current_sample = FetchSample(ch);
}

s16 Csnd::FetchSample(Channel& ch) {
    const u32 address = ch.block_address + ch.position;
    switch (GetFormat(ch.cnt)) {
    case Format::Pcm8: {
        const u8* data = memory.GetPhysicalPointer(address);
        ch.position++;
        return data ? static_cast<s16>(static_cast<s8>(*data) * 256) : 0;
    }
    case Format::Pcm16: {
        // At physical memory boundaries, the high byte may end outside of the
        // memory region, sacrifice that single OOB read edge case for speed.
        const u8* low = memory.GetPhysicalPointer(address);
        const u8* high = low + 1;
        ch.position += 2;
        if (!low) {
            return 0;
        }
        return static_cast<s16>(*low | (*high << 8));
    }
    case Format::ImaAdpcm: {
        const u8* data = memory.GetPhysicalPointer(address);
        const u8 byte = data ? *data : 0;
        const u8 nibble = ch.high_nibble ? (byte >> 4) : (byte & 0xF);
        if (ch.high_nibble) {
            ch.position++;
        }
        ch.high_nibble = !ch.high_nibble;

        const s32 step = ImaStepTable[ch.adpcm_index];
        s32 diff = step >> 3;
        if (nibble & 1) {
            diff += step >> 2;
        }
        if (nibble & 2) {
            diff += step >> 1;
        }
        if (nibble & 4) {
            diff += step;
        }
        s32 predictor = ch.adpcm_predictor;
        if (nibble & 8) {
            predictor = std::max(predictor - diff, -0x7FFF);
        } else {
            predictor = std::min(predictor + diff, 0x7FFF);
        }
        ch.adpcm_predictor = static_cast<s16>(predictor);
        ch.adpcm_index =
            static_cast<u8>(std::clamp(ch.adpcm_index + ImaIndexTable[nibble & 7], 0, 88));
        return ch.adpcm_predictor;
    }
    case Format::PsgNoise:
        // TODO(PabloMK7): PSG square waves (channels 8-13) and noise (channels 14-15).
        return 0;
    }
    return 0;
}

s16 Csnd::Interpolated(const Channel& ch) const {
    if (False(ch.cnt & CsndCnt::LinearInterpolation)) {
        return ch.current_sample;
    }
    const u32 period = std::max<u32>(0x10000 - ch.timer, MinTimerPeriod);
    const s32 delta = ch.current_sample - ch.previous_sample;
    return static_cast<s16>(ch.previous_sample +
                            delta * static_cast<s32>(ch.counter) / static_cast<s32>(period));
}

void Csnd::Render(std::span<std::array<s16, 2>> out) {
    for (auto& frame : out) {
        s32 left = 0;
        s32 right = 0;
        for (u32 i = 0; i < NumChannels; i++) {
            Channel& ch = channels[i];
            if (False(ch.cnt & CsndCnt::Enable)) {
                if (ch.holding) {
                    left += ch.current_sample * ch.volume_left >> 15;
                    right += ch.current_sample * ch.volume_right >> 15;
                }
                continue;
            }
            if (True(ch.cnt & CsndCnt::Run)) {
                const u32 period = std::max<u32>(0x10000 - ch.timer, MinTimerPeriod);
                ch.counter += TimerTicksPerOutputSample;
                while (ch.counter >= period && True(ch.cnt & CsndCnt::Enable)) {
                    ch.counter -= period;
                    Step(ch);
                }
                if (False(ch.cnt & CsndCnt::Enable)) {
                    if (ch.holding) {
                        left += ch.current_sample * ch.volume_left >> 15;
                        right += ch.current_sample * ch.volume_right >> 15;
                    }
                    continue;
                }
            }
            const s32 sample = Interpolated(ch);
            left += sample * ch.volume_left >> 15;
            right += sample * ch.volume_right >> 15;
        }

        const s64 mixed_left = static_cast<s64>(left) * master_volume >> 15;
        const s64 mixed_right = static_cast<s64>(right) * master_volume >> 15;
        frame[0] = static_cast<s16>(std::clamp<s64>(mixed_left, -0x8000, 0x7FFF));
        frame[1] = static_cast<s16>(std::clamp<s64>(mixed_right, -0x8000, 0x7FFF));
    }
}

template <class Archive>
void Csnd::Channel::serialize(Archive& ar, const unsigned int) {
    ar & cnt;
    ar & timer;
    ar & volume_left;
    ar & volume_right;
    ar & capture_volume_left;
    ar & capture_volume_right;
    ar & start_address;
    ar & size;
    ar & loop_address;
    ar & adpcm_state;
    ar & loop_adpcm_state;
    ar & block_address;
    ar & block_size;
    ar & position;
    ar & high_nibble;
    ar & holding;
    ar & counter;
    ar & previous_sample;
    ar & current_sample;
    ar & adpcm_predictor;
    ar & adpcm_index;
}

template <class Archive>
void Csnd::CaptureUnit::serialize(Archive& ar, const unsigned int) {
    ar & cnt;
    ar & timer;
    ar & address;
    ar & size;
}

template <class Archive>
void Csnd::serialize(Archive& ar, const unsigned int) {
    ar & channels;
    ar & capture_units;
    ar & master_volume;
    ar & channel_mask_register;
    ar & capture_mask_register;
}
SERIALIZE_IMPL(Csnd)

} // namespace AudioCore
