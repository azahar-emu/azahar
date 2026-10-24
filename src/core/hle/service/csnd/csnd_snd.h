// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <array>
#include <bitset>
#include <memory>
#include <boost/serialization/array.hpp>
#include <boost/serialization/bitset.hpp>
#include <boost/serialization/shared_ptr.hpp>
#include "audio_core/audio_types.h"
#include "audio_core/csnd.h"
#include "common/archives.h"
#include "core/hle/kernel/mutex.h"
#include "core/hle/kernel/shared_memory.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
struct TimingEventType;
} // namespace Core

namespace Service::CSND {

struct Type0Command;

/// State the CSND module keeps for each client session.
struct ClientData : public Kernel::SessionRequestHandler::SessionDataBase {
    std::shared_ptr<Kernel::Mutex> mutex;
    std::shared_ptr<Kernel::SharedMemory> shared_memory;
    u32 master_state_offset = 0;
    u32 channel_state_offset = 0;
    u32 capture_state_offset = 0;
    u32 direct_sound_offset = 0;
    u32 channel_mask = 0;
    u8 capture_mask = 0;

private:
    template <class Archive>
    void serialize(Archive& ar, const unsigned int);
    friend class boost::serialization::access;
};

class CSND_SND final : public ServiceFramework<CSND_SND, ClientData> {
public:
    explicit CSND_SND(Core::System& system);
    ~CSND_SND();

    void ClientDisconnected(std::shared_ptr<Kernel::ServerSession> server_session) override;

private:
    /**
     * CSND_SND::Initialize service function
     *  Inputs:
     *      0 : Header Code[0x00010140]
     *      1 : Shared memory block size, for mem-block creation
     *      2 : offset to master state located in the shared-memory, region size=8
     *      3 : offset to channel state located in the shared-memory, region size=12*num_channels
     *      4 : offset to capture state located in the shared-memory, region size=8*num_captures
     *      5 : offset to the direct sound structure located in the shared-memory.
     *  Outputs:
     *      1 : Result of function, 0 on success, otherwise error code
     *      2 : Handle-list header
     *      3 : Mutex handle
     *      4 : Shared memory block handle
     */
    void Initialize(Kernel::HLERequestContext& ctx);

    /**
     * CSND_SND::Shutdown service function
     *  Inputs:
     *      0 : Header Code[0x00020000]
     *  Outputs:
     *      1 : Result of function, 0 on success, otherwise error code
     */
    void Shutdown(Kernel::HLERequestContext& ctx);

    /**
     * CSND_SND::ExecuteCommands service function
     *  Inputs:
     *      0 : Header Code[0x00030040]
     *      1 : Command offset in shared memory.
     *  Outputs:
     *      1 : Result of function, 0 on success, otherwise error code
     */
    void ExecuteCommands(Kernel::HLERequestContext& ctx);

    /**
     * CSND_SND::AcquireSoundChannels service function
     *  Inputs:
     *      0 : Header Code[0x00050000]
     *  Outputs:
     *      1 : Result of function, 0 on success, otherwise error code
     *      2 : Available channel bit mask
     */
    void AcquireSoundChannels(Kernel::HLERequestContext& ctx);

    /**
     * CSND_SND::ReleaseSoundChannels service function
     *  Inputs:
     *      0 : Header Code[0x00060000]
     *  Outputs:
     *      1 : Result of function, 0 on success, otherwise error code
     */
    void ReleaseSoundChannels(Kernel::HLERequestContext& ctx);

    /**
     * CSND_SND::AcquireCapUnit service function
     *     This function tries to acquire one capture device (max: 2).
     *     Returns index of which capture device was acquired.
     *  Inputs:
     *      0 : Header Code[0x00070000]
     *  Outputs:
     *      1 : Result of function, 0 on success, otherwise error code
     *      2 : Capture Unit
     */
    void AcquireCapUnit(Kernel::HLERequestContext& ctx);

    /**
     * CSND_SND::ReleaseCapUnit service function
     *  Inputs:
     *      0 : Header Code[0x00080040]
     *      1 : Capture Unit
     *  Outputs:
     *      1 : Result of function, 0 on success, otherwise error code
     */
    void ReleaseCapUnit(Kernel::HLERequestContext& ctx);

