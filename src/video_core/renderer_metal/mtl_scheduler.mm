// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <algorithm>
#include <cmath>
#include <optional>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/settings.h"
#include "video_core/renderer_metal/mtl_instance.h"
#include "video_core/renderer_metal/mtl_scheduler.h"
#import <QuartzCore/QuartzCore.h>

MICROPROFILE_DEFINE(Metal_WaitForGpu, "Metal", "Wait For GPU", MP_RGB(255, 0, 0));
MICROPROFILE_DEFINE(Metal_Submit, "Metal", "Submit", MP_RGB(255, 192, 255));

namespace Metal {

GpuStats& Stats() {
    static GpuStats stats;
    return stats;
}

namespace {
std::atomic<bool> g_frame_pacing{true};
/// How far ahead of the frames the pacing timeline runs, in milliseconds (see Scheduler::Flush).
std::atomic<int> g_pacing_buffer_ms{0};
std::atomic<int> g_pacing_display_hz{60};
/// Smoothed position of the frames within a display refresh as a point on a circle (a refresh
/// is one turn, so a frame that lands just after one refresh and one that lands just before the
/// next are close together). See Scheduler::Flush.
std::atomic<double> g_phase_cos{0.0};
std::atomic<double> g_phase_sin{0.0};
std::atomic<bool> g_phase_known{false};
/// Presented time of the previous frame of the main display, 0 when there is none.
std::atomic<double> g_last_presented{0.0};
/// Duration of an emulated frame at full speed (a 3DS runs at about 59.83 frames per second).
constexpr double kFramePeriod = 1.0 / 59.83;
} // namespace

namespace {
struct ReadbackEntry {
    u32 count = 0;
    double wait_ms = 0.0;
    u64 bytes = 0;
};
std::mutex g_readback_mutex;
std::map<std::string, ReadbackEntry> g_readbacks;
} // namespace

void RecordReadback(const std::string& what, double wait_ms, u64 bytes) {
    std::scoped_lock lock{g_readback_mutex};
    ReadbackEntry& entry = g_readbacks[what];
    entry.count++;
    entry.wait_ms += wait_ms;
    entry.bytes += bytes;
}

std::vector<std::string> TakeReadbackSummary(std::size_t max_lines) {
    std::map<std::string, ReadbackEntry> taken;
    {
        std::scoped_lock lock{g_readback_mutex};
        taken.swap(g_readbacks);
    }
    std::vector<std::pair<std::string, ReadbackEntry>> sorted(taken.begin(), taken.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second.wait_ms > b.second.wait_ms; });
    std::vector<std::string> lines;
    for (std::size_t i = 0; i < sorted.size() && i < max_lines; i++) {
        const auto& [what, entry] = sorted[i];
        lines.push_back(fmt::format("{} x{}, waited {:.1f} ms, {} KiB", what, entry.count,
                                    entry.wait_ms, entry.bytes / 1024));
    }
    return lines;
}

namespace {
struct PassEntry {
    u32 count = 0;
    u32 draws = 0;
    u32 empty = 0;
};
struct PassStats {
    std::map<std::string, PassEntry> targets;
    std::map<std::string, u32> end_reasons;
    std::map<std::string, u32> reopened_after;
    u32 total = 0;
    u32 reopened = 0;
    u64 bytes = 0;
    // The depth (and stencil) a pass stores is read again only when a later pass loads it. A pass
    // that clears it instead did not need what the pass before stored.
    u32 depth_passes = 0;
    u32 depth_loaded = 0;
    u32 depth_cleared = 0;
    u32 depth_store_unused = 0;
    u64 depth_store_unused_bytes = 0;
    // Hardware vertex shader draws: native ones (shared vertices shaded once), and the ones drawn
    // corner by corner (lit models, fans, or the setting off), with the vertices they draw.
    u32 hw_native = 0, hw_lit = 0, hw_other = 0;
    u64 hw_native_vertices = 0, hw_lit_vertices = 0, hw_other_vertices = 0;
};
std::mutex g_pass_mutex;
PassStats g_pass_stats;
} // namespace

