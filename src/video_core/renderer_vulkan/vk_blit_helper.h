// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "video_core/rasterizer_cache/pixel_format.h"
#include "video_core/renderer_vulkan/vk_resource_pool.h"

namespace VideoCore {
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

private:
    vk::Pipeline MakeComputePipeline(vk::ShaderModule shader, vk::PipelineLayout layout);
    vk::Pipeline MakeDepthStencilBlitPipeline();
    vk::Pipeline MakeFilterPipeline(
        vk::ShaderModule fragment_shader, vk::PipelineLayout layout,
        VideoCore::PixelFormat color_format = VideoCore::PixelFormat::RGBA8);

    void FilterAnime4K(Surface& surface, const VideoCore::TextureBlit& blit);
    void FilterBicubic(Surface& surface, const VideoCore::TextureBlit& blit);
    void FilterScaleForce(Surface& surface, const VideoCore::TextureBlit& blit);
    void FilterXbrz(Surface& surface, const VideoCore::TextureBlit& blit);
    void FilterMMPX(Surface& surface, const VideoCore::TextureBlit& blit);

    void FilterPass(Surface& source, Surface& dest, vk::Pipeline pipeline,
                    vk::PipelineLayout layout, const VideoCore::TextureBlit& blit);

    void FilterPassThreeTextures(Surface& source1, Surface& source2, Surface& source3,
                                 Surface& dest, vk::Pipeline pipeline, vk::PipelineLayout layout,
                                 const VideoCore::TextureBlit& blit);

    void FilterPassYGradient(Surface& source, Surface& dest, vk::Pipeline pipeline,
                             vk::PipelineLayout layout, const VideoCore::TextureBlit& blit);

private:
    const Instance& instance;
    Scheduler& scheduler;
    RenderManager& renderpass_cache;
    DescriptorUpdateQueue& update_queue;

    vk::Device device;
    vk::RenderPass r32_renderpass;

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
    vk::ShaderModule depth_to_buffer_comp;
    vk::ShaderModule blit_depth_stencil_frag;
    vk::ShaderModule bicubic_frag;
    vk::ShaderModule scale_force_frag;
    vk::ShaderModule xbrz_frag;
    vk::ShaderModule mmpx_frag;
    vk::ShaderModule refine_frag;

    vk::Pipeline d24s8_to_rgba8_pipeline;
    vk::Pipeline depth_to_buffer_pipeline;
    vk::Pipeline depth_blit_pipeline;
    vk::Sampler linear_sampler;
    vk::Sampler nearest_sampler;
};

} // namespace Vulkan
