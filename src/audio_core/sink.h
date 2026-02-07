// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include "audio_types.h"

namespace AudioCore {

constexpr char auto_device_name[] = "Auto";

/**
 * This class is an interface for an audio sink. An audio sink accepts samples in stereo signed
 * PCM16 format to be output. Sinks *do not* handle resampling and expect the correct sample rate.
 * They are dumb outputs.
 */
class Sink {
public:
    virtual ~Sink() = default;

    /// The native rate of this sink. The sink expects to be fed samples that respect this.
    /// (Units: samples/sec)
    virtual unsigned int GetNativeSampleRate() const = 0;

    /**
     * Set callback for samples
     * @param samples Samples in interleaved stereo PCM16 format.
     * @param sample_count Number of samples.
     */
    virtual void SetCallback(std::function<void(s16*, std::size_t)> cb) = 0;

    /**
     * Override and set this to true if the sink wants audio data submitted
     * immediately rather than requesting audio on demand
     * @return true if audio data should be pushed to the sink
     */
    virtual bool ImmediateSubmission() {
        return false;
    }

    /**
     * Push audio samples directly to the sink, bypassing the FIFO.
     * Only called when ImmediateSubmission() returns true.
     * @param data Pointer to stereo PCM16 samples (each sample is L+R pair)
     * @param num_samples Number of stereo samples
     */
    virtual void PushSamples(const void* data, std::size_t num_samples) {}
};

} // namespace AudioCore
