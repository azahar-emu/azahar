// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <time.h>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "common/assert.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/perf_stats.h"
#include "video_core/gpu.h"
#include "video_core/pica/pica_core.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_metal/mtl_instance.h"
#include "video_core/renderer_metal/mtl_rasterizer.h"
#include "video_core/renderer_metal/mtl_scheduler.h"
#include "video_core/renderer_metal/mtl_utility_shaders.h"
#include "video_core/renderer_metal/renderer_metal.h"

#import <QuartzCore/QuartzCore.h>
#import <objc/message.h>
#import <simd/simd.h>

// MetalFX is weakly linked, the spatial scaler needs iOS 16 / macOS 13.
#if __has_include(<MetalFX/MetalFX.h>)
#import <MetalFX/MetalFX.h>
#define AZAHAR_HAS_METALFX 1
#else
#define AZAHAR_HAS_METALFX 0
#endif

MICROPROFILE_DEFINE(Metal_RenderFrame, "Metal", "Render Frame", MP_RGB(128, 128, 64));

namespace Metal {

/// Frames of frame generation waiting for the display (display-driven pacing). The display link
/// takes the next one at a refresh once the one on screen has been shown for its slot (half a
/// frame of the game), so the frames follow the display's own refreshes.
struct DisplayPacer {
    struct Frame {
        id<MTLTexture> texture = nil;
        float width = 0.0f;
        float height = 0.0f;
    };
    std::mutex mutex;
    std::deque<Frame> waiting;
    double shown_at = 0.0; ///< Presentation time of the frame on screen, 0 for none
    double slot = 1.0 / 60.0;
    id<MTLCommandQueue> command_queue = nil;
    /// Draws a frame over the whole drawable.
    std::function<void(id<MTLRenderCommandEncoder>, const Frame&)> draw;
};
} // namespace Metal

API_AVAILABLE(ios(17.0), macos(14.0), tvos(17.0))
@interface AzaharPacerDelegate : NSObject <CAMetalDisplayLinkDelegate> {
@public
    std::shared_ptr<Metal::DisplayPacer> pacer;
}
@end

@implementation AzaharPacerDelegate
- (void)metalDisplayLink:(CAMetalDisplayLink*)link needsUpdate:(CAMetalDisplayLinkUpdate*)update {
    const std::shared_ptr<Metal::DisplayPacer> p = pacer;
    if (!p) {
        return;
    }
    const double target = update.targetPresentationTimestamp;
    Metal::DisplayPacer::Frame next;
    {
        std::scoped_lock lock{p->mutex};
        // Behind (the game or the interpolation hitched): keep only the newest pair.
        while (p->waiting.size() > 2) {
            p->waiting.pop_front();
        }
        const bool due = p->shown_at <= 0.0 || target - p->shown_at >= p->slot - 0.002 ||
                         target < p->shown_at;
        if (p->waiting.empty() || !due) {
            return; // The frame on screen stays for this refresh
        }
        next = p->waiting.front();
        p->waiting.pop_front();
        p->shown_at = target;
    }
    id<CAMetalDrawable> drawable = update.drawable;
    if (drawable == nil || next.texture == nil) {
        return;
    }
    id<MTLCommandBuffer> buffer = [p->command_queue commandBuffer];
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> draw = [buffer renderCommandEncoderWithDescriptor:pass];
    p->draw(draw, next);
    [draw endEncoding];
    [drawable addPresentedHandler:^(id<MTLDrawable> presented) {
      Metal::RecordPresentedTime(presented.presentedTime);
    }];
    [buffer presentDrawable:drawable];
    [buffer commit];
}
@end

namespace Metal {

namespace {

FrameGeneratorHooks g_fg_hooks;

struct ScreenRectVertex {
    ScreenRectVertex() = default;
    ScreenRectVertex(float x, float y, float u, float v) : position{x, y}, tex_coord{u, v} {}

    std::array<f32, 2> position{};
    std::array<f32, 2> tex_coord{};
};

struct PresentUniforms {
    std::array<f32, 4> o_resolution;
    std::array<f32, 4> fill_color;
    s32 use_fill;
    s32 reverse_interlaced;
    f32 interlace_rows;
    s32 interlace_use_y;
    s32 effect;
    s32 anaglyph_mode;
    s32 sharp_pixels;
    s32 sharpness;
    s32 sharpen_method;
    s32 scale_filter;
    s32 grid_strength;
    s32 saturation;
    s32 contrast;
    s32 brightness;
    s32 pad0;
    s32 pad1;
};
static_assert(sizeof(PresentUniforms) == 96);

std::atomic<int> g_post_effect{0};
std::atomic<int> g_sharp_pixels{0};
std::atomic<int> g_sharpness{0};
std::atomic<int> g_sharpen_method{0};
std::atomic<int> g_scale_filter{0};
std::atomic<int> g_grid_strength{0};
std::atomic<int> g_saturation{0};
std::atomic<int> g_contrast{0};
std::atomic<int> g_brightness{0};
std::atomic<int> g_anaglyph_mode{0};
std::mutex g_custom_post_mutex;
std::string g_custom_post_path;

std::atomic<int> g_upscaler_mode{static_cast<int>(UpscalerMode::Off)};
std::atomic<float> g_upscale_factor{0.0f};

/// The size MetalFX makes a picture: the factor times its size, never more than the size it is
/// shown at (more would only be scaled back down), or that size when no factor is set.
u32 UpscaledSize(u32 in, u32 shown) {
    const float factor = g_upscale_factor.load(std::memory_order_relaxed);
    if (factor <= 0.0f) {
        return shown;
    }
    return std::min(shown, static_cast<u32>(std::lround(static_cast<float>(in) * factor)));
}

/// Vertices that draw a screen texture (or the part of it given by `texcoords`) into a rectangle.
/// The 3DS framebuffers are stored rotated, the orientation takes care of that.
bool MakeScreenVertices(std::array<ScreenRectVertex, 4>& vertices,
                        const Common::Rectangle<f32>& texcoords, float x, float y, float w,
                        float h, Layout::DisplayOrientation orientation) {
    switch (orientation) {
    case Layout::DisplayOrientation::Landscape:
        vertices = {{
            ScreenRectVertex(x, y, texcoords.bottom, texcoords.left),
            ScreenRectVertex(x + w, y, texcoords.bottom, texcoords.right),
            ScreenRectVertex(x, y + h, texcoords.top, texcoords.left),
            ScreenRectVertex(x + w, y + h, texcoords.top, texcoords.right),
        }};
        return true;
    case Layout::DisplayOrientation::Portrait:
        vertices = {{
            ScreenRectVertex(x, y, texcoords.bottom, texcoords.right),
            ScreenRectVertex(x + w, y, texcoords.top, texcoords.right),
            ScreenRectVertex(x, y + h, texcoords.bottom, texcoords.left),
            ScreenRectVertex(x + w, y + h, texcoords.top, texcoords.left),
        }};
        return true;
    case Layout::DisplayOrientation::LandscapeFlipped:
        vertices = {{
            ScreenRectVertex(x, y, texcoords.top, texcoords.right),
            ScreenRectVertex(x + w, y, texcoords.top, texcoords.left),
            ScreenRectVertex(x, y + h, texcoords.bottom, texcoords.right),
            ScreenRectVertex(x + w, y + h, texcoords.bottom, texcoords.left),
        }};
        return true;
    case Layout::DisplayOrientation::PortraitFlipped:
        vertices = {{
            ScreenRectVertex(x, y, texcoords.top, texcoords.left),
            ScreenRectVertex(x + w, y, texcoords.bottom, texcoords.left),
            ScreenRectVertex(x, y + h, texcoords.top, texcoords.right),
            ScreenRectVertex(x + w, y + h, texcoords.bottom, texcoords.right),
        }};
        return true;
    default:
        return false;
    }
}

/// Draws a texture into a rectangle of the target (in pixels) with the present pipeline, without
/// an effect and at full opacity. `width` and `height` are the size of the target.
void EncodeRect(id<MTLRenderCommandEncoder> encoder, id<MTLRenderPipelineState> pipeline,
                id<MTLSamplerState> sampler, id<MTLTexture> texture, f32 width, f32 height,
                f32 x, f32 y, f32 w, f32 h, f32 u0 = 0.0f, f32 v0 = 0.0f, f32 u1 = 1.0f,
                f32 v1 = 1.0f) {
    [encoder setRenderPipelineState:pipeline];
    [encoder setViewport:MTLViewport{0.0, 0.0, width, height, 0.0, 1.0}];
    [encoder setBlendColorRed:0.0f green:0.0f blue:0.0f alpha:1.0f];
    [encoder setFragmentSamplerState:sampler atIndex:0];
    const std::array<ScreenRectVertex, 4> vertices{{
        ScreenRectVertex(x, y, u0, v0),
        ScreenRectVertex(x + w, y, u1, v0),
        ScreenRectVertex(x, y + h, u0, v1),
        ScreenRectVertex(x + w, y + h, u1, v1),
    }};
    const PresentUniforms uniforms = {
        .o_resolution = {width, height, 1.0f / width, 1.0f / height},
        .fill_color = {},
        .use_fill = 0,
        .effect = 0,
    };
    [encoder setVertexBytes:vertices.data()
                     length:sizeof(ScreenRectVertex) * vertices.size()
                    atIndex:0];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setFragmentTexture:texture atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
}

/// Draws a texture over the whole target except the rectangle (in pixels), which something else
/// is going to cover: four strips around it with the matching part of the texture. The strips
/// share their edges with the rectangle, so nothing is drawn twice and nothing is left out.
void EncodeAround(id<MTLRenderCommandEncoder> encoder, id<MTLRenderPipelineState> pipeline,
                  id<MTLSamplerState> sampler, id<MTLTexture> texture, f32 width, f32 height,
                  f32 x, f32 y, f32 w, f32 h) {
    const f32 left = std::clamp(x, 0.0f, width);
    const f32 right = std::clamp(x + w, left, width);
    const f32 top = std::clamp(y, 0.0f, height);
    const f32 bottom = std::clamp(y + h, top, height);
    if (right <= left || bottom <= top) {
        // Nothing to keep free.
        EncodeRect(encoder, pipeline, sampler, texture, width, height, 0.0f, 0.0f, width, height);
        return;
    }
    const auto strip = [&](f32 sx, f32 sy, f32 sw, f32 sh) {
        if (sw <= 0.0f || sh <= 0.0f) {
            return;
        }
        EncodeRect(encoder, pipeline, sampler, texture, width, height, sx, sy, sw, sh, sx / width,
                   sy / height, (sx + sw) / width, (sy + sh) / height);
    };
    strip(0.0f, 0.0f, width, top);                      // above
    strip(0.0f, bottom, width, height - bottom);        // below
    strip(0.0f, top, left, bottom - top);               // left
    strip(right, top, width - right, bottom - top);     // right
}

/// Draws a texture over the whole target.
void EncodeFullscreen(id<MTLRenderCommandEncoder> encoder, id<MTLRenderPipelineState> pipeline,
                      id<MTLSamplerState> sampler, id<MTLTexture> texture, f32 width, f32 height) {
    EncodeRect(encoder, pipeline, sampler, texture, width, height, 0.0f, 0.0f, width, height);
}

#if AZAHAR_HAS_METALFX
/// Spatial scalers only exist on iOS 16 / macOS 13 and newer, and not on every GPU.
bool MetalFXSupported(id<MTLDevice> device) {
    if (@available(iOS 16.0, macOS 13.0, *)) {
        return NSClassFromString(@"MTLFXSpatialScalerDescriptor") != nil &&
               [MTLFXSpatialScalerDescriptor supportsDevice:device];
    }
    return false;
}
#endif

class RendererMetal final : public VideoCore::RendererBase {
public:
    explicit RendererMetal(Core::System& system, Pica::PicaCore& pica_, Frontend::EmuWindow& window,
                           Frontend::EmuWindow* secondary_window)
        : RendererBase{system, window, secondary_window}, pica{pica_}, scheduler{instance},
          rasterizer{system.Memory(), pica_,    system.CustomTexManager(),
                     *this,           instance, scheduler} {
        CompilePresentPipelines();
        AcquireLayer();
    }