std::vector<std::string> TakeRenderPassSummary(std::size_t max_lines) {
    PassStats stats;
    {
        std::scoped_lock lock{g_pass_mutex};
        std::swap(stats, g_pass_stats);
    }
    std::vector<std::string> lines;
    if (stats.total == 0) {
        return lines;
    }
    const auto join = [](const std::map<std::string, u32>& counts) {
        std::vector<std::pair<std::string, u32>> sorted(counts.begin(), counts.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        std::string text;
        for (const auto& [name, count] : sorted) {
            text += fmt::format("{}{} {}", text.empty() ? "" : ", ", name, count);
        }
        return text.empty() ? std::string{"none"} : text;
    };
    u32 empty = 0;
    for (const auto& [key, entry] : stats.targets) {
        empty += entry.empty;
    }
    lines.push_back(fmt::format(
        "{} passes, {} without draws, {} reopened the target of the pass before (after: {}), "
        "about {} MiB of attachments loaded and stored",
        stats.total, empty, stats.reopened, join(stats.reopened_after),
        stats.bytes / (1024 * 1024)));
    lines.push_back(fmt::format("ended by: {}", join(stats.end_reasons)));
    if (stats.hw_native + stats.hw_lit + stats.hw_other > 0) {
        lines.push_back(fmt::format(
            "hardware vertex draws: {} native ({} vertices), {} corner by corner with lighting "
            "({} vertices), {} corner by corner otherwise ({} vertices)",
            stats.hw_native, stats.hw_native_vertices, stats.hw_lit, stats.hw_lit_vertices,
            stats.hw_other, stats.hw_other_vertices));
    }
    if (stats.depth_passes > 0) {
        lines.push_back(fmt::format(
            "depth: {} passes, {} loaded what the pass before stored, {} cleared it instead; "
            "those {} stores (about {} MiB, at most: reads outside the passes are not counted) "
            "could have been skipped",
            stats.depth_passes, stats.depth_loaded, stats.depth_cleared,
            stats.depth_store_unused, stats.depth_store_unused_bytes / (1024 * 1024)));
    }
    std::vector<std::pair<std::string, PassEntry>> sorted(stats.targets.begin(),
                                                          stats.targets.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second.count > b.second.count; });
    for (std::size_t i = 0; i < sorted.size() && lines.size() < max_lines; i++) {
        const auto& [key, entry] = sorted[i];
        lines.push_back(fmt::format("{}: x{}, {} draws, {} without draws", key, entry.count,
                                    entry.draws, entry.empty));
    }
    return lines;
}

void Scheduler::BeginPassStats(u32 width, u32 height, bool color, bool depth, bool stencil,
                               bool game, const RenderTarget* target) {
    if (!StatisticsEnabled()) {
        return;
    }
    std::string attachments;
    if (color) {
        attachments = "color";
    }
    if (depth) {
        attachments += attachments.empty() ? "depth" : "+depth";
    }
    if (stencil) {
        attachments += "+stencil";
    }
    if (attachments.empty()) {
        attachments = "no attachments";
    }
    // A utility pass is named by what asked for it and always draws its quad.
    const char* label = game ? nullptr : (pass_label != nullptr ? pass_label : "utility");
    pass_label = nullptr;
    pass_key = fmt::format("{} {}x{} {}", game ? "game" : label, width, height, attachments);
    // Every attachment is loaded at the start and stored at the end (rough: 4 bytes a pixel).
    pass_bytes = static_cast<u64>(width) * height * ((color ? 4 : 0) + (depth ? 4 : 0)) * 2;
    pass_draws = game ? 0 : 1;
    pass_open = true;

    std::scoped_lock lock{g_pass_mutex};
    g_pass_stats.total++;
    if (game && target != nullptr && last_pass_was_target && *target == last_pass_target) {
        g_pass_stats.reopened++;
        g_pass_stats.reopened_after[last_end_reason]++;
    }
}

void NoteHardwareDraw(int kind, u32 vertices) {
    if (!StatisticsEnabled()) {
        return;
    }
    std::scoped_lock lock{g_pass_mutex};
    switch (kind) {
    case 0:
        g_pass_stats.hw_native++;
        g_pass_stats.hw_native_vertices += vertices;
        break;
    case 1:
        g_pass_stats.hw_lit++;
        g_pass_stats.hw_lit_vertices += vertices;
        break;
    default:
        g_pass_stats.hw_other++;
        g_pass_stats.hw_other_vertices += vertices;
        break;
    }
}

