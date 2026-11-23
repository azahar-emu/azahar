// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <algorithm>
#include <chrono>
#include <cstring>
#include "common/alignment.h"
#include "common/hash.h"
#include "common/assert.h"
#include "common/literals.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "video_core/custom_textures/custom_format.h"
#include "video_core/custom_textures/material.h"
#include "video_core/rasterizer_cache/texture_codec.h"
#include "video_core/rasterizer_cache/utils.h"
#include "video_core/renderer_metal/mtl_texture_runtime.h"
#include "video_core/renderer_metal/mtl_utility_shaders.h"
#include "video_core/renderer_metal/pica_to_mtl.h"
#include "video_core/renderer_metal/renderer_metal.h"

namespace Metal {

namespace {

using namespace Common::Literals;
using VideoCore::MapType;
using VideoCore::PixelFormat;
using VideoCore::SurfaceType;
using VideoCore::TextureType;

constexpr u64 UPLOAD_BUFFER_SIZE = 64_MiB;
constexpr u64 DOWNLOAD_BUFFER_SIZE = 16_MiB;

struct BlitParams {
    std::array<f32, 4> src_rect;
    std::array<f32, 4> color;
    f32 depth;
    f32 lod;
    std::array<f32, 2> pad;
};
static_assert(sizeof(BlitParams) == 48);

id<MTLLibrary> CompileLibrary(id<MTLDevice> device, std::string_view source,
                              std::string_view name) {
    MTLCompileOptions* options = [MTLCompileOptions new];
    if (@available(iOS 16.0, macOS 13.0, *)) {
        options.languageVersion = MTLLanguageVersion2_4;
    }
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:ToNSString(source)
                                                  options:options
                                                    error:&error];
    if (!library) {
        LOG_CRITICAL(Render, "Failed to compile Metal library {}: {}", name,
                     error ? [error.localizedDescription UTF8String] : "unknown error");
    }
    return library;
}

/// Returns the size of a texture level.
std::pair<u32, u32> LevelExtent(id<MTLTexture> texture, u32 level) {
    return {std::max<u32>(static_cast<u32>(texture.width) >> level, 1u),
            std::max<u32>(static_cast<u32>(texture.height) >> level, 1u)};
}

/// Returns a 2D single level/slice view of a texture, used as a blit source.
id<MTLTexture> MakeLevelView(id<MTLTexture> texture, MTLPixelFormat format, u32 level, u32 slice) {
    return [texture newTextureViewWithPixelFormat:format
                                      textureType:MTLTextureType2D
                                           levels:NSMakeRange(level, 1)
                                           slices:NSMakeRange(slice, 1)];
}

/// Returns the bytes per row and rows of a custom texture level.
std::pair<u32, u32> CustomLayout(VideoCore::CustomPixelFormat format, u32 width, u32 height) {
    using VideoCore::CustomPixelFormat;
    const auto blocks = [&](u32 block_w, u32 block_h, u32 block_size) {
        const u32 blocks_x = (width + block_w - 1) / block_w;
        const u32 blocks_y = (height + block_h - 1) / block_h;
        return std::make_pair(blocks_x * block_size, blocks_y);
    };
    switch (format) {
    case CustomPixelFormat::BC1:
        return blocks(4, 4, 8);
    case CustomPixelFormat::BC3:
    case CustomPixelFormat::BC5:
    case CustomPixelFormat::BC7:
    case CustomPixelFormat::ASTC4:
        return blocks(4, 4, 16);
    case CustomPixelFormat::ASTC6:
        return blocks(6, 6, 16);
    case CustomPixelFormat::ASTC8:
        return blocks(8, 6, 16);
    case CustomPixelFormat::RGBA8:
    default:
        return {width * 4, height};
    }
}

} // Anonymous namespace

// ===========================================================================================
// BlitHelper
// ===========================================================================================

BlitHelper::BlitHelper(const Instance& instance_, Scheduler& scheduler_)
    : instance{instance_}, scheduler{scheduler_} {
    id<MTLDevice> device = instance.GetDevice();
    library = CompileLibrary(device, Shaders::UTILITY, "utility");
    if (instance.IsStencilExportSupported()) {
        stencil_library = CompileLibrary(device, Shaders::STENCIL, "stencil utility");
    }

    MTLSamplerDescriptor* sampler_desc = [MTLSamplerDescriptor new];
    sampler_desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_desc.minFilter = MTLSamplerMinMagFilterLinear;
    sampler_desc.magFilter = MTLSamplerMinMagFilterLinear;
    linear_sampler = [device newSamplerStateWithDescriptor:sampler_desc];
    sampler_desc.minFilter = MTLSamplerMinMagFilterNearest;
    sampler_desc.magFilter = MTLSamplerMinMagFilterNearest;
    nearest_sampler = [device newSamplerStateWithDescriptor:sampler_desc];

    MTLDepthStencilDescriptor* ds_desc = [MTLDepthStencilDescriptor new];
    ds_desc.depthCompareFunction = MTLCompareFunctionAlways;
    ds_desc.depthWriteEnabled = YES;
    depth_write_state = [device newDepthStencilStateWithDescriptor:ds_desc];

    MTLStencilDescriptor* stencil_desc = [MTLStencilDescriptor new];
    stencil_desc.stencilCompareFunction = MTLCompareFunctionAlways;
    stencil_desc.depthStencilPassOperation = MTLStencilOperationReplace;
    stencil_desc.stencilFailureOperation = MTLStencilOperationReplace;
    stencil_desc.depthFailureOperation = MTLStencilOperationReplace;
    stencil_desc.readMask = 0xFF;
    stencil_desc.writeMask = 0xFF;
    ds_desc.frontFaceStencil = stencil_desc;
    ds_desc.backFaceStencil = stencil_desc;
    depth_stencil_write_state = [device newDepthStencilStateWithDescriptor:ds_desc];

    // Leaves depth and stencil alone (a color clear drawn in a pass that has them).
    no_depth_state = [device newDepthStencilStateWithDescriptor:[MTLDepthStencilDescriptor new]];
}

void BlitHelper::DrawClear(id<MTLRenderCommandEncoder> encoder,
                           const Scheduler::PendingClear& clear, MTLPixelFormat color,
                           MTLPixelFormat depth, MTLPixelFormat stencil, u32 samples) {
    if (clear.is_depth) {
        id<MTLRenderPipelineState> pipeline =
            GetPipeline("clear_depth_fs", color, depth, stencil, false, samples);
        if (!pipeline) {
            return;
        }
        [encoder setRenderPipelineState:pipeline];
        [encoder setDepthStencilState:clear.has_stencil ? depth_stencil_write_state
                                                        : depth_write_state];
        [encoder setStencilReferenceValue:clear.stencil];
        SetupRect(encoder, clear.texture, clear.level, clear.rect, clear.rect, 1, 1, 0.0f, {},
                  static_cast<float>(clear.depth));
    } else {
        id<MTLRenderPipelineState> pipeline =
            GetPipeline("clear_color_fs", color, depth, stencil, true, samples);
        if (!pipeline) {
            return;
        }
        [encoder setRenderPipelineState:pipeline];
        [encoder setDepthStencilState:no_depth_state];
        const Common::Vec4f value{static_cast<f32>(clear.color.red),
                                  static_cast<f32>(clear.color.green),
                                  static_cast<f32>(clear.color.blue),
                                  static_cast<f32>(clear.color.alpha)};
        SetupRect(encoder, clear.texture, clear.level, clear.rect, clear.rect, 1, 1, 0.0f, value,
                  0.0f);
    }
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
}

BlitHelper::~BlitHelper() = default;

