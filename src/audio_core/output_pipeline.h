// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include "audio_core/audio_types.h"
#include "audio_core/frame_buffers.h"
#include "audio_core/period_splicer.h"
#include "audio_core/speedup_lowpass.h"
#include "audio_core/stream_ramp.h"
#include "audio_core/stretch_gate.h"
#include "audio_core/time_stretch.h"
#include "common/common_types.h"
#include "common/ring_buffer.h"

namespace AudioCore {

/// Everything between the DSP's output and the sink's callback: the FIFO the emulation thread
/// fills, the time stretcher and the bypass around it, the handover cross-fade and the stream
/// ramp. Render() is the sink's callback minus the volume, which DspInterface::OutputCallback()
/// (audio_core/dsp_interface.cpp) applies after it. Push() is the only entry from another
/// thread; the stream and setting calls flip atomics the audio thread reads.
///
/// StretchGate (audio_core/stretch_gate.h) picks the mode; this class carries it out. In
/// Bypass, frames go FIFO -> stash -> PeriodSplicer::Cut() -> out, and the splicer trims any
/// depth above the fill target one period at a time. In Warming the same path runs through
/// PeriodSplicer::Insert(), so it falls behind real time, while every frame pulled is also fed
/// to the stretcher; once the stretcher's output covers the raw play point, Sync discards up to
/// that point and Stretch reads from the stretcher. Drain lowers the stretcher's target at a
/// bounded tempo, and Handover flushes it into the stash, where PeriodSplicer::JoinFlush()
/// closes the seam at the flush's end and trims the excess with it.
class OutputPipeline {
public:
    /// The most a single Render() serves at once; a sink asking for more is served in pieces.
    /// Also the FIFO's capacity.
    static constexpr std::size_t kMaxCallbackFrames = 0x2000;
    /// The servo's target backlog while stretching. Public so a test can check the
    /// handover band stays within what Stretch actually holds.
    static constexpr double kStretchTargetBacklog = 0.125; // seconds, master's servo target
    // Excess the splicer leaves alone. The beat's trough wanders by a few dozen frames from
    // cycle to cycle, and trimming to the exact target would chase it with a lone cut every
    // couple of seconds.
    static constexpr std::size_t kTrimSlack = 128;
    /// The most Bypass trims per callback on average, as a fraction of it: the excess a
    /// handover brings goes at a tenth of real time, what the drain ran at, rather than in
    /// a burst of cuts. A gate on cutting, not a bound on the cut: each cut is still a whole
    /// period of the material, and puts the credit into debt until the rate has paid for it.
    static constexpr double kTrimRate = 0.1;
    /// How far back from the flush's end the join may retreat, a join at a time, when the
    /// end is a WSOLA blend that matches nothing: past one overlap and the reference.
    static constexpr std::size_t kJoinRetreat = 512;

    /// What the last Render() did, for tests and the edge log.
    struct RenderStats {
        std::size_t written = 0;  // frames the source filled, before the ramp
        std::size_t consumed = 0; // source frames taken from the stash (Bypass and Warming)
        std::size_t cut = 0;      // frames the splicer removed
        std::size_t inserted = 0; // frames the splicer repeated
        std::size_t flushed = 0;  // frames the stretcher handed to the stash at Handover
        std::size_t joined = 0;   // frames dropped where the flushed frames met the FIFO's:
                                  // the fed tail's copy of what the flush played, exactly
                                  // when join_matched, else as estimated under a fade
        bool join_matched = false;
        std::size_t join_at = 0;  // where the flushed run ended, in output frames from the
                                  // start of this callback (it can lie in the next); 0 for
                                  // none. The join's blend is the kJoinFrames before it
        std::size_t replayed = 0; // frames of stretcher output a forced Sync could not skip
        bool silenced = false;    // the core had the stream down; nothing was rendered
        std::size_t depth = 0;    // fifo + stash at the start of the callback
        StretchGate::Edge edge = StretchGate::Edge::None;
    };

    OutputPipeline();

