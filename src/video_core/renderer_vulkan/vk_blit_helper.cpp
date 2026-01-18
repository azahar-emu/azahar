// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/settings.h"
#include "common/vector_math.h"
#include "video_core/renderer_vulkan/vk_blit_helper.h"
#include "video_core/renderer_vulkan/vk_descriptor_update_queue.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/renderer_vulkan/vk_texture_runtime.h"

#include "video_core/host_shaders/format_reinterpreter/vulkan_d24s8_to_rgba8_comp.h"
#include "video_core/host_shaders/full_screen_triangle_vert.h"
#include "video_core/host_shaders/vulkan_blit_depth_stencil_frag.h"
#include "video_core/host_shaders/vulkan_depth_to_buffer_comp.h"

// Texture filtering shader includes
#include "video_core/host_shaders/texture_filtering/bicubic_frag.h"
#include "video_core/host_shaders/texture_filtering/mmpx_frag.h"
#include "video_core/host_shaders/texture_filtering/refine_frag.h"
#include "video_core/host_shaders/texture_filtering/scale_force_frag.h"
#include "video_core/host_shaders/texture_filtering/x_gradient_frag.h"
#include "video_core/host_shaders/texture_filtering/xbrz_freescale_frag.h"
#include "video_core/host_shaders/texture_filtering/y_gradient_frag.h"
#include "vk_blit_helper.h"

