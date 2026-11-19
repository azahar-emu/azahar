// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <array>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>
#include <fmt/format.h>
#include "video_core/rasterizer_cache/framebuffer_base.h"
#include "video_core/rasterizer_cache/rasterizer_cache_base.h"
#include "video_core/rasterizer_cache/surface_base.h"
#include "video_core/renderer_metal/mtl_instance.h"
#include "video_core/renderer_metal/mtl_scheduler.h"
#include "video_core/renderer_metal/mtl_stream_buffer.h"

namespace VideoCore {
struct Material;
}

namespace Metal {

class Surface;

enum Type {
    Current = -1,
    Base = 0,
    Scaled,
    Custom,
    Copy,
    Num,
};

/**
 * Render-pass based helpers for operations Metal has no fixed-function equivalent for: scaled
 * blits, partial clears and depth/stencil blits.
 */
class BlitHelper {
public:
    explicit BlitHelper(const Instance& instance, Scheduler& scheduler);
    ~BlitHelper();

    /// Blits a rectangle of a color texture into a rectangle of another color texture.
    void BlitColor(id<MTLTexture> src, u32 src_level, u32 src_slice,
                   const Common::Rectangle<u32>& src_rect, id<MTLTexture> dst, u32 dst_level,
                   u32 dst_slice, const Common::Rectangle<u32>& dst_rect, bool linear);

    /// Blits a rectangle of a depth(-stencil) texture into another depth(-stencil) texture.
    void BlitDepthStencil(id<MTLTexture> src, id<MTLTexture> src_stencil_view, u32 src_level,
                          const Common::Rectangle<u32>& src_rect, id<MTLTexture> dst, u32 dst_level,
                          const Common::Rectangle<u32>& dst_rect);

    /// Clears a rectangle of a color texture.
    void ClearColor(id<MTLTexture> texture, u32 level, u32 slice,
                    const Common::Rectangle<u32>& rect, const Common::Vec4f& color);

    /// Clears a rectangle of a depth(-stencil) texture.
    void ClearDepthStencil(id<MTLTexture> texture, u32 level, bool has_stencil,
                           const Common::Rectangle<u32>& rect, float depth, u8 stencil);

    /**
     * Upscales a rectangle of a texture into the same rectangle of its scaled copy with one of the
     * texture filters (Settings::values.texture_filter). `res_scale` is the scale of the
     * destination. Returns false when no filter is enabled, so the caller blits normally.
     */
    bool FilterColor(id<MTLTexture> src, const Common::Rectangle<u32>& src_rect, id<MTLTexture> dst,
                     const Common::Rectangle<u32>& dst_rect, u32 res_scale);

    /// Returns (and caches) a utility pipeline using the provided fragment function.
    /// `color_writes` false keeps the color attachment unchanged (a depth clear in a color pass).
    id<MTLRenderPipelineState> GetPipeline(std::string_view function, MTLPixelFormat color,
                                           MTLPixelFormat depth, MTLPixelFormat stencil,
                                           bool color_writes = true);

    /// Draws a partial clear into a render pass with the given attachment formats (see
    /// Scheduler::ClearColorRect), without touching the attachments it does not clear.
    void DrawClear(id<MTLRenderCommandEncoder> encoder, const Scheduler::PendingClear& clear,
                   MTLPixelFormat color, MTLPixelFormat depth, MTLPixelFormat stencil);