    ~RendererMetal() override {
        StopPacer();
        scheduler.Finish();
        // A made frame may still be on its way to the screen, its callbacks use this renderer.
        for (int i = 0; i < 200 && fg_busy.load(std::memory_order_acquire); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    [[nodiscard]] VideoCore::RasterizerInterface* Rasterizer() override {
        return &rasterizer;
    }

    void NotifySurfaceChanged(bool second) override {
        if (second) {
            second_layer_dirty = true;
        } else {
            layer_dirty = true;
        }
    }

    void SwapBuffers() override;

    void TryPresent(int timeout_ms, bool is_secondary) override {}

private:
    void CompilePresentPipelines();
    void AcquireLayer();
    CAMetalLayer* ConfigureLayer(void* surface);
    void PrepareRendertarget();
    void LoadFBToScreenInfo(const Pica::FramebufferConfig& framebuffer, ScreenInfo& screen_info,
                            bool right_eye);
    void RenderToLayer(CAMetalLayer* target, const Layout::FramebufferLayout& layout,
                       bool allow_upscale);
    bool UseFrameGeneration(const Layout::FramebufferLayout& layout);
    void RenderGenerated(CAMetalLayer* target, const Layout::FramebufferLayout& layout);
    bool RenderPacedPlain(const Layout::FramebufferLayout& layout);
    /// When frame generation last made a pair of frames: the display link stays a little while
    /// after, so a short stretch without frame generation does not restart it.
    std::chrono::steady_clock::time_point fg_last_generated{};
    bool PrepareFrameGenerationResources();
    void RenderScreenshot();
    void DrawScreens(id<MTLRenderCommandEncoder> encoder, const Layout::FramebufferLayout& layout);
    void DrawTopScreen(id<MTLRenderCommandEncoder> encoder, const Layout::FramebufferLayout& layout,
                       const Common::Rectangle<u32>& top_screen);
    void DrawBottomScreen(id<MTLRenderCommandEncoder> encoder,
                          const Layout::FramebufferLayout& layout,
                          const Common::Rectangle<u32>& bottom_screen);
    void DrawSingleScreen(id<MTLRenderCommandEncoder> encoder, u32 screen_id, float x, float y,
                          float w, float h, Layout::DisplayOrientation orientation);
    void DrawSingleScreenStereo(id<MTLRenderCommandEncoder> encoder, u32 screen_id_l,
                                u32 screen_id_r, float x, float y, float w, float h,
                                Layout::DisplayOrientation orientation);
    void SetOpacity(id<MTLRenderCommandEncoder> encoder, float alpha);
    void LimitFramesAheadOfGpu();
    void DrawQuad(id<MTLRenderCommandEncoder> encoder,
                  const std::array<ScreenRectVertex, 4>& vertices, id<MTLTexture> texture,
                  const PresentUniforms& uniforms);

#if AZAHAR_HAS_METALFX
    /// One screen draw that is run through the MetalFX spatial scaler.
    struct UpscaleJob {
        u32 screen_id{};
        Layout::DisplayOrientation orientation{};
        Common::Rectangle<f32> texcoords{};
        u32 in_width{};
        u32 in_height{};
        u32 out_width{};
        u32 out_height{};
        bool eligible{};
        bool ready{};
    };
    struct UpscaleSlot {
        id<MTLFXSpatialScaler> scaler = nil;
        id<MTLTexture> input = nil;
        id<MTLTexture> output = nil;
        u32 in_width{}, in_height{}, out_width{}, out_height{};
        bool failed{};
        bool has_output{}; ///< `output` holds the scaled result of a game frame
    };
    enum class UpscalePass { Off, Collect, Draw };

    bool UpscalingActive();
    void PrepareUpscaling(const Layout::FramebufferLayout& layout);
    bool EnsureUpscaleSlot(UpscaleSlot& slot, const UpscaleJob& job);
    void DrawUpscaled(id<MTLRenderCommandEncoder> encoder, const UpscaleJob& job, float x,
                      float y, float w, float h);
    UpscaleJob MakeUpscaleJob(u32 screen_id, float w, float h,
                              Layout::DisplayOrientation orientation) const;
#endif

private:
    Pica::PicaCore& pica;
    Instance instance;
    Scheduler scheduler;
    RasterizerMetal rasterizer;

    CAMetalLayer* layer = nil;
    bool layer_dirty{};
    CAMetalLayer* second_layer = nil; ///< Layer of the secondary window (an external display)
    bool second_layer_dirty{true};

    id<MTLRenderPipelineState> present_pipeline = nil;
    /// A post-processing shader the user wrote (see SetCustomPostShader), or nil.
    id<MTLRenderPipelineState> custom_post_pipeline = nil;
    id<MTLRenderPipelineState> present_anaglyph_pipeline = nil;
    id<MTLRenderPipelineState> present_interlaced_pipeline = nil;
    std::array<id<MTLSamplerState>, 2> present_samplers{};

    std::array<ScreenInfo, 3> screen_infos{};
    std::array<std::array<f32, 4>, 3> fill_colors{};
    std::array<bool, 3> use_fill{};
    std::array<f32, 4> o_resolution{};
    u64 previous_frame_tick{};
    u32 held_frames{};

    // Frame generation (see FrameGeneratorHooks). A frame of the game is drawn at full size into
    // a color picture, which is what is shown as the real frame. A smaller copy of the screens'
    // area goes into one of four slots; the interpolation makes the frame between the copies of
    // the previous frame and this one, which is scaled back up and shown before the real frame.
    static constexpr u32 FG_SLOTS = 4;
    // Enough for the frames that wait for the display with display-driven pacing.
    static constexpr u32 FG_COLORS = 5;
    static constexpr u32 FG_MADE = 3;
    bool fg_ready{};
    bool fg_failed{};
    u32 fg_layout_width{}, fg_layout_height{}; ///< Screen size the pictures were made for
    u32 fg_slot_width{}, fg_slot_height{};     ///< Size of the slots (the smaller copies)
    /// The area of the color picture that is copied into a slot and where the made frame is drawn
    /// back, in pixels of the screen: the screens, not the empty space around them.
    f32 fg_area_x{}, fg_area_y{}, fg_area_width{}, fg_area_height{};
    Common::Rectangle<u32> fg_screens{}; ///< The screens' area the slots were made for
    int fg_size{-1};                     ///< frame_generation_size the slots were made with
    u32 fg_cur{};
    int fg_prev{-1};
    /// A frame is being made: its callbacks show it and the real frame after it.
    std::atomic<bool> fg_busy{false};
    /// When the frame in progress was started (steady clock seconds), and a counter that makes
    /// the callbacks of a frame given up on (see RenderGenerated) do nothing.
    std::atomic<double> fg_busy_since{0.0};
    std::atomic<u32> fg_epoch{0};
    /// Made frames that failed in a row: when the interpolation rejects every frame, stop.
    std::atomic<u32> fg_failures{0};
    /// When the last real frame is set to reach the screen (CACurrentMediaTime), which spaces the
    /// frames after it evenly. Used by the callbacks that show the frames.
    std::atomic<double> fg_last_real_time{0.0};
    u32 fg_swaps{};      ///< SwapBuffers calls (60 per second of emulation)
    u32 fg_last_swaps{}; ///< fg_swaps at the previous new frame
    f32 fg_cadence{1.0f}; ///< Swaps per new frame of the game, smoothed: 2 for a 30 FPS game
    std::chrono::steady_clock::time_point fg_last_frame{};
    double fg_interval{};
    /// The pictures of the frames in color at full size, shown as the real frames.
    std::array<id<MTLTexture>, FG_COLORS> fg_color{};
    /// Made frames composed at full size, for display-driven pacing.
    std::array<id<MTLTexture>, FG_MADE> fg_made{};
    u32 fg_made_index{};
    /// The method the resources were made for (MetalFX's interpolator needs shader writes on the
    /// made frames), and the color picture of the previous real frame.
    bool fg_metalfx{false};
    int fg_prev_color{-1};
    // MetalFX frame interpolation (the other method of frame generation). The objects are used
    // through the Objective-C runtime (selectors and key-value coding), so the code does not
    // depend on the SDK's declarations.
    id fx_interpolator = nil;
    id fx_scaler = nil; ///< A temporal scaler at 1:1, only when the interpolator wants one
    std::array<id<MTLTexture>, 2> fx_scaled{};
    id<MTLTexture> fx_depth = nil;
    id<MTLTexture> fx_motion = nil;
    /// The interpolation runs on a queue of its own, after the real picture (an event), so the
    /// next frames of the game do not wait behind it on the GPU.
    id<MTLCommandQueue> fx_queue = nil;
    id<MTLEvent> fx_event = nil;
    u64 fx_event_value{};
    u32 fx_width{}, fx_height{};             ///< Size the interpolator works at
    u32 fx_layout_width{}, fx_layout_height{}; ///< Screen size it was made for
    f32 fx_scale{1.0f};
    f32 fx_area_width{}, fx_area_height{}; ///< Area of the screen the interpolation covers
    /// With a scale below one the pictures are shrunk before and the made frame is enlarged
    /// after: the interpolator's cost goes with the pixels.
    std::array<id<MTLTexture>, 2> fx_small_color{};
    id<MTLTexture> fx_small_out = nil;
    id<MTLTexture> fx_full_out = nil; ///< Output at the screen size (when not shrunk)
    u32 fx_small_index{};
    bool fx_failed{false};
    bool fx_reset{true};
    u32 fx_scaled_index{};
    bool PrepareMetalFX(u32 layout_width, u32 layout_height, f32 area_width, f32 area_height,
                        f32 scale);
    bool EncodeMetalFX(id<MTLCommandBuffer> buffer, id<MTLTexture> color, id<MTLTexture> previous,
                       id<MTLTexture> output, double delta_time);
    /// Display-driven pacing: a CAMetalDisplayLink on a thread of its own shows the frames.
    std::shared_ptr<DisplayPacer> fg_pacer;
    id fg_pacer_link = nil;     ///< CAMetalDisplayLink (iOS 17)
    id fg_pacer_delegate = nil; ///< AzaharPacerDelegate
    NSThread* fg_pacer_thread = nil;
    // Signalled by the pacing thread once it has invalidated the display link.
    dispatch_semaphore_t fg_pacer_done = nil;
    CAMetalLayer* fg_pacer_layer = nil;
    int fg_pacer_hz{};
    bool StartPacer(CAMetalLayer* layer);
    void StopPacer();
    u32 fg_color_index{};
#if AZAHAR_HAS_METALFX
    /// Scales the made frame back up to the size of its area on the screen.
    UpscaleSlot fg_upscale{};
#endif
    id<MTLComputePipelineState> fg_to_ycbcr = nil;   ///< Color picture to the slot's planes
    id<MTLRenderPipelineState> fg_from_ycbcr = nil;  ///< Slot planes onto the screen
    id<MTLSamplerState> fg_sampler = nil;

#if AZAHAR_HAS_METALFX
    std::vector<UpscaleJob> upscale_jobs;
    std::vector<UpscaleSlot> upscale_slots;
    UpscalePass upscale_pass{UpscalePass::Off};
    std::size_t upscale_cursor{};
    int metalfx_support{-1}; // -1 unknown, 0 no, 1 yes
#endif
};

void RendererMetal::CompilePresentPipelines() {
    id<MTLDevice> device = instance.GetDevice();
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:ToNSString(Shaders::PRESENT)
                                                  options:[MTLCompileOptions new]
                                                    error:&error];
    ASSERT_MSG(library, "Failed to compile Metal present shaders: {}",
               error ? [error.localizedDescription UTF8String] : "unknown error");

    const auto make_pipeline = [&](MTLPixelFormat format, NSString* fragment = @"present_fs") {
        MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
        desc.vertexFunction = [library newFunctionWithName:@"present_vs"];
        desc.fragmentFunction = [library newFunctionWithName:fragment];
        MTLRenderPipelineColorAttachmentDescriptor* color = desc.colorAttachments[0];
        color.pixelFormat = format;
        // The second layer (e.g. a translucent bottom screen) is blended using the blend color.
        color.blendingEnabled = YES;
        color.rgbBlendOperation = MTLBlendOperationAdd;
        color.alphaBlendOperation = MTLBlendOperationAdd;
        color.sourceRGBBlendFactor = MTLBlendFactorBlendAlpha;
        color.destinationRGBBlendFactor = MTLBlendFactorOneMinusBlendAlpha;
        color.sourceAlphaBlendFactor = MTLBlendFactorOne;
        color.destinationAlphaBlendFactor = MTLBlendFactorZero;
        NSError* pipeline_error = nil;
        id<MTLRenderPipelineState> pipeline =
            [device newRenderPipelineStateWithDescriptor:desc error:&pipeline_error];
        ASSERT_MSG(pipeline, "Failed to create Metal present pipeline: {}",
                   pipeline_error ? [pipeline_error.localizedDescription UTF8String] : "unknown");
        return pipeline;
    };
    present_pipeline = make_pipeline(MTLPixelFormatBGRA8Unorm);

    // A shader the user wrote: their post_effect function inside a fragment function of ours.
    std::string custom_path;
    {
        std::scoped_lock lock{g_custom_post_mutex};
        custom_path = g_custom_post_path;
    }
    if (!custom_path.empty()) {
        std::string user_source;
        if (FileUtil::ReadFileToString(true, custom_path, user_source) > 0) {
            std::string source{Shaders::PRESENT};
            source += "\n// ---- user shader ----\n";
            source += user_source;
            source += "\n// ---- end of user shader ----\n";
            source += Shaders::PRESENT_POST_WRAPPER;
            NSError* custom_error = nil;
            id<MTLLibrary> custom_library =
                [device newLibraryWithSource:ToNSString(source)
                                     options:[MTLCompileOptions new]
                                       error:&custom_error];
            if (custom_library == nil) {
                LOG_ERROR(Render, "The post-processing shader {} does not compile: {}", custom_path,
                          custom_error ? [custom_error.localizedDescription UTF8String]
                                       : "unknown error");
            } else {
                MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
                desc.vertexFunction = [custom_library newFunctionWithName:@"present_vs"];
                desc.fragmentFunction = [custom_library newFunctionWithName:@"present_post_fs"];
                MTLRenderPipelineColorAttachmentDescriptor* color = desc.colorAttachments[0];
                color.pixelFormat = MTLPixelFormatBGRA8Unorm;
                color.blendingEnabled = YES;
                color.rgbBlendOperation = MTLBlendOperationAdd;
                color.alphaBlendOperation = MTLBlendOperationAdd;
                color.sourceRGBBlendFactor = MTLBlendFactorBlendAlpha;
                color.destinationRGBBlendFactor = MTLBlendFactorOneMinusBlendAlpha;
                color.sourceAlphaBlendFactor = MTLBlendFactorOne;
                color.destinationAlphaBlendFactor = MTLBlendFactorZero;
                NSError* pipeline_error = nil;
                custom_post_pipeline =
                    [device newRenderPipelineStateWithDescriptor:desc error:&pipeline_error];
                if (custom_post_pipeline == nil) {
                    LOG_ERROR(Render, "The post-processing shader {} cannot be used: {}",
                              custom_path,
                              pipeline_error ? [pipeline_error.localizedDescription UTF8String]
                                             : "unknown error");
                } else {
                    LOG_INFO(Render, "Post-processing shader: {}", custom_path);
                }
            }
        } else {
            LOG_ERROR(Render, "The post-processing shader {} cannot be read", custom_path);
        }
    }
    present_anaglyph_pipeline = make_pipeline(MTLPixelFormatBGRA8Unorm, @"present_anaglyph_fs");
    present_interlaced_pipeline = make_pipeline(MTLPixelFormatBGRA8Unorm, @"present_interlaced_fs");

    MTLSamplerDescriptor* sampler_desc = [MTLSamplerDescriptor new];
    sampler_desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_desc.minFilter = MTLSamplerMinMagFilterLinear;
    sampler_desc.magFilter = MTLSamplerMinMagFilterLinear;
    present_samplers[0] = [device newSamplerStateWithDescriptor:sampler_desc];
    sampler_desc.minFilter = MTLSamplerMinMagFilterNearest;
    sampler_desc.magFilter = MTLSamplerMinMagFilterNearest;
    present_samplers[1] = [device newSamplerStateWithDescriptor:sampler_desc];
}

CAMetalLayer* RendererMetal::ConfigureLayer(void* surface) {
    if (!surface) {
        return nil;
    }
    CAMetalLayer* result = (__bridge CAMetalLayer*)surface;
    result.device = instance.GetDevice();
    result.pixelFormat = MTLPixelFormatBGRA8Unorm;
    result.framebufferOnly = YES;
    result.maximumDrawableCount = 3;
    return result;
}

void RendererMetal::AcquireLayer() {
    layer_dirty = false;
    layer = ConfigureLayer(render_window.GetWindowInfo().render_surface);
    if (layer == nil) {
        LOG_WARNING(Render, "No render surface provided, the Metal renderer runs headless");
    }
}

void RendererMetal::PrepareRendertarget() {
    const auto& framebuffer_config = pica.regs.framebuffer_config;
    const auto& regs_lcd = pica.regs_lcd;
    for (u32 i = 0; i < 3; i++) {
        const u32 fb_id = i == 2 ? 1 : 0;
        const auto& framebuffer = framebuffer_config[fb_id];

        const auto color_fill = fb_id == 0 ? regs_lcd.color_fill_top : regs_lcd.color_fill_bottom;
        if (color_fill.is_enabled) {
            const auto color = color_fill.AsVector();
            fill_colors[i] = {color.r() / 255.0f, color.g() / 255.0f, color.b() / 255.0f, 1.0f};
            use_fill[i] = true;
            continue;
        }
        use_fill[i] = false;
        LoadFBToScreenInfo(framebuffer, screen_infos[i], i == 1);
    }
}

void RendererMetal::LoadFBToScreenInfo(const Pica::FramebufferConfig& framebuffer,
                                       ScreenInfo& screen_info, bool right_eye) {
    if (framebuffer.address_right1 == 0 || framebuffer.address_right2 == 0) {
        right_eye = false;
    }

    const PAddr framebuffer_addr =
        framebuffer.active_fb == 0
            ? (right_eye ? framebuffer.address_right1 : framebuffer.address_left1)
            : (right_eye ? framebuffer.address_right2 : framebuffer.address_left2);

    const u32 bpp = Pica::BytesPerPixel(framebuffer.color_format);
    const std::size_t pixel_stride = framebuffer.stride / bpp;
    if (pixel_stride * bpp != framebuffer.stride || pixel_stride % 4 != 0) {
        LOG_ERROR(Render, "Unsupported framebuffer stride {}", framebuffer.stride);
        return;
    }

    if (!rasterizer.AccelerateDisplay(framebuffer, framebuffer_addr, static_cast<u32>(pixel_stride),
                                      screen_info)) {
        // Keep presenting the previous texture of this screen.
        LOG_DEBUG(Render, "Unable to display framebuffer at {:#010x}", framebuffer_addr);
    }
}

void RendererMetal::SetOpacity(id<MTLRenderCommandEncoder> encoder, float alpha) {
    [encoder setBlendColorRed:0.0f green:0.0f blue:0.0f alpha:alpha];
}

void RendererMetal::DrawQuad(id<MTLRenderCommandEncoder> encoder,
                             const std::array<ScreenRectVertex, 4>& vertices,
                             id<MTLTexture> texture, const PresentUniforms& uniforms) {
    [encoder setVertexBytes:vertices.data()
                     length:sizeof(ScreenRectVertex) * vertices.size()
                    atIndex:0];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    if (texture != nil) {
        [encoder setFragmentTexture:texture atIndex:0];
    }
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
}

void RendererMetal::DrawSingleScreen(id<MTLRenderCommandEncoder> encoder, u32 screen_id, float x,
                                     float y, float w, float h,
                                     Layout::DisplayOrientation orientation) {
    const ScreenInfo& screen_info = screen_infos[screen_id];
    if (!use_fill[screen_id] && screen_info.texture == nil) {
        return;
    }

#if AZAHAR_HAS_METALFX
    if (upscale_pass == UpscalePass::Collect) {
        // Only records what would be drawn, the scaler runs before the actual draw.
        upscale_jobs.push_back(MakeUpscaleJob(screen_id, w, h, orientation));
        return;
    }
    if (upscale_pass == UpscalePass::Draw) {
        if (upscale_cursor < upscale_jobs.size()) {
            const UpscaleJob& job = upscale_jobs[upscale_cursor++];
            if (job.ready) {
                DrawUpscaled(encoder, job, x, y, w, h);
                return;
            }
        }
    }
#endif

    std::array<ScreenRectVertex, 4> vertices;
    if (!MakeScreenVertices(vertices, screen_info.texcoords, x, y, w, h, orientation)) {
        LOG_ERROR(Render, "Unknown DisplayOrientation: {}", orientation);
        return;
    }
    const PresentUniforms uniforms = {
        .o_resolution = o_resolution,
        .fill_color = fill_colors[screen_id],
        .use_fill = use_fill[screen_id] ? 1 : 0,
        .effect = g_post_effect.load(std::memory_order_relaxed),
        .anaglyph_mode = 0,
        .sharp_pixels = g_sharp_pixels.load(std::memory_order_relaxed),
        .sharpness = g_sharpness.load(std::memory_order_relaxed),
        .sharpen_method = g_sharpen_method.load(std::memory_order_relaxed),
        .scale_filter = g_scale_filter.load(std::memory_order_relaxed),
        .grid_strength = g_grid_strength.load(std::memory_order_relaxed),
        .saturation = g_saturation.load(std::memory_order_relaxed),
        .contrast = g_contrast.load(std::memory_order_relaxed),
        .brightness = g_brightness.load(std::memory_order_relaxed),
        .pad0 = 0,
        .pad1 = 0,
    };
    DrawQuad(encoder, vertices, screen_info.texture, uniforms);
}

void RendererMetal::DrawSingleScreenStereo(id<MTLRenderCommandEncoder> encoder, u32 screen_id_l,
                                           u32 screen_id_r, float x, float y, float w, float h,
                                           Layout::DisplayOrientation orientation) {
    const ScreenInfo& left = screen_infos[screen_id_l];
    const ScreenInfo& right = screen_infos[screen_id_r];
    if (left.texture == nil || right.texture == nil) {
        return;
    }

    id<MTLTexture> left_texture = left.texture;
    id<MTLTexture> right_texture = right.texture;
    bool upscaled = false;
#if AZAHAR_HAS_METALFX
    // Each eye goes through the scaler on its own, the screens that show the same picture in both
    // eyes (the bottom screen) only once.
    if (upscale_pass == UpscalePass::Collect) {
        upscale_jobs.push_back(MakeUpscaleJob(screen_id_l, w, h, orientation));
        if (screen_id_r != screen_id_l) {
            upscale_jobs.push_back(MakeUpscaleJob(screen_id_r, w, h, orientation));
        }
        return;
    }
    if (upscale_pass == UpscalePass::Draw) {
        const std::size_t first = upscale_cursor;
        upscale_cursor += screen_id_r != screen_id_l ? 2 : 1;
        if (upscale_cursor <= upscale_jobs.size()) {
            const std::size_t second = screen_id_r != screen_id_l ? first + 1 : first;
            if (upscale_jobs[first].ready && upscale_jobs[second].ready) {
                left_texture = upscale_slots[first].output;
                right_texture = upscale_slots[second].output;
                upscaled = true;
            }
        }
    }
#endif

    std::array<ScreenRectVertex, 4> vertices;
    if (upscaled) {
        // The scaled screens already have the orientation they are shown in.
        vertices = {{
            ScreenRectVertex(x, y, 0.0f, 0.0f),
            ScreenRectVertex(x + w, y, 1.0f, 0.0f),
            ScreenRectVertex(x, y + h, 0.0f, 1.0f),
            ScreenRectVertex(x + w, y + h, 1.0f, 1.0f),
        }};
    } else if (!MakeScreenVertices(vertices, left.texcoords, x, y, w, h, orientation)) {
        LOG_ERROR(Render, "Unknown DisplayOrientation: {}", orientation);
        return;
    }
    // The interlacing alternates over the extent along the texture's x axis, which is the height
    // of the rectangle when it is shown rotated.
    const bool rotated = orientation == Layout::DisplayOrientation::Portrait ||
                         orientation == Layout::DisplayOrientation::PortraitFlipped;

    const auto mode = Settings::values.render_3d.GetValue();
    PresentUniforms uniforms = {
        .o_resolution = o_resolution,
        .fill_color = {},
        .use_fill = 0,
        .reverse_interlaced = mode == Settings::StereoRenderOption::ReverseInterlaced ? 1 : 0,
        // Scaled screens have their rows along the y axis of the texture, native ones along x.
        .interlace_rows = (rotated && !upscaled) ? w : h,
        .interlace_use_y = upscaled ? 1 : 0,
        .effect = 0,
        .anaglyph_mode = g_anaglyph_mode.load(std::memory_order_relaxed),
    };
    [encoder setRenderPipelineState:mode == Settings::StereoRenderOption::Anaglyph
                                        ? present_anaglyph_pipeline
                                        : present_interlaced_pipeline];
    [encoder setFragmentTexture:left_texture atIndex:0];
    [encoder setFragmentTexture:right_texture atIndex:1];
    [encoder setVertexBytes:vertices.data()
                     length:sizeof(ScreenRectVertex) * vertices.size()
                    atIndex:0];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    // The other screens use the plain pipeline again.
    [encoder setRenderPipelineState:custom_post_pipeline != nil ? custom_post_pipeline
                                                                : present_pipeline];
}

#if AZAHAR_HAS_METALFX

RendererMetal::UpscaleJob RendererMetal::MakeUpscaleJob(
    u32 screen_id, float w, float h, Layout::DisplayOrientation orientation) const {
    const ScreenInfo& screen_info = screen_infos[screen_id];
    UpscaleJob job;
    job.screen_id = screen_id;
    job.orientation = orientation;
    job.texcoords = screen_info.texcoords;
    id<MTLTexture> texture = screen_info.texture;
    if (!use_fill[screen_id] && texture != nil && w >= 1.0f && h >= 1.0f) {
        const bool swap = orientation == Layout::DisplayOrientation::Landscape ||
                          orientation == Layout::DisplayOrientation::LandscapeFlipped;
        const auto& tc = job.texcoords;
        const float source_width = std::abs(tc.bottom - tc.top) * static_cast<float>(texture.width);
        const float source_height =
            std::abs(tc.right - tc.left) * static_cast<float>(texture.height);
        // Size of the screen at the resolution the game renders it in, in the orientation it is
        // shown in.
        job.in_width = static_cast<u32>(std::lround(swap ? source_height : source_width));
        job.in_height = static_cast<u32>(std::lround(swap ? source_width : source_height));
        job.out_width = UpscaledSize(job.in_width, static_cast<u32>(std::lround(w)));
        job.out_height = UpscaledSize(job.in_height, static_cast<u32>(std::lround(h)));
        // Only worth it when the screen is shown clearly larger than it is drawn: the scaler costs
        // several milliseconds of GPU time, and for a small enlargement (such as the bottom screen
        // at 3x) plain bilinear filtering looks the same.
        job.eligible = job.in_width >= 16 && job.in_height >= 16 &&
                       job.out_width * 4 >= job.in_width * 5 &&
                       job.out_height * 4 >= job.in_height * 5;
    }
    return job;
}

bool RendererMetal::UpscalingActive() {
    if (g_upscaler_mode.load(std::memory_order_relaxed) != static_cast<int>(UpscalerMode::Spatial)) {
        return false;
    }
    if (metalfx_support < 0) {
        metalfx_support = MetalFXSupported(instance.GetDevice()) ? 1 : 0;
        LOG_INFO(Render, "MetalFX spatial upscaling is {}",
                 metalfx_support ? "available" : "not available on this device");
    }
    return metalfx_support == 1;
}

bool RendererMetal::EnsureUpscaleSlot(UpscaleSlot& slot, const UpscaleJob& job) {
    if (slot.in_width == job.in_width && slot.in_height == job.in_height &&
        slot.out_width == job.out_width && slot.out_height == job.out_height) {
        return !slot.failed;
    }
    slot = UpscaleSlot{};
    slot.in_width = job.in_width;
    slot.in_height = job.in_height;
    slot.out_width = job.out_width;
    slot.out_height = job.out_height;
    slot.failed = true;

    if (@available(iOS 16.0, macOS 13.0, *)) {
        id<MTLDevice> device = instance.GetDevice();
        MTLFXSpatialScalerDescriptor* desc = [MTLFXSpatialScalerDescriptor new];
        desc.inputWidth = job.in_width;
        desc.inputHeight = job.in_height;
        desc.outputWidth = job.out_width;
        desc.outputHeight = job.out_height;
        desc.colorTextureFormat = MTLPixelFormatBGRA8Unorm;
        desc.outputTextureFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
        id<MTLFXSpatialScaler> scaler = [desc newSpatialScalerWithDevice:device];
        if (scaler == nil) {
            LOG_WARNING(Render, "MetalFX refused to scale {}x{} to {}x{}, drawing it unscaled",
                        job.in_width, job.in_height, job.out_width, job.out_height);
            return false;
        }

        MTLTextureDescriptor* in_desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:job.in_width
                                                              height:job.in_height
                                                           mipmapped:NO];
        in_desc.usage = MTLTextureUsageRenderTarget | scaler.colorTextureUsage;
        in_desc.storageMode = MTLStorageModePrivate;
        MTLTextureDescriptor* out_desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:job.out_width
                                                              height:job.out_height
                                                           mipmapped:NO];
        out_desc.usage = MTLTextureUsageShaderRead | scaler.outputTextureUsage;
        out_desc.storageMode = MTLStorageModePrivate;
        id<MTLTexture> input = [device newTextureWithDescriptor:in_desc];
        id<MTLTexture> output = [device newTextureWithDescriptor:out_desc];
        if (input == nil || output == nil) {
            LOG_WARNING(Render, "Unable to allocate the MetalFX textures");
            return false;
        }
        SetLabel(input, "MetalFX input");
        SetLabel(output, "MetalFX output");
        slot.scaler = scaler;
        slot.input = input;
        slot.output = output;
        slot.failed = false;
        LOG_INFO(Render, "MetalFX spatial scaler: {}x{} -> {}x{}", job.in_width, job.in_height,
                 job.out_width, job.out_height);
        return true;
    }
    return false;
}

void RendererMetal::PrepareUpscaling(const Layout::FramebufferLayout& layout) {
    upscale_jobs.clear();
    upscale_cursor = 0;
    upscale_pass = UpscalePass::Off;
    if (!UpscalingActive()) {
        return;
    }

    // Find out which screens are drawn and how big, without drawing anything.
    upscale_pass = UpscalePass::Collect;
    DrawScreens(nil, layout);

    if (upscale_slots.size() < upscale_jobs.size()) {
        upscale_slots.resize(upscale_jobs.size());
    }
    bool any = false;
    for (std::size_t i = 0; i < upscale_jobs.size(); i++) {
        UpscaleJob& job = upscale_jobs[i];
        if (!job.eligible || !EnsureUpscaleSlot(upscale_slots[i], job)) {
            continue;
        }
        UpscaleSlot& slot = upscale_slots[i];

        // The game did not present a new frame since the output was made (a 30 fps game shown at
        // 60 Hz), so scaling the same picture again would only burn GPU time.
        if (slot.has_output && !Core::PerfStats::game_frames_updated) {
            job.ready = true;
            any = true;
            continue;
        }

        // Draw the screen at the resolution it is rendered in, in the orientation it is shown in.
        // This also crops the part of the texture that is displayed and converts its format.
        MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
        desc.colorAttachments[0].texture = slot.input;
        desc.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        desc.colorAttachments[0].storeAction = MTLStoreActionStore;
        scheduler.SetPassLabel("metalfx input");
        id<MTLRenderCommandEncoder> encoder = scheduler.BeginRendering(desc);
        const float width = static_cast<float>(job.in_width);
        const float height = static_cast<float>(job.in_height);
        std::array<ScreenRectVertex, 4> vertices;
        if (!MakeScreenVertices(vertices, job.texcoords, 0.0f, 0.0f, width, height,
                                job.orientation)) {
            scheduler.EndEncoding();
            continue;
        }
        [encoder setRenderPipelineState:present_pipeline];
        [encoder setFragmentSamplerState:present_samplers[1] atIndex:0];
        [encoder setViewport:MTLViewport{0.0, 0.0, width, height, 0.0, 1.0}];
        SetOpacity(encoder, 1.0f);
        const PresentUniforms uniforms = {
            .o_resolution = {width, height, 1.0f / width, 1.0f / height},
            .fill_color = {},
            .use_fill = 0,
        };
        DrawQuad(encoder, vertices, screen_infos[job.screen_id].texture, uniforms);
        scheduler.EndEncoding();

        if (@available(iOS 16.0, macOS 13.0, *)) {
            slot.scaler.colorTexture = slot.input;
            slot.scaler.outputTexture = slot.output;
            slot.scaler.inputContentWidth = job.in_width;
            slot.scaler.inputContentHeight = job.in_height;
            [slot.scaler encodeToCommandBuffer:scheduler.CommandBuffer()];
            slot.has_output = true;
            job.ready = true;
            any = true;
        }
    }
    upscale_pass = any ? UpscalePass::Draw : UpscalePass::Off;
    upscale_cursor = 0;
}

void RendererMetal::DrawUpscaled(id<MTLRenderCommandEncoder> encoder, const UpscaleJob& job,
                                 float x, float y, float w, float h) {
    // The scaler output already has the right orientation and covers the whole rectangle.
    const std::array<ScreenRectVertex, 4> vertices{{
        ScreenRectVertex(x, y, 0.0f, 0.0f),
        ScreenRectVertex(x + w, y, 1.0f, 0.0f),
        ScreenRectVertex(x, y + h, 0.0f, 1.0f),
        ScreenRectVertex(x + w, y + h, 1.0f, 1.0f),
    }};
    // Find the slot of this job: jobs and slots share their index.
    const std::size_t index = static_cast<std::size_t>(&job - upscale_jobs.data());
    const PresentUniforms uniforms = {
        .o_resolution = o_resolution,
        .fill_color = {},
        .use_fill = 0,
        .effect = g_post_effect.load(std::memory_order_relaxed),
        .anaglyph_mode = 0,
        .sharp_pixels = g_sharp_pixels.load(std::memory_order_relaxed),
        .sharpness = g_sharpness.load(std::memory_order_relaxed),
        .sharpen_method = g_sharpen_method.load(std::memory_order_relaxed),
        .scale_filter = g_scale_filter.load(std::memory_order_relaxed),
        .grid_strength = g_grid_strength.load(std::memory_order_relaxed),
        .saturation = g_saturation.load(std::memory_order_relaxed),
        .contrast = g_contrast.load(std::memory_order_relaxed),
        .brightness = g_brightness.load(std::memory_order_relaxed),
        .pad0 = 0,
        .pad1 = 0,
    };
    DrawQuad(encoder, vertices, upscale_slots[index].output, uniforms);
}

#endif // AZAHAR_HAS_METALFX

void RendererMetal::DrawTopScreen(id<MTLRenderCommandEncoder> encoder,
                                  const Layout::FramebufferLayout& layout,
                                  const Common::Rectangle<u32>& top_screen) {
    if (!layout.top_screen_enabled) {
        return;
    }
    const int leftside = Settings::values.swap_eyes_3d.GetValue() ? 1 : 0;
    const int rightside = Settings::values.swap_eyes_3d.GetValue() ? 0 : 1;
    const float left = static_cast<float>(top_screen.left);
    const float top = static_cast<float>(top_screen.top);
    const float width = static_cast<float>(top_screen.GetWidth());
    const float height = static_cast<float>(top_screen.GetHeight());

    const auto orientation = layout.is_rotated ? Layout::DisplayOrientation::Landscape
                                               : Layout::DisplayOrientation::Portrait;
    switch (layout.render_3d_mode) {
    case Settings::StereoRenderOption::SideBySide:
        DrawSingleScreen(encoder, leftside, left / 2, top, width / 2, height, orientation);
        DrawSingleScreen(encoder, rightside, (left / 2) + (layout.width / 2), top, width / 2,
                         height, orientation);
        break;
    case Settings::StereoRenderOption::SideBySideFull:
        DrawSingleScreen(encoder, leftside, left, top, width, height, orientation);
        DrawSingleScreen(encoder, rightside, left + layout.width / 2, top, width, height,
                         orientation);
        break;
    case Settings::StereoRenderOption::CardboardVR:
        DrawSingleScreen(encoder, leftside, left, top, width, height, orientation);
        DrawSingleScreen(
            encoder, rightside,
            static_cast<float>(layout.cardboard.top_screen_right_eye + (layout.width / 2)), top,
            width, height, orientation);
        break;
    case Settings::StereoRenderOption::Anaglyph:
    case Settings::StereoRenderOption::Interlaced:
    case Settings::StereoRenderOption::ReverseInterlaced:
        DrawSingleScreenStereo(encoder, leftside, rightside, left, top, width, height,
                               orientation);
        break;
    case Settings::StereoRenderOption::Off:
    default: {
        const int eye = layout.render_3d_mode == Settings::StereoRenderOption::Off
                            ? static_cast<int>(Settings::values.mono_render_option.GetValue())
                            : leftside;
        DrawSingleScreen(encoder, eye, left, top, width, height, orientation);
        break;
    }
    }
}

void RendererMetal::DrawBottomScreen(id<MTLRenderCommandEncoder> encoder,
                                     const Layout::FramebufferLayout& layout,
                                     const Common::Rectangle<u32>& bottom_screen) {
    if (!layout.bottom_screen_enabled) {
        return;
    }
    const float left = static_cast<float>(bottom_screen.left);
    const float top = static_cast<float>(bottom_screen.top);
    const float width = static_cast<float>(bottom_screen.GetWidth());
    const float height = static_cast<float>(bottom_screen.GetHeight());

    const auto orientation = layout.is_rotated ? Layout::DisplayOrientation::Landscape
                                               : Layout::DisplayOrientation::Portrait;
    switch (layout.render_3d_mode) {
    case Settings::StereoRenderOption::SideBySide:
        DrawSingleScreen(encoder, 2, left / 2, top, width / 2, height, orientation);
        DrawSingleScreen(encoder, 2, (left / 2) + (layout.width / 2), top, width / 2, height,
                         orientation);
        break;
    case Settings::StereoRenderOption::SideBySideFull:
        DrawSingleScreen(encoder, 2, left, top, width, height, orientation);
        DrawSingleScreen(encoder, 2, left + layout.width / 2, top, width, height, orientation);
        break;
    case Settings::StereoRenderOption::CardboardVR:
        DrawSingleScreen(encoder, 2, left, top, width, height, orientation);
        DrawSingleScreen(
            encoder, 2,
            static_cast<float>(layout.cardboard.bottom_screen_right_eye + (layout.width / 2)), top,
            width, height, orientation);
        break;
    case Settings::StereoRenderOption::Anaglyph:
    case Settings::StereoRenderOption::Interlaced:
    case Settings::StereoRenderOption::ReverseInterlaced:
        DrawSingleScreenStereo(encoder, 2, 2, left, top, width, height, orientation);
        break;
    default:
        DrawSingleScreen(encoder, 2, left, top, width, height, orientation);
        break;
    }
}

void RendererMetal::DrawScreens(id<MTLRenderCommandEncoder> encoder,
                                const Layout::FramebufferLayout& layout) {
    o_resolution = {static_cast<f32>(layout.width), static_cast<f32>(layout.height),
                    1.0f / static_cast<f32>(layout.width), 1.0f / static_cast<f32>(layout.height)};

    [encoder setRenderPipelineState:custom_post_pipeline != nil ? custom_post_pipeline
                                                                : present_pipeline];
    [encoder setFragmentSamplerState:present_samplers[!Settings::values.filter_mode.GetValue()]
                             atIndex:0];
    [encoder setViewport:MTLViewport{0.0, 0.0, static_cast<double>(layout.width),
                                     static_cast<double>(layout.height), 0.0, 1.0}];
    SetOpacity(encoder, 1.0f);

    if (!Settings::values.swap_screen.GetValue()) {
        DrawTopScreen(encoder, layout, layout.top_screen);
        if (layout.bottom_opacity < 1) {
            SetOpacity(encoder, layout.bottom_opacity);
        }
        DrawBottomScreen(encoder, layout, layout.bottom_screen);
    } else {
        DrawBottomScreen(encoder, layout, layout.bottom_screen);
        if (layout.top_opacity < 1) {
            SetOpacity(encoder, layout.top_opacity);
        }
        DrawTopScreen(encoder, layout, layout.top_screen);
    }

    if (layout.additional_screen_enabled) {
        SetOpacity(encoder, 1.0f);
        if (!layout.additional_screen_is_bottom) {
            DrawTopScreen(encoder, layout, layout.additional_screen);
        } else {
            DrawBottomScreen(encoder, layout, layout.additional_screen);
        }
    }
}

void RendererMetal::RenderToLayer(CAMetalLayer* target, const Layout::FramebufferLayout& layout,
                                  bool allow_upscale) {
    if (target == nil || layout.width == 0 || layout.height == 0) {
        return;
    }

    const CGSize size = CGSizeMake(layout.width, layout.height);
    if (!CGSizeEqualToSize(target.drawableSize, size)) {
        target.drawableSize = size;
    }

    // What is about to be shown, about every two seconds: both screens black while the game runs
    // on needs this to tell a screen without a picture from a picture that is not reaching the
    // display.
    {
        static std::chrono::steady_clock::time_point last_report{};
        const auto now = std::chrono::steady_clock::now();
        if (now - last_report > std::chrono::seconds(2)) {
            last_report = now;
            const auto describe = [&](u32 i) {
                const ScreenInfo& info = screen_infos[i];
                return fmt::format("{}{}x{}", use_fill[i] ? "fill " : "",
                                   info.texture != nil ? static_cast<u32>(info.texture.width) : 0u,
                                   info.texture != nil ? static_cast<u32>(info.texture.height) : 0u);
            };
            LOG_INFO(Render,
                     "Showing: top {}, bottom {}, target {}x{}, frame generation {} (pacer {}, "
                     "ready {}, busy {})",
                     describe(0), describe(2), layout.width, layout.height,
                     GetQualityOptions().frame_generation, fg_pacer_link != nil ? "on" : "off",
                     fg_ready, fg_busy.load(std::memory_order_relaxed));
        }
    }

    if (allow_upscale && UseFrameGeneration(layout)) {
        // Takes its drawables itself, when the frames are ready to be shown.
        RenderGenerated(target, layout);
        return;
    }
    if (allow_upscale && fg_pacer_link != nil) {
        // A frame without frame generation. For a short while (a loading screen, a moment at 60
        // FPS) it goes through the display link too: stopping and restarting the link each time
        // would hitch. After that, or when frame generation is turned off, the layer hands its
        // drawables to us again.
        const QualityOptions& options = GetQualityOptions();
        const bool keep = options.frame_generation > 0 && options.frame_generation_display_paced &&
                          std::chrono::steady_clock::now() - fg_last_generated <
                              std::chrono::seconds(2);
        if (keep && RenderPacedPlain(layout)) {
            return;
        }
        StopPacer();
    }

    id<CAMetalDrawable> drawable = nil;
    const auto drawable_start = std::chrono::steady_clock::now();
    @autoreleasepool {
        drawable = [target nextDrawable];
    }
    Stats().drawable_ns.fetch_add(static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                       std::chrono::steady_clock::now() - drawable_start)
                                                       .count()),
                                  std::memory_order_relaxed);
    if (drawable == nil) {
        return;
    }

    // Submit the rendering of the game so the presentation (upscaling and drawing the screens)
    // gets a command buffer of its own, whose GPU time is reported separately.
    scheduler.Flush();
    scheduler.MarkPresentation();