void Scheduler::NoteDepthPass(id<MTLTexture> texture, u32 level, u32 width, u32 height,
                              bool stencil, bool cleared) {
    if (!StatisticsEnabled() || texture == nil) {
        return;
    }
    const auto key = reinterpret_cast<std::uintptr_t>((__bridge void*)texture) * 31u + level;
    bool& stored = depth_stored[key];
    std::scoped_lock lock{g_pass_mutex};
    g_pass_stats.depth_passes++;
    if (cleared) {
        g_pass_stats.depth_cleared++;
        if (stored) {
            g_pass_stats.depth_store_unused++;
            g_pass_stats.depth_store_unused_bytes +=
                static_cast<u64>(width) * height * (stencil ? 5 : 4);
        }
    } else {
        g_pass_stats.depth_loaded++;
    }
    // This pass stores what it ends with.
    stored = true;
}

void Scheduler::EndPassStats() {
    if (!pass_open) {
        return;
    }
    pass_open = false;
    const char* reason = end_reason != nullptr ? end_reason : "its caller";
    end_reason = nullptr;
    last_end_reason = reason;
    last_pass_was_target = has_render_target;
    last_pass_target = has_render_target ? current_target : RenderTarget{};

    std::scoped_lock lock{g_pass_mutex};
    PassEntry& entry = g_pass_stats.targets[pass_key];
    entry.count++;
    entry.draws += pass_draws;
    if (pass_draws == 0) {
        entry.empty++;
    }
    g_pass_stats.end_reasons[reason]++;
    g_pass_stats.bytes += pass_bytes;
}

namespace {
std::atomic<bool> g_statistics{true};
} // namespace

void SetStatisticsEnabled(bool enabled) {
    g_statistics.store(enabled, std::memory_order_relaxed);
}

bool StatisticsEnabled() {
    return g_statistics.load(std::memory_order_relaxed);
}

void SetFramePacing(bool enabled) {
    g_frame_pacing.store(enabled, std::memory_order_relaxed);
}

void SetPacingDisplayHz(int hz) {
    g_pacing_display_hz.store(std::clamp(hz, 30, 240), std::memory_order_relaxed);
}

/// A paced frame was shown `slack` seconds after the time it was due. The display shows a frame
/// at the first refresh after its due time, so a frame due just before a refresh may flip to the
/// refresh before or after with the smallest change in timing, which is what shows as a
/// stutter. Kept as a point on the circle of one refresh.
void RecordPaceSlack(double slack) {
    const double refresh = 1.0 / g_pacing_display_hz.load(std::memory_order_relaxed);
    if (slack < 0.0 || slack > refresh * 3.0) {
        return;
    }
    const double angle = 2.0 * M_PI * std::fmod(slack, refresh) / refresh;
    const double c = std::cos(angle), s = std::sin(angle);
    if (!g_phase_known.exchange(true, std::memory_order_relaxed)) {
        g_phase_cos.store(c, std::memory_order_relaxed);
        g_phase_sin.store(s, std::memory_order_relaxed);
    } else {
        g_phase_cos.store(g_phase_cos.load(std::memory_order_relaxed) * 0.9 + c * 0.1,
                          std::memory_order_relaxed);
        g_phase_sin.store(g_phase_sin.load(std::memory_order_relaxed) * 0.9 + s * 0.1,
                          std::memory_order_relaxed);
    }
    GpuStats& stats = Stats();
    stats.phase_cos_micro.fetch_add(static_cast<s64>(c * 1e6), std::memory_order_relaxed);
    stats.phase_sin_micro.fetch_add(static_cast<s64>(s * 1e6), std::memory_order_relaxed);
    stats.phase_count.fetch_add(1, std::memory_order_relaxed);
}

void SetPacingBuffer(int milliseconds) {
    g_pacing_buffer_ms.store(std::clamp(milliseconds, 0, 50), std::memory_order_relaxed);
}

Scheduler::Scheduler(const Instance& instance) : queue{instance.GetCommandQueue()} {}

Scheduler::~Scheduler() {
    Finish();
}

