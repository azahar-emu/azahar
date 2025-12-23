// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <boost/serialization/array.hpp>
#include "audio_core/audio_types.h"
#include "audio_core/hle/shared_memory.h"

namespace AudioCore::HLE {

class Mixers final {
public:
    Mixers() {
        Reset();
    }

    void Reset();

    void Sleep();
    void Wakeup();

    DspStatus Tick(DspConfiguration& config, const IntermediateMixSamples& read_samples,
                   IntermediateMixSamples& write_samples, const std::array<QuadFrame32, 3>& input);

    StereoFrame16 GetOutput() const {
        return current_frame;
    }

private:
    StereoFrame16 current_frame = {};
    StereoFrame16 backup_frame = {}; // TODO(PabloMK7): Check if we actually need this

    using OutputFormat = DspConfiguration::OutputFormat;

    struct MixerState {
        std::array<float, 3> intermediate_mixer_volume = {};

        std::array<bool, 2> aux_bus_enable = {};
        std::array<QuadFrame32, 3> intermediate_mix_buffer = {};

        OutputFormat output_format = OutputFormat::Stereo;

        template <class Archive>
        void serialize(Archive& ar, const unsigned int) {
            ar & intermediate_mixer_volume;
            ar & aux_bus_enable;
            ar & intermediate_mix_buffer;
            ar & output_format;
        }
    };

    MixerState state;
    MixerState backup_state;

    /// INTERNAL: Update our internal state based on the current config.
    void ParseConfig(DspConfiguration& config);
    /// INTERNAL: Read samples from shared memory that have been modified by the ARM11.
    void AuxReturn(const IntermediateMixSamples& read_samples);
    /// INTERNAL: Write samples to shared memory for the ARM11 to modify.
    void AuxSend(IntermediateMixSamples& write_samples, const std::array<QuadFrame32, 3>& input);
    /// INTERNAL: Mix current_frame.
    void MixCurrentFrame();
    /// INTERNAL: Downmix from quadraphonic to stereo based on status.output_format and accumulate
    /// into current_frame.
    void DownmixAndMixIntoCurrentFrame(float gain, const QuadFrame32& samples);
    /// INTERNAL: Generate DspStatus based on internal state.
    DspStatus GetCurrentStatus() const;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar & current_frame;
        ar & backup_frame;
        ar & state;
        ar & backup_state;
    }
    friend class boost::serialization::access;
};

} // namespace AudioCore::HLE