#if AZAHAR_HAS_METALFX
    // MetalFX keeps its textures per screen, only the main layer uses it.
    if (allow_upscale) {
        PrepareUpscaling(layout);
    }
#endif

    MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
    desc.colorAttachments[0].texture = drawable.texture;
    desc.colorAttachments[0].loadAction = MTLLoadActionClear;
    desc.colorAttachments[0].storeAction = MTLStoreActionStore;
    desc.colorAttachments[0].clearColor =
        MTLClearColorMake(Settings::values.bg_red.GetValue(), Settings::values.bg_green.GetValue(),
                          Settings::values.bg_blue.GetValue(), 1.0);

    scheduler.SetPassLabel("present");
    id<MTLRenderCommandEncoder> encoder = scheduler.BeginRendering(desc);
    DrawScreens(encoder, layout);
    scheduler.EndEncoding();
    // The second display is not paced or measured, it only follows the first.
    scheduler.PresentDrawable(drawable, allow_upscale);
#if AZAHAR_HAS_METALFX
    upscale_pass = UpscalePass::Off;
#endif
}

bool RendererMetal::UseFrameGeneration(const Layout::FramebufferLayout& layout) {
    const int mode = GetQualityOptions().frame_generation;
    if (mode <= 0 || !g_fg_hooks.prepare || !g_fg_hooks.texture || !g_fg_hooks.interpolate) {
        fg_prev = -1;
        return false;
    }
    // The area of the screen the game is shown in. Interpolation costs the same for every pixel,
    // so the empty space around the screens is left out.
    Common::Rectangle<u32> screens{0, 0, layout.width, layout.height};
    {
        bool any = false;
        Common::Rectangle<u32> box{};
        const auto add = [&](const Common::Rectangle<u32>& r) {
            if (r.GetWidth() == 0 || r.GetHeight() == 0) {
                return;
            }
            if (!any) {
                box = r;
                any = true;
            } else {
                box.left = std::min(box.left, r.left);
                box.top = std::min(box.top, r.top);
                box.right = std::max(box.right, r.right);
                box.bottom = std::max(box.bottom, r.bottom);
            }
        };
        // Only the top screen when asked (and shown): the bottom one then keeps the game's frames.
        const bool top_only =
            GetQualityOptions().frame_generation_top_only && layout.top_screen_enabled;
        if (layout.top_screen_enabled) {
            add(layout.top_screen);
        }
        if (layout.bottom_screen_enabled && !top_only) {
            add(layout.bottom_screen);
        }
        if (any) {
            screens.left = std::min(box.left, layout.width);
            screens.top = std::min(box.top, layout.height);
            screens.right = std::clamp(box.right, screens.left, layout.width);
            screens.bottom = std::clamp(box.bottom, screens.top, layout.height);
        }
    }

    const int size = std::clamp(GetQualityOptions().frame_generation_size, 0, 4);
    if (layout.width != fg_layout_width || layout.height != fg_layout_height ||
        screens.left != fg_screens.left || screens.top != fg_screens.top ||
        screens.right != fg_screens.right || screens.bottom != fg_screens.bottom ||
        size != fg_size || GetQualityOptions().frame_generation_metalfx != fg_metalfx) {
        // The old setup (the interpolation session and its buffers, the textures, the pacing) may
        // still be in use by the frame that was last handed over, on a callback thread or the
        // display link. Rotating the device or swapping the screens changes the size, and
        // replacing all of it under that frame showed half a picture of unset planes (purple)
        // and froze the game. Stop the pacing and wait (bounded) for the last frame first.
        StopPacer();
        for (int i = 0; i < 250 && fg_busy.load(std::memory_order_acquire); i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if (fg_busy.load(std::memory_order_acquire)) {
            LOG_WARNING(Render, "Frame generation: the last frame is still being made, changing "
                                "the size anyway");
        }
        fg_size = size;
        fg_metalfx = GetQualityOptions().frame_generation_metalfx;
        fx_interpolator = nil;
        fx_scaler = nil;
        fx_failed = false;
        fg_layout_width = layout.width;
        fg_layout_height = layout.height;
        fg_screens = screens;
        fg_prev = -1;
        fg_ready = false;
        fg_failures.store(0, std::memory_order_relaxed);
        // The interpolation works on a copy of the chosen size (half the size is about a quarter
        // of the work), and the made frame is scaled back up. Only the chosen size: when the
        // interpolation refuses it, frame generation stays off rather than picking another size.
        const auto round_up = [](u32 value) { return (value + 15u) & ~15u; };
        // The values are stored settings: 0 full, 1 half, 2 three eighths, 3 a quarter, 4 three
        // quarters (added later, so it comes last).
        constexpr std::array<f32, 5> all_scales{1.0f, 0.5f, 0.375f, 0.25f, 0.75f};
        const std::array<f32, 1> scales{all_scales[size]};
        for (const f32 scale : scales) {
            if (scale <= 0.0f) {
                break;
            }
            // The whole screen at this scale, in multiples of 16 (video processors work in
            // blocks), rounded down so the area never reaches outside the screen.
            const u32 width = static_cast<u32>(std::floor(layout.width * scale)) & ~15u;
            const u32 height = static_cast<u32>(std::floor(layout.height * scale)) & ~15u;
            if (width < 64 || height < 64) {
                break;
            }
            const f32 sx = static_cast<f32>(width) / static_cast<f32>(layout.width);
            const f32 sy = static_cast<f32>(height) / static_cast<f32>(layout.height);
            const u32 slot_width = std::clamp(
                round_up(static_cast<u32>(std::ceil(screens.GetWidth() * sx))), 64u, width);
            const u32 slot_height = std::clamp(
                round_up(static_cast<u32>(std::ceil(screens.GetHeight() * sy))), 64u, height);
            if (g_fg_hooks.prepare((__bridge void*)instance.GetDevice(), slot_width,
                                   slot_height)) {
                fg_slot_width = slot_width;
                fg_slot_height = slot_height;
                // The slot covers a little more than the screens (the rounding up).
                fg_area_width = static_cast<f32>(slot_width) / sx;
                fg_area_height = static_cast<f32>(slot_height) / sy;
                fg_area_x = std::clamp(static_cast<f32>(screens.left), 0.0f,
                                       std::max(0.0f, layout.width - fg_area_width));
                fg_area_y = std::clamp(static_cast<f32>(screens.top), 0.0f,
                                       std::max(0.0f, layout.height - fg_area_height));
                fg_ready = PrepareFrameGenerationResources();
                if (!fg_ready) {
                    break;
                }
                const char* scaling = ", stretched with plain filtering";
#if AZAHAR_HAS_METALFX
                if (fg_upscale.scaler != nil) {
                    scaling = ", scaled up with MetalFX";
                }
#endif
                LOG_INFO(Render,
                         "Frame generation ready: {}x{} frames for an area of {:.0f}x{:.0f} of a "
                         "{}x{} screen{}",
                         slot_width, slot_height, fg_area_width, fg_area_height, layout.width,
                         layout.height, scaling);
                break;
            }
            LOG_WARNING(Render,
                        "Frame generation is off: the interpolation does not take {}x{} frames "
                        "(the chosen size); a smaller Frame Generation Size may work",
                        slot_width, slot_height);
        }
        fg_failed = !fg_ready;
        if (!fg_ready) {
            LOG_WARNING(Render, "Frame generation is not available for this screen size");
        }
    }
    if (fg_ready && fg_failures.load(std::memory_order_relaxed) >= 120) {
        LOG_WARNING(Render, "Frame generation stopped: the interpolation rejected 120 frames in a "
                            "row");
        fg_ready = false;
        fg_failed = true;
        fg_prev = -1;
    }
    if (!fg_ready) {
        return false;
    }

    // The time between the game's frames, smoothed, which spaces the made frame. A pause or a
    // menu does not count.
    const auto now = std::chrono::steady_clock::now();
    if (fg_last_frame.time_since_epoch().count() != 0) {
        const double elapsed = std::chrono::duration<double>(now - fg_last_frame).count();
        if (elapsed > 0.1) {
            fg_prev = -1;
            fg_interval = 0.0;
        } else {
            fg_interval = fg_interval == 0.0 ? elapsed : fg_interval * 0.7 + elapsed * 0.3;
        }
    }
    fg_last_frame = now;

    // Whether the game draws at 30 FPS is counted in emulated time (how many 60 Hz refreshes go
    // by per new frame), not in real time: a game slowed down by anything must not look like a
    // 30 FPS game, or frame generation would keep it slow.
    const u32 swaps = fg_swaps - fg_last_swaps;
    fg_last_swaps = fg_swaps;
    fg_cadence = fg_cadence * 0.8f + static_cast<f32>(std::min(swaps, 4u)) * 0.2f;
    const bool thirty_fps = fg_cadence >= 1.7f;
    // Only games that draw at 30 FPS: making a frame takes over 10 ms on an A18 Pro, which does
    // not fit between the frames of a 60 FPS game. Not while fast forwarding either, where the
    // frames come as fast as those of a 60 FPS game or faster.
    const double limit = Settings::GetFrameLimit();
    // 60 FPS games too ("All Games"): only with display-driven pacing on a 120 Hz display, where a
    // made frame fits in the 8 ms between the refreshes.
    const QualityOptions& options = GetQualityOptions();
    const bool sixty_fps = mode >= 2 && options.frame_generation_display_paced &&
                           options.display_hz >= 100 && fg_cadence < 1.3f;
    const bool eligible = fg_interval > 0.0 && (thirty_fps || sixty_fps) && limit > 0.0 &&
                          limit <= 110.0;
    if (!eligible) {
        // The frame is drawn the usual way and the next one starts over.
        fg_prev = -1;
        return false;
    }
    return true;
}

namespace {
/// Converts the picture of a frame into the 4:2:0 YCbCr planes the interpolation takes (BT.709,
/// video range), and draws such planes back onto the screen.
constexpr const char* kFrameGenerationShaders = R"(
#include <metal_stdlib>
using namespace metal;

// The area of the color picture that is copied, in its pixels.
struct ConvertArea {
    float2 origin;
    float2 size;
};

// Copies the area into the planes, which are smaller: every pixel of the planes is a filtered
// sample of the picture.
kernel void to_ycbcr(texture2d<float> color [[texture(0)]],
                     texture2d<float, access::write> luma [[texture(1)]],
                     texture2d<float, access::write> chroma [[texture(2)]],
                     constant ConvertArea& area [[buffer(0)]],
                     uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= chroma.get_width() || gid.y >= chroma.get_height()) {
        return;
    }
    constexpr sampler smp(filter::linear, address::clamp_to_edge);
    const float2 luma_size = float2(luma.get_width(), luma.get_height());
    const float2 color_size = float2(color.get_width(), color.get_height());
    float2 cbcr = float2(0.0);
    for (uint dy = 0; dy < 2; ++dy) {
        for (uint dx = 0; dx < 2; ++dx) {
            const uint2 p = gid * 2 + uint2(dx, dy);
            const float2 uv =
                (area.origin + (float2(p) + 0.5) / luma_size * area.size) / color_size;
            const float3 c = color.sample(smp, uv, level(0.0)).rgb;
            const float y = dot(c, float3(0.2126, 0.7152, 0.0722));
            luma.write(float4((16.0 + 219.0 * y) / 255.0), p);
            cbcr += float2((c.b - y) / 1.8556, (c.r - y) / 1.5748);
        }
    }
    cbcr *= 0.25;
    chroma.write(float4((128.0 + 224.0 * cbcr) / 255.0, 0.0, 0.0), gid);
}

struct FullscreenOut {
    float4 position [[position]];
    float2 uv;
};

vertex FullscreenOut fullscreen_vertex(uint vid [[vertex_id]]) {
    const float2 uv = float2((vid << 1) & 2, vid & 2);
    FullscreenOut out;
    out.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    out.uv = uv;
    return out;
}

fragment float4 from_ycbcr(FullscreenOut in [[stage_in]],
                           texture2d<float> luma [[texture(0)]],
                           texture2d<float> chroma [[texture(1)]],
                           sampler s [[sampler(0)]]) {
    const float y = (luma.sample(s, in.uv).r * 255.0 - 16.0) / 219.0;
    const float2 c = (chroma.sample(s, in.uv).rg * 255.0 - 128.0) / 224.0;
    const float3 rgb = float3(y + 1.5748 * c.y, y - 0.1873 * c.x - 0.4681 * c.y,
                              y + 1.8556 * c.x);
    return float4(saturate(rgb), 1.0);
}
)";
} // Anonymous namespace