namespace Vulkan {

using Settings::TextureFilter;
using VideoCore::PixelFormat;

namespace {
struct PushConstants {
    std::array<float, 2> tex_scale;
    std::array<float, 2> tex_offset;
};

struct ComputeInfo {
    Common::Vec2i src_offset;
    Common::Vec2i dst_offset;
    Common::Vec2i src_extent;
};

inline constexpr vk::PushConstantRange COMPUTE_PUSH_CONSTANT_RANGE{
    .stageFlags = vk::ShaderStageFlagBits::eCompute,
    .offset = 0,
    .size = sizeof(ComputeInfo),
};

constexpr std::array<vk::DescriptorSetLayoutBinding, 3> COMPUTE_BINDINGS = {{
    {0, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eCompute},
    {1, vk::DescriptorType::eSampledImage, 1, vk::ShaderStageFlagBits::eCompute},
    {2, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eCompute},
}};

constexpr std::array<vk::DescriptorSetLayoutBinding, 3> COMPUTE_BUFFER_BINDINGS = {{
    {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
    {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eCompute},
    {2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute},
}};

constexpr std::array<vk::DescriptorSetLayoutBinding, 2> TWO_TEXTURES_BINDINGS = {{
    {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
    {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
}};

// Texture filtering descriptor set bindings
constexpr std::array<vk::DescriptorSetLayoutBinding, 1> SINGLE_TEXTURE_BINDINGS = {{
    {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
}};

constexpr std::array<vk::DescriptorSetLayoutBinding, 3> THREE_TEXTURES_BINDINGS = {{
    {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
    {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
    {2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
}};

// Note: Removed FILTER_UTILITY_BINDINGS as texture filtering doesn't need shadow buffers

// Push constant structure for texture filtering
struct FilterPushConstants {
    std::array<float, 2> tex_scale;
    std::array<float, 2> tex_offset;
    float res_scale; // For xBRZ filter
};

inline constexpr vk::PushConstantRange FILTER_PUSH_CONSTANT_RANGE{
    .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
    .offset = 0,
    .size = sizeof(FilterPushConstants),
};
inline constexpr vk::PushConstantRange PUSH_CONSTANT_RANGE{
    .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
    .offset = 0,
    .size = sizeof(PushConstants),
};
constexpr vk::PipelineVertexInputStateCreateInfo PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO{
    .vertexBindingDescriptionCount = 0,
    .pVertexBindingDescriptions = nullptr,
    .vertexAttributeDescriptionCount = 0,
    .pVertexAttributeDescriptions = nullptr,
};
constexpr vk::PipelineInputAssemblyStateCreateInfo PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO{
    .topology = vk::PrimitiveTopology::eTriangleList,
    .primitiveRestartEnable = VK_FALSE,
};
constexpr vk::PipelineViewportStateCreateInfo PIPELINE_VIEWPORT_STATE_CREATE_INFO{
    .viewportCount = 1,
    .pViewports = nullptr,
    .scissorCount = 1,
    .pScissors = nullptr,
};
constexpr vk::PipelineRasterizationStateCreateInfo PIPELINE_RASTERIZATION_STATE_CREATE_INFO{
    .depthClampEnable = VK_FALSE,
    .rasterizerDiscardEnable = VK_FALSE,
    .polygonMode = vk::PolygonMode::eFill,
    .cullMode = vk::CullModeFlagBits::eBack,
    .frontFace = vk::FrontFace::eClockwise,
    .depthBiasEnable = VK_FALSE,
    .depthBiasConstantFactor = 0.0f,
    .depthBiasClamp = 0.0f,
    .depthBiasSlopeFactor = 0.0f,
    .lineWidth = 1.0f,
};
constexpr vk::PipelineMultisampleStateCreateInfo PIPELINE_MULTISAMPLE_STATE_CREATE_INFO{
    .rasterizationSamples = vk::SampleCountFlagBits::e1,
    .sampleShadingEnable = VK_FALSE,
    .minSampleShading = 0.0f,
    .pSampleMask = nullptr,
    .alphaToCoverageEnable = VK_FALSE,
    .alphaToOneEnable = VK_FALSE,
};
constexpr std::array DYNAMIC_STATES{
    vk::DynamicState::eViewport,
    vk::DynamicState::eScissor,
};
constexpr vk::PipelineDynamicStateCreateInfo PIPELINE_DYNAMIC_STATE_CREATE_INFO{
    .dynamicStateCount = static_cast<u32>(DYNAMIC_STATES.size()),
    .pDynamicStates = DYNAMIC_STATES.data(),
};

constexpr vk::PipelineColorBlendAttachmentState COLOR_BLEND_ATTACHMENT{
    .blendEnable = VK_FALSE,
    .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                      vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
};

constexpr vk::PipelineColorBlendStateCreateInfo PIPELINE_COLOR_BLEND_STATE_CREATE_INFO{
    .logicOpEnable = VK_FALSE,
    .attachmentCount = 1,
    .pAttachments = &COLOR_BLEND_ATTACHMENT,
};
constexpr vk::PipelineDepthStencilStateCreateInfo PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO{
    .depthTestEnable = VK_TRUE,
    .depthWriteEnable = VK_TRUE,
    .depthCompareOp = vk::CompareOp::eAlways,
    .depthBoundsTestEnable = VK_FALSE,
    .stencilTestEnable = VK_FALSE,
    .front = vk::StencilOpState{},
    .back = vk::StencilOpState{},
    .minDepthBounds = 0.0f,
    .maxDepthBounds = 0.0f,
};

template <vk::Filter filter>
inline constexpr vk::SamplerCreateInfo SAMPLER_CREATE_INFO{
    .magFilter = filter,
    .minFilter = filter,
    .mipmapMode = vk::SamplerMipmapMode::eNearest,
    .addressModeU = vk::SamplerAddressMode::eClampToEdge,
    .addressModeV = vk::SamplerAddressMode::eClampToEdge,
    .addressModeW = vk::SamplerAddressMode::eClampToEdge,
    .mipLodBias = 0.0f,
    .anisotropyEnable = VK_FALSE,
    .maxAnisotropy = 0.0f,
    .compareEnable = VK_FALSE,
    .compareOp = vk::CompareOp::eNever,
    .minLod = 0.0f,
    .maxLod = 0.0f,
    .borderColor = vk::BorderColor::eFloatOpaqueWhite,
    .unnormalizedCoordinates = VK_FALSE,
};

constexpr vk::PipelineLayoutCreateInfo PipelineLayoutCreateInfo(
    const vk::DescriptorSetLayout* set_layout, bool compute = false, bool filter = false) {
    return vk::PipelineLayoutCreateInfo{
        .setLayoutCount = 1,
        .pSetLayouts = set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges =
            (compute ? &COMPUTE_PUSH_CONSTANT_RANGE
                     : (filter ? &FILTER_PUSH_CONSTANT_RANGE : &PUSH_CONSTANT_RANGE)),
    };
}

constexpr std::array<vk::PipelineShaderStageCreateInfo, 2> MakeStages(
    vk::ShaderModule vertex_shader, vk::ShaderModule fragment_shader) {
    return std::array{
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = vertex_shader,
            .pName = "main",
        },
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = fragment_shader,
            .pName = "main",
        },
    };
}

constexpr vk::PipelineShaderStageCreateInfo MakeStages(vk::ShaderModule compute_shader) {
    return vk::PipelineShaderStageCreateInfo{
        .stage = vk::ShaderStageFlagBits::eCompute,
        .module = compute_shader,
        .pName = "main",
    };
}

} // Anonymous namespace

BlitHelper::BlitHelper(const Instance& instance_, Scheduler& scheduler_,
                       RenderManager& renderpass_cache_, DescriptorUpdateQueue& update_queue_)
    : instance{instance_}, scheduler{scheduler_}, renderpass_cache{renderpass_cache_},
      update_queue{update_queue_}, device{instance.GetDevice()},
      compute_provider{instance, scheduler.GetMasterSemaphore(), COMPUTE_BINDINGS},
      compute_buffer_provider{instance, scheduler.GetMasterSemaphore(), COMPUTE_BUFFER_BINDINGS},
      two_textures_provider{instance, scheduler.GetMasterSemaphore(), TWO_TEXTURES_BINDINGS, 16},
      single_texture_provider{instance, scheduler.GetMasterSemaphore(), SINGLE_TEXTURE_BINDINGS,
                              16},
      three_textures_provider{instance, scheduler.GetMasterSemaphore(), THREE_TEXTURES_BINDINGS,
                              16},
      compute_pipeline_layout{
          device.createPipelineLayout(PipelineLayoutCreateInfo(&compute_provider.Layout(), true))},
      compute_buffer_pipeline_layout{device.createPipelineLayout(
          PipelineLayoutCreateInfo(&compute_buffer_provider.Layout(), true))},
      two_textures_pipeline_layout{
          device.createPipelineLayout(PipelineLayoutCreateInfo(&two_textures_provider.Layout()))},
      single_texture_pipeline_layout{device.createPipelineLayout(
          PipelineLayoutCreateInfo(&single_texture_provider.Layout(), false, true))},
      three_textures_pipeline_layout{device.createPipelineLayout(
          PipelineLayoutCreateInfo(&three_textures_provider.Layout(), false, true))},
      full_screen_vert{Compile(HostShaders::FULL_SCREEN_TRIANGLE_VERT,
                               vk::ShaderStageFlagBits::eVertex, device)},
      d24s8_to_rgba8_comp{Compile(HostShaders::VULKAN_D24S8_TO_RGBA8_COMP,
                                  vk::ShaderStageFlagBits::eCompute, device)},
      depth_to_buffer_comp{Compile(HostShaders::VULKAN_DEPTH_TO_BUFFER_COMP,
                                   vk::ShaderStageFlagBits::eCompute, device)},
      blit_depth_stencil_frag{Compile(HostShaders::VULKAN_BLIT_DEPTH_STENCIL_FRAG,
                                      vk::ShaderStageFlagBits::eFragment, device)},
      // Texture filtering shader modules
      bicubic_frag{Compile(HostShaders::BICUBIC_FRAG, vk::ShaderStageFlagBits::eFragment, device)},
      scale_force_frag{
          Compile(HostShaders::SCALE_FORCE_FRAG, vk::ShaderStageFlagBits::eFragment, device)},
      xbrz_frag{
          Compile(HostShaders::XBRZ_FREESCALE_FRAG, vk::ShaderStageFlagBits::eFragment, device)},
      mmpx_frag{Compile(HostShaders::MMPX_FRAG, vk::ShaderStageFlagBits::eFragment, device)},
      refine_frag{Compile(HostShaders::REFINE_FRAG, vk::ShaderStageFlagBits::eFragment, device)},
      d24s8_to_rgba8_pipeline{MakeComputePipeline(d24s8_to_rgba8_comp, compute_pipeline_layout)},
      depth_to_buffer_pipeline{
          MakeComputePipeline(depth_to_buffer_comp, compute_buffer_pipeline_layout)},
      depth_blit_pipeline{MakeDepthStencilBlitPipeline()},
      linear_sampler{device.createSampler(SAMPLER_CREATE_INFO<vk::Filter::eLinear>)},
      nearest_sampler{device.createSampler(SAMPLER_CREATE_INFO<vk::Filter::eNearest>)} {

    if (instance.HasDebuggingToolAttached()) {
        SetObjectName(device, compute_pipeline_layout, "BlitHelper: compute_pipeline_layout");
        SetObjectName(device, compute_buffer_pipeline_layout,
                      "BlitHelper: compute_buffer_pipeline_layout");
        SetObjectName(device, two_textures_pipeline_layout,
                      "BlitHelper: two_textures_pipeline_layout");
        SetObjectName(device, full_screen_vert, "BlitHelper: full_screen_vert");
        SetObjectName(device, d24s8_to_rgba8_comp, "BlitHelper: d24s8_to_rgba8_comp");
        SetObjectName(device, depth_to_buffer_comp, "BlitHelper: depth_to_buffer_comp");
        SetObjectName(device, blit_depth_stencil_frag, "BlitHelper: blit_depth_stencil_frag");
        SetObjectName(device, d24s8_to_rgba8_pipeline, "BlitHelper: d24s8_to_rgba8_pipeline");
        SetObjectName(device, depth_to_buffer_pipeline, "BlitHelper: depth_to_buffer_pipeline");
        if (depth_blit_pipeline) {
            SetObjectName(device, depth_blit_pipeline, "BlitHelper: depth_blit_pipeline");
        }
        SetObjectName(device, linear_sampler, "BlitHelper: linear_sampler");
        SetObjectName(device, nearest_sampler, "BlitHelper: nearest_sampler");
    }
}

BlitHelper::~BlitHelper() {
    device.destroyPipelineLayout(compute_pipeline_layout);
    device.destroyPipelineLayout(compute_buffer_pipeline_layout);
    device.destroyPipelineLayout(two_textures_pipeline_layout);
    device.destroyPipelineLayout(single_texture_pipeline_layout);
    device.destroyPipelineLayout(three_textures_pipeline_layout);
    device.destroyShaderModule(full_screen_vert);
    device.destroyShaderModule(d24s8_to_rgba8_comp);
    device.destroyShaderModule(depth_to_buffer_comp);
    device.destroyShaderModule(blit_depth_stencil_frag);
    // Destroy texture filtering shader modules
    device.destroyShaderModule(bicubic_frag);
    device.destroyShaderModule(scale_force_frag);
    device.destroyShaderModule(xbrz_frag);
    device.destroyShaderModule(mmpx_frag);
    device.destroyShaderModule(refine_frag);
    device.destroyPipeline(depth_to_buffer_pipeline);
    device.destroyPipeline(d24s8_to_rgba8_pipeline);
    device.destroyPipeline(depth_blit_pipeline);
    device.destroySampler(linear_sampler);
    device.destroySampler(nearest_sampler);
}

void BindBlitState(vk::CommandBuffer cmdbuf, vk::PipelineLayout layout,
                   const VideoCore::TextureBlit& blit, const Surface& dest) {
    const vk::Offset2D offset{
        .x = std::min<s32>(blit.dst_rect.left, blit.dst_rect.right),
        .y = std::min<s32>(blit.dst_rect.bottom, blit.dst_rect.top),
    };
    const vk::Extent2D extent{
        .width = blit.dst_rect.GetWidth(),
        .height = blit.dst_rect.GetHeight(),
    };
    const vk::Viewport viewport{
        .x = static_cast<float>(offset.x),
        .y = static_cast<float>(offset.y),
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    const vk::Rect2D scissor{
        .offset = offset,
        .extent = extent,
    };
    const float scale_x = static_cast<float>(blit.src_rect.GetWidth());
    const float scale_y = static_cast<float>(blit.src_rect.GetHeight());
    const PushConstants push_constants{
        .tex_scale = {scale_x, scale_y},
        .tex_offset = {static_cast<float>(blit.src_rect.left),
                       static_cast<float>(blit.src_rect.bottom)},
    };
    cmdbuf.setViewport(0, viewport);
    cmdbuf.setScissor(0, scissor);
    cmdbuf.pushConstants(layout,
                         vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0,
                         sizeof(push_constants), &push_constants);
}

bool BlitHelper::BlitDepthStencil(Surface& source, Surface& dest,
                                  const VideoCore::TextureBlit& blit) {
    if (!instance.IsShaderStencilExportSupported()) {
        LOG_ERROR(Render_Vulkan, "Unable to emulate depth stencil images");
        return false;
    }

    const vk::Rect2D dst_render_area = {
        .offset = {0, 0},
        .extent = {dest.GetScaledWidth(), dest.GetScaledHeight()},
    };

    const auto descriptor_set = two_textures_provider.Commit();
    update_queue.AddImageSampler(descriptor_set, 0, 0, source.DepthView(), nearest_sampler);
    update_queue.AddImageSampler(descriptor_set, 1, 0, source.StencilView(), nearest_sampler);

    const RenderPass depth_pass = {
        .framebuffer = dest.Framebuffer(),
        .render_pass =
            renderpass_cache.GetRenderpass(PixelFormat::Invalid, dest.pixel_format, false),
        .render_area = dst_render_area,
    };
    renderpass_cache.BeginRendering(depth_pass);

    scheduler.Record([blit, descriptor_set, &dest, this](vk::CommandBuffer cmdbuf) {
        const vk::PipelineLayout layout = two_textures_pipeline_layout;

        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, depth_blit_pipeline);
        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, descriptor_set, {});
        BindBlitState(cmdbuf, layout, blit, dest);
        cmdbuf.draw(3, 1, 0, 0);
    });
    scheduler.MakeDirty(StateFlags::Pipeline);
    return true;
}

bool BlitHelper::ConvertDS24S8ToRGBA8(Surface& source, Surface& dest,
                                      const VideoCore::TextureCopy& copy) {
    const auto descriptor_set = compute_provider.Commit();
    update_queue.AddImageSampler(descriptor_set, 0, 0, source.DepthView(), VK_NULL_HANDLE,
                                 vk::ImageLayout::eDepthStencilReadOnlyOptimal);
    update_queue.AddImageSampler(descriptor_set, 1, 0, source.StencilView(), VK_NULL_HANDLE,
                                 vk::ImageLayout::eDepthStencilReadOnlyOptimal);
    update_queue.AddStorageImage(descriptor_set, 2, dest.ImageView());

    renderpass_cache.EndRendering();
    scheduler.Record([this, descriptor_set, copy, src_image = source.Image(),
                      dst_image = dest.Image()](vk::CommandBuffer cmdbuf) {
        const std::array pre_barriers = {
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                .oldLayout = vk::ImageLayout::eGeneral,
                .newLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = src_image,
                .subresourceRange{
                    .aspectMask =
                        vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
                    .baseMipLevel = 0,
                    .levelCount = VK_REMAINING_MIP_LEVELS,
                    .baseArrayLayer = 0,
                    .layerCount = VK_REMAINING_ARRAY_LAYERS,
                },
            },
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eNone,
                .dstAccessMask = vk::AccessFlagBits::eShaderWrite,
                .oldLayout = vk::ImageLayout::eUndefined,
                .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = dst_image,
                .subresourceRange{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = VK_REMAINING_MIP_LEVELS,
                    .baseArrayLayer = 0,
                    .layerCount = VK_REMAINING_ARRAY_LAYERS,
                },
            },
        };
        const std::array post_barriers = {
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eShaderRead,
                .dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite |
                                 vk::AccessFlagBits::eDepthStencilAttachmentRead,
                .oldLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal,
                .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = src_image,
                .subresourceRange{
                    .aspectMask =
                        vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
                    .baseMipLevel = 0,
                    .levelCount = VK_REMAINING_MIP_LEVELS,
                    .baseArrayLayer = 0,
                    .layerCount = VK_REMAINING_ARRAY_LAYERS,
                },
            },
            vk::ImageMemoryBarrier{
                .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
                .dstAccessMask = vk::AccessFlagBits::eTransferRead,
                .oldLayout = vk::ImageLayout::eGeneral,
                .newLayout = vk::ImageLayout::eGeneral,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = dst_image,
                .subresourceRange{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = VK_REMAINING_MIP_LEVELS,
                    .baseArrayLayer = 0,
                    .layerCount = VK_REMAINING_ARRAY_LAYERS,
                },
            }};
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eEarlyFragmentTests |
                                   vk::PipelineStageFlagBits::eLateFragmentTests,
                               vk::PipelineStageFlagBits::eComputeShader,
                               vk::DependencyFlagBits::eByRegion, {}, {}, pre_barriers);

        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, compute_pipeline_layout, 0,
                                  descriptor_set, {});
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, d24s8_to_rgba8_pipeline);