id<MTLRenderPipelineState> BlitHelper::GetPipeline(std::string_view function, MTLPixelFormat color,
                                                   MTLPixelFormat depth, MTLPixelFormat stencil,
                                                   bool color_writes, u32 samples) {
    samples = std::max(samples, 1u);
    const std::string key =
        fmt::format("{}:{}:{}:{}:{}:{}", function, static_cast<u64>(color), static_cast<u64>(depth),
                    static_cast<u64>(stencil), color_writes ? 1 : 0, samples);
    if (const auto it = pipelines.find(key); it != pipelines.end()) {
        return it->second;
    }

    id<MTLLibrary> fs_library = function == "blit_depth_stencil_fs" ? stencil_library : library;
    if (function.starts_with("filter_")) {
        if (!filter_library) {
            filter_library = CompileLibrary(instance.GetDevice(), Shaders::FILTERS, "filters");
        }
        fs_library = filter_library;
    }
    if (!library || !fs_library) {
        return nil;
    }

    MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
    desc.vertexFunction = [library newFunctionWithName:@"blit_vs"];
    desc.fragmentFunction = [fs_library newFunctionWithName:ToNSString(function)];
    desc.colorAttachments[0].pixelFormat = color;
    desc.depthAttachmentPixelFormat = depth;
    desc.stencilAttachmentPixelFormat = stencil;
    desc.rasterSampleCount = samples;
    if (color == MTLPixelFormatInvalid || !color_writes) {
        desc.colorAttachments[0].writeMask = MTLColorWriteMaskNone;
    }
    desc.label = ToNSString(function);

    NSError* error = nil;
    id<MTLRenderPipelineState> pipeline =
        [instance.GetDevice() newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!pipeline) {
        LOG_CRITICAL(Render, "Failed to create utility pipeline {}: {}", function,
                     error ? [error.localizedDescription UTF8String] : "unknown error");
    }
    pipelines.emplace(key, pipeline);
    return pipeline;
}

void BlitHelper::SetupRect(id<MTLRenderCommandEncoder> encoder, id<MTLTexture> dst, u32 dst_level,
                           const Common::Rectangle<u32>& dst_rect,
                           const Common::Rectangle<u32>& src_rect, u32 src_width, u32 src_height,
                           float lod, const Common::Vec4f& color, float depth) {
    // Rectangles use row coordinates where "bottom" is the first row. Either axis can be
    // mirrored, normalize the destination and mirror the source coordinates instead.
    const u32 dst_x0 = std::min(dst_rect.left, dst_rect.right);
    const u32 dst_x1 = std::max(dst_rect.left, dst_rect.right);
    const u32 dst_y0 = std::min(dst_rect.bottom, dst_rect.top);
    const u32 dst_y1 = std::max(dst_rect.bottom, dst_rect.top);
    const bool mirror_x = dst_rect.left > dst_rect.right;
    const bool mirror_y = dst_rect.bottom > dst_rect.top;

    f32 u0 = static_cast<f32>(src_rect.left) / static_cast<f32>(std::max(src_width, 1u));
    f32 u1 = static_cast<f32>(src_rect.right) / static_cast<f32>(std::max(src_width, 1u));
    f32 v0 = static_cast<f32>(src_rect.bottom) / static_cast<f32>(std::max(src_height, 1u));
    f32 v1 = static_cast<f32>(src_rect.top) / static_cast<f32>(std::max(src_height, 1u));
    if (mirror_x) {
        std::swap(u0, u1);
    }
    if (mirror_y) {
        std::swap(v0, v1);
    }

    const auto [level_width, level_height] = LevelExtent(dst, dst_level);
    const u32 x0 = std::min(dst_x0, level_width);
    const u32 y0 = std::min(dst_y0, level_height);
    const u32 x1 = std::min(dst_x1, level_width);
    const u32 y1 = std::min(dst_y1, level_height);

    const MTLViewport viewport = {
        .originX = static_cast<double>(dst_x0),
        .originY = static_cast<double>(dst_y0),
        .width = static_cast<double>(dst_x1 - dst_x0),
        .height = static_cast<double>(dst_y1 - dst_y0),
        .znear = 0.0,
        .zfar = 1.0,
    };
    [encoder setViewport:viewport];
    [encoder setScissorRect:MTLScissorRect{x0, y0, std::max(x1 - x0, 0u), std::max(y1 - y0, 0u)}];

    const BlitParams params = {
        .src_rect = {u0, v0, u1, v1},
        .color = {color.x, color.y, color.z, color.w},
        .depth = depth,
        .lod = lod,
    };
    [encoder setVertexBytes:&params length:sizeof(params) atIndex:0];
    [encoder setFragmentBytes:&params length:sizeof(params) atIndex:0];
}