bool RendererMetal::PrepareFrameGenerationResources() {
    id<MTLDevice> device = instance.GetDevice();
    if (fg_to_ycbcr == nil || fg_from_ycbcr == nil) {
        NSError* error = nil;
        id<MTLLibrary> library =
            [device newLibraryWithSource:[NSString stringWithUTF8String:kFrameGenerationShaders]
                                 options:nil
                                   error:&error];
        if (library == nil) {
            LOG_ERROR(Render, "Frame generation shaders failed to compile: {}",
                      error ? error.localizedDescription.UTF8String : "unknown error");
            return false;
        }
        fg_to_ycbcr = [device newComputePipelineStateWithFunction:[library newFunctionWithName:@"to_ycbcr"]
                                                            error:&error];
        MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
        desc.vertexFunction = [library newFunctionWithName:@"fullscreen_vertex"];
        desc.fragmentFunction = [library newFunctionWithName:@"from_ycbcr"];
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        fg_from_ycbcr = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (fg_to_ycbcr == nil || fg_from_ycbcr == nil) {
            LOG_ERROR(Render, "Frame generation pipelines failed: {}",
                      error ? error.localizedDescription.UTF8String : "unknown error");
            return false;
        }
        MTLSamplerDescriptor* sampler = [MTLSamplerDescriptor new];
        sampler.minFilter = MTLSamplerMinMagFilterLinear;
        sampler.magFilter = MTLSamplerMinMagFilterLinear;
        sampler.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sampler.tAddressMode = MTLSamplerAddressModeClampToEdge;
        fg_sampler = [device newSamplerStateWithDescriptor:sampler];
    }

    MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:fg_layout_width
                                                          height:fg_layout_height
                                                       mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModePrivate;
    for (u32 i = 0; i < FG_COLORS; i++) {
        fg_color[i] = [device newTextureWithDescriptor:desc];
        if (fg_color[i] == nil) {
            return false;
        }
    }
    if (fg_metalfx) {
        // MetalFX's interpolator writes the made frames itself.
        desc.usage |= MTLTextureUsageShaderWrite;
    }
    for (u32 i = 0; i < FG_MADE; i++) {
        fg_made[i] = [device newTextureWithDescriptor:desc];
        if (fg_made[i] == nil) {
            return false;
        }
    }

#if AZAHAR_HAS_METALFX
    // The made frame is scaled up to its area on the screen with MetalFX, like the real picture
    // can be. Without it (not supported, or refused) it is stretched with plain filtering.
    fg_upscale = UpscaleSlot{};
    if (MetalFXSupported(device)) {
        UpscaleJob job;
        job.in_width = fg_slot_width;
        job.in_height = fg_slot_height;
        job.out_width = UpscaledSize(job.in_width, static_cast<u32>(std::lround(fg_area_width)));
        job.out_height = UpscaledSize(job.in_height, static_cast<u32>(std::lround(fg_area_height)));
        if (!EnsureUpscaleSlot(fg_upscale, job)) {
            fg_upscale = UpscaleSlot{};
        }
    }
#endif
    return true;
}


