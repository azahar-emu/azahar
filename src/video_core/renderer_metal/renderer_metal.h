// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <chrono>
#include <functional>

#include <memory>
#include <string>
#include <vector>
#include "common/common_types.h"

// Public entry point of the native Metal renderer. This header is plain C++ so it can be included
// from the rest of the code base; the implementation is Objective-C++.

namespace Core {
class System;
}

namespace Frontend {
class EmuWindow;
}

namespace Pica {
class PicaCore;
}

namespace VideoCore {
class RendererBase;
}

namespace Metal {

/// How the 3DS screens are scaled up to the display.
enum class UpscalerMode : int {
    /// Plain texture filtering.
    Off = 0,
    /// MetalFX spatial upscaling: each screen is rendered at the game's internal resolution and
    /// scaled to its size on the display by the GPU's upscaler. Applies from the next frame.
    Spatial = 1,
};

void SetUpscalerMode(UpscalerMode mode);

/// How far MetalFX scales a screen up, as a factor of the size it is drawn at (1.5, 2, 2.5 ...).
/// The result is stretched to the screen's size on the display. 0 scales to the size on the
/// display directly. Applies from the next frame.
void SetUpscaleFactor(float factor);

/// Built-in post-processing (0 none, 1 sharpen, 2 LCD grid, 3 scanlines) and the anaglyph variant
/// (0 Dubois, 1 gray, 2 half color, 3 color). Both apply at once.
void SetPostProcessing(int effect, int anaglyph_mode);

/// Sharp pixels (a screen's pixels stay crisp when it is enlarged) and a sharpening amount from 0
/// (off) to 100 percent. Both apply to the screens as they are drawn, after MetalFX, from the
/// next frame.
void SetSharpness(bool sharp_pixels, int sharpness);

/// How the screens look: the sharpening method (0 unsharp mask, 1 contrast adaptive), the filter
/// used when a screen is enlarged (0 bilinear, 1 bicubic, 2 Lanczos), how dark the gaps of the LCD
/// grid are (0 default, 1-100), and a color adjustment (-100 to 100 each, 0 for none).
void SetScreenStyle(int sharpen_method, int scale_filter, int grid_strength, int saturation,
                    int contrast, int brightness);

/// A Metal file with a post_effect function that replaces the built-in effect, applied to games
/// started afterwards. See the post-processing section of the iOS settings. Empty for none.
void SetCustomPostShader(const std::string& path);

/// Holds each frame on screen for the time of an emulated frame so frames are evenly spaced.
void SetFramePacing(bool enabled);

/// Delay (milliseconds) the frame pacing adds so slow frames still meet their slot, 0 for none.
void SetPacingBuffer(int milliseconds);

/// The refresh rate of the display (60 or 120), for locking the pacing to its refreshes.
void SetPacingDisplayHz(int hz);

/// Called by the display link at every refresh: the time it was called and the time that
/// refresh reaches the screen (both CACurrentMediaTime). Keeps the display's refresh grid.
void NoteDisplayCallback(double now, double presentation);

/// For the frame limiter (Core::FrameLimiter::SetDeadlineSnap): the wake-up deadline moved to
/// just after the nearest refresh of the display, so each frame is made right after a refresh
/// and is ready well before the next one. Unchanged when the display link is not running.
std::chrono::steady_clock::time_point SnapDeadlineToDisplay(
    std::chrono::steady_clock::time_point deadline);

/// How textures are kept in GPU memory, see Surface::CreateTexture.
enum class TextureCompression : int {
    Off = 0,
    Lossless = 1, ///< The GPU's lossless compression, identical results
    Lossy = 2,    ///< Lossy compression (A15 and later), lossless on older GPUs
};

/// Hooks of the frame generator, which the app provides (see AZFrameGenerating). Textures and
/// command buffers are passed as the Objective-C objects behind the pointers.
struct FrameGeneratorHooks {
    /// Gets ready for frames of this size (the device is an id<MTLDevice>).
    std::function<bool(void* device, u32 width, u32 height)> prepare;
    /// A plane of a slot (id<MTLTexture>): 0 luma (R8), 1 chroma (RG8, half size). The frames are
    /// 4:2:0 YCbCr, the only format the interpolation takes. nullptr when there is none.
    std::function<void*(u32 slot, u32 plane)> texture;
    /// Starts making the frame between `previous` and `current` into `destination` (the planes
    /// must be written already). `done` is called on another thread when it is finished, with
    /// whether it worked. Returns false when it could not start (`done` is not called then).
    std::function<bool(u32 previous, u32 current, u32 destination,
                       std::function<void(bool made)> done)>
        interpolate;
};
void SetFrameGeneratorHooks(FrameGeneratorHooks hooks);

/// Fragment shader math and texture sampling options. They apply to games started afterwards.
struct QualityOptions {
    /// Compile fragment shaders with fast math (assumes no NaN/Inf, allows reassociation). Faster,
    /// but can change the result of a shader that relies on exact floating point behaviour.
    bool fast_math = false;
    /// Fragment lighting in half precision with fast normalization. Faster on Apple GPUs, the
    /// lighting can come out very slightly different (lookup table positions are less exact).
    bool fast_lighting = false;
    /// Lighting lookup tables read through a filtered texture, see MetalFSConfig::lut_texture.
    bool lut_textures = false;
    /// Map the depth in the vertex shader so fragment shaders do not write it, which keeps the
    /// hidden surface removal of Apple GPUs working (not for W-buffering).
    bool vertex_depth = false;
    /// Draw-time optimizations: depth/stencil-only draws without a fragment function, the
    /// scissor test inside the box as the GPU scissor rectangle, and blending that keeps the
    /// source color drawn unblended. Off draws as before they existed.
    bool draw_optimizations = true;
    /// Waits each frame until the GPU finished the previous one, so the emulation is never more
    /// than a frame ahead of the GPU. Off lets it run further ahead (Azahar's default on other
    /// renderers since its frames-ahead limit became optional).
    bool wait_for_gpu = true;
    /// GPU timestamps for every render pass, for the GPU time per pass in the log. A diagnostic:
    /// it can keep passes from overlapping on the GPU.
    bool gpu_pass_timing = false;
    /// Keeps the MSL source of the programs and writes the most used ones to
    /// shader_dump/<title id>/ every 10 seconds or so, to study the generated code.
    bool dump_shaders = false;
    /// Texture combiners in half precision.
    bool fast_color = false;
    /// Fast versions of the math functions in fragment shaders, without fast math's assumptions
    /// about NaN and infinity (iOS 18).
    bool fast_functions = false;
    /// The vertex color goes from the vertex to the fragment stage in half precision.
    bool half_interpolants = false;
    /// The stream buffers the CPU writes and the GPU reads are not tracked by Metal (they track
    /// their own reuse), which saves CPU time at every draw.
    bool untracked_buffers = false;
    /// Hardware vertex shader draws without lit surfaces are drawn natively (with an index buffer
    /// where the game has one), so a vertex shared by several triangles is shaded once instead of
    /// once for every triangle that uses it.
    bool vertex_reuse = false;
    /// Fast versions of the math functions in hardware vertex shaders (iOS 18), without fast
    /// math's assumptions about NaN and infinity (the accurate multiplication still works).
    bool vertex_fast_functions = false;
    /// Hardware vertex shaders compiled with fast math. It assumes there is no NaN or infinity,
    /// which the accurate multiplication relies on, so that is switched off with it.
    bool vertex_fast_math = false;
    /// Textures of dropped surfaces are kept and reused by the next surfaces of the same kind.
    bool texture_pool = false;
    TextureCompression texture_compression = TextureCompression::Off;
    /// Small depth areas the CPU reads every frame get the previous frame's values instead of
    /// waiting for the GPU (Mario Kart 7's sun glare check).
    bool async_depth_readback = false;
    /// Maximum anisotropic filtering level for linearly filtered textures, 1 disables it.
    int max_anisotropy = 4;
    /// A frame in which a draw was skipped (its shader was still being built) is not shown, the
    /// previous frame stays (at most three in a row), instead of a frame with parts missing.
    bool hold_frames_for_shaders = false;
    /// Frame generation: 0 off, 1 for games below 40 FPS only, 2 for every game.
    int frame_generation = 0;
    /// Size the frames are made at: 0 full, 1 half, 2 three eighths, 3 a quarter, 4 three quarters
    /// of the screen, 5 adaptive (starts at half, goes smaller while the made frames come too
    /// late and back up when there is room again).
    /// Smaller is faster (less work for the interpolation) and softer.
    int frame_generation_size = 1;
    /// Frame generation pauses (the game's own frames are shown) while the made frames come too
    /// late even at the smallest size it may use, and comes back when the scene gets lighter.
    bool frame_generation_pause = false;
    /// The display link keeps one more frame waiting before it shows them, so a frame that comes
    /// a little late still makes its refresh (steadier, one refresh more latency).
    bool frame_generation_buffer = false;
    /// Make frames for the top screen only; the bottom screen shows the game's own frames.
    bool frame_generation_top_only = false;
    /// Frames without frame generation go to the screen through the display link, and the
    /// emulation makes them in step with the display's refreshes (see SnapDeadlineToDisplay).
    bool display_sync = false;
    /// With display sync, each new frame of the game is shown for this many of the 3DS's
    /// refreshes (2: a steady 30 FPS), whatever its own timing was. 0 follows the game.
    int frame_cap = 0;
    /// Frame generation with MetalFX's own frame interpolator instead of VideoToolbox's.
    bool frame_generation_metalfx = false;
    /// Frames of frame generation are shown by a display link at the display's refreshes.
    bool frame_generation_display_paced = false;
    /// Highest refresh rate of the display, frame generation for 60 FPS games needs 100 or more.
    int display_hz = 60;
    /// How long (microseconds) a draw waits for its shader to build before it is skipped.
    u32 shader_wait_us = 0;
};
void SetQualityOptions(const QualityOptions& options);
const QualityOptions& GetQualityOptions();

/// Averages per presented frame since the previous call.
struct FrameStats {
    double gpu_ms = 0.0;   ///< GPU time
    double present_ms = 0.0; ///< Part of the GPU time spent presenting (MetalFX, screens)
    double stall_ms = 0.0; ///< Time the emulation thread waited for the GPU (readbacks, pacing)
    double stalls = 0.0;   ///< Number of those waits
    double draws = 0.0;    ///< Draw calls
    double skipped = 0.0;  ///< Draws skipped while their pipeline was being built
    double render_passes = 0.0;   ///< Render passes (each loads and stores its attachments)
    double msaa_refreshes = 0.0;  ///< Multisampled copies refreshed (MSAA), each an extra pass
    double blit_passes = 0.0;     ///< Blit passes (copies, uploads), each splits a render pass
    double feedback_copies = 0.0; ///< Copies of a render target that is also sampled
    double drawable_ms = 0.0;     ///< Time waiting for the layer to hand out a drawable
    double present_interval_ms = 0.0; ///< Average time between frames reaching the display
    double present_jitter_ms = 0.0;   ///< Standard deviation of that time
    double fg_ms = 0.0;      ///< GPU time per made frame (frame generation)
    double fg_made = 0.0;    ///< Frames made in the interval
    double fg_skipped = 0.0; ///< Frames not made because the previous one was still being made
    double present_late = 0.0;        ///< Frames shown clearly later than the frame time, per second
    /// Where frames land within a display refresh (ms after their due time, mean) and how
    /// tightly (1 all in the same place, 0 spread over the whole refresh).
    double phase_ms = 0.0;
    double phase_lock = 0.0;
    double presented = 0.0;  ///< Frames that reached the screen in the interval
    double cpu_ms = 0.0;     ///< Emulation thread CPU time per frame (without waiting)
    double cpu_max_ms = 0.0; ///< The longest frame of those
    u32 frames = 0;
};
FrameStats TakeFrameStats();

/// Turns the collecting of the statistics for the log (render passes, CPU time) on or off. Off,
/// the renderer does not do the work of keeping them. Defined in mtl_scheduler.mm.
void SetStatisticsEnabled(bool enabled);
bool StatisticsEnabled();

/// Where the GPU readbacks of the last interval came from, see mtl_scheduler.h.
std::vector<std::string> TakeReadbackSummary(std::size_t max_lines);

/// What the render passes of the last interval were, see mtl_scheduler.h.
std::vector<std::string> TakeRenderPassSummary(std::size_t max_lines);

/// How long the GPU spent on each kind of render pass over the last interval, per frame, from
/// the GPU's own timestamps (see Scheduler::AttachPassTiming). Empty when the GPU cannot time
/// passes.
std::vector<std::string> TakePassTimingSummary(std::size_t max_lines);

/// Whether MetalFX upscaling can be used on this device.
bool IsUpscalingSupported();

/**
 * Creates the Metal renderer. The render surface of the window (WindowSystemInfo::render_surface)
 * must be a CAMetalLayer, which the renderer configures and presents to.
 */
std::unique_ptr<VideoCore::RendererBase> CreateRenderer(Core::System& system, Pica::PicaCore& pica,
                                                        Frontend::EmuWindow& window,
                                                        Frontend::EmuWindow* secondary_window);

} // namespace Metal