void BlitHelper::BlitColor(id<MTLTexture> src, u32 src_level, u32 src_slice,
                           const Common::Rectangle<u32>& src_rect, id<MTLTexture> dst,
                           u32 dst_level, u32 dst_slice, const Common::Rectangle<u32>& dst_rect,
                           bool linear) {
    // A multisampled destination is the copy of a render target being refreshed (MSAA). A render
    // target with an up to date copy is blitted into the copy, which is resolved into it.
    id<MTLTexture> msaa = scheduler.UpToDateMsaa(dst, dst_level, dst_slice);
    id<MTLRenderPipelineState> pipeline =
        GetPipeline("blit_color_fs", dst.pixelFormat, MTLPixelFormatInvalid, MTLPixelFormatInvalid,
                    true, static_cast<u32>(msaa != nil ? msaa.sampleCount : dst.sampleCount));
    if (!pipeline) {
        return;
    }

    id<MTLTexture> src_view = MakeLevelView(src, src.pixelFormat, src_level, src_slice);
    const auto [src_width, src_height] = LevelExtent(src, src_level);

    MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
    desc.colorAttachments[0].texture = dst;
    desc.colorAttachments[0].level = dst_level;
    desc.colorAttachments[0].slice = dst_slice;
    desc.colorAttachments[0].loadAction = MTLLoadActionLoad;
    desc.colorAttachments[0].storeAction = MTLStoreActionStore;
    if (msaa != nil) {
        desc.colorAttachments[0].texture = msaa;
        desc.colorAttachments[0].level = 0;
        desc.colorAttachments[0].slice = 0;
        desc.colorAttachments[0].resolveTexture = dst;
        desc.colorAttachments[0].resolveLevel = dst_level;
        desc.colorAttachments[0].resolveSlice = dst_slice;
        desc.colorAttachments[0].storeAction = MTLStoreActionStoreAndMultisampleResolve;
    }

    scheduler.SetPassLabel("blit color (scale or copy)");
    id<MTLRenderCommandEncoder> encoder = scheduler.BeginRendering(desc);
    [encoder setRenderPipelineState:pipeline];
    SetupRect(encoder, dst, dst_level, dst_rect, src_rect, src_width, src_height, 0.0f, {}, 0.0f);
    [encoder setFragmentTexture:src_view atIndex:0];
    [encoder setFragmentSamplerState:(linear ? linear_sampler : nearest_sampler) atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    scheduler.EndEncoding();
}

bool BlitHelper::FilterColor(id<MTLTexture> src, const Common::Rectangle<u32>& src_rect,
                             id<MTLTexture> dst, const Common::Rectangle<u32>& dst_rect,
                             u32 res_scale) {
    using TextureFilter = Settings::TextureFilter;
    const char* function = nullptr;
    switch (Settings::values.texture_filter.GetValue()) {
    case TextureFilter::Anime4K:
        function = "filter_anime4k_fs";
        break;
    case TextureFilter::Bicubic:
        function = "filter_bicubic_fs";
        break;
    case TextureFilter::ScaleForce:
        function = "filter_scale_force_fs";
        break;
    case TextureFilter::xBRZ:
        function = "filter_xbrz_fs";
        break;
    case TextureFilter::MMPX:
        function = "filter_mmpx_fs";
        break;
    default:
        return false;
    }

    id<MTLRenderPipelineState> pipeline =
        GetPipeline(function, dst.pixelFormat, MTLPixelFormatInvalid, MTLPixelFormatInvalid);
    if (!pipeline) {
        // The filter cannot be built, fall back to the plain scaling.
        return false;
    }

    const auto [src_width, src_height] = LevelExtent(src, 0);

    MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
    desc.colorAttachments[0].texture = dst;
    desc.colorAttachments[0].loadAction = MTLLoadActionLoad;
    desc.colorAttachments[0].storeAction = MTLStoreActionStore;

    scheduler.SetPassLabel("texture filter (xBRZ ...)");
    id<MTLRenderCommandEncoder> encoder = scheduler.BeginRendering(desc);
    [encoder setRenderPipelineState:pipeline];
    // The resolution scale travels in BlitParams::lod (used by xBRZ).
    SetupRect(encoder, dst, 0, dst_rect, src_rect, src_width, src_height,
              static_cast<float>(res_scale), {}, 0.0f);
    [encoder setFragmentTexture:src atIndex:0];
    [encoder setFragmentSamplerState:linear_sampler atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    scheduler.EndEncoding();
    return true;
}

void BlitHelper::BlitDepthStencil(id<MTLTexture> src, id<MTLTexture> src_stencil_view,
                                  u32 src_level, const Common::Rectangle<u32>& src_rect,
                                  id<MTLTexture> dst, u32 dst_level,
                                  const Common::Rectangle<u32>& dst_rect) {
    const bool has_stencil = dst.pixelFormat == MTLPixelFormatDepth32Float_Stencil8;
    const bool copy_stencil = has_stencil && src_stencil_view != nil && stencil_library != nil;
    const MTLPixelFormat stencil_format =
        has_stencil ? MTLPixelFormatDepth32Float_Stencil8 : MTLPixelFormatInvalid;
    id<MTLTexture> msaa = scheduler.UpToDateMsaa(dst, dst_level, 0);
    id<MTLRenderPipelineState> pipeline =
        GetPipeline(copy_stencil ? "blit_depth_stencil_fs" : "blit_depth_fs", MTLPixelFormatInvalid,
                    dst.pixelFormat, stencil_format, true,
                    static_cast<u32>(msaa != nil ? msaa.sampleCount : dst.sampleCount));
    if (!pipeline) {
        return;
    }
    if (has_stencil && !copy_stencil) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            LOG_WARNING(Render, "A depth blit cannot copy stencil (no stencil export or stencil "
                                "view), the stencil data is not copied");
        }
    }

    id<MTLTexture> src_view = MakeLevelView(src, src.pixelFormat, src_level, 0);
    const auto [src_width, src_height] = LevelExtent(src, src_level);

    MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
    desc.depthAttachment.texture = dst;
    desc.depthAttachment.level = dst_level;
    desc.depthAttachment.loadAction = MTLLoadActionLoad;
    desc.depthAttachment.storeAction = MTLStoreActionStore;
    if (has_stencil) {
        desc.stencilAttachment.texture = dst;
        desc.stencilAttachment.level = dst_level;
        desc.stencilAttachment.loadAction = MTLLoadActionLoad;
        desc.stencilAttachment.storeAction = MTLStoreActionStore;
    }
    if (msaa != nil) {
        desc.depthAttachment.texture = msaa;
        desc.depthAttachment.level = 0;
        desc.depthAttachment.resolveTexture = dst;
        desc.depthAttachment.resolveLevel = dst_level;
        desc.depthAttachment.depthResolveFilter = MTLMultisampleDepthResolveFilterSample0;
        desc.depthAttachment.storeAction = MTLStoreActionStoreAndMultisampleResolve;
        if (has_stencil) {
            desc.stencilAttachment.texture = msaa;
            desc.stencilAttachment.level = 0;
            desc.stencilAttachment.resolveTexture = dst;
            desc.stencilAttachment.resolveLevel = dst_level;
            desc.stencilAttachment.stencilResolveFilter = MTLMultisampleStencilResolveFilterSample0;
            desc.stencilAttachment.storeAction = MTLStoreActionStoreAndMultisampleResolve;
        }
    }

    scheduler.SetPassLabel("blit depth");
    id<MTLRenderCommandEncoder> encoder = scheduler.BeginRendering(desc);
    [encoder setRenderPipelineState:pipeline];
    [encoder setDepthStencilState:copy_stencil ? depth_stencil_write_state : depth_write_state];
    SetupRect(encoder, dst, dst_level, dst_rect, src_rect, src_width, src_height, 0.0f, {}, 0.0f);
    [encoder setFragmentTexture:src_view atIndex:0];
    if (copy_stencil) {
        [encoder setFragmentTexture:src_stencil_view atIndex:1];
    }
    [encoder setFragmentSamplerState:nearest_sampler atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    scheduler.EndEncoding();
}

void BlitHelper::ClearColor(id<MTLTexture> texture, u32 level, u32 slice,
                            const Common::Rectangle<u32>& rect, const Common::Vec4f& color) {
    id<MTLTexture> msaa = scheduler.UpToDateMsaa(texture, level, slice);
    id<MTLRenderPipelineState> pipeline = GetPipeline(
        "clear_color_fs", texture.pixelFormat, MTLPixelFormatInvalid, MTLPixelFormatInvalid, true,
        msaa != nil ? static_cast<u32>(msaa.sampleCount) : 1u);
    if (!pipeline) {
        return;
    }

    MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
    desc.colorAttachments[0].texture = texture;
    desc.colorAttachments[0].level = level;
    desc.colorAttachments[0].slice = slice;
    desc.colorAttachments[0].loadAction = MTLLoadActionLoad;
    desc.colorAttachments[0].storeAction = MTLStoreActionStore;
    if (msaa != nil) {
        desc.colorAttachments[0].texture = msaa;
        desc.colorAttachments[0].level = 0;
        desc.colorAttachments[0].slice = 0;
        desc.colorAttachments[0].resolveTexture = texture;
        desc.colorAttachments[0].resolveLevel = level;
        desc.colorAttachments[0].resolveSlice = slice;
        desc.colorAttachments[0].storeAction = MTLStoreActionStoreAndMultisampleResolve;
    }

    scheduler.SetPassLabel("partial clear color");
    id<MTLRenderCommandEncoder> encoder = scheduler.BeginRendering(desc);
    [encoder setRenderPipelineState:pipeline];
    SetupRect(encoder, texture, level, rect, rect, 1, 1, 0.0f, color, 0.0f);
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    scheduler.EndEncoding();
}

void BlitHelper::ClearDepthStencil(id<MTLTexture> texture, u32 level, bool has_stencil,
                                   const Common::Rectangle<u32>& rect, float depth, u8 stencil) {
    const MTLPixelFormat stencil_format = has_stencil ? texture.pixelFormat : MTLPixelFormatInvalid;
    id<MTLTexture> msaa = scheduler.UpToDateMsaa(texture, level, 0);
    id<MTLRenderPipelineState> pipeline =
        GetPipeline("clear_depth_fs", MTLPixelFormatInvalid, texture.pixelFormat, stencil_format,
                    true, msaa != nil ? static_cast<u32>(msaa.sampleCount) : 1u);
    if (!pipeline) {
        return;
    }

    MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
    desc.depthAttachment.texture = texture;
    desc.depthAttachment.level = level;
    desc.depthAttachment.loadAction = MTLLoadActionLoad;
    desc.depthAttachment.storeAction = MTLStoreActionStore;
    if (has_stencil) {
        desc.stencilAttachment.texture = texture;
        desc.stencilAttachment.level = level;
        desc.stencilAttachment.loadAction = MTLLoadActionLoad;
        desc.stencilAttachment.storeAction = MTLStoreActionStore;
    }
    if (msaa != nil) {
        desc.depthAttachment.texture = msaa;
        desc.depthAttachment.level = 0;
        desc.depthAttachment.resolveTexture = texture;
        desc.depthAttachment.resolveLevel = level;
        desc.depthAttachment.depthResolveFilter = MTLMultisampleDepthResolveFilterSample0;
        desc.depthAttachment.storeAction = MTLStoreActionStoreAndMultisampleResolve;
        if (has_stencil) {
            desc.stencilAttachment.texture = msaa;
            desc.stencilAttachment.level = 0;
            desc.stencilAttachment.resolveTexture = texture;
            desc.stencilAttachment.resolveLevel = level;
            desc.stencilAttachment.stencilResolveFilter = MTLMultisampleStencilResolveFilterSample0;
            desc.stencilAttachment.storeAction = MTLStoreActionStoreAndMultisampleResolve;
        }
    }

    scheduler.SetPassLabel("partial clear depth");
    id<MTLRenderCommandEncoder> encoder = scheduler.BeginRendering(desc);
    [encoder setRenderPipelineState:pipeline];
    [encoder setDepthStencilState:has_stencil ? depth_stencil_write_state : depth_write_state];
    [encoder setStencilReferenceValue:stencil];
    SetupRect(encoder, texture, level, rect, rect, 1, 1, 0.0f, {}, depth);
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    scheduler.EndEncoding();
}

// ===========================================================================================
// TextureRuntime
// ===========================================================================================

TextureRuntime::TextureRuntime(const Instance& instance_, Scheduler& scheduler_)
    : instance{instance_}, scheduler{scheduler_}, blit_helper{instance_, scheduler_},
      texture_pool{GetQualityOptions().texture_pool},
      upload_buffer{instance_, scheduler_, UPLOAD_BUFFER_SIZE, "Upload buffer"},
      download_buffer{instance_, scheduler_, DOWNLOAD_BUFFER_SIZE, "Download buffer", true} {
    scheduler.SetRectClearDrawer([this](id<MTLRenderCommandEncoder> encoder,
                                        const Scheduler::PendingClear& clear,
                                        MTLPixelFormat color, MTLPixelFormat depth,
                                        MTLPixelFormat stencil, u32 samples) {
        blit_helper.DrawClear(encoder, clear, color, depth, stencil, samples);
    });
    // MSAA: a multisampled copy is refreshed with the blits, every sample of a pixel taking the
    // pixel's value. Edges drawn before keep the smoothing they were resolved with.
    scheduler.SetMsaaRefresher([this](id<MTLTexture> resolved, id<MTLTexture> stencil_view,
                                      id<MTLTexture> msaa, bool is_depth) {
        const u32 width = static_cast<u32>(resolved.width);
        const u32 height = static_cast<u32>(resolved.height);
        const Common::Rectangle<u32> rect{0, height, width, 0};
        scheduler.SetPassLabel("MSAA refresh");
        if (is_depth) {
            if (stencil_view == nil && resolved.pixelFormat == MTLPixelFormatDepth32Float_Stencil8 &&
                (resolved.usage & MTLTextureUsagePixelFormatView) != 0) {
                stencil_view = [resolved newTextureViewWithPixelFormat:MTLPixelFormatX32_Stencil8];
            }
            blit_helper.BlitDepthStencil(resolved, stencil_view, 0, rect, msaa, 0, rect);
        } else {
            blit_helper.BlitColor(resolved, 0, 0, rect, msaa, 0, 0, rect, false);
        }
    });
}

TextureRuntime::~TextureRuntime() {
    scheduler.SetRectClearDrawer({});
    scheduler.SetMsaaRefresher({});
}

VideoCore::StagingData TextureRuntime::FindStaging(u32 size, bool upload) {
    StreamBuffer& buffer = upload ? upload_buffer : download_buffer;
    const auto [data, offset, invalidate] = buffer.Map(size, 16);
    return VideoCore::StagingData{
        .size = size,
        .offset = offset,
        .mapped = std::span{data, size},
    };
}

u64 TextureRuntime::GetResourceTick() {
    return scheduler.CurrentTick();
}

u64 TextureRuntime::GetResourceFreeTick() {
    return scheduler.KnownGpuTick();
}

void TextureRuntime::Finish() {
    scheduler.Finish();
}

bool TextureRuntime::NeedsConversion(const Surface& surface) const {
    // Depth-stencil is de-interleaved by the runtime itself.
    return surface.traits.needs_conversion && !surface.traits.has_stencil;
}

bool TextureRuntime::Reinterpret(Surface& source, Surface& dest,
                                 const VideoCore::TextureCopy& copy) {
    const PixelFormat src_format = source.pixel_format;
    const PixelFormat dst_format = dest.pixel_format;
    ASSERT_MSG(src_format != dst_format, "Reinterpretation with the same format is invalid");

    if (source.traits.native == dest.traits.native && !source.traits.is_depth) {
        // All color formats are stored as RGBA8, so a reinterpretation is a plain copy.
        return CopyTextures(source, dest, copy);
    }

    if (src_format == PixelFormat::D24S8 && dst_format == PixelFormat::RGBA8) {
        id<MTLRenderPipelineState> pipeline = blit_helper.GetPipeline(
            "d24s8_to_rgba8_fs", dest.traits.native, MTLPixelFormatInvalid, MTLPixelFormatInvalid);
        if (!pipeline) {
            return false;
        }
        id<MTLTexture> source_texture = source.ViewableTexture(Type::Current);
        id<MTLTexture> depth_view =
            MakeLevelView(source_texture, source.traits.native, copy.src_level, 0);
        id<MTLTexture> stencil_view =
            MakeLevelView(source_texture, MTLPixelFormatX32_Stencil8, copy.src_level, 0);

        MTLRenderPassDescriptor* desc = [MTLRenderPassDescriptor renderPassDescriptor];
        desc.colorAttachments[0].texture = dest.Texture();
        desc.colorAttachments[0].level = copy.dst_level;
        desc.colorAttachments[0].loadAction = MTLLoadActionLoad;
        desc.colorAttachments[0].storeAction = MTLStoreActionStore;

        const Common::Rectangle<u32> dst_rect{
            copy.dst_offset.x, copy.dst_offset.y + copy.extent.height,
            copy.dst_offset.x + copy.extent.width, copy.dst_offset.y};
        scheduler.SetPassLabel("depth to color copy");
        id<MTLRenderCommandEncoder> encoder = scheduler.BeginRendering(desc);
        [encoder setRenderPipelineState:pipeline];
        // Reuse the blit parameter setup, then override the source offset.
        blit_helper.SetupRect(encoder, dest.Texture(), copy.dst_level, dst_rect, dst_rect, 1, 1,
                              0.0f, {}, 0.0f);
        const BlitParams params = {
            .src_rect = {static_cast<f32>(copy.src_offset.x) - static_cast<f32>(copy.dst_offset.x),
                         static_cast<f32>(copy.src_offset.y) - static_cast<f32>(copy.dst_offset.y),
                         0.0f, 0.0f},
        };
        [encoder setFragmentBytes:&params length:sizeof(params) atIndex:0];
        [encoder setFragmentTexture:depth_view atIndex:0];
        [encoder setFragmentTexture:stencil_view atIndex:1];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        scheduler.EndEncoding();
        return true;
    }

    LOG_WARNING(Render, "Unimplemented reinterpretation {} -> {}",
                VideoCore::PixelFormatAsString(src_format),
                VideoCore::PixelFormatAsString(dst_format));
    return false;
}

bool TextureRuntime::ClearTexture(Surface& surface, const VideoCore::TextureClear& clear) {
    id<MTLTexture> texture = surface.Texture();
    if (!texture) {
        return false;
    }

    const auto [level_width, level_height] = LevelExtent(texture, clear.texture_level);
    const auto& rect = clear.texture_rect;
    const bool full_clear = rect.left == 0 && std::min(rect.bottom, rect.top) == 0 &&
                            rect.GetWidth() >= level_width && rect.GetHeight() >= level_height;

    if (!full_clear && !GetQualityOptions().draw_optimizations) {
        // A pass of its own (Draw Optimizations off).
        if (surface.IsDepth()) {
            blit_helper.ClearDepthStencil(texture, clear.texture_level, surface.HasStencil(), rect,
                                          clear.value.depth, clear.value.stencil);
        } else {
            blit_helper.ClearColor(texture, clear.texture_level, clear.texture_layer, rect,
                                   clear.value.color);
        }
        return true;
    }
    if (!full_clear) {
        // Drawn into the pass that renders to the texture (now or next) instead of a pass of its
        // own, see Scheduler::ClearColorRect.
        if (surface.IsDepth()) {
            scheduler.ClearDepthStencilRect(texture, clear.texture_level, surface.HasStencil(),
                                            rect, clear.value.depth, clear.value.stencil);
        } else {
            const auto& color = clear.value.color;
            scheduler.ClearColorRect(texture, clear.texture_level, clear.texture_layer, rect,
                                     MTLClearColorMake(color.x, color.y, color.z, color.w));
        }
        return true;
    }

    // A whole level is cleared by the next pass that draws to it (its load action), or on its
    // own when something else comes first, see Scheduler::ClearColor.
    if (surface.IsDepth()) {
        scheduler.ClearDepthStencil(texture, clear.texture_level, surface.HasStencil(),
                                    clear.value.depth, clear.value.stencil);
    } else {
        const auto& color = clear.value.color;
        scheduler.ClearColor(texture, clear.texture_level, clear.texture_layer,
                             MTLClearColorMake(color.x, color.y, color.z, color.w));
    }
    return true;
}

bool TextureRuntime::CopyTextures(Surface& source, Surface& dest,
                                  std::span<const VideoCore::TextureCopy> copies) {
    id<MTLTexture> src = source.Texture();
    id<MTLTexture> dst = dest.Texture();
    if (!src || !dst) {
        return false;
    }
    if (src.pixelFormat != dst.pixelFormat) {
        LOG_ERROR(Render, "Attempted copy between incompatible formats {} -> {}",
                  VideoCore::PixelFormatAsString(source.pixel_format),
                  VideoCore::PixelFormatAsString(dest.pixel_format));
        return false;
    }

    id<MTLBlitCommandEncoder> blit = scheduler.BlitEncoder();
    scheduler.NoteWritten(dst, "copy");
    for (const auto& copy : copies) {
        const auto [src_w, src_h] = LevelExtent(src, copy.src_level);
        const auto [dst_w, dst_h] = LevelExtent(dst, copy.dst_level);
        if (copy.src_offset.x >= src_w || copy.src_offset.y >= src_h ||
            copy.dst_offset.x >= dst_w || copy.dst_offset.y >= dst_h) {
            continue;
        }
        const u32 width =
            std::min({copy.extent.width, src_w - copy.src_offset.x, dst_w - copy.dst_offset.x});
        const u32 height =
            std::min({copy.extent.height, src_h - copy.src_offset.y, dst_h - copy.dst_offset.y});
        if (width == 0 || height == 0) {
            continue;
        }
        [blit copyFromTexture:src
                  sourceSlice:copy.src_layer
                  sourceLevel:copy.src_level
                 sourceOrigin:MTLOriginMake(copy.src_offset.x, copy.src_offset.y, 0)
                   sourceSize:MTLSizeMake(width, height, 1)
                    toTexture:dst
             destinationSlice:copy.dst_layer
             destinationLevel:copy.dst_level
            destinationOrigin:MTLOriginMake(copy.dst_offset.x, copy.dst_offset.y, 0)];
    }
    return true;
}

bool TextureRuntime::BlitTextures(Surface& source, Surface& dest,
                                  const VideoCore::TextureBlit& blit) {
    id<MTLTexture> src = source.Texture();
    id<MTLTexture> dst = dest.Texture();
    if (!src || !dst) {
        return false;
    }

    const bool same_size =
        blit.src_rect.GetWidth() == blit.dst_rect.GetWidth() &&
        blit.src_rect.GetHeight() == blit.dst_rect.GetHeight() &&
        (blit.src_rect.left <= blit.src_rect.right) ==
            (blit.dst_rect.left <= blit.dst_rect.right) &&
        (blit.src_rect.bottom <= blit.src_rect.top) == (blit.dst_rect.bottom <= blit.dst_rect.top);

    if (source.IsDepth()) {
        if (same_size && src.pixelFormat == dst.pixelFormat) {
            const VideoCore::TextureCopy copy = {
                .src_level = blit.src_level,
                .dst_level = blit.dst_level,
                .src_layer = blit.src_layer,
                .dst_layer = blit.dst_layer,
                .src_offset = {std::min(blit.src_rect.left, blit.src_rect.right),
                               std::min(blit.src_rect.bottom, blit.src_rect.top)},
                .dst_offset = {std::min(blit.dst_rect.left, blit.dst_rect.right),
                               std::min(blit.dst_rect.bottom, blit.dst_rect.top)},
                .extent = {blit.src_rect.GetWidth(), blit.src_rect.GetHeight()},
            };
            return CopyTextures(source, dest, copy);
        }
        blit_helper.BlitDepthStencil(src, source.HasStencil() ? source.StencilTexture() : nil,
                                     blit.src_level, blit.src_rect, dst, blit.dst_level,
                                     blit.dst_rect);
        return true;
    }

    blit_helper.BlitColor(src, blit.src_level, blit.src_layer, blit.src_rect, dst, blit.dst_level,
                          blit.dst_layer, blit.dst_rect, true);
    return true;
}

void TextureRuntime::GenerateMipmaps(Surface& surface) {
    if (VideoCore::IsCustomFormatCompressed(surface.custom_format)) {
        LOG_ERROR(Render, "Generating mipmaps for compressed formats unsupported!");
        return;
    }
    if (surface.IsDepth() || surface.levels <= 1) {
        return;
    }
    id<MTLTexture> texture = surface.Texture();
    if (!texture || texture.mipmapLevelCount <= 1) {
        return;
    }
    [scheduler.BlitEncoder() generateMipmapsForTexture:texture];
}

// ===========================================================================================
// TexturePool
// ===========================================================================================

namespace {
/// How much the pool may hold, and for how many frames a texture nobody asked for stays.
constexpr u64 POOL_MAX_BYTES = 192ull * 1024 * 1024;
constexpr u64 POOL_MAX_AGE_FRAMES = 600;
} // namespace

TexturePool::Key TexturePool::KeyOf(MTLTextureDescriptor* desc) {
    return Key{
        .type = static_cast<u32>(desc.textureType),
        .format = static_cast<u32>(desc.pixelFormat),
        .width = static_cast<u32>(desc.width),
        .height = static_cast<u32>(desc.height),
        .levels = static_cast<u32>(desc.mipmapLevelCount),
        .usage = static_cast<u64>(desc.usage),
    };
}

id<MTLTexture> TexturePool::Acquire(const Key& key) {
    std::scoped_lock lock{mutex};
    for (std::size_t i = 0; i < entries.size(); i++) {
        if (entries[i].key == key) {
            id<MTLTexture> texture = entries[i].texture;
            total_size -= entries[i].size;
            entries[i] = std::move(entries.back());
            entries.pop_back();
            ++reused;
            return texture;
        }
    }
    return nil;
}

void TexturePool::Release(id<MTLTexture> texture) {
    if (texture == nil) {
        return;
    }
    // Only plain private 2D textures: views and shared textures belong to someone else.
    if (texture.storageMode != MTLStorageModePrivate || texture.parentTexture != nil ||
        texture.textureType == MTLTextureTypeCube) {
        return;
    }
    Entry entry;
    entry.key = Key{
        .type = static_cast<u32>(texture.textureType),
        .format = static_cast<u32>(texture.pixelFormat),
        .width = static_cast<u32>(texture.width),
        .height = static_cast<u32>(texture.height),
        .levels = static_cast<u32>(texture.mipmapLevelCount),
        .usage = static_cast<u64>(texture.usage),
    };
    entry.texture = texture;
    entry.size = texture.allocatedSize;
    std::scoped_lock lock{mutex};
    entry.last_used = frame;
    if (entry.size > POOL_MAX_BYTES / 4) {
        return; // A very large texture is not worth keeping
    }
    // Make room by letting the oldest go.
    while (total_size + entry.size > POOL_MAX_BYTES && !entries.empty()) {
        auto oldest = std::min_element(entries.begin(), entries.end(),
                                       [](const Entry& a, const Entry& b) {
                                           return a.last_used < b.last_used;
                                       });
        total_size -= oldest->size;
        ++dropped;
        *oldest = std::move(entries.back());
        entries.pop_back();
    }
    total_size += entry.size;
    entries.push_back(std::move(entry));
}

void TexturePool::Tick() {
    std::scoped_lock lock{mutex};
    ++frame;
    for (std::size_t i = 0; i < entries.size();) {
        if (frame - entries[i].last_used > POOL_MAX_AGE_FRAMES) {
            total_size -= entries[i].size;
            ++dropped;
            entries[i] = std::move(entries.back());
            entries.pop_back();
        } else {
            ++i;
        }
    }
    if (frame % 300 == 0 && (reused + created + dropped) > 0) {
        LOG_INFO(Render,
                 "Texture pool: {} kept ({:.1f} MiB), {} textures reused, {} created, {} let go "
                 "since the start",
                 entries.size(), static_cast<double>(total_size) / (1024.0 * 1024.0), reused,
                 created, dropped);
    }
}

// ===========================================================================================
// Surface
// ===========================================================================================

Surface::Surface(TextureRuntime& runtime_, const VideoCore::SurfaceParams& params,
                 const VideoCore::SurfaceFlagBits& initial_flag_bits)
    : SurfaceBase{params, initial_flag_bits}, runtime{&runtime_}, instance{&runtime_.GetInstance()},
      scheduler{&runtime_.GetScheduler()}, traits{instance->GetTraits(pixel_format)} {
    if (pixel_format == PixelFormat::Invalid || !traits.transfer_support) {
        return;
    }
    storage_capable = traits.native == MTLPixelFormatRGBA8Unorm;

    textures[Type::Base] = CreateTexture(width, height, traits.native, "base");
    if (res_scale != 1) {
        textures[Type::Scaled] =
            CreateTexture(GetScaledWidth(), GetScaledHeight(), traits.native, "scaled");
    }
    current = res_scale != 1 ? Type::Scaled : Type::Base;
    SyncHandle();
}

Surface::Surface(TextureRuntime& runtime_, const VideoCore::SurfaceBase& surface,
                 const VideoCore::Material* mat)
    : SurfaceBase{surface}, runtime{&runtime_}, instance{&runtime_.GetInstance()},
      scheduler{&runtime_.GetScheduler()}, traits{instance->GetTraits(mat->format)} {
    if (!traits.transfer_support) {
        return;
    }

    // Set before creating the textures, the texture usage depends on it.
    custom_format = mat->format;
    material = mat;

    const bool has_normal = mat && mat->Map(MapType::Normal);
    textures[Type::Base] = CreateTexture(mat->width, mat->height, traits.native, "custom");
    if (res_scale != 1) {
        textures[Type::Scaled] =
            CreateTexture(mat->width, mat->height, MTLPixelFormatRGBA8Unorm, "custom scaled");
    }
    if (has_normal) {
        textures[Type::Custom] = CreateTexture(mat->width, mat->height, traits.native, "normal");
    }
    current = res_scale != 1 ? Type::Scaled : Type::Base;
    SyncHandle();
}

Surface::~Surface() {
    // Views keep their texture alive, they go first. The textures go back to the pool.
    storage_view = nil;
    stencil_view = nil;
    if (runtime != nullptr && runtime->GetTexturePool().Enabled()) {
        for (auto& texture : textures) {
            if (texture != nil) {
                runtime->GetTexturePool().Release(texture);
                texture = nil;
            }
        }
    }
}

id<MTLTexture> Surface::CreateTexture(u32 tex_width, u32 tex_height, MTLPixelFormat format,
                                      std::string_view label, bool full_usage) const {
    MTLTextureDescriptor* desc = [MTLTextureDescriptor new];
    desc.textureType = texture_type == TextureType::CubeMap ? MTLTextureTypeCube : MTLTextureType2D;
    desc.pixelFormat = format;
    desc.width = std::max(tex_width, 1u);
    desc.height = std::max(tex_height, 1u);
    desc.mipmapLevelCount = std::max<u32>(levels, 1u);
    desc.storageMode = MTLStorageModePrivate;

    const bool is_compressed =
        VideoCore::IsCustomFormatCompressed(custom_format) && format != MTLPixelFormatRGBA8Unorm;
    MTLTextureUsage usage = MTLTextureUsageShaderRead;
    // Apple GPUs compress textures in memory (losslessly, or lossy when asked) unless they can be
    // written by shaders or viewed in another format. Color textures only need that for shadow
    // mapping, depth textures for stencil views, and the texture is replaced by one that allows
    // it the first time it is needed (StorageTexture, ViewableTexture).
    const TextureCompression compression = GetQualityOptions().texture_compression;
    const bool is_depth_format = format == MTLPixelFormatDepth16Unorm ||
                                 format == MTLPixelFormatDepth32Float ||
                                 format == MTLPixelFormatDepth32Float_Stencil8;
    const bool compress = compression != TextureCompression::Off && !full_usage &&
                          !is_compressed &&
                          (format == MTLPixelFormatRGBA8Unorm || is_depth_format);
    if (compress) {
        usage |= MTLTextureUsageRenderTarget;
        // Lossy compression is for color, depth stays lossless.
        if (compression == TextureCompression::Lossy && !is_depth_format) {
            if (@available(iOS 15.0, macOS 12.0, *)) {
                if ([instance->GetDevice() supportsFamily:MTLGPUFamilyApple8]) {
                    desc.compressionType = MTLTextureCompressionTypeLossy;
                }
            }
        }
    } else {
        if (!is_compressed) {
            usage |= MTLTextureUsageRenderTarget | MTLTextureUsagePixelFormatView;
        }
        if (format == MTLPixelFormatRGBA8Unorm) {
            // Allows R32Uint views for shadow mapping and texture writes for shadow rendering.
            usage |= MTLTextureUsageShaderWrite;
        }
    }
    desc.usage = usage;

    TexturePool& pool = runtime->GetTexturePool();
    if (pool.Enabled()) {
        if (id<MTLTexture> reused = pool.Acquire(TexturePool::KeyOf(desc))) {
            // Its multisampled copy (MSAA) has the pixels of the surface that had it before.
            scheduler->NoteWritten(reused, "recycled texture");
            return reused;
        }
    }
    pool.CountCreated();
    id<MTLTexture> texture = [instance->GetDevice() newTextureWithDescriptor:desc];
    ASSERT_MSG(texture, "Failed to create {}x{} texture for {}", tex_width, tex_height,
               VideoCore::PixelFormatAsString(pixel_format));
    if (Settings::values.renderer_debug) {
        SetLabel(texture, fmt::format("Surface {:#x} {} {}x{} ({})", addr,
                                      VideoCore::PixelFormatAsString(pixel_format), tex_width,
                                      tex_height, label));
    }
    return texture;
}

id<MTLTexture> Surface::CopyTexture() {
    id<MTLTexture> src = Texture();
    __strong id<MTLTexture>& copy = textures[Type::Copy];
    if (!copy || copy.width != src.width || copy.height != src.height) {
        copy = CreateTexture(static_cast<u32>(src.width), static_cast<u32>(src.height),
                             src.pixelFormat, "copy");
    }
    [scheduler->BlitEncoder() copyFromTexture:src toTexture:copy];
    scheduler->NoteWritten(copy, "feedback copy");
    Stats().feedback_copies.fetch_add(1, std::memory_order_relaxed);
    return copy;
}

id<MTLTexture> Surface::StorageTexture() {
    id<MTLTexture> texture = Texture();
    if (!storage_capable || !texture) {
        return nil;
    }
    if ((texture.usage & MTLTextureUsageShaderWrite) == 0) {
        // Made compressible: replace it by one that allows the R32Uint view.
        texture = ReplaceWithFullUsage(current);
    }
    if (!storage_view || storage_view.parentTexture != texture) {
        storage_view = [texture newTextureViewWithPixelFormat:MTLPixelFormatR32Uint];
    }
    return storage_view;
}

id<MTLTexture> Surface::ReplaceWithFullUsage(Type type) {
    // Same contents, framebuffers follow through the handle.
    id<MTLTexture> texture = textures[type];
    id<MTLTexture> replacement =
        CreateTexture(static_cast<u32>(texture.width), static_cast<u32>(texture.height),
                      texture.pixelFormat, "full usage", true);
    [scheduler->BlitEncoder() copyFromTexture:texture toTexture:replacement];
    // The replacement may come from the texture pool with a multisampled copy of other pixels.
    scheduler->NoteWritten(replacement, "replacement texture");
    textures[type] = replacement;
    textures[Type::Copy] = nil;
    storage_view = nil;
    stencil_view = nil;
    if (type == current) {
        SyncHandle();
    }
    return replacement;
}

id<MTLTexture> Surface::ViewableTexture(Type type) {
    const Type resolved = type == Type::Current ? current : type;
    id<MTLTexture> texture = textures[resolved];
    if (texture && (texture.usage & MTLTextureUsagePixelFormatView) == 0) {
        texture = ReplaceWithFullUsage(resolved);
    }
    return texture;
}

id<MTLTexture> Surface::StencilTexture() {
    id<MTLTexture> texture = ViewableTexture(Type::Current);
    if (!traits.has_stencil || !texture) {
        return nil;
    }
    if (!stencil_view || stencil_view.parentTexture != texture) {
        stencil_view = [texture newTextureViewWithPixelFormat:MTLPixelFormatX32_Stencil8];
    }
    return stencil_view;
}

void Surface::Upload(const VideoCore::BufferTextureCopy& upload,
                     const VideoCore::StagingData& staging) {
    id<MTLTexture> texture = Texture(Type::Base);
    if (!texture) {
        return;
    }

    const auto& rect = upload.texture_rect;
    const u32 rect_width = rect.GetWidth();
    const u32 rect_height = rect.GetHeight();
    const MTLOrigin origin = MTLOriginMake(rect.left, std::min(rect.bottom, rect.top), 0);
    const MTLSize size = MTLSizeMake(rect_width, rect_height, 1);
    id<MTLBuffer> buffer = runtime->upload_buffer.Handle();
    scheduler->NoteWritten(texture, "upload");

    if (traits.has_stencil) {
        // De-interleave the (D24 << 8 | S8) words into a float depth plane and a stencil plane.
        const u32 num_pixels = rect_width * rect_height;
        u8* data = staging.mapped.data();
        for (u32 i = 0; i < num_pixels; i++) {
            u32 value;
            std::memcpy(&value, data + i * 4, sizeof(value));
            const f32 depth = static_cast<f32>(value >> 8) / 16777215.0f;
            std::memcpy(data + i * 4, &depth, sizeof(depth));
            data[num_pixels * 4 + i] = static_cast<u8>(value & 0xFF);
        }

        id<MTLBlitCommandEncoder> blit = scheduler->BlitEncoder();
        [blit copyFromBuffer:buffer
                   sourceOffset:upload.buffer_offset
              sourceBytesPerRow:rect_width * 4
            sourceBytesPerImage:num_pixels * 4
                     sourceSize:size
                      toTexture:texture
               destinationSlice:0
               destinationLevel:upload.texture_level
              destinationOrigin:origin
                        options:MTLBlitOptionDepthFromDepthStencil];
        [blit copyFromBuffer:buffer
                   sourceOffset:upload.buffer_offset + num_pixels * 4
              sourceBytesPerRow:rect_width
            sourceBytesPerImage:num_pixels
                     sourceSize:size
                      toTexture:texture
               destinationSlice:0
               destinationLevel:upload.texture_level
              destinationOrigin:origin
                        options:MTLBlitOptionStencilFromDepthStencil];
    } else {
        const u32 bpp = GetInternalBytesPerPixel();
        [scheduler->BlitEncoder() copyFromBuffer:buffer
                                    sourceOffset:upload.buffer_offset
                               sourceBytesPerRow:rect_width * bpp
                             sourceBytesPerImage:rect_width * rect_height * bpp
                                      sourceSize:size
                                       toTexture:texture
                                destinationSlice:0
                                destinationLevel:upload.texture_level
                               destinationOrigin:origin];
    }

    runtime->upload_buffer.Commit(staging.size);

    if (res_scale != 1) {
        ASSERT_MSG(textures[Type::Scaled], "Scaled allocation missing during upload");
        const VideoCore::TextureBlit blit = {
            .src_level = upload.texture_level,
            .dst_level = upload.texture_level,
            .src_rect = upload.texture_rect,
            .dst_rect = upload.texture_rect * res_scale,
        };
        // Colors and textures can go through one of the texture filters instead of plain scaling.
        bool filtered = false;
        if ((type == SurfaceType::Color || type == SurfaceType::Texture) &&
            Settings::values.texture_filter.GetValue() != Settings::TextureFilter::NoFilter) {
            if (upload.texture_level != 0) {
                // Like the other backends, only the base level is filtered.
                filtered = true;
            } else {
                filtered = runtime->GetBlitHelper().FilterColor(
                    textures[Type::Base], blit.src_rect, textures[Type::Scaled], blit.dst_rect,
                    res_scale);
            }
        }
        if (!filtered) {
            BlitScale(blit, true);
        }
    }
}

void Surface::UploadCustom(const VideoCore::Material* custom_material, u32 level) {
    const u32 mat_width = std::max(custom_material->width >> level, 1u);
    const u32 mat_height = std::max(custom_material->height >> level, 1u);

    const auto upload = [&](Type type, VideoCore::CustomTexture* texture) {
        id<MTLTexture> target = textures[type];
        if (!target || !texture) {
            return;
        }
        const u32 custom_size = static_cast<u32>(texture->data.size());
        const auto [data, offset, invalidate] = runtime->upload_buffer.Map(custom_size, 16);
        std::memcpy(data, texture->data.data(), custom_size);
        runtime->upload_buffer.Commit(custom_size);

        const auto [bytes_per_row, rows] = CustomLayout(custom_format, mat_width, mat_height);
        scheduler->NoteWritten(target, "custom texture upload");
        [scheduler->BlitEncoder() copyFromBuffer:runtime->upload_buffer.Handle()
                                    sourceOffset:offset
                               sourceBytesPerRow:bytes_per_row
                             sourceBytesPerImage:bytes_per_row * rows
                                      sourceSize:MTLSizeMake(mat_width, mat_height, 1)
                                       toTexture:target
                                destinationSlice:0
                                destinationLevel:level
                               destinationOrigin:MTLOriginMake(0, 0, 0)];
    };

    upload(Type::Base, custom_material->textures[0]);
    if (auto* texture = custom_material->textures[static_cast<u32>(MapType::Normal)]) {
        upload(Type::Custom, texture);
    }
}

void Surface::Download(const VideoCore::BufferTextureCopy& download,
                       const VideoCore::StagingData& staging) {
    const auto readback_start = std::chrono::steady_clock::now();

    // A small depth area the emulated CPU reads (Mario Kart 7 checks the depth around the sun
    // every frame for the glare): with Fast Depth Readback it gets the result of the previous
    // read while this one is copied without waiting for the GPU, so the frame is not stalled.
    const u32 async_pixels = download.texture_rect.GetWidth() * download.texture_rect.GetHeight();
    const bool async_ok = GetQualityOptions().async_depth_readback && traits.is_depth &&
                          async_pixels <= 16 * 16 && g_readback_reason != nullptr &&
                          std::strcmp(g_readback_reason, "CPU access to GPU memory") == 0;
    u64 async_key = 0;
    if (async_ok) {
        const auto& r = download.texture_rect;
        async_key = Common::HashCombine(static_cast<u64>(addr), static_cast<u64>(r.left),
                                        static_cast<u64>(r.bottom), static_cast<u64>(r.right),
                                        static_cast<u64>(r.top),
                                        static_cast<u64>(download.texture_level),
                                        static_cast<u64>(pixel_format));
    }
    if (res_scale != 1) {
        const VideoCore::TextureBlit blit = {
            .src_level = download.texture_level,
            .dst_level = download.texture_level,
            .src_rect = download.texture_rect * res_scale,
            .dst_rect = download.texture_rect,
        };
        BlitScale(blit, false);
    }

    id<MTLTexture> texture = Texture(Type::Base);
    if (!texture) {
        runtime->download_buffer.Commit(staging.size);
        return;
    }

    const auto& rect = download.texture_rect;
    const u32 rect_width = rect.GetWidth();
    const u32 rect_height = rect.GetHeight();
    const u32 num_pixels = rect_width * rect_height;
    const MTLOrigin origin = MTLOriginMake(rect.left, std::min(rect.bottom, rect.top), 0);
    const MTLSize size = MTLSizeMake(rect_width, rect_height, 1);
    id<MTLBuffer> buffer = runtime->download_buffer.Handle();

    // Records the copy of the area into `target` at `offset`, in the staging layout.
    const auto encode_copy = [&](id<MTLBuffer> target, NSUInteger offset) {
        id<MTLBlitCommandEncoder> blit = scheduler->BlitEncoder();
        if (traits.has_stencil) {
            [blit copyFromTexture:texture
                             sourceSlice:0
                             sourceLevel:download.texture_level
                            sourceOrigin:origin
                              sourceSize:size
                                toBuffer:target
                       destinationOffset:offset
                  destinationBytesPerRow:rect_width * 4
                destinationBytesPerImage:num_pixels * 4
                                 options:MTLBlitOptionDepthFromDepthStencil];
            [blit copyFromTexture:texture
                             sourceSlice:0
                             sourceLevel:download.texture_level
                            sourceOrigin:origin
                              sourceSize:size
                                toBuffer:target
                       destinationOffset:offset + num_pixels * 4
                  destinationBytesPerRow:rect_width
                destinationBytesPerImage:num_pixels
                                 options:MTLBlitOptionStencilFromDepthStencil];
        } else {
            const u32 bpp = GetInternalBytesPerPixel();
            [blit copyFromTexture:texture
                             sourceSlice:0
                             sourceLevel:download.texture_level
                            sourceOrigin:origin
                              sourceSize:size
                                toBuffer:target
                       destinationOffset:offset
                  destinationBytesPerRow:rect_width * bpp
                destinationBytesPerImage:num_pixels * bpp];
        }
    };
    // Interleaves depth-stencil back into (D24 << 8 | S8) words, which the codec then rotates.
    const auto convert = [&](u8* data) {
        if (!traits.has_stencil) {
            return;
        }
        for (u32 i = 0; i < num_pixels; i++) {
            f32 depth;
            std::memcpy(&depth, data + i * 4, sizeof(depth));
            const u32 d24 = static_cast<u32>(std::clamp(depth, 0.0f, 1.0f) * 16777215.0f);
            const u32 value = (d24 << 8) | data[num_pixels * 4 + i];
            std::memcpy(data + i * 4, &value, sizeof(value));
        }
    };

    if (async_ok) {
        auto& readbacks = runtime->async_readbacks;
        if (readbacks.size() > 64 && !readbacks.contains(async_key)) {
            readbacks.clear();
        }
        auto& entry = readbacks[async_key];
        if (entry.pending && scheduler->IsFree(entry.tick)) {
            entry.last.assign(static_cast<const u8*>(entry.buffer.contents),
                              static_cast<const u8*>(entry.buffer.contents) + staging.size);
            convert(entry.last.data());
            entry.pending = false;
        }
        if (entry.last.size() == staging.size) {
            std::memcpy(staging.mapped.data(), entry.last.data(), staging.size);
            if (!entry.pending) {
                if (entry.buffer == nil || entry.buffer.length < staging.size) {
                    entry.buffer = [instance->GetDevice()
                        newBufferWithLength:staging.size
                                    options:MTLResourceStorageModeShared];
                }
                encode_copy(entry.buffer, 0);
                entry.tick = scheduler->CurrentTick();
                entry.pending = true;
            }
            RecordReadback(fmt::format("{} (one frame late): type {} format {} {}x{} scale {} at "
                                       "{:#x}",
                                       g_readback_reason, static_cast<int>(type),
                                       static_cast<int>(pixel_format), rect_width, rect_height,
                                       res_scale, addr),
                           std::chrono::duration<double, std::milli>(
                               std::chrono::steady_clock::now() - readback_start)
                               .count(),
                           static_cast<u64>(staging.size));
            runtime->download_buffer.Commit(staging.size);
            return;
        }
    }

    encode_copy(buffer, download.buffer_offset);

    // The rasterizer cache reads the staging memory right after this returns.
    scheduler->Finish();
    RecordReadback(
        fmt::format("{}: type {} format {} {}x{} scale {} at {:#x}",
                    g_readback_reason != nullptr ? g_readback_reason : "cache (texture validation)",
                    static_cast<int>(type), static_cast<int>(pixel_format), rect_width, rect_height,
                    res_scale, addr),
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                  readback_start)
            .count(),
        static_cast<u64>(staging.size));

    convert(staging.mapped.data());
    if (async_ok) {
        // The first read waited, keep its result for the next one.
        runtime->async_readbacks[async_key].last.assign(
            staging.mapped.data(), staging.mapped.data() + staging.size);
    }

    runtime->download_buffer.Commit(staging.size);
}