namespace {
void SetFX(id object, NSString* key, id value) {
    @try {
        [object setValue:value forKey:key];
    } @catch (NSException* exception) {
        LOG_WARNING(Render, "MetalFX frame interpolation: {} not set ({})", key.UTF8String,
                    exception.reason.UTF8String);
    }
}

id NewFX(id descriptor, NSString* selector, id<MTLDevice> device) {
    SEL sel = NSSelectorFromString(selector);
    if (![descriptor respondsToSelector:sel]) {
        return nil;
    }
    return ((id(*)(id, SEL, id))objc_msgSend)(descriptor, sel, device);
}

NSNumber* PixelFormatNumber(MTLPixelFormat format) {
    return @(static_cast<unsigned long>(format));
}
} // namespace

bool RendererMetal::PrepareMetalFX(u32 layout_width, u32 layout_height, f32 area_width,
                                   f32 area_height, f32 scale) {
    const bool same = fx_layout_width == layout_width && fx_layout_height == layout_height &&
                      fx_area_width == area_width && fx_area_height == area_height &&
                      fx_scale == scale;
    if (fx_interpolator != nil && same) {
        return true;
    }
    if (fx_failed && same) {
        return false;
    }
    fx_interpolator = nil;
    fx_scaler = nil;
    fx_scaled = {};
    fx_depth = nil;
    fx_motion = nil;
    fx_small_color = {};
    fx_small_out = nil;
    fx_full_out = nil;
    fx_layout_width = layout_width;
    fx_layout_height = layout_height;
    fx_scale = scale;
    fx_area_width = area_width;
    fx_area_height = area_height;
    // The interpolation covers the area of the screens (the top screen alone with Top Screen
    // Only) at the chosen size, not the empty space around them.
    const u32 width = std::max(16u, static_cast<u32>(std::floor(area_width * scale)) & ~1u);
    const u32 height = std::max(16u, static_cast<u32>(std::floor(area_height * scale)) & ~1u);
    fx_width = width;
    fx_height = height;
    fx_reset = true;
    fx_failed = true;

    id<MTLDevice> device = instance.GetDevice();
    Class descriptor_class = NSClassFromString(@"MTLFXFrameInterpolatorDescriptor");
    if (descriptor_class == Nil) {
        LOG_WARNING(Render, "MetalFX frame interpolation is not available on this system");
        return false;
    }

    // The depth and the motion vectors of a game are not there for a 3DS game: a flat depth and
    // no motion, so the interpolator has to find the movement in the pictures themselves.
    MTLTextureDescriptor* depth_desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    depth_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    depth_desc.storageMode = MTLStorageModePrivate;
    MTLTextureDescriptor* motion_desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG16Float
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    motion_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    motion_desc.storageMode = MTLStorageModePrivate;
    fx_depth = [device newTextureWithDescriptor:depth_desc];
    fx_motion = [device newTextureWithDescriptor:motion_desc];
    if (fx_depth == nil || fx_motion == nil) {
        return false;
    }
    {
        id<MTLCommandBuffer> buffer = [instance.GetCommandQueue() commandBuffer];
        for (int i = 0; i < 2; i++) {
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = i == 0 ? fx_depth : fx_motion;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor =
                i == 0 ? MTLClearColorMake(0.5, 0.0, 0.0, 0.0) : MTLClearColorMake(0, 0, 0, 0);
            [[buffer renderCommandEncoderWithDescriptor:pass] endEncoding];
        }
        [buffer commit];
    }

    const auto fill = [&](id descriptor_object) {
        SetFX(descriptor_object, @"inputWidth", @(width));
        SetFX(descriptor_object, @"inputHeight", @(height));
        SetFX(descriptor_object, @"outputWidth", @(width));
        SetFX(descriptor_object, @"outputHeight", @(height));
        SetFX(descriptor_object, @"colorTextureFormat", PixelFormatNumber(MTLPixelFormatBGRA8Unorm));
        SetFX(descriptor_object, @"depthTextureFormat", PixelFormatNumber(MTLPixelFormatR32Float));
        SetFX(descriptor_object, @"motionTextureFormat", PixelFormatNumber(MTLPixelFormatRG16Float));
        SetFX(descriptor_object, @"outputTextureFormat",
              PixelFormatNumber(MTLPixelFormatBGRA8Unorm));
    };

    // First without a scaler; when the system refuses that, with a temporal scaler of the same
    // size in front.
    id descriptor = [[descriptor_class alloc] init];
    fill(descriptor);
    id interpolator = NewFX(descriptor, @"newFrameInterpolatorWithDevice:", device);
    if (interpolator == nil) {
        LOG_INFO(Render, "MetalFX frame interpolation: no interpolator without a scaler, trying "
                         "with a temporal scaler");
        Class scaler_class = NSClassFromString(@"MTLFXTemporalScalerDescriptor");
        if (scaler_class != Nil) {
            id scaler_descriptor = [[scaler_class alloc] init];
            fill(scaler_descriptor);
            id scaler = NewFX(scaler_descriptor, @"newTemporalScalerWithDevice:", device);
            if (scaler != nil) {
                SetFX(descriptor, @"scaler", scaler);
                interpolator = NewFX(descriptor, @"newFrameInterpolatorWithDevice:", device);
                if (interpolator != nil) {
                    fx_scaler = scaler;
                    MTLTextureDescriptor* scaled_desc = [MTLTextureDescriptor
                        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                     width:width
                                                    height:height
                                                 mipmapped:NO];
                    scaled_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead |
                                        MTLTextureUsageShaderWrite;
                    scaled_desc.storageMode = MTLStorageModePrivate;
                    for (auto& texture : fx_scaled) {
                        texture = [device newTextureWithDescriptor:scaled_desc];
                    }
                }
            }
        }
    }
    if (interpolator == nil) {
        LOG_WARNING(Render, "MetalFX frame interpolation: the system did not make an interpolator "
                            "for {}x{}", width, height);
        return false;
    }
    if (width != layout_width || height != layout_height) {
        MTLTextureDescriptor* small_desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:width
                                                              height:height
                                                           mipmapped:NO];
        small_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead |
                           MTLTextureUsageShaderWrite;
        small_desc.storageMode = MTLStorageModePrivate;
        for (auto& texture : fx_small_color) {
            texture = [device newTextureWithDescriptor:small_desc];
        }
        fx_small_out = [device newTextureWithDescriptor:small_desc];
        if (fx_small_color[0] == nil || fx_small_color[1] == nil || fx_small_out == nil) {
            return false;
        }
    }
    if (fx_small_out == nil) {
        MTLTextureDescriptor* full_desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:width
                                                              height:height
                                                           mipmapped:NO];
        full_desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead |
                          MTLTextureUsageShaderWrite;
        full_desc.storageMode = MTLStorageModePrivate;
        fx_full_out = [device newTextureWithDescriptor:full_desc];
        if (fx_full_out == nil) {
            return false;
        }
    }
    fx_interpolator = interpolator;
    fx_failed = false;
    LOG_INFO(Render, "MetalFX frame interpolation ready for {}x{} (screen {}x{}){}; usages: color {}, "
                     "output {}, depth {}, motion {}",
             width, height, layout_width, layout_height, fx_scaler != nil ? " (with a temporal scaler)" : "",
             [[interpolator valueForKey:@"colorTextureUsage"] unsignedLongValue],
             [[interpolator valueForKey:@"outputTextureUsage"] unsignedLongValue],
             [[interpolator valueForKey:@"depthTextureUsage"] unsignedLongValue],
             [[interpolator valueForKey:@"motionTextureUsage"] unsignedLongValue]);
    return true;
}