        const ComputeInfo info = {
            .src_offset = Common::Vec2i{static_cast<int>(copy.src_offset.x),
                                        static_cast<int>(copy.src_offset.y)},
            .dst_offset = Common::Vec2i{static_cast<int>(copy.dst_offset.x),
                                        static_cast<int>(copy.dst_offset.y)},
        };
        cmdbuf.pushConstants(compute_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0,
                             sizeof(info), &info);

        cmdbuf.dispatch(copy.extent.width / 8, copy.extent.height / 8, 1);

        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                               vk::PipelineStageFlagBits::eEarlyFragmentTests |
                                   vk::PipelineStageFlagBits::eLateFragmentTests |
                                   vk::PipelineStageFlagBits::eTransfer,
                               vk::DependencyFlagBits::eByRegion, {}, {}, post_barriers);
    });
    return true;
}

bool BlitHelper::DepthToBuffer(Surface& source, vk::Buffer buffer,
                               const VideoCore::BufferTextureCopy& copy) {
    const auto descriptor_set = compute_buffer_provider.Commit();
    update_queue.AddImageSampler(descriptor_set, 0, 0, source.DepthView(), nearest_sampler,
                                 vk::ImageLayout::eDepthStencilReadOnlyOptimal);
    update_queue.AddImageSampler(descriptor_set, 1, 0, source.StencilView(), nearest_sampler,
                                 vk::ImageLayout::eDepthStencilReadOnlyOptimal);
    update_queue.AddBuffer(descriptor_set, 2, buffer, copy.buffer_offset, copy.buffer_size,
                           vk::DescriptorType::eStorageBuffer);

    renderpass_cache.EndRendering();
    scheduler.Record([this, descriptor_set, copy, src_image = source.Image(),
                      extent = source.RealExtent(false)](vk::CommandBuffer cmdbuf) {
        const vk::ImageMemoryBarrier pre_barrier = {
            .srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite,
            .dstAccessMask = vk::AccessFlagBits::eShaderRead,
            .oldLayout = vk::ImageLayout::eGeneral,
            .newLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = src_image,
            .subresourceRange{
                .aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
                .baseMipLevel = 0,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        };
        const vk::ImageMemoryBarrier post_barrier = {
            .srcAccessMask = vk::AccessFlagBits::eShaderRead,
            .dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite |
                             vk::AccessFlagBits::eDepthStencilAttachmentRead,
            .oldLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            .newLayout = vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = src_image,
            .subresourceRange{
                .aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
                .baseMipLevel = 0,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        };
        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eEarlyFragmentTests |
                                   vk::PipelineStageFlagBits::eLateFragmentTests,
                               vk::PipelineStageFlagBits::eComputeShader,
                               vk::DependencyFlagBits::eByRegion, {}, {}, pre_barrier);

        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eCompute, compute_buffer_pipeline_layout,
                                  0, descriptor_set, {});
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, depth_to_buffer_pipeline);

        const ComputeInfo info = {
            .src_offset = Common::Vec2i{static_cast<int>(copy.texture_rect.left),
                                        static_cast<int>(copy.texture_rect.bottom)},
            .src_extent =
                Common::Vec2i{static_cast<int>(extent.width), static_cast<int>(extent.height)},
        };
        cmdbuf.pushConstants(compute_buffer_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0,
                             sizeof(ComputeInfo), &info);

        cmdbuf.dispatch(copy.texture_rect.GetWidth() / 8, copy.texture_rect.GetHeight() / 8, 1);

        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                               vk::PipelineStageFlagBits::eEarlyFragmentTests |
                                   vk::PipelineStageFlagBits::eLateFragmentTests |
                                   vk::PipelineStageFlagBits::eTransfer,
                               vk::DependencyFlagBits::eByRegion, {}, {}, post_barrier);
    });
    return true;
}