id<MTLCommandBuffer> Scheduler::CommandBuffer() {
    if (command_buffer == nil) {
        @autoreleasepool {
            command_buffer = [queue commandBuffer];
        }
        const u64 tick = current_tick;
        // The presentation gets a command buffer of its own, see RendererMetal::RenderToLayer.
        const bool is_presentation = tick == presentation_tick;
        std::atomic<u64>* gpu_tick_ptr = &gpu_tick;
        [command_buffer addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
          if (buffer.status == MTLCommandBufferStatusError) {
              LOG_CRITICAL(Render, "Metal command buffer {} failed: {}", tick,
                           [buffer.error.localizedDescription UTF8String]);
          }
          if (buffer.GPUEndTime > buffer.GPUStartTime) {
              const u64 ns = static_cast<u64>((buffer.GPUEndTime - buffer.GPUStartTime) * 1e9);
              Stats().gpu_ns.fetch_add(ns, std::memory_order_relaxed);
              if (is_presentation) {
                  Stats().present_ns.fetch_add(ns, std::memory_order_relaxed);
              }
          }
          u64 known = gpu_tick_ptr->load(std::memory_order_relaxed);
          while (known < tick &&
                 !gpu_tick_ptr->compare_exchange_weak(known, tick, std::memory_order_release)) {
          }
        }];
    }
    return command_buffer;
}

void Scheduler::AddPendingClear(const PendingClear& clear) {
    // A newer clear of the same texture level replaces the older one.
    std::erase_if(pending_clears, [&](const PendingClear& other) {
        return other.texture == clear.texture && other.level == clear.level &&
               other.slice == clear.slice && other.has_stencil == clear.has_stencil;
    });
    pending_clears.push_back(clear);
}

void Scheduler::ClearColor(id<MTLTexture> texture, u32 level, u32 slice, MTLClearColor color) {
    // The texture may be in use by the active encoder (sampled), end it so the clear is ordered
    // after what was recorded.
    end_reason = "a clear";
    EndEncoding();
    end_reason = nullptr;
    AddPendingClear({.texture = texture, .level = level, .slice = slice, .color = color});
}

void Scheduler::ClearDepthStencil(id<MTLTexture> texture, u32 level, bool stencil, double depth,
                                  u32 stencil_value) {
    end_reason = "a clear";
    EndEncoding();
    end_reason = nullptr;
    AddPendingClear({.texture = texture,
                     .level = level,
                     .is_depth = true,
                     .has_stencil = stencil,
                     .depth = depth,
                     .stencil = stencil_value});
}

void Scheduler::FlushPendingClears() {
    if (pending_clears.empty()) {
        return;
    }
    std::vector<PendingClear> clears;
    clears.swap(pending_clears);
    EndEncoding();
    for (const PendingClear& clear : clears) {
        MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
        if (clear.is_depth) {
            desc.depthAttachment.texture = clear.texture;
            desc.depthAttachment.level = clear.level;
            desc.depthAttachment.loadAction = MTLLoadActionClear;
            desc.depthAttachment.storeAction = MTLStoreActionStore;
            NoteDepthPass(clear.texture, clear.level, static_cast<u32>(clear.texture.width),
                          static_cast<u32>(clear.texture.height), clear.has_stencil, true);
            desc.depthAttachment.clearDepth = clear.depth;
            if (clear.has_stencil) {
                desc.stencilAttachment.texture = clear.texture;
                desc.stencilAttachment.level = clear.level;
                desc.stencilAttachment.loadAction = MTLLoadActionClear;
                desc.stencilAttachment.storeAction = MTLStoreActionStore;
                desc.stencilAttachment.clearStencil = clear.stencil;
            }
        } else {
            desc.colorAttachments[0].texture = clear.texture;
            desc.colorAttachments[0].level = clear.level;
            desc.colorAttachments[0].slice = clear.slice;
            desc.colorAttachments[0].loadAction = MTLLoadActionClear;
            desc.colorAttachments[0].storeAction = MTLStoreActionStore;
            desc.colorAttachments[0].clearColor = clear.color;
        }
        render_encoder = [CommandBuffer() renderCommandEncoderWithDescriptor:desc];
        Stats().render_passes.fetch_add(1, std::memory_order_relaxed);
        id<MTLTexture> sized = clear.texture;
        BeginPassStats(static_cast<u32>(std::max<NSUInteger>(sized.width >> clear.level, 1)),
                       static_cast<u32>(std::max<NSUInteger>(sized.height >> clear.level, 1)),
                       !clear.is_depth, clear.is_depth, clear.has_stencil, false, nullptr);
        has_render_target = false;
        encoder_generation++;
        end_reason = "a clear that no pass could take over";
        EndEncoding();
        end_reason = nullptr;
    }
}

