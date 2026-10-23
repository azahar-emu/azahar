// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <array>
#include <span>
#include <boost/serialization/access.hpp>
#include "common/common_funcs.h"
#include "common/common_types.h"

namespace Memory {
class MemorySystem;
}

namespace AudioCore {

/// Bits of a CSND channel CNT register.
enum class CsndCnt : u16 {
    None = 0,

    LinearInterpolation = 1 << 6, // Linear interpolation on each two samples (0=Disable, 1=Enable)
    Hold = 1 << 7,                // (0=Normal, 1=Hold last sample after one-shot sound)
    Run = 1 << 14,                // Run DMA (0=Pause the channel, 1=Run the channel)
    Enable = 1 << 15,             // Start/Stop control (0=Stop, 1=Start/Busy)
};
DECLARE_ENUM_FLAG_OPERATORS(CsndCnt)

/// Bits of a CSND capture unit CNT register.
enum class CsndCaptureCnt : u16 {
    None = 0,

    OneShot = 1 << 0,
    Pcm8 = 1 << 1,
    Unknown2 = 1 << 2,
    Enable = 1 << 15,
};
DECLARE_ENUM_FLAG_OPERATORS(CsndCaptureCnt)

/**
 * Register level model of the CSND sound hardware (0x10103000). It has 32 channels that play
 * PCM8, PCM16 or IMA-ADPCM samples from physical memory (or a PSG square wave / noise, not
 * emulated yet), and outputs the stereo mix at the I2S rate, the same rate as the DSP.
 * On hardware the mix goes to the codec chip on its own I2S line, where it is mixed with the DSP
 * output.
 *
 * Render() produces samples and advances the channels, the caller decides
 * when. Writes to the registers take effect from the next rendered sample.
 */
class Csnd {
public:
    static constexpr u32 NumChannels = 32;
    static constexpr u32 NumCaptureUnits = 2;

    static constexpr u32 CntLoopModeShift = 10;
    static constexpr u32 CntLoopModeMask = 0x3 << CntLoopModeShift;

    static constexpr u32 CntFormatShift = 12;
    static constexpr u32 CntFormatMask = 0x3 << CntFormatShift;

    /// Reloads the IMA-ADPCM state at the loop address.
    static constexpr u32 LoopAdpcmReload = 1u << 31;

    enum class Format : u32 {
        Pcm8 = 0,
        Pcm16 = 1,
        ImaAdpcm = 2,
        PsgNoise = 3, ///< Not emulated yet
    };

    enum class LoopMode : u32 {
        Manual = 0,         ///< Plays ignoring the size
        Normal = 1,         ///< At the end, restarts from the loop address, reloading the size
        OneShot = 2,        ///< Stops at the end
        NormalNoReload = 3, ///< Like Normal, but latches the size at start
    };

    explicit Csnd(Memory::MemorySystem& memory);
    ~Csnd();

    /// Get CNT bits of a format.
    static constexpr CsndCnt CntFormat(Format format) {
        return static_cast<CsndCnt>(static_cast<u16>(format) << CntFormatShift);
    }
    /// Get CNT bits of a loop mode.
    static constexpr CsndCnt CntLoopMode(LoopMode loop_mode) {
        return static_cast<CsndCnt>(static_cast<u16>(loop_mode) << CntLoopModeShift);
    }

    /// Get format from CNT bits.
    static constexpr Format GetFormat(CsndCnt cnt) {
        return static_cast<Format>((static_cast<u32>(cnt) & CntFormatMask) >> CntFormatShift);
    }
    // Get loop mode from CNT bits.
    static constexpr LoopMode GetLoopMode(CsndCnt cnt) {
        return static_cast<LoopMode>((static_cast<u32>(cnt) & CntLoopModeMask) >> CntLoopModeShift);
    }

    CsndCnt GetCnt(u32 channel) const;
    void SetCnt(u32 channel, CsndCnt value);

    void SetTimer(u32 channel, u16 value);
    void SetVolume(u32 channel, u16 left, u16 right);
    void SetCaptureVolume(u32 channel, u16 left, u16 right);
    void SetStartAddress(u32 channel, u32 address);
    void SetSize(u32 channel, u32 size);
    void SetLoopAddress(u32 channel, u32 address);

    /// IMA-ADPCM state register. PCM16 value in bits 0-15, table index in bits 16-22.
    /// While the channel plays it reads back the decoder's current state.
    u32 GetAdpcmState(u32 channel) const;
    void SetAdpcmState(u32 channel, u32 value);

    /// Loop IMA-ADPCM state register. Bit 31 reloads it at the loop address.
    u32 GetLoopAdpcmState(u32 channel) const;
    void SetLoopAdpcmState(u32 channel, u32 value);

    // Capture registers. Stored only as capture is not emulated.
    CsndCaptureCnt GetCaptureCnt(u32 unit) const;
    void SetCaptureCnt(u32 unit, CsndCaptureCnt value);
    void SetCaptureTimer(u32 unit, u16 value);
    void SetCaptureAddress(u32 unit, u32 address, u32 size);

    // Global registers.
    void SetMasterVolume(u16 value);
    u32 GetChannelMaskRegister() const;
    void SetChannelMaskRegister(u32 value);
    void SetCaptureMaskRegister(u8 value);

    /// True when at least one channel is enabled.
    bool IsAnyChannelBusy() const;

    /// Renders samples into out, advancing every channel.
    void Render(std::span<std::array<s16, 2>> out);

private:
    struct Channel {
        // Registers
        CsndCnt cnt = CsndCnt::None;
        u16 timer = 0;
        u16 volume_left = 0;
        u16 volume_right = 0;
        u16 capture_volume_left = 0;
        u16 capture_volume_right = 0;
        u32 start_address = 0;
        u32 size = 0;
        u32 loop_address = 0;
        u32 adpcm_state = 0;
        u32 loop_adpcm_state = 0;

        // Playback state
        u32 block_address = 0; ///< Start of the block being played
        u32 block_size = 0;    ///< Size latched for the block being played
        u32 position = 0;      ///< Byte offset in the block
        bool high_nibble = false;
        bool holding = false; ///< Ended a one-shot block with Hold set, outputs its last sample
        u32 counter = 0;      ///< Timer, in units of 1/2048 output samples
        s16 previous_sample = 0;
        s16 current_sample = 0;
        s16 adpcm_predictor = 0;
        u8 adpcm_index = 0;

        template <class Archive>
        void serialize(Archive& ar, const unsigned int);
        friend class boost::serialization::access;
    };

    struct CaptureUnit {
        CsndCaptureCnt cnt = CsndCaptureCnt::None;
        u16 timer = 0;
        u32 address = 0;
        u32 size = 0;

        template <class Archive>
        void serialize(Archive& ar, const unsigned int);
        friend class boost::serialization::access;
    };

    void StartChannel(u32 index);
    void StopChannel(Channel& channel);
    void Step(Channel& channel);

    s16 FetchSample(Channel& channel);
    s16 Interpolated(const Channel& channel) const;
    bool ReachedBlockEnd(const Channel& channel) const;

    Memory::MemorySystem& memory;

    std::array<Channel, NumChannels> channels{};
    std::array<CaptureUnit, NumCaptureUnits> capture_units{};
    u16 master_volume = 0x8000;
    u32 channel_mask_register = 0;
    u8 capture_mask_register = 0;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int);
    friend class boost::serialization::access;
};

} // namespace AudioCore