vk::Pipeline BlitHelper::MakeComputePipeline(vk::ShaderModule shader, vk::PipelineLayout layout) {
    const vk::ComputePipelineCreateInfo compute_info = {
        .stage = MakeStages(shader),
        .layout = layout,
    };

    if (const auto result = device.createComputePipeline({}, compute_info);
        result.result == vk::Result::eSuccess) {
        return result.value;
    } else {
        LOG_CRITICAL(Render_Vulkan, "Compute pipeline creation failed!");
        UNREACHABLE();
    }
}

vk::Pipeline BlitHelper::MakeDepthStencilBlitPipeline() {
    if (!instance.IsShaderStencilExportSupported()) {
        return VK_NULL_HANDLE;
    }

    const std::array stages = MakeStages(full_screen_vert, blit_depth_stencil_frag);
    const auto renderpass = renderpass_cache.GetRenderpass(VideoCore::PixelFormat::Invalid,
                                                           VideoCore::PixelFormat::D24S8, false);
    vk::GraphicsPipelineCreateInfo depth_stencil_info = {
        .stageCount = static_cast<u32>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pInputAssemblyState = &PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pTessellationState = nullptr,
        .pViewportState = &PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pRasterizationState = &PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pMultisampleState = &PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pDepthStencilState = &PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .pColorBlendState = &PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pDynamicState = &PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .layout = two_textures_pipeline_layout,
        .renderPass = renderpass,
    };

    if (const auto result = device.createGraphicsPipeline({}, depth_stencil_info);
        result.result == vk::Result::eSuccess) {
        return result.value;
    } else {
        LOG_CRITICAL(Render_Vulkan, "Depth stencil blit pipeline creation failed!");
        UNREACHABLE();
    }
    return VK_NULL_HANDLE;
}

