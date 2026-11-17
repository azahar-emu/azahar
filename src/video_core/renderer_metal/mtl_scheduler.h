// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <cstdint>
#include <vector>
#include "video_core/renderer_metal/mtl_common.h"

namespace Metal {

class Instance;

/// Counters of what the GPU and the emulation thread spent time on, see TakeFrameStats.
struct GpuStats {
    std::atomic<u64> gpu_ns{};     ///< GPU execution time of the command buffers
    std::atomic<u64> present_ns{}; ///< Part of gpu_ns spent in the presentation (MetalFX, screens)
    std::atomic<u64> stall_ns{};   ///< Time the emulation thread waited for the GPU
    std::atomic<u32> stalls{};     ///< Number of those waits
    std::atomic<u32> draws{};      ///< Draw calls
    std::atomic<u32> frame_skipped{}; ///< Same, since the last presented frame
    std::atomic<u32> skipped{};    ///< Draws skipped because their pipeline was not built yet
    std::atomic<u32> frames{};     ///< Presented frames
    std::atomic<u32> render_passes{}; ///< Render encoders, each loads and stores its attachments
    std::atomic<u32> blit_passes{};   ///< Blit encoders, each one splits the render pass around it
    std::atomic<u32> feedback_copies{}; ///< Copies of a render target sampled while drawn to
    std::atomic<u64> drawable_ns{};   ///< Time spent waiting for a drawable from the layer
    // Intervals between frames reaching the display (see Scheduler::PresentDrawable)
    std::atomic<u32> present_intervals{};
    std::atomic<u64> present_interval_us{};    ///< Sum of the intervals
    std::atomic<u64> present_interval_sq_us{}; ///< Sum of their squares
    std::atomic<u32> present_late{};           ///< Intervals well above the frame time
    /// Where on the display's refresh a paced frame landed: how long after the time it was due
    /// it was shown, as an angle on the circle of one refresh (the sums of cos and sin, in
    /// millionths), see Scheduler::Flush.
    std::atomic<s64> phase_cos_micro{};
    std::atomic<s64> phase_sin_micro{};
    std::atomic<u32> phase_count{};
    std::atomic<u32> presented{};              ///< Frames that reached the screen
    std::atomic<u64> fg_ns{};      ///< GPU time of making frames (frame generation)
    std::atomic<u32> fg_made{};    ///< Frames made
    std::atomic<u32> fg_skipped{}; ///< Frames not made because the previous one was not done yet
    std::atomic<u64> cpu_ns{};     ///< CPU time of the emulation thread between presented frames
    std::atomic<u64> cpu_max_ns{}; ///< The longest of those since the last report
    std::atomic<u32> cpu_frames{};
};

/// Notes a hardware vertex shader draw for the statistics: 0 native, 1 corner by corner with
/// lighting, 2 corner by corner otherwise, and the vertices it draws.
void NoteHardwareDraw(int kind, u32 vertices);

/// Counts a frame of the main display that reached the screen at this time (the presentedTime
/// of its drawable), for the statistics of the intervals between frames.
void RecordPresentedTime(double time);

/// Turns the collecting of the statistics for the log (render passes, CPU time) on or off. Off,
/// the renderer does not do the work of keeping them.
void SetStatisticsEnabled(bool enabled);
bool StatisticsEnabled();

/// Turns even frame pacing on or off, see Scheduler::PresentDrawable.
void SetFramePacing(bool enabled);

/// Delay (milliseconds) the frame pacing adds so slow frames still meet their slot, 0 for none.
void SetPacingBuffer(int milliseconds);

/// The refresh rate of the display (60 or 120), for locking the pacing to its refreshes.
void SetPacingDisplayHz(int hz);

GpuStats& Stats();

/// What is asking for the data of a surface while it is read back from the GPU (every readback
/// makes the emulation thread wait for the GPU), to tell where the waits come from.
inline thread_local const char* g_readback_reason = nullptr;

struct ReadbackReason {
    explicit ReadbackReason(const char* reason) : previous{g_readback_reason} {
        g_readback_reason = reason;
    }
    ~ReadbackReason() {
        g_readback_reason = previous;
    }
    const char* previous;
};

/// Adds a readback (what was read, how long the thread waited, how many bytes) to the summary.
void RecordReadback(const std::string& what, double wait_ms, u64 bytes);

/// The readbacks since the last call, the ones that cost the most first, one line each. Empty
/// when there were none.
std::vector<std::string> TakeReadbackSummary(std::size_t max_lines);

/// What the render passes since the last call were (targets, why they ended, how many reopened
/// the target the previous pass used), one line each. Empty when there were none.
std::vector<std::string> TakeRenderPassSummary(std::size_t max_lines);

/// Describes the attachments of a render pass.
struct RenderTarget {
    id<MTLTexture> color = nil;
    u32 color_level = 0;
    u32 color_slice = 0;
    id<MTLTexture> depth = nil;
    u32 depth_level = 0;
    bool has_stencil = false;
    /// Render area size, the intersection of the attachment sizes.
    u32 width = 0;
    u32 height = 0;