    /// Sets the viewport/scissor to the destination rectangle and returns source coordinates
    /// adjusted for flipped rectangles.
    void SetupRect(id<MTLRenderCommandEncoder> encoder, id<MTLTexture> dst, u32 dst_level,
                   const Common::Rectangle<u32>& dst_rect, const Common::Rectangle<u32>& src_rect,
                   u32 src_width, u32 src_height, float lod, const Common::Vec4f& color,
                   float depth);

private:
    const Instance& instance;
    Scheduler& scheduler;
    id<MTLLibrary> library;
    id<MTLLibrary> stencil_library;
    id<MTLLibrary> filter_library = nil; ///< Compiled when the first filter is used
    id<MTLSamplerState> linear_sampler;
    id<MTLSamplerState> nearest_sampler;
    id<MTLDepthStencilState> depth_write_state;
    id<MTLDepthStencilState> depth_stencil_write_state;
    id<MTLDepthStencilState> no_depth_state; ///< Depth and stencil untouched
    std::unordered_map<std::string, id<MTLRenderPipelineState>> pipelines;
};

/**
 * Provides texture manipulation functions to the rasterizer cache
 * Separating this into a class makes it easier to abstract graphics API code
 */
/**
 * Keeps the textures of surfaces that were dropped (the cache's garbage collector removes the
 * surfaces a game stopped using) so the next surface of the same size, format and usage takes one
 * of them instead of asking Metal for a new texture. Creating a texture costs time and memory
 * churn, and games that stream assets create them in bursts.
 *
 * A reused texture still has the old surface's pixels. Surfaces start out with nothing valid, the
 * cache uploads or copies into a region before it is used, so that does not show; it can only
 * make a game that reads pixels it never wrote see leftovers instead of zeros.
 */
class TexturePool {
public:
    struct Key {
        u32 type{};
        u32 format{};
        u32 width{};
        u32 height{};
        u32 levels{};
        u64 usage{};
        bool operator==(const Key&) const = default;
    };

    explicit TexturePool(bool enabled_) : enabled{enabled_} {}

    /// A texture for the key, nil when the pool has none.
    id<MTLTexture> Acquire(const Key& key);

    /// Takes a texture a surface let go of. Returns without keeping it when the pool is full of
    /// more recent ones.
    void Release(id<MTLTexture> texture);

    /// Drops the textures nobody asked for in a while. Called once per frame.
    void Tick();

    bool Enabled() const noexcept {
        return enabled;
    }

    static Key KeyOf(MTLTextureDescriptor* desc);

private:
    struct Entry {
        Key key;
        id<MTLTexture> texture = nil;
        u64 size = 0;
        u64 last_used = 0;
    };

    bool enabled;
    std::mutex mutex;
    std::vector<Entry> entries;
    u64 total_size{};
    u64 frame{};
    u64 reused{};
    u64 created{};
    u64 dropped{};

public:
    /// Counted for the statistics line.
    void CountCreated() noexcept {
        ++created;
    }
};

class TextureRuntime {
    friend class Surface;

public:
    explicit TextureRuntime(const Instance& instance, Scheduler& scheduler);
    ~TextureRuntime();

    const Instance& GetInstance() const {
        return instance;
    }

    Scheduler& GetScheduler() const {
        return scheduler;
    }

    BlitHelper& GetBlitHelper() {
        return blit_helper;
    }

    TexturePool& GetTexturePool() {
        return texture_pool;
    }

    /// Once per frame.
    void TickFrame() {
        texture_pool.Tick();
    }

    bool SupportsCustomFormat(VideoCore::CustomPixelFormat pixel_format) const {
        return instance.GetTraits(pixel_format).transfer_support;
    }

    /// Gets an opaque tick-value used to indicate to the garbage collector when a surface was made.
    u64 GetResourceTick();

    /// Gets an opaque tick-value used to indicate to the garbage collector what surfaces can be
    /// safely deleted.
    u64 GetResourceFreeTick();

    /// Submits and waits for current GPU work.
    void Finish();

    /// Maps an internal staging buffer of the provided size for pixel uploads/downloads
    VideoCore::StagingData FindStaging(u32 size, bool upload);

    /// Attempts to reinterpret a rectangle of source to another rectangle of dest
    bool Reinterpret(Surface& source, Surface& dest, const VideoCore::TextureCopy& copy);

    /// Fills the rectangle of the texture with the clear value provided
    bool ClearTexture(Surface& surface, const VideoCore::TextureClear& clear);

    /// Copies a rectangle of src_tex to another rectange of dst_rect
    bool CopyTextures(Surface& source, Surface& dest,
                      std::span<const VideoCore::TextureCopy> copies);

    bool CopyTextures(Surface& source, Surface& dest, const VideoCore::TextureCopy& copy) {
        return CopyTextures(source, dest, std::array{copy});
    }