bool RendererMetal::EncodeMetalFX(id<MTLCommandBuffer> buffer, id<MTLTexture> color,
                                  id<MTLTexture> previous, id<MTLTexture> output,
                                  double delta_time) {
    if (fx_interpolator == nil) {
        return false;
    }
    SEL encode = NSSelectorFromString(@"encodeToCommandBuffer:");
    id<MTLTexture> frame_color = color;
    id<MTLTexture> frame_previous = previous;

    if (fx_scaler != nil) {
        // The scaler runs on every real frame; the interpolator takes its results.
        fx_scaled_index ^= 1;
        id<MTLTexture> scaled = fx_scaled[fx_scaled_index];
        id<MTLTexture> scaled_previous = fx_scaled[fx_scaled_index ^ 1];
        SetFX(fx_scaler, @"colorTexture", color);
        SetFX(fx_scaler, @"depthTexture", fx_depth);
        SetFX(fx_scaler, @"motionTexture", fx_motion);
        SetFX(fx_scaler, @"outputTexture", scaled);
        SetFX(fx_scaler, @"inputContentWidth", @(fx_width));
        SetFX(fx_scaler, @"inputContentHeight", @(fx_height));
        SetFX(fx_scaler, @"jitterOffsetX", @(0.0f));
        SetFX(fx_scaler, @"jitterOffsetY", @(0.0f));
        SetFX(fx_scaler, @"motionVectorScaleX", @(1.0f));
        SetFX(fx_scaler, @"motionVectorScaleY", @(1.0f));
        SetFX(fx_scaler, @"reset", @(fx_reset));
        if (![fx_scaler respondsToSelector:encode]) {
            return false;
        }
        ((void (*)(id, SEL, id))objc_msgSend)(fx_scaler, encode, buffer);
        frame_color = scaled;
        frame_previous = scaled_previous;
    }

    SetFX(fx_interpolator, @"colorTexture", frame_color);
    SetFX(fx_interpolator, @"prevColorTexture", frame_previous);
    SetFX(fx_interpolator, @"depthTexture", fx_depth);
    SetFX(fx_interpolator, @"motionTexture", fx_motion);
    SetFX(fx_interpolator, @"outputTexture", output);
    SetFX(fx_interpolator, @"deltaTime", @(static_cast<float>(delta_time)));
    SetFX(fx_interpolator, @"motionVectorScaleX", @(1.0f));
    SetFX(fx_interpolator, @"motionVectorScaleY", @(1.0f));
    SetFX(fx_interpolator, @"jitterOffsetX", @(0.0f));
    SetFX(fx_interpolator, @"jitterOffsetY", @(0.0f));
    SetFX(fx_interpolator, @"nearPlane", @(0.1f));
    SetFX(fx_interpolator, @"farPlane", @(100.0f));
    SetFX(fx_interpolator, @"fieldOfView", @(1.0f));
    SetFX(fx_interpolator, @"aspectRatio",
          @(static_cast<float>(fx_width) / static_cast<float>(std::max(fx_height, 1u))));
    SetFX(fx_interpolator, @"contentWidth", @(fx_width));
    SetFX(fx_interpolator, @"contentHeight", @(fx_height));
    SetFX(fx_interpolator, @"shouldResetHistory", @(fx_reset));
    // No camera: identity matrices.
    for (NSString* name in @[ @"setWorldToViewMatrix:", @"setViewToClipMatrix:" ]) {
        SEL setter = NSSelectorFromString(name);
        if ([fx_interpolator respondsToSelector:setter]) {
            ((void (*)(id, SEL, simd_float4x4))objc_msgSend)(fx_interpolator, setter,
                                                              matrix_identity_float4x4);
        }
    }
    if (![fx_interpolator respondsToSelector:encode]) {
        return false;
    }
    ((void (*)(id, SEL, id))objc_msgSend)(fx_interpolator, encode, buffer);
    fx_reset = false;
    return true;
}