    /// A new sink: sets the stretcher's output rate and resets the stream. Not safe against a
    /// concurrent Render(); call before the sink's callback is installed.
    void SetOutputSampleRate(unsigned rate);
    /// Back to a fresh stream: empty, in Bypass, prefilling, opening on the ramp.
    void Reset();

    /// Emulation thread. Returns the frames accepted; the rest are dropped.
    std::size_t Push(const void* frames, std::size_t num_frames);
    /// Audio thread. Fills `out` completely.
    void Render(s16* out, std::size_t num_frames);

    void SetStretching(bool enable);
    /// The speed the frontend is asking for, from its frame limit: 1.0 at the default, the
    /// fast-forward factor while it is held, infinity with the limiter off. A hint only, for
    /// reseeding the servo when the request drops: the measured speed still governs, since the
    /// host may not reach what was asked. Any thread.
    void SetRequestedSpeed(double speed);
    void SetRamp(bool enable);
    /// Reference cutoff in Hz for the fast-forward low-pass, from the setting. The applied
    /// cutoff is this over the speed actually reached; kSpeedupLowPassOff and zero mean off.
    /// Any thread.
    void SetSpeedupLowPass(u16 reference);
    /// The core has stopped producing audio on purpose: end the stream on a ramp rather than
    /// wherever the waveform happens to be, and discard whatever it had already produced.
    /// Any thread.
    void StreamEnd();
    /// The core is producing again: the next frames ramp back in. Any thread.
    void StreamBegin();
    /// Bracket a jump the frontend makes in the game's state, a load or a reset, so the splice
    /// lands in silence: takes the stream down and waits, bounded, for the tail to reach the
    /// sink. Returns false and does nothing if the stream is already down, so the ramp back up
    /// stays with whatever took it down. Emulation thread.
    bool JumpBegin();
    void JumpEnd(bool ramped);

    /// Bypass's fill target, for the depth at its beat trough: kFillBursts video frames of
    /// audio plus one callback. One burst is what a late burst costs, the second is the slack
    /// a slowdown is detected within.
    std::size_t FillTarget(std::size_t num_frames) const;
    /// What the handover must put in the stash for Drain to hand over, at the least it can:
    /// at least the fill target, so the beat's trough stays above the engage depth, and at
    /// most one output batch over it. The content falls a tenth of a callback per callback
    /// in Drain, so the upper bound is crossed on the way down and is where the handover
    /// lands, and the excess the trim removes at kTrimRate is one batch plus the last
    /// round's offset: about a second at 512-frame callbacks, longer at large ones, whose
    /// 32-callback low-water window takes longer to see the new depth.
    std::size_t HandoverLow(std::size_t num_frames) const;
    std::size_t HandoverHigh(std::size_t num_frames) const;
    /// Below this depth Bypass engages the stretcher: one callback plus half a burst, so an
    /// engage on a real slowdown still has a callback in hand, while a burst arriving late at
    /// the trough of a healthy stream stays above it.
    std::size_t EngageDepth(std::size_t num_frames) const;
    /// Where prefill lets Bypass start playing: the fill target plus one callback, since the
    /// beat between bursts and callbacks brings the depth back down by a callback at its
    /// trough, and the trough is what the fill target is for.
    std::size_t PrefillTarget(std::size_t num_frames) const;

    // Audio-thread state, read between Render() calls: for tests and logging.
    StretchGate::Mode CurrentMode() const {
        return gate.CurrentMode();
    }
    RenderStats LastRender() const {
        return last;
    }

private:
    void RenderChunk(s16* out, std::size_t num_frames);
    std::size_t RenderBypass(s16* out, std::size_t num_frames, RenderStats& stats);
    std::size_t RenderWarming(s16* out, std::size_t num_frames, RenderStats& stats);
    std::size_t RenderStretch(s16* out, std::size_t num_frames);
    void Engage();
    void Sync(RenderStats& stats);
    void Handover(RenderStats& stats);
    void EnterStretch();
    void EnterDrain(std::size_t num_frames);
    /// Where the servo should sit right now: the fast estimate, capped by the request.
    double ServoSeed() const;
    void DiscardPending();
    std::size_t WarmDiscardNeeded() const;
    std::size_t Excess(std::size_t num_frames) const;
    /// The least a handover now would put in the stash, what the handover band is judged
    /// on: the content less the last round's offset, a seek window at most.
    std::size_t FlushYield() const;
    /// Arms the fade to begin `offset` frames into the output that follows, from the level
    /// just before it; at 0, from the last frame handed on.
    void ArmSeamFade(std::size_t offset = 0);
    void ApplySpeedupLowPass(s16* out, std::size_t num_frames);
    void ApplySeamFade(s16* buffer, std::size_t num_frames);