    /**
     * CSND_SND::FlushDataCache service function
     *
     * This Function is a no-op, We aren't emulating the CPU cache any time soon.
     *
     *  Inputs:
     *      0 : Header Code[0x00090082]
     *      1 : Address
     *      2 : Size
     *      3 : Value 0, some descriptor for the KProcess Handle
     *      4 : KProcess handle
     *  Outputs:
     *      1 : Result of function, 0 on success, otherwise error code
     */
    void FlushDataCache(Kernel::HLERequestContext& ctx);

    /**
     * CSND_SND::StoreDataCache service function
     *
     * This Function is a no-op, We aren't emulating the CPU cache any time soon.
     *
     *  Inputs:
     *      0 : Header Code[0x000A0082]
     *      1 : Address
     *      2 : Size
     *      3 : Value 0, some descriptor for the KProcess Handle
     *      4 : KProcess handle
     *  Outputs:
     *      1 : Result of function, 0 on success, otherwise error code
     */
    void StoreDataCache(Kernel::HLERequestContext& ctx);

    /**
     * CSND_SND::InvalidateDataCache service function
     *
     * This Function is a no-op, We aren't emulating the CPU cache any time soon.
     *
     *  Inputs:
     *      0 : Header Code[0x000B0082]
     *      1 : Address
     *      2 : Size
     *      3 : Value 0, some descriptor for the KProcess Handle
     *      4 : KProcess handle
     *  Outputs:
     *      1 : Result of function, 0 on success, otherwise error code
     */
    void InvalidateDataCache(Kernel::HLERequestContext& ctx);

    /**
     * CSND_SND::Reset service function
     *  Inputs:
     *      0 : Header Code[0x000C0000]
     *  Outputs:
     *      1 : Result of function, 0 on success, otherwise error code
     */
    void Reset(Kernel::HLERequestContext& ctx);

    void CacheOperation(Kernel::HLERequestContext& ctx);

    void ExecuteCommand(ClientData& client, const Type0Command& command);
    void UpdateState(ClientData& client);

    /// Renders the pending samples up to the current emulated time, so that the registers read and
    /// written respresent the proper values at that time.
    void UpdateHardware();

    bool IsRunning(u32 channel);
    void SetPlayStateRaw(u32 channel, bool play);
    void SetPlayState(u32 channel, bool play);
    void StopChannel(u32 channel);
    void SetRequestedVolume(u32 channel, u16 left, u16 right, bool start_fade);
    void SetTargetVolume(u32 channel, u16 left, u16 right, bool start_fade);
    void WriteCurrentVolume(u32 channel);
    void StartFade();
    void FadeCallback(std::uintptr_t step, s64 cycles_late);
    void StartTicking();
    void AudioTickCallback(s64 cycles_late);

    Core::System& system;

    AudioCore::Csnd csnd_hw;

    struct ChannelVolume {
        std::array<u16, 2> requested{};
        std::array<u16, 2> target{};
        std::array<s32, 2> current{};
        std::array<s32, 2> step{};

    private:
        template <class Archive>
        void serialize(Archive& ar, const unsigned int) {
            ar & requested;
            ar & target;
            ar & current;
            ar & step;
        }
        friend class boost::serialization::access;
    };
    std::array<ChannelVolume, AudioCore::Csnd::NumChannels> volumes{};
    using ChannelSet = std::bitset<AudioCore::Csnd::NumChannels>;
    ChannelSet started;       ///< Channels started through command 1
    ChannelSet fading;        ///< Channels fading towards their target
    ChannelSet stop_pending;  ///< Channels to stop once faded out
    ChannelSet pause_pending; ///< Channels to pause once faded out
    bool fade_scheduled = false;

    std::array<bool, AudioCore::Csnd::NumCaptureUnits> capture_units_in_use{};

    bool is_ticking = false;
    u32 initialized_clients = 0;
    AudioCore::StereoFrame16 frame{};
    u32 frame_samples = 0;
    s64 frame_start_ticks = 0;
    s64 frame_length_ticks = 0;

    Core::TimingEventType* audio_tick_event = nullptr;
    Core::TimingEventType* fade_event = nullptr;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int);
    friend class boost::serialization::access;
};

/// Initializes the CSND_SND Service
void InstallInterfaces(Core::System& system);

} // namespace Service::CSND

BOOST_CLASS_EXPORT_KEY(Service::CSND::CSND_SND)
BOOST_CLASS_EXPORT_KEY(Service::CSND::ClientData)
SERVICE_CONSTRUCT(Service::CSND::CSND_SND)
