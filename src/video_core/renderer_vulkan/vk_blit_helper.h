// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "common/math_util.h"
#include "video_core/rasterizer_cache/pixel_format.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"

namespace VideoCore {
struct Extent;
struct TextureBlit;
struct TextureCopy;
struct BufferTextureCopy;
} // namespace VideoCore

namespace Vulkan {

class Instance;
class RenderManager;
class Scheduler;
class Surface;
class DescriptorUpdateQueue;

class BlitHelper {
    friend class TextureRuntime;

public:
    explicit BlitHelper(const Instance& instance, Scheduler& scheduler,
                        RenderManager& renderpass_cache, DescriptorUpdateQueue& update_queue);
    ~BlitHelper();
    bool Filter(Surface& surface, const VideoCore::TextureBlit& blit);

    bool BlitDepthStencil(Surface& source, Surface& dest, const VideoCore::TextureBlit& blit);

    bool ConvertDS24S8ToRGBA8(Surface& source, Surface& dest, const VideoCore::TextureCopy& copy);

    bool DepthToBuffer(Surface& source, vk::Buffer buffer,
                       const VideoCore::BufferTextureCopy& copy);

    void ResolveTexture(Surface& surface);

private:
    vk::Pipeline MakeComputePipeline(vk::ShaderModule shader, vk::PipelineLayout layout);
    vk::Pipeline MakeDepthStencilBlitPipeline();
    vk::Pipeline MakeFilterPipeline(
        vk::ShaderModule fragment_shader, vk::PipelineLayout layout,
        VideoCore::PixelFormat color_format = VideoCore::PixelFormat::RGBA8);
    vk::Pipeline MakeFilterPipeline(vk::ShaderModule fragment_shader, vk::PipelineLayout layout,
                                    vk::RenderPass renderpass);

    void FilterAnime4K(Surface& surface, const VideoCore::TextureBlit& blit);
    void FilterBicubic(Surface& surface, const VideoCore::TextureBlit& blit);
    void FilterScaleForce(Surface& surface, const VideoCore::TextureBlit& blit);
    void FilterXbrz(Surface& surface, const VideoCore::TextureBlit& blit);
    void FilterMMPX(Surface& surface, const VideoCore::TextureBlit& blit);

    void FilterPass(Surface& surface, vk::Pipeline pipeline, vk::PipelineLayout layout,
                    const VideoCore::TextureBlit& blit);

    /// Draws draw_rect of the framebuffer, sampling the inputs over tex_rect of an image of size
    /// tex_extent.
    void DrawFilter(vk::Pipeline pipeline, vk::PipelineLayout layout,
                    vk::DescriptorSet descriptor_set, vk::Framebuffer framebuffer,
                    vk::RenderPass renderpass, Common::Rectangle<u32> draw_rect,
                    Common::Rectangle<u32> tex_rect, const VideoCore::Extent& tex_extent,
                    float res_scale);

    struct Anime4KImages;

    /// Returns intermediate images of the given size for Anime4K, creating them if needed.
    Anime4KImages& GetAnime4KImages(u32 width, u32 height);

private:
    const Instance& instance;
    Scheduler& scheduler;
    RenderManager& renderpass_cache;
    DescriptorUpdateQueue& update_queue;

    vk::Device device;

    DescriptorHeap compute_provider;
    DescriptorHeap compute_buffer_provider;
    DescriptorHeap two_textures_provider;
    DescriptorHeap single_texture_provider;
    DescriptorHeap three_textures_provider;
    vk::PipelineLayout compute_pipeline_layout;
    vk::PipelineLayout compute_buffer_pipeline_layout;
    vk::PipelineLayout two_textures_pipeline_layout;
    vk::PipelineLayout single_texture_pipeline_layout;
    vk::PipelineLayout three_textures_pipeline_layout;

    vk::ShaderModule full_screen_vert;
    vk::ShaderModule d24s8_to_rgba8_comp;
    vk::ShaderModule d24s8_to_rgba8_ms_comp;
    vk::ShaderModule depth_to_buffer_comp;
    vk::ShaderModule blit_depth_stencil_frag;
    vk::ShaderModule bicubic_frag;
    vk::ShaderModule scale_force_frag;
    vk::ShaderModule xbrz_frag;
    vk::ShaderModule mmpx_frag;
    vk::ShaderModule refine_frag;
    vk::ShaderModule x_gradient_frag;
    vk::ShaderModule y_gradient_frag;

    /// Render passes of the Anime4K intermediate images (R16G16 and R16 float)
    vk::RenderPass xy_renderpass;
    vk::RenderPass lumad_renderpass;
    std::vector<std::unique_ptr<Anime4KImages>> anime4k_images;

    vk::Pipeline d24s8_to_rgba8_pipeline;
    vk::Pipeline d24s8_to_rgba8_ms_pipeline;
    vk::Pipeline depth_to_buffer_pipeline;
    vk::Pipeline depth_blit_pipeline;
    vk::Sampler linear_sampler;
    vk::Sampler nearest_sampler;

    /// Cache of texture filter pipelines (keyed by shader+layout+format hash)
    std::unordered_map<std::uint64_t, vk::Pipeline> filter_pipeline_cache;
};

} // namespace Vulkan