void RendererMetal::RenderGenerated(CAMetalLayer* target,
                                    const Layout::FramebufferLayout& layout) {
    // The frames go to the screen from the callbacks of the interpolation, in order. While the
    // previous ones are still on their way, this frame is dropped: the emulation never waits for
    // frame generation, and a frame shown out of order would be worse than one not shown.
    if (fg_busy.load(std::memory_order_acquire)) {
        // A frame whose interpolation never reports back (seen as both screens staying black
        // while the game runs on) must not hold every later frame back forever: after a while
        // it is given up on, and its callbacks, should they ever come, do nothing.
        const double since = fg_busy_since.load(std::memory_order_relaxed);
        const double now_s = CACurrentMediaTime();
        if (since > 0.0 && now_s - since > 0.5) {
            LOG_WARNING(Render, "Frame generation: a frame did not finish in {:.0f} ms, "
                                "giving it up",
                        (now_s - since) * 1000.0);
            fg_epoch.fetch_add(1, std::memory_order_acq_rel);
            fg_busy.store(false, std::memory_order_release);
            fg_failures.fetch_add(30, std::memory_order_relaxed);
        } else {
            Stats().fg_skipped.fetch_add(1, std::memory_order_relaxed);
            fg_prev = -1;
            return;
        }
    }
    const double arrived = CACurrentMediaTime();
    fg_last_generated = std::chrono::steady_clock::now();

    fg_color_index = (fg_color_index + 1) % FG_COLORS;
    id<MTLTexture> current = fg_color[fg_color_index];
    id<MTLTexture> current_luma = (__bridge id<MTLTexture>)g_fg_hooks.texture(fg_cur, 0);
    id<MTLTexture> current_chroma = (__bridge id<MTLTexture>)g_fg_hooks.texture(fg_cur, 1);
    if (current == nil || current_luma == nil || current_chroma == nil) {
        fg_ready = false;
        return;
    }

    // The frame of the game at full size, drawn exactly as it is without frame generation.
    scheduler.Flush();
    scheduler.MarkPresentation();
#if AZAHAR_HAS_METALFX
    PrepareUpscaling(layout);
#endif
    MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
    desc.colorAttachments[0].texture = current;
    desc.colorAttachments[0].loadAction = MTLLoadActionClear;
    desc.colorAttachments[0].storeAction = MTLStoreActionStore;
    desc.colorAttachments[0].clearColor =
        MTLClearColorMake(Settings::values.bg_red.GetValue(), Settings::values.bg_green.GetValue(),
                          Settings::values.bg_blue.GetValue(), 1.0);
    scheduler.SetPassLabel("frame generation picture");
    id<MTLRenderCommandEncoder> encoder = scheduler.BeginRendering(desc);
    DrawScreens(encoder, layout);
    scheduler.EndEncoding();
#if AZAHAR_HAS_METALFX
    upscale_pass = UpscalePass::Off;
#endif

    const u32 previous = fg_prev >= 0 ? static_cast<u32>(fg_prev) : 0;
    const bool make_between = fg_prev >= 0;
    const u32 source = fg_cur;
    const u32 destination = (fg_cur + 2) % FG_SLOTS;
    id<MTLTexture> between_luma = (__bridge id<MTLTexture>)g_fg_hooks.texture(destination, 0);
    id<MTLTexture> between_chroma = (__bridge id<MTLTexture>)g_fg_hooks.texture(destination, 1);

    // MetalFX's interpolator works on the color pictures themselves: no planes to convert. It has
    // a command buffer of its own (after the one with the real picture), which also gives the
    // time it takes on the GPU.
    bool use_fx = false;
    id<MTLCommandBuffer> fx_buffer = nil;
    if (!make_between) {
        fx_reset = true;
    }
    if (make_between && fg_metalfx && fg_prev_color >= 0) {
        constexpr std::array<f32, 5> fx_scales{1.0f, 0.5f, 0.375f, 0.25f, 0.75f};
        const f32 scale = fx_scales[std::clamp(GetQualityOptions().frame_generation_size, 0, 4)];
        if (PrepareMetalFX(layout.width, layout.height, fg_area_width, fg_area_height, scale)) {
            if (fx_queue == nil) {
                fx_queue = [instance.GetDevice() newCommandQueue];
                fx_event = [instance.GetDevice() newEvent];
            }
            // The real picture is done before the interpolation starts (a signal at the end of
            // its buffer, a wait at the start of the interpolation's).
            fx_event_value++;
            [scheduler.CommandBuffer() encodeSignalEvent:fx_event value:fx_event_value];
            scheduler.Flush();
            fx_buffer = [fx_queue commandBuffer];
            fx_buffer.label = @"MetalFX frame interpolation";
            [fx_buffer encodeWaitForEvent:fx_event value:fx_event_value];
            id<MTLTexture> made = fg_made[(fg_made_index + 1) % FG_MADE];
            id<MTLTexture> in_current = current;
            id<MTLTexture> in_previous = fg_color[fg_prev_color];
            id<MTLTexture> out = fx_full_out;
            const bool shrink = fx_small_out != nil;
            if (shrink) {
                fx_small_index ^= 1;
                in_current = fx_small_color[fx_small_index];
                in_previous = fx_small_color[fx_small_index ^ 1];
                out = fx_small_out;
                MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
                pass.colorAttachments[0].texture = in_current;
                pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
                pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                id<MTLRenderCommandEncoder> shrink_encoder =
                    [fx_buffer renderCommandEncoderWithDescriptor:pass];
                // The area of the screens of the real picture, at the size of the interpolation.
                const f32 target_w = static_cast<f32>(fx_width);
                const f32 target_h = static_cast<f32>(fx_height);
                const f32 layout_w = static_cast<f32>(layout.width);
                const f32 layout_h = static_cast<f32>(layout.height);
                EncodeRect(shrink_encoder, present_pipeline, present_samplers[0], current,
                           target_w, target_h, 0.0f, 0.0f, target_w, target_h,
                           fg_area_x / layout_w, fg_area_y / layout_h,
                           (fg_area_x + fg_area_width) / layout_w,
                           (fg_area_y + fg_area_height) / layout_h);
                [shrink_encoder endEncoding];
            }
            const double delta = std::clamp(fg_interval, 0.012, 0.1);
            use_fx = EncodeMetalFX(fx_buffer, in_current, in_previous, out, delta);
            if (use_fx) {
                // The made picture is the real one with the interpolated one over the area of
                // the screens that frame generation covers (only the top screen with "Top Screen
                // Only"), so what is outside of it keeps the game's own frames.
                MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
                pass.colorAttachments[0].texture = made;
                pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
                pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                id<MTLRenderCommandEncoder> compose =
                    [fx_buffer renderCommandEncoderWithDescriptor:pass];
                const f32 full_width = static_cast<f32>(layout.width);
                const f32 full_height = static_cast<f32>(layout.height);
                EncodeFullscreen(compose, present_pipeline, present_samplers[0], current,
                                 full_width, full_height);
                EncodeRect(compose, present_pipeline, present_samplers[0],
                           shrink ? fx_small_out : fx_full_out, full_width, full_height,
                           fg_area_x, fg_area_y, fg_area_width, fg_area_height, 0.0f, 0.0f, 1.0f,
                           1.0f);
                [compose endEncoding];
            }
            if (!use_fx) {
                [fx_buffer commit];
                fx_buffer = nil;
            }
        }
    }

    // The smaller copy of the screens' area, in the planes the interpolation reads.
    if (!use_fx) {
        id<MTLComputeCommandEncoder> convert = [scheduler.CommandBuffer() computeCommandEncoder];
        [convert setComputePipelineState:fg_to_ycbcr];
        [convert setTexture:current atIndex:0];
        [convert setTexture:current_luma atIndex:1];
        [convert setTexture:current_chroma atIndex:2];
        const std::array<f32, 4> area{fg_area_x, fg_area_y, fg_area_width, fg_area_height};
        [convert setBytes:area.data() length:sizeof(area) atIndex:0];
        const MTLSize group = MTLSizeMake(8, 8, 1);
        const MTLSize groups =
            MTLSizeMake((fg_slot_width / 2 + 7) / 8, (fg_slot_height / 2 + 7) / 8, 1);
        [convert dispatchThreadgroups:groups threadsPerThreadgroup:group];
        [convert endEncoding];
    }

    const f32 width = static_cast<f32>(layout.width);
    const f32 height = static_cast<f32>(layout.height);
    const f32 area_x = fg_area_x, area_y = fg_area_y;
    const f32 area_width = fg_area_width, area_height = fg_area_height;

    // The time between two frames of the game, which the frames on screen are spaced by: the
    // exact time of the emulated frames when the game holds its speed, the measured one otherwise.
    const double nominal = std::round(fg_cadence) / 59.83 * 100.0 / Settings::GetFrameLimit();
    const double interval =
        std::clamp(std::abs(fg_interval - nominal) < nominal * 0.05 ? nominal : fg_interval, 0.012,
                   0.1);

    fg_busy_since.store(CACurrentMediaTime(), std::memory_order_relaxed);
    fg_busy.store(true, std::memory_order_release);
    std::atomic<bool>* busy = &fg_busy;
    const u32 epoch = fg_epoch.load(std::memory_order_acquire);
    std::atomic<u32>* epoch_now = &fg_epoch;
    std::atomic<u32>* failures = &fg_failures;
    std::atomic<double>* last_real_time = &fg_last_real_time;
    id<MTLCommandQueue> queue = instance.GetCommandQueue();
    // Display-driven pacing: the frames go to the display link instead of being presented here.
    const bool paced = GetQualityOptions().frame_generation_display_paced && StartPacer(target);
    const std::shared_ptr<DisplayPacer> pacer = paced ? fg_pacer : nullptr;
    fg_made_index = (fg_made_index + 1) % FG_MADE;
    id<MTLTexture> made_texture = fg_made[fg_made_index];
    id<MTLRenderPipelineState> color_pipeline = present_pipeline;
    id<MTLSamplerState> color_sampler = present_samplers[0];
    id<MTLRenderPipelineState> ycbcr_pipeline = fg_from_ycbcr;
    id<MTLSamplerState> ycbcr_sampler = fg_sampler;
    const auto interpolate = g_fg_hooks.interpolate;
#if AZAHAR_HAS_METALFX
    id<MTLFXSpatialScaler> scaler = fg_upscale.scaler;
    id<MTLTexture> scaler_input = fg_upscale.input;
    id<MTLTexture> scaler_output = fg_upscale.output;
    const u32 scaler_width = fg_upscale.in_width;
    const u32 scaler_height = fg_upscale.in_height;
#endif

    // Shows the made frame (when there is one) and the real one. Runs on a callback thread, which
    // is where the waits for drawables happen, not on the emulation thread.
    const auto show = std::make_shared<std::function<void(bool)>>([=](bool made) {
        if (epoch_now->load(std::memory_order_acquire) != epoch) {
            return; // Given up on: a newer frame owns the screen now
        }
        made = made && (use_fx || (between_luma != nil && between_chroma != nil));
        id<MTLCommandBuffer> buffer = [queue commandBuffer];
        const auto begin = [&](id<MTLTexture> to) {
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = to;
            pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            return [buffer renderCommandEncoderWithDescriptor:pass];
        };
        // The planes of the made frame as a color picture over the whole target or viewport.
        const auto draw_planes = [&](id<MTLRenderCommandEncoder> draw, MTLViewport viewport) {
            [draw setRenderPipelineState:ycbcr_pipeline];
            [draw setViewport:viewport];
            [draw setFragmentTexture:between_luma atIndex:0];
            [draw setFragmentTexture:between_chroma atIndex:1];
            [draw setFragmentSamplerState:ycbcr_sampler atIndex:0];
            [draw drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        };

        id<CAMetalDrawable> drawable_between = nil;
        if (made && !paced) {
            @autoreleasepool {
                drawable_between = [target nextDrawable];
            }
            made = drawable_between != nil;
        }
        if (made && paced) {
            made = made_texture != nil;
        }
        if (made && use_fx) {
            // The interpolator wrote the whole picture. For display-driven pacing it is already
            // in the made texture, otherwise it is drawn onto the drawable.
            if (!paced) {
                id<MTLRenderCommandEncoder> draw = begin(drawable_between.texture);
                EncodeFullscreen(draw, color_pipeline, color_sampler, made_texture, width, height);
                [draw endEncoding];
            }
        } else if (made) {
            bool scaled = false;
#if AZAHAR_HAS_METALFX
            if (scaler != nil) {
                if (@available(iOS 16.0, macOS 13.0, *)) {
                    // The made frame in color at its own size, then scaled up by MetalFX.
                    id<MTLRenderCommandEncoder> convert = begin(scaler_input);
                    draw_planes(convert,
                                MTLViewport{0.0, 0.0, static_cast<double>(scaler_width),
                                            static_cast<double>(scaler_height), 0.0, 1.0});
                    [convert endEncoding];
                    scaler.colorTexture = scaler_input;
                    scaler.outputTexture = scaler_output;
                    scaler.inputContentWidth = scaler_width;
                    scaler.inputContentHeight = scaler_height;
                    [scaler encodeToCommandBuffer:buffer];
                    scaled = true;
                }
            }
#endif
            id<MTLRenderCommandEncoder> draw =
                begin(paced ? made_texture : drawable_between.texture);
            // Around the screens (borders, background) the real frame, over them the made one. The
            // real frame is not drawn under the made one: that part of the target would be
            // written twice.
            EncodeAround(draw, color_pipeline, color_sampler, current, width, height, area_x,
                         area_y, area_width, area_height);
#if AZAHAR_HAS_METALFX
            if (scaled) {
                EncodeRect(draw, color_pipeline, color_sampler, scaler_output, width, height,
                           area_x, area_y, area_width, area_height);
            }
#endif
            if (!scaled) {
                draw_planes(draw, MTLViewport{area_x, area_y, area_width, area_height, 0.0, 1.0});
            }
            [draw endEncoding];
        }

        if (paced) {
            // The display link shows them, the made frame first. The composition goes to the GPU
            // before the frames are handed over, so it is done before the display link draws them.
            [buffer commit];
            {
                std::scoped_lock lock{pacer->mutex};
                pacer->slot = std::clamp(interval * 0.5, 0.008, 0.05);
                if (made) {
                    pacer->waiting.push_back({made_texture, width, height});
                }
                pacer->waiting.push_back({current, width, height});
            }
            busy->store(false, std::memory_order_release);
            return;
        }

        id<CAMetalDrawable> drawable_current = nil;
        @autoreleasepool {
            drawable_current = [target nextDrawable];
        }
        if (drawable_current != nil) {
            id<MTLRenderCommandEncoder> draw = begin(drawable_current.texture);
            EncodeFullscreen(draw, color_pipeline, color_sampler, current, width, height);
            [draw endEncoding];
        }

        // When the frames reach the screen. The made frame goes half a frame of the game after the
        // previous real frame and the real frame half a frame after it, so the frames are evenly
        // spaced however long making the frame took. The schedule moves a little earlier with
        // every frame until making a frame is only just in time, which keeps the delay as small as
        // the interpolation allows.
        const double now = CACurrentMediaTime();
        // A little ahead, the GPU still has to draw the frames.
        const double soonest = now + 0.004;
        double previous_real = last_real_time->load(std::memory_order_relaxed);
        if (previous_real < now - 0.1) {
            previous_real = 0.0; // After a pause or a stretch without frame generation
        }
        double real_time;
        if (made) {
            double made_time = previous_real > 0.0 ? previous_real + interval * 0.5 - 0.0003
                                                   : soonest;
            made_time = std::max(made_time, soonest);
            // Never more than a frame of the game behind the game.
            if (made_time > arrived + interval) {
                made_time = std::max(soonest, arrived + interval);
            }
            real_time = made_time + interval * 0.5;
            [drawable_between addPresentedHandler:^(id<MTLDrawable> presented) {
              RecordPresentedTime(presented.presentedTime);
            }];
            [buffer presentDrawable:drawable_between atTime:made_time];
        } else {
            // Not before a frame that is already on its way.
            real_time = std::max(now, previous_real > 0.0 ? previous_real + 0.008 : now);
        }
        if (drawable_current != nil) {
            [drawable_current addPresentedHandler:^(id<MTLDrawable> presented) {
              RecordPresentedTime(presented.presentedTime);
            }];
            if (real_time > now) {
                [buffer presentDrawable:drawable_current atTime:real_time];
            } else {
                [buffer presentDrawable:drawable_current];
            }
            last_real_time->store(real_time, std::memory_order_relaxed);
        }
        [buffer commit];
        busy->store(false, std::memory_order_release);
    });

    // Once the GPU has the picture in the planes, the interpolation makes the frame between the
    // previous one and this one; without a previous frame only this one is shown.
    if (use_fx) {
        // The interpolator's own buffer: the frame is shown when the GPU has made it.
        [fx_buffer addCompletedHandler:^(id<MTLCommandBuffer> done) {
          const double gpu_seconds = done.GPUEndTime - done.GPUStartTime;
          if (gpu_seconds > 0.0) {
              Stats().fg_ns.fetch_add(static_cast<u64>(gpu_seconds * 1e9),
                                      std::memory_order_relaxed);
          }
          Stats().fg_made.fetch_add(1, std::memory_order_relaxed);
          failures->store(0, std::memory_order_relaxed);
          (*show)(true);
        }];
        [fx_buffer commit];
    }
    [scheduler.CommandBuffer() addCompletedHandler:^(id<MTLCommandBuffer>) {
      if (use_fx) {
          return;
      }
      if (!make_between) {
          (*show)(false);
          return;
      }
      const auto start = std::chrono::steady_clock::now();
      const bool started = interpolate(previous, source, destination, [show, start,
                                                                      failures](bool ok) {
          if (ok) {
              const auto took = std::chrono::steady_clock::now() - start;
              Stats().fg_ns.fetch_add(
                  static_cast<u64>(
                      std::chrono::duration_cast<std::chrono::nanoseconds>(took).count()),
                  std::memory_order_relaxed);
              Stats().fg_made.fetch_add(1, std::memory_order_relaxed);
              failures->store(0, std::memory_order_relaxed);
          } else {
              failures->fetch_add(1, std::memory_order_relaxed);
          }
          (*show)(ok);
      });
      if (!started) {
          (*show)(false);
      }
    }];
    scheduler.Flush();

    fg_prev = static_cast<int>(fg_cur);
    fg_prev_color = static_cast<int>(fg_color_index);
    fg_cur = (fg_cur + 1) % FG_SLOTS;
}

bool RendererMetal::RenderPacedPlain(const Layout::FramebufferLayout& layout) {
    if (!fg_pacer || !fg_ready || layout.width != fg_layout_width ||
        layout.height != fg_layout_height) {
        return false;
    }
    if (fg_busy.load(std::memory_order_acquire)) {
        // A pair is still being made: this frame would get ahead of it.
        return true;
    }
    fg_prev = -1;
    fg_color_index = (fg_color_index + 1) % FG_COLORS;
    id<MTLTexture> current = fg_color[fg_color_index];
    if (current == nil) {
        return false;
    }
    scheduler.Flush();
    scheduler.MarkPresentation();
#if AZAHAR_HAS_METALFX
    PrepareUpscaling(layout);
#endif
    MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
    desc.colorAttachments[0].texture = current;
    desc.colorAttachments[0].loadAction = MTLLoadActionClear;
    desc.colorAttachments[0].storeAction = MTLStoreActionStore;
    desc.colorAttachments[0].clearColor =
        MTLClearColorMake(Settings::values.bg_red.GetValue(), Settings::values.bg_green.GetValue(),
                          Settings::values.bg_blue.GetValue(), 1.0);
    scheduler.SetPassLabel("frame generation picture (plain)");
    id<MTLRenderCommandEncoder> encoder = scheduler.BeginRendering(desc);
    DrawScreens(encoder, layout);
    scheduler.EndEncoding();
#if AZAHAR_HAS_METALFX
    upscale_pass = UpscalePass::Off;
#endif
    // Submitted before it is handed over: the display link draws it on the same queue, after.
    scheduler.Flush();
    std::scoped_lock lock{fg_pacer->mutex};
    fg_pacer->slot = 1.0 / 60.0;
    fg_pacer->waiting.push_back(
        {current, static_cast<float>(layout.width), static_cast<float>(layout.height)});
    return true;
}

bool RendererMetal::StartPacer(CAMetalLayer* layer) {
    if (@available(iOS 17.0, macOS 14.0, tvOS 17.0, *)) {
        const int hz = std::clamp(GetQualityOptions().display_hz, 60, 120);
        if (fg_pacer_link != nil && fg_pacer_layer == layer && fg_pacer_hz == hz) {
            return true;
        }
        StopPacer();
        if (layer == nil) {
            return false;
        }
        auto pacer = std::make_shared<DisplayPacer>();
        pacer->command_queue = instance.GetCommandQueue();
        id<MTLRenderPipelineState> pipeline = present_pipeline;
        id<MTLSamplerState> sampler = present_samplers[0];
        pacer->draw = [pipeline, sampler](id<MTLRenderCommandEncoder> encoder,
                                          const DisplayPacer::Frame& frame) {
            EncodeFullscreen(encoder, pipeline, sampler, frame.texture, frame.width, frame.height);
        };

        AzaharPacerDelegate* delegate = [AzaharPacerDelegate new];
        delegate->pacer = pacer;
        CAMetalDisplayLink* link = [[CAMetalDisplayLink alloc] initWithMetalLayer:layer];
        if (link == nil) {
            return false;
        }
        link.delegate = delegate;
        link.preferredFrameLatency = 2.0f;
        link.preferredFrameRateRange =
            CAFrameRateRangeMake(static_cast<float>(hz), static_cast<float>(hz),
                                 static_cast<float>(hz));
        // A thread of its own, so the refreshes are not held up by the app's main thread.
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        NSThread* thread = [[NSThread alloc] initWithBlock:^{
          [link addToRunLoop:NSRunLoop.currentRunLoop forMode:NSDefaultRunLoopMode];
          while (!NSThread.currentThread.isCancelled) {
              @autoreleasepool {
                  [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode
                                         beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
              }
          }
          [link invalidate];
          dispatch_semaphore_signal(done);
        }];
        thread.name = @"Azahar frame pacing";
        thread.qualityOfService = NSQualityOfServiceUserInteractive;
        [thread start];

        fg_pacer = pacer;
        fg_pacer_link = link;
        fg_pacer_delegate = delegate;
        fg_pacer_thread = thread;
        fg_pacer_done = done;
        fg_pacer_layer = layer;
        fg_pacer_hz = hz;
        LOG_INFO(Render, "Frame generation: display-driven pacing at {} Hz", hz);
        return true;
    }
    return false;
}

void RendererMetal::StopPacer() {
    if (fg_pacer_link == nil) {
        return;
    }
    if (@available(iOS 17.0, macOS 14.0, tvOS 17.0, *)) {
        CAMetalDisplayLink* link = fg_pacer_link;
        link.paused = YES;
    }
    if (fg_pacer) {
        std::scoped_lock lock{fg_pacer->mutex};
        fg_pacer->waiting.clear();
    }
    // The thread invalidates the link and ends.
    [fg_pacer_thread cancel];
    fg_pacer_thread = nil;
    dispatch_semaphore_t done = fg_pacer_done;
    fg_pacer_done = nil;
    fg_pacer_link = nil;
    fg_pacer_delegate = nil;
    fg_pacer_layer = nil;
    fg_pacer.reset();
    // Wait until the link is invalidated: a new link or a drawable taken from the layer while the
    // old link still holds it could hang (seen when the screens were swapped, which stops and
    // restarts the pacing). The thread checks every 5 ms; the wait is bounded all the same.
    if (done != nil) {
        dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

void RendererMetal::RenderScreenshot() {
    if (!settings.screenshot_requested.exchange(false)) {
        return;
    }

    const Layout::FramebufferLayout layout{settings.screenshot_framebuffer_layout};
    const u32 width = layout.width;
    const u32 height = layout.height;
    if (width == 0 || height == 0 || !settings.screenshot_bits) {
        // The callback's argument is "invert Y" for the frontends, so a failure can't be reported
        // through it: the request is dropped.
        LOG_ERROR(Render, "Dropping a screenshot request with an invalid layout or buffer");
        return;
    }

    id<MTLDevice> device = instance.GetDevice();
    MTLTextureDescriptor* tex_desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    tex_desc.usage = MTLTextureUsageRenderTarget;
    tex_desc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> target = [device newTextureWithDescriptor:tex_desc];
    id<MTLBuffer> readback = [device newBufferWithLength:width * height * 4
                                                 options:MTLResourceStorageModeShared];

    MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
    desc.colorAttachments[0].texture = target;
    desc.colorAttachments[0].loadAction = MTLLoadActionClear;
    desc.colorAttachments[0].storeAction = MTLStoreActionStore;
    desc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

    scheduler.SetPassLabel("screenshot");
    id<MTLRenderCommandEncoder> encoder = scheduler.BeginRendering(desc);
    DrawScreens(encoder, layout);
    scheduler.EndEncoding();

    [scheduler.BlitEncoder() copyFromTexture:target
                                 sourceSlice:0
                                 sourceLevel:0
                                sourceOrigin:MTLOriginMake(0, 0, 0)
                                  sourceSize:MTLSizeMake(width, height, 1)
                                    toBuffer:readback
                           destinationOffset:0
                      destinationBytesPerRow:width * 4
                    destinationBytesPerImage:width * height * 4];
    scheduler.Finish();

    // QImage::Format_RGB32 and the other frontends expect BGRA byte order.
    std::memcpy(settings.screenshot_bits, readback.contents, width * height * 4);
    if (settings.screenshot_complete_callback) {
        // The image is already top-down, it does not have to be inverted.
        settings.screenshot_complete_callback(false);
    }
}

void RendererMetal::LimitFramesAheadOfGpu() {
    if (previous_frame_tick != 0) {
        scheduler.Wait(previous_frame_tick);
    }
    previous_frame_tick = scheduler.CurrentTick() - 1;
}

void RendererMetal::SwapBuffers() {
    MICROPROFILE_SCOPE(Metal_RenderFrame);
    system.perf_stats->StartSwap();

    // The CPU time this thread used since the previous frame: the emulation (the game's CPU
    // code, the GPU commands, the renderer) without the time it sleeps or waits.
    if (StatisticsEnabled()) {
        thread_local u64 last_cpu_ns = 0;
        const u64 now_ns = clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID);
        if (last_cpu_ns != 0 && now_ns > last_cpu_ns && now_ns - last_cpu_ns < 200'000'000) {
            const u64 used = now_ns - last_cpu_ns;
            GpuStats& stats = Stats();
            stats.cpu_ns.fetch_add(used, std::memory_order_relaxed);
            stats.cpu_frames.fetch_add(1, std::memory_order_relaxed);
            u64 worst = stats.cpu_max_ns.load(std::memory_order_relaxed);
            while (used > worst && !stats.cpu_max_ns.compare_exchange_weak(worst, used)) {
            }
        }
        last_cpu_ns = now_ns;
    }

    const Layout::FramebufferLayout& layout = render_window.GetFramebufferLayout();
    PrepareRendertarget();
    RenderScreenshot();
    fg_swaps++;

    // A frame with a draw that was skipped (shader still being built) would show up with parts
    // missing for a moment: keep the previous frame instead.
    bool hold_frame = false;
    if (Stats().frame_skipped.exchange(0, std::memory_order_relaxed) > 0 &&
        GetQualityOptions().hold_frames_for_shaders && held_frames < 3) {
        hold_frame = true;
        ++held_frames;
    } else {
        held_frames = 0;
    }

    if (!hold_frame && (!Settings::values.use_skip_duplicate_frames.GetValue() ||
                        Core::PerfStats::game_frames_updated)) {
        if (layer_dirty) {
            AcquireLayer();
        }
        RenderToLayer(layer, layout, true);
        // The secondary window (an external display) shows the other part of the layout.
        if (secondary_window != nullptr) {
            if (second_layer_dirty) {
                second_layer_dirty = false;
                second_layer = ConfigureLayer(secondary_window->GetWindowInfo().render_surface);
            }
            if (second_layer != nil) {
                RenderToLayer(second_layer, secondary_window->GetFramebufferLayout(), false);
            }
        }
        Core::PerfStats::game_frames_updated = false;
    }

    scheduler.Flush();
    LimitFramesAheadOfGpu();
    Stats().frames.fetch_add(1, std::memory_order_relaxed);

    system.perf_stats->EndSwap();
    rasterizer.TickFrame();
    EndFrame();
}

} // Anonymous namespace

QualityOptions g_quality_options;

void SetFrameGeneratorHooks(FrameGeneratorHooks hooks) {
    g_fg_hooks = std::move(hooks);
}

void SetQualityOptions(const QualityOptions& options) {
    g_quality_options = options;
}

const QualityOptions& GetQualityOptions() {
    return g_quality_options;
}

FrameStats TakeFrameStats() {
    GpuStats& stats = Stats();
    FrameStats result;
    result.frames = stats.frames.exchange(0);
    result.presented = static_cast<double>(stats.presented.exchange(0));
    const u64 cpu_ns = stats.cpu_ns.exchange(0);
    const u32 cpu_frames = stats.cpu_frames.exchange(0);
    result.cpu_max_ms = static_cast<double>(stats.cpu_max_ns.exchange(0)) / 1e6;
    if (cpu_frames > 0) {
        result.cpu_ms = static_cast<double>(cpu_ns) / 1e6 / cpu_frames;
    }
    const u64 gpu_ns = stats.gpu_ns.exchange(0);
    const u64 present_ns = stats.present_ns.exchange(0);
    const u64 stall_ns = stats.stall_ns.exchange(0);
    const u32 stalls = stats.stalls.exchange(0);
    const u32 draws = stats.draws.exchange(0);
    const u32 skipped = stats.skipped.exchange(0);
    const u32 render_passes = stats.render_passes.exchange(0);
    const u32 blit_passes = stats.blit_passes.exchange(0);
    const u32 feedback_copies = stats.feedback_copies.exchange(0);
    const u64 drawable_ns = stats.drawable_ns.exchange(0);
    const u32 intervals = stats.present_intervals.exchange(0);
    const u64 interval_us = stats.present_interval_us.exchange(0);
    const u64 interval_sq_us = stats.present_interval_sq_us.exchange(0);
    result.present_late = static_cast<double>(stats.present_late.exchange(0));
    const u64 fg_ns = stats.fg_ns.exchange(0);
    const u32 fg_made = stats.fg_made.exchange(0);
    result.fg_made = static_cast<double>(fg_made);
    result.fg_skipped = static_cast<double>(stats.fg_skipped.exchange(0));
    if (fg_made > 0) {
        result.fg_ms = static_cast<double>(fg_ns) / 1e6 / fg_made;
    }
    if (intervals > 1) {
        const double n = static_cast<double>(intervals);
        const double mean = static_cast<double>(interval_us) / n;
        const double variance =
            std::max(0.0, static_cast<double>(interval_sq_us) / n - mean * mean);
        result.present_interval_ms = mean / 1000.0;
        result.present_jitter_ms = std::sqrt(variance) / 1000.0;
    }
    if (result.frames > 0) {
        const double frames = static_cast<double>(result.frames);
        result.gpu_ms = static_cast<double>(gpu_ns) / 1e6 / frames;
        result.present_ms = static_cast<double>(present_ns) / 1e6 / frames;
        result.stall_ms = static_cast<double>(stall_ns) / 1e6 / frames;
        result.stalls = static_cast<double>(stalls) / frames;
        result.draws = static_cast<double>(draws) / frames;
        result.skipped = static_cast<double>(skipped) / frames;
        result.render_passes = static_cast<double>(render_passes) / frames;
        result.blit_passes = static_cast<double>(blit_passes) / frames;
        result.feedback_copies = static_cast<double>(feedback_copies) / frames;
        result.drawable_ms = static_cast<double>(drawable_ns) / 1e6 / frames;
    }
    return result;
}

void SetPostProcessing(int effect, int anaglyph_mode) {
    g_post_effect.store(effect, std::memory_order_relaxed);
    g_anaglyph_mode.store(anaglyph_mode, std::memory_order_relaxed);
}

void SetScreenStyle(int sharpen_method, int scale_filter, int grid_strength, int saturation,
                    int contrast, int brightness) {
    g_sharpen_method.store(std::clamp(sharpen_method, 0, 1), std::memory_order_relaxed);
    g_scale_filter.store(std::clamp(scale_filter, 0, 2), std::memory_order_relaxed);
    g_grid_strength.store(std::clamp(grid_strength, 0, 100), std::memory_order_relaxed);
    g_saturation.store(std::clamp(saturation, -100, 100), std::memory_order_relaxed);
    g_contrast.store(std::clamp(contrast, -100, 100), std::memory_order_relaxed);
    g_brightness.store(std::clamp(brightness, -100, 100), std::memory_order_relaxed);
}

void SetSharpness(bool sharp_pixels, int sharpness) {
    g_sharp_pixels.store(sharp_pixels ? 1 : 0, std::memory_order_relaxed);
    g_sharpness.store(std::clamp(sharpness, 0, 100), std::memory_order_relaxed);
}

void SetCustomPostShader(const std::string& path) {
    std::scoped_lock lock{g_custom_post_mutex};
    g_custom_post_path = path;
}

void SetUpscalerMode(UpscalerMode mode) {
    g_upscaler_mode.store(static_cast<int>(mode), std::memory_order_relaxed);
}

void SetUpscaleFactor(float factor) {
    g_upscale_factor.store(std::clamp(factor, 0.0f, 8.0f), std::memory_order_relaxed);
}

bool IsUpscalingSupported() {
#if AZAHAR_HAS_METALFX
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    return device != nil && MetalFXSupported(device);
#else
    return false;
#endif
}

std::unique_ptr<VideoCore::RendererBase> CreateRenderer(Core::System& system, Pica::PicaCore& pica,
                                                        Frontend::EmuWindow& window,
                                                        Frontend::EmuWindow* secondary_window) {
    return std::make_unique<RendererMetal>(system, pica, window, secondary_window);
}

} // namespace Metal