bool BlitHelper::Filter(Surface& surface, const VideoCore::TextureBlit& blit) {
    const auto filter = Settings::values.texture_filter.GetValue();
    const bool is_depth =
        surface.type == VideoCore::SurfaceType::Depth ||
        surface.type == VideoCore::SurfaceType::DepthStencil; // Skip filtering for depth textures
                                                              // and when no filter is selected
    if (filter == Settings::TextureFilter::NoFilter || is_depth) {
        return false;
    } // Only filter base mipmap level
    if (blit.src_level != 0) {
        return true;
    }

    switch (filter) {
    case TextureFilter::Anime4K:
        FilterAnime4K(surface, blit);
        break;
    case TextureFilter::Bicubic:
        FilterBicubic(surface, blit);
        break;
    case TextureFilter::ScaleForce:
        FilterScaleForce(surface, blit);
        break;
    case TextureFilter::xBRZ:
        FilterXbrz(surface, blit);
        break;
    case TextureFilter::MMPX:
        FilterMMPX(surface, blit);
        break;
    default:
        LOG_ERROR(Render_Vulkan, "Unknown texture filter {}", filter);
        return false;
    }
    return true;
}

void BlitHelper::FilterAnime4K(Surface& surface, const VideoCore::TextureBlit& blit) {
    const bool is_depth = surface.type == VideoCore::SurfaceType::Depth ||
                          surface.type == VideoCore::SurfaceType::DepthStencil;
    const auto color_format = is_depth ? VideoCore::PixelFormat::Invalid : surface.pixel_format;
    auto pipeline = MakeFilterPipeline(refine_frag, three_textures_pipeline_layout, color_format);
    FilterPassThreeTextures(surface, surface, surface, surface, pipeline,
                            three_textures_pipeline_layout, blit);
}