id<MTLBlitCommandEncoder> Scheduler::BlitEncoder() {
    if (blit_encoder != nil) {
        return blit_encoder;
    }
    FlushPendingClears();
    end_reason = "a blit (copy or upload)";
    EndEncoding();
    end_reason = nullptr;
    blit_encoder = [CommandBuffer() blitCommandEncoder];
    Stats().blit_passes.fetch_add(1, std::memory_order_relaxed);
    return blit_encoder;
}

id<MTLRenderCommandEncoder> Scheduler::BeginRendering(const RenderTarget& target) {
    if (IsRenderingTo(target) && pending_clears.empty()) {
        return render_encoder;
    }
    end_reason = "another target";
    EndEncoding();
    end_reason = nullptr;

    // Clears of this pass's attachments become its load actions, the others happen first.
    std::optional<PendingClear> color_clear;
    std::optional<PendingClear> depth_clear;
    if (!pending_clears.empty()) {
        std::vector<PendingClear> others;
        for (const PendingClear& clear : pending_clears) {
            if (!clear.is_depth && target.color != nil && clear.texture == target.color &&
                clear.level == target.color_level && clear.slice == target.color_slice) {
                color_clear = clear;
            } else if (clear.is_depth && target.depth != nil && clear.texture == target.depth &&
                       clear.level == target.depth_level &&
                       (!clear.has_stencil || target.has_stencil)) {
                depth_clear = clear;
            } else {
                others.push_back(clear);
            }
        }
        pending_clears.swap(others);
        FlushPendingClears();
    }

    MTLRenderPassDescriptor* descriptor = [MTLRenderPassDescriptor renderPassDescriptor];
    if (target.color != nil) {
        MTLRenderPassColorAttachmentDescriptor* color = descriptor.colorAttachments[0];
        color.texture = target.color;
        color.level = target.color_level;
        color.slice = target.color_slice;
        color.loadAction = MTLLoadActionLoad;
        color.storeAction = MTLStoreActionStore;
        if (color_clear) {
            color.loadAction = MTLLoadActionClear;
            color.clearColor = color_clear->color;
        }
    }
    if (target.depth != nil) {
        MTLRenderPassDepthAttachmentDescriptor* depth = descriptor.depthAttachment;
        depth.texture = target.depth;
        depth.level = target.depth_level;
        depth.loadAction = MTLLoadActionLoad;
        depth.storeAction = MTLStoreActionStore;
        if (depth_clear) {
            depth.loadAction = MTLLoadActionClear;
            depth.clearDepth = depth_clear->depth;
        }
        if (target.has_stencil) {
            MTLRenderPassStencilAttachmentDescriptor* stencil = descriptor.stencilAttachment;
            stencil.texture = target.depth;
            stencil.level = target.depth_level;
            stencil.loadAction = MTLLoadActionLoad;
            stencil.storeAction = MTLStoreActionStore;
            if (depth_clear && depth_clear->has_stencil) {
                stencil.loadAction = MTLLoadActionClear;
                stencil.clearStencil = depth_clear->stencil;
            }
        }
    }
    if (target.depth != nil) {
        NoteDepthPass(target.depth, target.depth_level, target.width, target.height,
                      target.has_stencil, depth_clear.has_value());
    }
    // The render area is the intersection of the attachments, which may have different sizes.
    descriptor.renderTargetWidth = std::max(target.width, 1u);
    descriptor.renderTargetHeight = std::max(target.height, 1u);
    if (target.color == nil && target.depth == nil) {
        // Attachment-less rendering, used for shadow map rendering.
        descriptor.defaultRasterSampleCount = 1;
    }

    render_encoder = [CommandBuffer() renderCommandEncoderWithDescriptor:descriptor];
    Stats().render_passes.fetch_add(1, std::memory_order_relaxed);
    BeginPassStats(target.width, target.height, target.color != nil, target.depth != nil,
                   target.has_stencil, true, &target);
    current_target = target;
    has_render_target = true;
    encoder_generation++;
    return render_encoder;
}