void Surface::ScaleUp(u32 new_scale, u8 new_sample_count) {
    // The sample count only says this surface is rendered to with MSAA: the multisampled copy is
    // made by the scheduler for the passes that need it (see Framebuffer).
    sample_count = std::max(sample_count, new_sample_count);
    if (res_scale == new_scale || new_scale == 1) {
        return;
    }

    res_scale = new_scale;
    textures[Type::Scaled] =
        CreateTexture(GetScaledWidth(), GetScaledHeight(), traits.native, "scaled");
    textures[Type::Copy] = nil;
    storage_view = nil;
    stencil_view = nil;
    current = Type::Scaled;
    SyncHandle();

    for (u32 level = 0; level < levels; level++) {
        const VideoCore::TextureBlit blit = {
            .src_level = level,
            .dst_level = level,
            .src_rect = GetRect(level),
            .dst_rect = GetScaledRect(level),
        };
        BlitScale(blit, true);
    }
}

void Surface::BlitScale(const VideoCore::TextureBlit& blit, bool up_scale) {
    id<MTLTexture> src = up_scale ? textures[Type::Base] : textures[Type::Scaled];
    id<MTLTexture> dst = up_scale ? textures[Type::Scaled] : textures[Type::Base];
    if (traits.has_stencil && src) {
        // The stencil is read through a view of another format.
        src = ViewableTexture(up_scale ? Type::Base : Type::Scaled);
    }
    if (!src || !dst) {
        return;
    }

    auto& helper = runtime->GetBlitHelper();
    if (traits.is_depth) {
        id<MTLTexture> stencil = nil;
        if (traits.has_stencil) {
            stencil = [src newTextureViewWithPixelFormat:MTLPixelFormatX32_Stencil8];
        }
        helper.BlitDepthStencil(src, stencil, blit.src_level, blit.src_rect, dst, blit.dst_level,
                                blit.dst_rect);
        return;
    }

    const u32 slices = texture_type == TextureType::CubeMap ? 6 : 1;
    for (u32 slice = 0; slice < slices; slice++) {
        helper.BlitColor(src, blit.src_level, slice, blit.src_rect, dst, blit.dst_level, slice,
                         blit.dst_rect, true);
    }
}