    /// Blits a rectangle of src_tex to another rectange of dst_rect
    bool BlitTextures(Surface& surface, Surface& dest, const VideoCore::TextureBlit& blit);

    /// Generates mipmaps for all the available levels of the texture
    void GenerateMipmaps(Surface& surface);

    /// Returns true if the provided pixel format needs convertion
    bool NeedsConversion(const Surface& surface) const;

private:
    const Instance& instance;
    Scheduler& scheduler;
    BlitHelper blit_helper;
    TexturePool texture_pool;
    StreamBuffer upload_buffer;
    StreamBuffer download_buffer;

    /// Small depth readbacks the CPU asks for every frame, answered with the previous result
    /// while the next one is copied in the background (see Surface::Download).
    struct AsyncReadback {
        id<MTLBuffer> buffer = nil;
        u64 tick = 0;
        bool pending = false;
        std::vector<u8> last;
    };
    std::unordered_map<u64, AsyncReadback> async_readbacks;
};

/// The current texture of a surface, shared with the framebuffers that render to it so they
/// follow when the surface replaces its texture (see Surface::StorageTexture).
struct TextureHandle {
    id<MTLTexture> texture = nil;
};

class Surface : public VideoCore::SurfaceBase {
    friend class TextureRuntime;

public:
    explicit Surface(TextureRuntime& runtime, const VideoCore::SurfaceParams& params,
                     const VideoCore::SurfaceFlagBits& initial_flag_bits = {});
    explicit Surface(TextureRuntime& runtime, const VideoCore::SurfaceBase& surface,
                     const VideoCore::Material* materal);
    ~Surface();

    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    Surface(Surface&& o) noexcept = default;
    Surface& operator=(Surface&& o) noexcept = default;

    /// Returns the texture at index, otherwise the current one.
    id<MTLTexture> Texture(Type type = Type::Current) const noexcept {
        return textures[type == Type::Current ? current : type];
    }

    /// Returns a copy of the current texture, used to break feedback loops.
    id<MTLTexture> CopyTexture();

    /// Returns a R32Uint view of the current texture used by shadow mapping. With texture
    /// compression the texture is made without the usage that needs, it is replaced by one that
    /// has it (and keeps its contents) the first time.
    id<MTLTexture> StorageTexture();

    /// The shared handle of the current texture.
    const std::shared_ptr<TextureHandle>& Handle() const noexcept {
        return handle;
    }

    /// Returns a stencil view of the current depth-stencil texture.
    id<MTLTexture> StencilTexture();

    /// Returns the texture, replaced first by one that allows views of other formats when it was
    /// made compressible without (see CreateTexture).
    id<MTLTexture> ViewableTexture(Type type);

    /// Returns width of the surface
    u32 GetWidth() const noexcept {
        return width;
    }

    /// Returns height of the surface
    u32 GetHeight() const noexcept {
        return height;
    }

    /// Returns resolution scale of the surface
    u32 GetResScale() const noexcept {
        return res_scale;
    }

    bool IsDepth() const noexcept {
        return traits.is_depth;
    }

    bool HasStencil() const noexcept {
        return traits.has_stencil;
    }

    /// Uploads pixel data in staging to a rectangle region of the surface texture
    void Upload(const VideoCore::BufferTextureCopy& upload, const VideoCore::StagingData& staging);

    /// Uploads the custom material to the surface allocation.
    void UploadCustom(const VideoCore::Material* material, u32 level);

    /// Downloads pixel data to staging from a rectangle region of the surface texture
    void Download(const VideoCore::BufferTextureCopy& download,
                  const VideoCore::StagingData& staging);

    /// Scales up the surface to match the new resolution scale.
    void ScaleUp(u32 new_scale);

    /// Returns the bpp of the internal surface format
    u32 GetInternalBytesPerPixel() const;

private:
    /// `full_usage` adds the usage texture compression leaves out (shader writes, other format
    /// views).
    id<MTLTexture> CreateTexture(u32 width, u32 height, MTLPixelFormat format,
                                 std::string_view label, bool full_usage = false) const;

    id<MTLTexture> ReplaceWithFullUsage(Type type);