void BlitHelper::FilterBicubic(Surface& surface, const VideoCore::TextureBlit& blit) {
    const bool is_depth = surface.type == VideoCore::SurfaceType::Depth ||
                          surface.type == VideoCore::SurfaceType::DepthStencil;
    const auto color_format = is_depth ? VideoCore::PixelFormat::Invalid : surface.pixel_format;
    auto pipeline = MakeFilterPipeline(bicubic_frag, single_texture_pipeline_layout, color_format);
    FilterPass(surface, surface, pipeline, single_texture_pipeline_layout, blit);
}

void BlitHelper::FilterScaleForce(Surface& surface, const VideoCore::TextureBlit& blit) {
    const bool is_depth = surface.type == VideoCore::SurfaceType::Depth ||
                          surface.type == VideoCore::SurfaceType::DepthStencil;
    const auto color_format = is_depth ? VideoCore::PixelFormat::Invalid : surface.pixel_format;
    auto pipeline =
        MakeFilterPipeline(scale_force_frag, single_texture_pipeline_layout, color_format);
    FilterPass(surface, surface, pipeline, single_texture_pipeline_layout, blit);
}

void BlitHelper::FilterXbrz(Surface& surface, const VideoCore::TextureBlit& blit) {
    const bool is_depth = surface.type == VideoCore::SurfaceType::Depth ||
                          surface.type == VideoCore::SurfaceType::DepthStencil;
    const auto color_format = is_depth ? VideoCore::PixelFormat::Invalid : surface.pixel_format;
    auto pipeline = MakeFilterPipeline(xbrz_frag, single_texture_pipeline_layout, color_format);
    FilterPass(surface, surface, pipeline, single_texture_pipeline_layout, blit);
}