u32 Surface::GetInternalBytesPerPixel() const {
    return traits.bytes_per_pixel;
}

// ===========================================================================================
// Framebuffer
// ===========================================================================================

Framebuffer::Framebuffer(TextureRuntime& runtime, const VideoCore::FramebufferParams& params,
                         Surface* color, Surface* depth)
    : VideoCore::FramebufferParams{params},
      res_scale{color ? color->res_scale : (depth ? depth->res_scale : 1u)} {
    width = height = std::numeric_limits<u32>::max();

    const auto level_extent = [](Surface* surface, u32 level) {
        const auto extent = surface->RealExtent();
        return std::make_pair(std::max(extent.width >> level, 1u),
                              std::max(extent.height >> level, 1u));
    };

    if (shadow_rendering) {
        if (!color) {
            return;
        }
        // Shadow rendering writes into the color surface through an R32Uint image without any
        // attachment bound.
        const auto [w, h] = level_extent(color, color_level);
        width = w;
        height = h;
        formats[0] = color->pixel_format;
        shadow_buffer = color->StorageTexture();
        target = RenderTarget{.width = width, .height = height};
        valid = shadow_buffer != nil;
        return;
    }

    // MSAA (Settings::values.antialiasing, see RenderTarget::samples) for the base level of 2D
    // render targets, which is what games draw their frames into.
    const u32 wanted_samples =
        std::max(color ? color->sample_count : u8{1}, depth ? depth->sample_count : u8{1});
    u32 samples = runtime.MsaaSamples(wanted_samples);
    if ((color && (color_level != 0 || color->texture_type != VideoCore::TextureType::Texture2D)) ||
        (depth && (depth_level != 0 || depth->texture_type != VideoCore::TextureType::Texture2D))) {
        samples = 1;
    }
    if (samples > 1 && depth && depth->HasStencil()) {
        // Taken before the depth texture below: it may replace the texture with one that can
        // have a stencil view.
        target.stencil_view = depth->StencilTexture();
    }

    if (color) {
        const auto [w, h] = level_extent(color, color_level);
        width = std::min(width, w);
        height = std::min(height, h);
        formats[0] = color->pixel_format;
        attachments[0] = color->Texture();
        handles[0] = color->Handle();
        target.color = attachments[0];
        target.color_level = color_level;
    }
    if (depth) {
        const auto [w, h] = level_extent(depth, depth_level);
        width = std::min(width, w);
        height = std::min(height, h);
        formats[1] = depth->pixel_format;
        attachments[1] = depth->Texture();
        handles[1] = depth->Handle();
        target.depth = attachments[1];
        target.depth_level = depth_level;
        target.has_stencil = depth->HasStencil();
    }

    if (!color && !depth) {
        width = height = 0;
        return;
    }
    target.width = width;
    target.height = height;
    target.samples = samples;
    valid = attachments[0] != nil || attachments[1] != nil;
}