    static constexpr double kSpeedTimeConstant = 0.3; // seconds, the fast estimate
    // The most one callback moves the fast estimate. Callbacks over a tenth of the time
    // constant (OpenAL Soft's chunks, cubeb at a high device minimum) would otherwise
    // read each one's whole burst count, three or four, as the speed.
    static constexpr double kSpeedAlphaMax = 0.1;
    // The slow estimate's window. Its precision is one burst over its length, since a burst
    // either lands inside the window or not: ten seconds gives 0.17%, enough to tell a host
    // at 99.5%, which Bypass cannot serve, from one at full speed. Two seconds could not.
    static constexpr std::size_t kSpeedWindowFrames = 327680;
    // Callbacks the ring holds. A ring full of real entries counts as settled too, so a
    // sink with callbacks under kSpeedWindowFrames / kSpeedWindowMax frames (80) still
    // reaches Drain, on a shorter window; under about 67 that window is too short to hold
    // the band steady against the beat, and no shipped sink asks for so little.
    static constexpr std::size_t kSpeedWindowMax = 4096;
    static constexpr std::size_t kSpeedWindowSettle = 32768; // frames before it is trusted
    static constexpr double kStretchMinRatio = 0.05;
    static constexpr std::size_t kFillBursts = 2;
    // Beyond this fraction either side of the fast speed estimate, the servo is reseeded from
    // it: its low-pass takes most of a second to follow a step, and a fast-forward released
    // at tempo 3 runs the backlog dry long before then.
    static constexpr double kServoResyncBand = 0.25;
    // Callbacks the low-water window spans. The emulator's 60 Hz bursts beat against the
    // sink's callbacks, so the depth sawtooths over a cycle of about 15 callbacks for any
    // power-of-two callback size; a window shorter than that sees only the crest.
    // The low-water window: 32 callbacks up to 512 frames each, which is two beat cycles
    // at 512 and more at smaller sizes, and the same half second above that (16 at 1024, 8
    // at 2048), where 32 callbacks were two seconds the trim waited out after every
    // handover. The beat cycle is about four callbacks at 2048 and eight at 1024.
    static constexpr std::size_t kLowWaterFrames = 16384;
    static constexpr std::size_t kLowWaterWindowMax = 32;
    static constexpr std::size_t kLowWaterWindowMin = 4;
    static std::size_t LowWaterEntries(std::size_t num_frames) {
        return std::clamp((kLowWaterFrames + num_frames - 1) / num_frames, kLowWaterWindowMin,
                          kLowWaterWindowMax);
    }

    // Filled by DspInterface::OutputFrame() on the emulation thread, drained here.
    Common::RingBuffer<s16, kMaxCallbackFrames, 2> fifo;
    FrameStash stash;
    // Frames the raw path emitted, post-splice and pre-fade: what the stretcher is primed from.
    FrameHistory history;
    TimeStretcher time_stretcher;
    StretchGate gate;
    PeriodSplicer splicer;
    std::array<s16, kMaxCallbackFrames * 2> pop_scratch{};
    std::array<s16, FrameStash::kCapacity * 2> flush_scratch{};

    std::atomic<bool> enable_stretching{false};
    std::atomic<double> requested_speed{1.0};
    std::atomic<bool> enable_ramp{true};
    std::atomic<u16> speedup_lowpass_reference{kSpeedupLowPassOff};
    std::atomic<bool> core_silenced{false};