id<MTLRenderCommandEncoder> Scheduler::BeginRendering(MTLRenderPassDescriptor* descriptor) {
    end_reason = "a utility pass (clear, blit, present)";
    EndEncoding();
    end_reason = nullptr;
    FlushPendingClears();
    render_encoder = [CommandBuffer() renderCommandEncoderWithDescriptor:descriptor];
    Stats().render_passes.fetch_add(1, std::memory_order_relaxed);
    {
        id<MTLTexture> color = descriptor.colorAttachments[0].texture;
        id<MTLTexture> depth = descriptor.depthAttachment.texture;
        id<MTLTexture> sized = color != nil ? color : depth;
        BeginPassStats(sized != nil ? static_cast<u32>(sized.width) : 0,
                       sized != nil ? static_cast<u32>(sized.height) : 0, color != nil,
                       depth != nil, descriptor.stencilAttachment.texture != nil, false, nullptr);
    }
    has_render_target = false;
    encoder_generation++;
    return render_encoder;
}

void Scheduler::EndEncoding() {
    if (render_encoder != nil) {
        EndPassStats();
        [render_encoder endEncoding];
        render_encoder = nil;
        has_render_target = false;
    }
    if (blit_encoder != nil) {
        [blit_encoder endEncoding];
        blit_encoder = nil;
    }
}

void RecordPresentedTime(double now) {
    if (now <= 0.0) {
        return; // The frame was dropped
    }
    Stats().presented.fetch_add(1, std::memory_order_relaxed);
    const double last = g_last_presented.exchange(now, std::memory_order_relaxed);
    if (last <= 0.0 || now <= last) {
        return;
    }
    const double interval = now - last;
    if (interval > 0.25) {
        return; // A pause or a menu, not a pacing problem
    }
    GpuStats& stats = Stats();
    const u64 us = static_cast<u64>(interval * 1e6);
    stats.present_intervals.fetch_add(1, std::memory_order_relaxed);
    stats.present_interval_us.fetch_add(us, std::memory_order_relaxed);
    stats.present_interval_sq_us.fetch_add(us * us, std::memory_order_relaxed);
    // Compared with the frame time of the moment, which changes with fast forward.
    const double limit_now = Settings::GetFrameLimit();
    if (limit_now > 0.0 && interval > kFramePeriod * 100.0 / limit_now * 1.4) {
        stats.present_late.fetch_add(1, std::memory_order_relaxed);
    }
}

void Scheduler::PresentDrawable(id<CAMetalDrawable> drawable, bool measure, u32 swaps) {
    pending_drawables.push_back({drawable, measure, std::max(swaps, 1u)});
}

void Scheduler::PushDebugGroup(std::string_view label) {
    [CommandBuffer() pushDebugGroup:ToNSString(label)];
    debug_group_depth++;
}

void Scheduler::PopDebugGroup() {
    if (debug_group_depth == 0 || command_buffer == nil) {
        return;
    }
    [command_buffer popDebugGroup];
    debug_group_depth--;
}