Framebuffer::~Framebuffer() = default;

// ===========================================================================================
// Sampler
// ===========================================================================================

Sampler::Sampler(TextureRuntime& runtime, const VideoCore::SamplerParams& params) {
    using TextureConfig = VideoCore::SamplerParams::TextureConfig;

    MTLSamplerDescriptor* desc = [MTLSamplerDescriptor new];
    desc.magFilter = PicaToMTL::TextureFilterMode(params.mag_filter);
    desc.minFilter = PicaToMTL::TextureFilterMode(params.min_filter);
    desc.mipFilter = PicaToMTL::TextureMipFilterMode(params.mip_filter);
    desc.sAddressMode = PicaToMTL::WrapModeToMTL(params.wrap_s);
    desc.tAddressMode = PicaToMTL::WrapModeToMTL(params.wrap_t);
    desc.rAddressMode = MTLSamplerAddressModeClampToEdge;
    desc.lodMinClamp = static_cast<float>(params.lod_min);
    desc.lodMaxClamp = static_cast<float>(params.lod_max);

    // Anisotropic filtering only applies when linear filtering is used everywhere.
    const bool use_anisotropy = params.mag_filter == TextureConfig::TextureFilter::Linear &&
                                params.min_filter == TextureConfig::TextureFilter::Linear;
    desc.maxAnisotropy =
        use_anisotropy ? static_cast<NSUInteger>(std::clamp(GetQualityOptions().max_anisotropy, 1, 16))
                       : 1;

    sampler = [runtime.GetInstance().GetDevice() newSamplerStateWithDescriptor:desc];
}

Sampler::~Sampler() = default;

// ===========================================================================================
// DebugScope
// ===========================================================================================

DebugScope::DebugScope(TextureRuntime& runtime, Common::Vec4f, std::string_view label)
    : scheduler{runtime.GetScheduler()}, enabled{Settings::values.renderer_debug.GetValue()} {
    if (enabled) {
        scheduler.PushDebugGroup(label);
    }
}

DebugScope::~DebugScope() {
    if (enabled) {
        scheduler.PopDebugGroup();
    }
}

} // namespace Metal