    bool operator==(const RenderTarget& other) const noexcept {
        return color == other.color && color_level == other.color_level &&
               color_slice == other.color_slice && depth == other.depth &&
               depth_level == other.depth_level && has_stencil == other.has_stencil &&
               width == other.width && height == other.height;
    }
};

/**
 * Records Metal commands on the GPU thread. Command buffers are identified by monotonically
 * increasing ticks which are used to know when resources (stream buffer regions, surfaces pending
 * deletion) are no longer in use by the GPU.
 */
class Scheduler {
public:
    explicit Scheduler(const Instance& instance);
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    /// Returns the command buffer currently being recorded, creating it if needed.
    id<MTLCommandBuffer> CommandBuffer();

    /// Returns a blit encoder, ending any other active encoder.
    id<MTLBlitCommandEncoder> BlitEncoder();

    /**
     * Returns a render encoder for the provided target. If the same target is already being
     * rendered to, the active encoder is reused. Attachments are loaded and stored.
     */
    id<MTLRenderCommandEncoder> BeginRendering(const RenderTarget& target);

    /// Begins a render pass using a custom descriptor (clears, presentation). Always starts a new
    /// encoder.
    id<MTLRenderCommandEncoder> BeginRendering(MTLRenderPassDescriptor* descriptor);

    /// Returns the active render encoder or nil.
    id<MTLRenderCommandEncoder> RenderEncoder() const {
        return render_encoder;
    }

    /// The target of the active render pass, or null when none is being rendered to.
    const RenderTarget* ActiveTarget() const {
        return render_encoder != nil && has_render_target ? &current_target : nullptr;
    }

    /// Returns true if the provided target is the one being rendered to.
    bool IsRenderingTo(const RenderTarget& target) const {
        return render_encoder != nil && has_render_target && current_target == target;
    }

    /// Incremented every time a new render encoder begins. Used to rebind encoder state.
    u64 EncoderGeneration() const {
        return encoder_generation;
    }

    /// Ends the active encoder, if any.
    void EndEncoding();

    /**
     * Clears a whole texture level, later: when the next render pass draws to it, the clear
     * becomes the pass's load action instead of a pass of its own. Anything else that comes
     * first (another pass, a blit, a submit) performs the clear before it.
     */
    void ClearColor(id<MTLTexture> texture, u32 level, u32 slice, MTLClearColor color);
    void ClearDepthStencil(id<MTLTexture> texture, u32 level, bool stencil, double depth,
                           u32 stencil_value);

    /// Names the next utility pass (BeginRendering with a descriptor) in the render pass
    /// statistics, so they say what asked for it. Utility passes draw their own quad, which is not
    /// counted as a draw of the game.
    void SetPassLabel(const char* label) {
        pass_label = label;
    }

    /// Counts a draw in the active render pass, for the render pass statistics.
    void NoteDraw() {
        pass_draws++;
    }