void Scheduler::Flush() {
    MICROPROFILE_SCOPE(Metal_Submit);
    for (const auto& func : on_submit) {
        func();
    }
    end_reason = "a submit (readback, frame end)";
    EndEncoding();
    end_reason = nullptr;
    FlushPendingClears();
    if (command_buffer == nil && pending_drawables.empty()) {
        return;
    }

    id<MTLCommandBuffer> buffer = CommandBuffer();
    while (debug_group_depth > 0) {
        [buffer popDebugGroup];
        debug_group_depth--;
    }
    // The time a frame lasts at the current speed limit. No pacing when there is none (fast
    // forward, unlimited speed), where holding frames back would only slow things down.
    const double limit = Settings::GetFrameLimit();
    const double period = limit > 0.0 ? kFramePeriod * 100.0 / limit : 0.0;
    const bool pace = g_frame_pacing.load(std::memory_order_relaxed) && period > 0.0;
    for (const PendingDrawable& pending : pending_drawables) {
        if (pace && pending.measure) {
            // A timeline: every frame is due one frame of the game after the one before it
            // (a whole number of the game's refreshes, 2 for a 30 FPS game), whatever the exact
            // moment the emulation thread finished it at. A frame that was handed over a little
            // early waits for its time, one that is late is shown at once and the timeline
            // starts again from it. Without this each frame was shown at the first display
            // refresh after it was ready, which jitters by a whole refresh (8 ms at 120 Hz).
            // The buffer moves the whole timeline a little into the future: a frame that is up to
            // that much late (the emulation thread had a slow frame) still reaches its slot
            // instead of being shown a refresh late, at the price of that much more delay.
            const double lead = g_pacing_buffer_ms.load(std::memory_order_relaxed) * 0.001;
            const double now = CACurrentMediaTime() + lead;
            const double step = period * pending.swaps;
            double target = pace_last_target > 0.0 ? pace_last_target + step : now;
            if (target < now - period * 0.5 - lead || target > now + period) {
                target = now;
            }
            // Lock to the display's refreshes: a frame is shown at the first refresh after its
            // due time, so the due time should sit in the middle between two refreshes, where
            // small differences in timing do not change which refresh it gets. The timeline
            // is moved slowly toward that (at most 0.3 ms per frame).
            if (g_phase_known.load(std::memory_order_relaxed)) {
                const double refresh = 1.0 / g_pacing_display_hz.load(std::memory_order_relaxed);
                const double angle = std::atan2(g_phase_sin.load(std::memory_order_relaxed),
                                                g_phase_cos.load(std::memory_order_relaxed));
                double mean = angle / (2.0 * M_PI) * refresh; // -refresh/2 ... refresh/2
                if (mean < 0.0) {
                    mean += refresh;
                }
                // Positive: shown too long after its due time, so due too early: move later.
                double error = mean - refresh * 0.5;
                error = std::clamp(error * 0.1, -0.0003, 0.0003);
                target += error;
            }
            pace_last_target = target;
            const double asked = target;
            [buffer presentDrawable:pending.drawable atTime:target];
            [pending.drawable addPresentedHandler:^(id<MTLDrawable> presented) {
              if (presented.presentedTime > 0.0) {
                  RecordPaceSlack(presented.presentedTime - asked);
              }
            }];
        } else {
            [buffer presentDrawable:pending.drawable];
        }
        if (pending.measure) {
            [pending.drawable addPresentedHandler:^(id<MTLDrawable> presented) {
              RecordPresentedTime(presented.presentedTime);
            }];
        }
    }
    pending_drawables.clear();
    [buffer commit];

    in_flight.emplace_back(current_tick, buffer);
    command_buffer = nil;
    current_tick++;
    PruneInFlight();
}

void Scheduler::Finish() {
    const u64 tick = current_tick;
    Flush();
    Wait(tick);
}

void Scheduler::Wait(u64 tick) {
    if (tick >= current_tick) {
        // The requested tick is still being recorded, submit it.
        Flush();
    }
    if (IsFree(tick)) {
        PruneInFlight();
        return;
    }

    MICROPROFILE_SCOPE(Metal_WaitForGpu);
    const auto wait_start = std::chrono::steady_clock::now();
    while (!in_flight.empty() && in_flight.front().first <= tick) {
        [in_flight.front().second waitUntilCompleted];
        in_flight.pop_front();
    }
    GpuStats& stats = Stats();
    stats.stall_ns.fetch_add(
        static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - wait_start)
                             .count()),
        std::memory_order_relaxed);
    stats.stalls.fetch_add(1, std::memory_order_relaxed);

    // Completion handlers may run slightly after waitUntilCompleted returns, make sure the known
    // tick reflects the wait.
    u64 known = gpu_tick.load(std::memory_order_relaxed);
    while (known < tick && !gpu_tick.compare_exchange_weak(known, tick)) {
    }
}

void Scheduler::PruneInFlight() {
    while (!in_flight.empty()) {
        const auto status = in_flight.front().second.status;
        if (status != MTLCommandBufferStatusCompleted && status != MTLCommandBufferStatusError) {
            break;
        }
        in_flight.pop_front();
    }
}

} // namespace Metal