    // Arrival over request: the speed the emulation actually runs at, as the audio thread
    // sees it. fifo_left is the FIFO's depth after the previous callback's pops, so the
    // difference at the next is what arrived in between. Arrivals come a video frame at a
    // time and beat against the callbacks, so `speed` is a ratio of sums over ten seconds,
    // within a fifth of a percent of the truth at any callback size, and `speed_fast` a
    // short EMA that answers in a tenth of a second but dips to ~0.95 on every burst gap.
    double speed = 1.0;
    bool speed_settled = false;
    bool stream_seen = false;      // whether a full interval of arrivals has been seen yet
    std::size_t speed_entries = 0; // real entries in the ring, up to kSpeedWindowMax
    double speed_fast = 1.0;
    std::array<std::size_t, kSpeedWindowMax> arrivals{};
    std::array<std::size_t, kSpeedWindowMax> requests{};
    std::size_t speed_pos = 0;
    std::size_t fifo_left = 0;
    // Bypass emits nothing until the buffer first reaches the fill target: at reset, and after
    // each silence.
    bool prefilling = true;
    // Depth at the start of each of the last LowWaterEntries() callbacks: excess is judged
    // on the window's minimum, since arrivals come a burst at a time and an instantaneous
    // depth would trigger cuts at the right average depth. Each cut lowers every entry by
    // what it removed, so the minimum stays what the trough would be now rather than what
    // it was.
    std::array<std::size_t, kLowWaterWindowMax> depth_window{};
    // Whether the trim may cut now: grows by kTrimRate of each Bypass callback, capped at
    // two minimum periods (or one callback's accrual plus one, at large callbacks) so a
    // stale window cannot prepay a burst, and each cut is charged in full, into debt for a
    // long period. Audio thread only.
    s64 trim_credit = 0;
    std::size_t window_pos = 0;
    // Flushed frames still at the front of the stash after a Handover. They continue the
    // stretcher's output exactly; the seam is where they run out and the FIFO's frames take
    // over, off by the flush's count error, and PeriodSplicer::JoinFlush() closes it there.
    std::size_t flush_left = 0;
    std::size_t flush_tail = 0; // fed-tail frames appended after the flush at Handover
    // Warm-up: source frames the raw path consumed since Engage, and how far the stretcher's
    // next output frame sits behind the frame the history ended on (history fed minus output
    // dropped at priming).
    std::size_t warm_raw_pos = 0;
    s64 warm_lag = 0;
    RenderStats last{};

    // Cross-fade across a seam, in output frames: the Sync switch, and a flush's end when the
    // matched join could not be made. Kept short: both sides are the same material at
    // different points, so a long overlap is heard for itself. The gain at its midpoint:
    // lower attenuates the discontinuity the seam carries, at the cost of a deeper notch.
    // Audio thread only.
    static constexpr unsigned kSeamFadeFrames = 256;
    static constexpr float kSeamDipGain = 0.15f;
    unsigned fade_out_frames = 0;
    std::size_t fade_delay = 0;
    std::array<float, 2> fade_out_from{};
    // Last frame handed on before the volume, normalized: the level a seam fades from.
    std::array<float, 2> fade_last_out{};

    // Ends the stream on a ramp and brings it back on one, on the last buffer before the sink.
    // Audio thread only; core_silenced is how the other threads reach it.
    StreamRamp ramp;
    // Whether the ramp ran on the previous callback, so it can be dropped on the edge rather
    // than frozen mid-tail. Audio thread only.
    bool ramp_was_enabled = true;
    SpeedupLowPass low_pass;
    bool lowpass_was_active = false;
    double sample_rate = native_sample_rate;
    // Whether the last callback found the stream settled: down, with its tail fully out. What
    // JumpBegin() waits on, since it cannot read the ramp from its own thread. A fresh stream
    // is settled, having nothing to take down. The audio thread signals the condition variable
    // on the rising edge alone; the mutex is the waiter's, and the callback never takes it.
    std::atomic<bool> stream_settled{true};
    // The last callback's size, so JumpBegin() can wait at least two callbacks for the tail
    // to reach the sink whatever the sink's callback size.
    std::atomic<std::size_t> last_callback_frames{0};
    std::mutex settled_mutex;
    std::condition_variable settled_cv;
};

} // namespace AudioCore