    /// Schedules a drawable to be presented when the current command buffer is committed. With
    /// frame pacing on, it is held on screen for the time of an emulated frame, which keeps the
    /// frames evenly spaced on displays that refresh faster than the game runs. `measure` adds the
    /// presentation to the frame interval statistics (the main display only).
    void PresentDrawable(id<CAMetalDrawable> drawable, bool measure = true, u32 swaps = 1);

    /// Commits the current command buffer to the GPU.
    void Flush();

    /// Marks the command buffer being recorded as the presentation one for GpuStats::present_ns.
    void MarkPresentation() {
        presentation_tick = current_tick;
    }

    /// Commits the current command buffer and waits for all GPU work to complete.
    void Finish();

    /// Waits until the command buffer with the provided tick has completed.
    void Wait(u64 tick);

    /// Returns true when the command buffer with the provided tick has completed.
    bool IsFree(u64 tick) const {
        return KnownGpuTick() >= tick;
    }

    /// Tick of the command buffer being recorded.
    u64 CurrentTick() const {
        return current_tick;
    }

    /// Tick of the latest command buffer known to have completed.
    u64 KnownGpuTick() const {
        return gpu_tick.load(std::memory_order_acquire);
    }

    /// Registers a callback invoked before a command buffer is committed.
    void RegisterOnSubmit(std::function<void()>&& func) {
        on_submit.emplace_back(std::move(func));
    }

    /// Pushes and pops a debug group on the active command buffer.
    void PushDebugGroup(std::string_view label);
    void PopDebugGroup();

private:
    void PruneInFlight();
    struct PendingClear {
        id<MTLTexture> texture = nil;
        u32 level = 0;
        u32 slice = 0;
        bool is_depth = false;
        bool has_stencil = false;
        MTLClearColor color{};
        double depth = 0.0;
        u32 stencil = 0;
    };
    void AddPendingClear(const PendingClear& clear);
    /// Performs the clears that are waiting, each as a pass of its own.
    void FlushPendingClears();

    void BeginPassStats(u32 width, u32 height, bool color, bool depth, bool stencil, bool game,
                        const RenderTarget* target);
    void EndPassStats();
    /// Notes a pass that renders to a depth texture (cleared or loaded) for the statistics of
    /// depth stores nothing needed again.
    void NoteDepthPass(id<MTLTexture> texture, u32 level, u32 width, u32 height, bool stencil,
                       bool cleared);

private:
    /// Per depth texture level: the last pass stored it (see NoteDepthPass).
    std::unordered_map<std::uintptr_t, bool> depth_stored;
    id<MTLCommandQueue> queue;
    id<MTLCommandBuffer> command_buffer = nil;
    id<MTLRenderCommandEncoder> render_encoder = nil;
    id<MTLBlitCommandEncoder> blit_encoder = nil;
    RenderTarget current_target{};
    bool has_render_target{};
    u64 encoder_generation{};
    u64 current_tick{1};
    u64 presentation_tick{};
    std::atomic<u64> gpu_tick{0};
    std::deque<std::pair<u64, id<MTLCommandBuffer>>> in_flight;
    std::vector<std::function<void()>> on_submit;
    struct PendingDrawable {
        id<CAMetalDrawable> drawable;
        bool measure;
        u32 swaps; ///< Refreshes of the game (1/60 s) this frame lasts: 2 for a 30 FPS game
    };
    /// When the last paced frame was due on the display (CACurrentMediaTime), the next one is due
    /// a whole number of the game's refreshes later (see Flush).
    double pace_last_target{0.0};
    std::vector<PendingDrawable> pending_drawables;
    u32 debug_group_depth{};

    std::vector<PendingClear> pending_clears;

    // Render pass statistics.
    const char* end_reason = nullptr; ///< Why the active pass is about to end, null: its caller
    const char* last_end_reason = "";
    std::string pass_key;
    u64 pass_bytes{};
    u32 pass_draws{};
    const char* pass_label = nullptr;
    bool pass_open{};
    bool last_pass_was_target{};
    RenderTarget last_pass_target{};
};

} // namespace Metal