void BlitHelper::FilterMMPX(Surface& surface, const VideoCore::TextureBlit& blit) {
    const bool is_depth = surface.type == VideoCore::SurfaceType::Depth ||
                          surface.type == VideoCore::SurfaceType::DepthStencil;
    const auto color_format = is_depth ? VideoCore::PixelFormat::Invalid : surface.pixel_format;
    auto pipeline = MakeFilterPipeline(mmpx_frag, single_texture_pipeline_layout, color_format);
    FilterPass(surface, surface, pipeline, single_texture_pipeline_layout, blit);
}

vk::Pipeline BlitHelper::MakeFilterPipeline(vk::ShaderModule fragment_shader,
                                            vk::PipelineLayout layout,
                                            VideoCore::PixelFormat color_format) {
    const std::array stages = MakeStages(full_screen_vert, fragment_shader);
    // Use the provided color format for render pass compatibility
    const auto renderpass =
        renderpass_cache.GetRenderpass(color_format, VideoCore::PixelFormat::Invalid, false);

    vk::GraphicsPipelineCreateInfo pipeline_info = {
        .stageCount = static_cast<u32>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .pInputAssemblyState = &PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .pTessellationState = nullptr,
        .pViewportState = &PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .pRasterizationState = &PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .pMultisampleState = &PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .pDepthStencilState = nullptr,
        .pColorBlendState = &PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .pDynamicState = &PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .layout = layout,
        .renderPass = renderpass,
    };

    if (const auto result = device.createGraphicsPipeline({}, pipeline_info);
        result.result == vk::Result::eSuccess) {
        return result.value;
    } else {
        LOG_CRITICAL(Render_Vulkan, "Filter pipeline creation failed!");
        UNREACHABLE();
    }
}