    void SyncHandle() {
        if (!handle) {
            handle = std::make_shared<TextureHandle>();
        }
        handle->texture = Texture();
    }

    /// Performs blit between the scaled/unscaled images
    void BlitScale(const VideoCore::TextureBlit& blit, bool up_scale);

public:
    TextureRuntime* runtime;
    const Instance* instance;
    Scheduler* scheduler;
    FormatTraits traits;
    std::array<id<MTLTexture>, Type::Num> textures{};
    Type current{};
    id<MTLTexture> storage_view = nil;
    id<MTLTexture> stencil_view = nil;
    bool storage_capable{};
    std::shared_ptr<TextureHandle> handle;
};

class Framebuffer : public VideoCore::FramebufferParams {
public:
    explicit Framebuffer(TextureRuntime& runtime, const VideoCore::FramebufferParams& params,
                         Surface* color, Surface* depth_stencil);
    ~Framebuffer();

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    Framebuffer(Framebuffer&& other) noexcept = default;
    Framebuffer& operator=(Framebuffer&& other) noexcept = default;

    VideoCore::PixelFormat Format(VideoCore::SurfaceType type) const noexcept {
        return formats[Index(type)];
    }

    /// Returns the texture attached for the surface type (nil if none).
    id<MTLTexture> Attachment(VideoCore::SurfaceType type) const noexcept {
        Refresh();
        return attachments[Index(type)];
    }

    /// Returns the shadow buffer (R32Uint view) used for shadow rendering.
    id<MTLTexture> ShadowBuffer() const noexcept {
        return shadow_buffer;
    }

    /// Returns the render pass description for this framebuffer.
    const RenderTarget& Target() const noexcept {
        Refresh();
        return target;
    }

    /// Returns true if the framebuffer can be rendered to.
    bool IsValid() const noexcept {
        return valid;
    }

    u32 Width() const noexcept {
        return width;
    }

    u32 Height() const noexcept {
        return height;
    }

    u32 Scale() const noexcept {
        return res_scale;
    }

private:
    /// Follows a surface that replaced its texture.
    void Refresh() const noexcept {
        if (handles[0] && handles[0]->texture != attachments[0]) {
            attachments[0] = handles[0]->texture;
            target.color = attachments[0];
        }
        if (handles[1] && handles[1]->texture != attachments[1]) {
            attachments[1] = handles[1]->texture;
            target.depth = attachments[1];
        }
    }

    mutable std::array<id<MTLTexture>, 2> attachments{};
    std::array<std::shared_ptr<TextureHandle>, 2> handles{};
    std::array<VideoCore::PixelFormat, 2> formats{VideoCore::PixelFormat::Invalid,
                                                  VideoCore::PixelFormat::Invalid};
    id<MTLTexture> shadow_buffer = nil;
    mutable RenderTarget target{};
    u32 width{};
    u32 height{};
    u32 res_scale{1};
    bool valid{};
};

class Sampler {
public:
    Sampler(TextureRuntime& runtime, const VideoCore::SamplerParams& params);
    ~Sampler();

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;

    Sampler(Sampler&& o) noexcept = default;
    Sampler& operator=(Sampler&& o) noexcept = default;

    [[nodiscard]] id<MTLSamplerState> Handle() const noexcept {
        return sampler;
    }

private:
    id<MTLSamplerState> sampler;
};

class DebugScope {
public:
    template <typename... T>
    explicit DebugScope(TextureRuntime& runtime, Common::Vec4f color,
                        fmt::format_string<T...> format, T... args)
        : DebugScope{runtime, color, fmt::format(format, std::forward<T>(args)...)} {}
    explicit DebugScope(TextureRuntime& runtime, Common::Vec4f color, std::string_view label);
    ~DebugScope();

private:
    Scheduler& scheduler;
    bool enabled;
};

struct Traits {
    using Runtime = Metal::TextureRuntime;
    using Surface = Metal::Surface;
    using Sampler = Metal::Sampler;
    using Framebuffer = Metal::Framebuffer;
    using DebugScope = Metal::DebugScope;
};

using RasterizerCache = VideoCore::RasterizerCache<Traits>;

} // namespace Metal