void BlitHelper::FilterPass(Surface& source, Surface& dest, vk::Pipeline pipeline,
                            vk::PipelineLayout layout, const VideoCore::TextureBlit& blit) {
    const auto texture_descriptor_set = single_texture_provider.Commit();
    update_queue.AddImageSampler(texture_descriptor_set, 0, 0, source.ImageView(0), linear_sampler,
                                 vk::ImageLayout::eGeneral);

    const bool is_depth = dest.type == VideoCore::SurfaceType::Depth ||
                          dest.type == VideoCore::SurfaceType::DepthStencil;
    const auto color_format = is_depth ? VideoCore::PixelFormat::Invalid : dest.pixel_format;
    const auto depth_format = is_depth ? dest.pixel_format : VideoCore::PixelFormat::Invalid;
    const auto renderpass = renderpass_cache.GetRenderpass(color_format, depth_format, false);

    const RenderPass render_pass = {
        .framebuffer = dest.Framebuffer(),
        .render_pass = renderpass,
        .render_area =
            {
                .offset = {0, 0},
                .extent = {dest.GetScaledWidth(), dest.GetScaledHeight()},
            },
    };
    renderpass_cache.BeginRendering(render_pass);
    const float src_scale = static_cast<float>(source.GetResScale());
    // Calculate normalized texture coordinates like OpenGL does
    const auto src_extent = source.RealExtent(false); // Get unscaled texture extent
    const float tex_scale_x =
        static_cast<float>(blit.src_rect.GetWidth()) / static_cast<float>(src_extent.width);
    const float tex_scale_y =
        static_cast<float>(blit.src_rect.GetHeight()) / static_cast<float>(src_extent.height);
    const float tex_offset_x =
        static_cast<float>(blit.src_rect.left) / static_cast<float>(src_extent.width);
    const float tex_offset_y =
        static_cast<float>(blit.src_rect.bottom) / static_cast<float>(src_extent.height);

    scheduler.Record([pipeline, layout, texture_descriptor_set, blit, tex_scale_x, tex_scale_y,
                      tex_offset_x, tex_offset_y, src_scale](vk::CommandBuffer cmdbuf) {
        const FilterPushConstants push_constants{.tex_scale = {tex_scale_x, tex_scale_y},
                                                 .tex_offset = {tex_offset_x, tex_offset_y},
                                                 .res_scale = src_scale};

        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

        // Bind single texture descriptor set
        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0,
                                  texture_descriptor_set, {});

        cmdbuf.pushConstants(layout, FILTER_PUSH_CONSTANT_RANGE.stageFlags,
                             FILTER_PUSH_CONSTANT_RANGE.offset, FILTER_PUSH_CONSTANT_RANGE.size,
                             &push_constants);

        // Set up viewport and scissor for filtering (don't use BindBlitState as it overwrites push
        // constants)
        const vk::Offset2D offset{
            .x = std::min<s32>(blit.dst_rect.left, blit.dst_rect.right),
            .y = std::min<s32>(blit.dst_rect.bottom, blit.dst_rect.top),
        };
        const vk::Extent2D extent{
            .width = blit.dst_rect.GetWidth(),
            .height = blit.dst_rect.GetHeight(),
        };
        const vk::Viewport viewport{
            .x = static_cast<float>(offset.x),
            .y = static_cast<float>(offset.y),
            .width = static_cast<float>(extent.width),
            .height = static_cast<float>(extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        const vk::Rect2D scissor{
            .offset = offset,
            .extent = extent,
        };
        cmdbuf.setViewport(0, viewport);
        cmdbuf.setScissor(0, scissor);
        cmdbuf.draw(3, 1, 0, 0);
    });
    scheduler.MakeDirty(StateFlags::Pipeline);
}

void BlitHelper::FilterPassThreeTextures(Surface& source1, Surface& source2, Surface& source3,
                                         Surface& dest, vk::Pipeline pipeline,
                                         vk::PipelineLayout layout,
                                         const VideoCore::TextureBlit& blit) {
    const auto texture_descriptor_set = three_textures_provider.Commit();

    update_queue.AddImageSampler(texture_descriptor_set, 0, 0, source1.ImageView(0), linear_sampler,
                                 vk::ImageLayout::eGeneral);
    update_queue.AddImageSampler(texture_descriptor_set, 1, 0, source2.ImageView(0), linear_sampler,
                                 vk::ImageLayout::eGeneral);
    update_queue.AddImageSampler(texture_descriptor_set, 2, 0, source3.ImageView(0), linear_sampler,
                                 vk::ImageLayout::eGeneral);

    const bool is_depth = dest.type == VideoCore::SurfaceType::Depth ||
                          dest.type == VideoCore::SurfaceType::DepthStencil;
    const auto color_format = is_depth ? VideoCore::PixelFormat::Invalid : dest.pixel_format;
    const auto depth_format = is_depth ? dest.pixel_format : VideoCore::PixelFormat::Invalid;
    const auto renderpass = renderpass_cache.GetRenderpass(color_format, depth_format, false);

    const RenderPass render_pass = {
        .framebuffer = dest.Framebuffer(),
        .render_pass = renderpass,
        .render_area =
            {
                .offset = {0, 0},
                .extent = {dest.GetScaledWidth(), dest.GetScaledHeight()},
            },
    };
    renderpass_cache.BeginRendering(render_pass);

    const float src_scale = static_cast<float>(source1.GetResScale());
    // Calculate normalized texture coordinates like OpenGL does
    const auto src_extent = source1.RealExtent(false); // Get unscaled texture extent
    const float tex_scale_x =
        static_cast<float>(blit.src_rect.GetWidth()) / static_cast<float>(src_extent.width);
    const float tex_scale_y =
        static_cast<float>(blit.src_rect.GetHeight()) / static_cast<float>(src_extent.height);
    const float tex_offset_x =
        static_cast<float>(blit.src_rect.left) / static_cast<float>(src_extent.width);
    const float tex_offset_y =
        static_cast<float>(blit.src_rect.bottom) / static_cast<float>(src_extent.height);

    scheduler.Record([pipeline, layout, texture_descriptor_set, blit, tex_scale_x, tex_scale_y,
                      tex_offset_x, tex_offset_y, src_scale](vk::CommandBuffer cmdbuf) {
        const FilterPushConstants push_constants{.tex_scale = {tex_scale_x, tex_scale_y},
                                                 .tex_offset = {tex_offset_x, tex_offset_y},
                                                 .res_scale = src_scale};

        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

        // Bind single texture descriptor set
        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0,
                                  texture_descriptor_set, {});

        cmdbuf.pushConstants(layout, FILTER_PUSH_CONSTANT_RANGE.stageFlags,
                             FILTER_PUSH_CONSTANT_RANGE.offset, FILTER_PUSH_CONSTANT_RANGE.size,
                             &push_constants);

        // Set up viewport and scissor using safe viewport like working filters
        const vk::Offset2D offset{
            .x = std::min<s32>(blit.dst_rect.left, blit.dst_rect.right),
            .y = std::min<s32>(blit.dst_rect.bottom, blit.dst_rect.top),
        };
        const vk::Extent2D extent{
            .width = blit.dst_rect.GetWidth(),
            .height = blit.dst_rect.GetHeight(),
        };
        const vk::Viewport viewport{
            .x = static_cast<float>(offset.x),
            .y = static_cast<float>(offset.y),
            .width = static_cast<float>(extent.width),
            .height = static_cast<float>(extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        const vk::Rect2D scissor{
            .offset = offset,
            .extent = extent,
        };
        cmdbuf.setViewport(0, viewport);
        cmdbuf.setScissor(0, scissor);
        cmdbuf.draw(3, 1, 0, 0);
    });
    scheduler.MakeDirty(StateFlags::Pipeline);
}

} // namespace Vulkan
