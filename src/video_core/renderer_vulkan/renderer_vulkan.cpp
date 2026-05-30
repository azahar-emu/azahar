// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/logging/log.h"
#include "common/memory_detect.h"
#include "common/microprofile.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/frontend/emu_window.h"
#include "video_core/gpu.h"
#include "video_core/pica/pica_core.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#include "video_core/renderer_vulkan/vk_memory_util.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

#include "video_core/host_shaders/vulkan_present_anaglyph_frag.h"
#include "video_core/host_shaders/vulkan_present_frag.h"
#include "video_core/host_shaders/vulkan_present_interlaced_frag.h"
#include "video_core/host_shaders/vulkan_present_vert.h"

#include "video_core/host_shaders/vulkan_cursor_frag.h"
#include "video_core/host_shaders/vulkan_cursor_vert.h"

#include "video_core/host_shaders/vulkan_simple_present_frag.h"
#include "video_core/host_shaders/vulkan_simple_present_vert.h"

#include "video_core/host_shaders/antialiasing/Vulkan/vulkan_fxaa_frag.h"
#include "video_core/host_shaders/antialiasing/Vulkan/vulkan_fxaa_vert.h"
#include "video_core/host_shaders/antialiasing/Vulkan/vulkan_smaa_pass0_pre_frag.h"
#include "video_core/host_shaders/antialiasing/Vulkan/vulkan_smaa_pass0_pre_vert.h"
#include "video_core/host_shaders/antialiasing/Vulkan/vulkan_smaa_pass0_post_frag.h"
#include "video_core/host_shaders/antialiasing/Vulkan/vulkan_smaa_pass0_post_vert.h"
#include "video_core/host_shaders/antialiasing/Vulkan/vulkan_smaa_pass1_pre_frag.h"
#include "video_core/host_shaders/antialiasing/Vulkan/vulkan_smaa_pass1_pre_vert.h"
#include "video_core/host_shaders/antialiasing/Vulkan/vulkan_smaa_pass1_post_frag.h"
#include "video_core/host_shaders/antialiasing/Vulkan/vulkan_smaa_pass1_post_vert.h"
#include "video_core/host_shaders/antialiasing/Vulkan/vulkan_smaa_pass2_pre_frag.h"
#include "video_core/host_shaders/antialiasing/Vulkan/vulkan_smaa_pass2_pre_vert.h"
#include "video_core/host_shaders/antialiasing/Vulkan/vulkan_smaa_pass2_post_frag.h"
#include "video_core/host_shaders/antialiasing/Vulkan/vulkan_smaa_pass2_post_vert.h"
#include "video_core/host_shaders/antialiasing/Vulkan/vulkan_smaa_hlsl.h"
#include "video_core/host_shaders/antialiasing/AreaTex.h"
#include "video_core/host_shaders/antialiasing/SearchTex.h"
#include "video_core/host_shaders/scaling/vulkan_area_sampling_frag.h"
#include "video_core/host_shaders/scaling/vulkan_area_sampling_vert.h"

#include <vk_mem_alloc.h>

#if defined(__APPLE__) && !defined(HAVE_LIBRETRO)
#include "common/apple_utils.h"
#endif

#ifdef ENABLE_SDL2
#include <SDL.h>
#endif

MICROPROFILE_DEFINE(Vulkan_RenderFrame, "Vulkan", "Render Frame", MP_RGB(128, 128, 64));

namespace Vulkan {


constexpr u32 VERTEX_BUFFER_SIZE = sizeof(ScreenRectVertex) * 8192;

constexpr std::array<f32, 4 * 4> MakeOrthographicMatrix(u32 width, u32 height) {
    // clang-format off
    return { 2.f / width, 0.f,         0.f, -1.f,
            0.f,         2.f / height, 0.f, -1.f,
            0.f,         0.f,          1.f,  0.f,
            0.f,         0.f,          0.f,  1.f};
    // clang-format on
}

constexpr static std::array<vk::DescriptorSetLayoutBinding, 3> PRESENT_BINDINGS = {{
    {0, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
    {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
    {2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment},
}};

namespace {
static bool IsLowRefreshRate() {
#if (defined(__APPLE__) || defined(ENABLE_SDL2)) && !defined(HAVE_LIBRETRO)
    if (!Settings::values.use_display_refresh_rate_detection) {
        LOG_INFO(Render_Vulkan, "Refresh rate detection is currently disabled via settings");
        return false;
    }
#ifdef __APPLE__
    // Apple's low power mode sometimes limits applications to 30fps without changing the refresh
    // rate, meaning the above code doesn't catch it.
    if (AppleUtils::IsLowPowerModeEnabled()) {
        LOG_WARNING(Render_Vulkan, "Apple's low power mode is enabled, assuming low application "
                                   "framerate. FIFO will be disabled");
        return true;
    }

    const auto cur_refresh_rate = AppleUtils::GetRefreshRate();
#elif defined(ENABLE_SDL2)
    if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
        LOG_ERROR(Render_Vulkan, "Attempted to check refresh rate via SDL, but failed because "
                                 "SDL_INIT_VIDEO wasn't initialized");
        return false;
    }

    SDL_DisplayMode cur_display_mode;
    SDL_GetCurrentDisplayMode(0, &cur_display_mode); // TODO: Multimonitor handling. -OS

    const auto cur_refresh_rate = cur_display_mode.refresh_rate;
#endif // ENABLE_SDL2

    if (cur_refresh_rate < SCREEN_REFRESH_RATE) {
        LOG_WARNING(Render_Vulkan,
                    "Detected refresh rate lower than the emulated 3DS screen: {}hz. FIFO will "
                    "be disabled",
                    cur_refresh_rate);
        return true;
    } else {
        LOG_INFO(Render_Vulkan, "Refresh rate is above emulated 3DS screen: {}hz. Good.",
                 cur_refresh_rate);
    }
#endif // (defined(__APPLE__) || defined(ENABLE_SDL2)) && !defined(HAVE_LIBRETRO)

    // We have no available method of checking refresh rate. Just assume that everything is fine :)
    return false;
}
} // Anonymous namespace

RendererVulkan::RendererVulkan(Core::System& system, Pica::PicaCore& pica_,
                               Frontend::EmuWindow& window, Frontend::EmuWindow* secondary_window)
    : RendererBase{system, window, secondary_window}, memory{system.Memory()}, pica{pica_},
      instance{window, Settings::values.physical_device.GetValue()}, scheduler{instance},
      renderpass_cache{instance, scheduler},
      main_present_window{window, instance, scheduler, IsLowRefreshRate()},
      vertex_buffer{instance, scheduler, vk::BufferUsageFlagBits::eVertexBuffer,
                    VERTEX_BUFFER_SIZE},
      update_queue{instance}, rasterizer{memory,
                                         pica,
                                         system.CustomTexManager(),
                                         *this,
                                         render_window,
                                         instance,
                                         scheduler,
                                         renderpass_cache,
                                         update_queue,
                                         main_present_window.ImageCount()},
      present_heap{instance, scheduler.GetMasterSemaphore(), PRESENT_BINDINGS, 32} {
    CompileShaders();
    BuildLayouts();
    CreateTextureRenderPass();
    AllocateSMAATextures();
    AllocatePPTextures();
    CreatePPTextureFramebuffers();
    BuildPipelines();
    if (secondary_window) {
        secondary_present_window_ptr = std::make_unique<PresentWindow>(
            *secondary_window, instance, scheduler, IsLowRefreshRate());
    }
}

RendererVulkan::~RendererVulkan() {
    vk::Device device = instance.GetDevice();
    scheduler.Finish();
    main_present_window.WaitPresent();
    device.waitIdle();

    device.destroyShaderModule(present_vertex_shader);
    for (u32 i = 0; i < PRESENT_PIPELINES; i++) {
        device.destroyPipeline(present_pipelines[i]);
        device.destroyShaderModule(present_shaders[i]);
    }

    for (u32 i = 0; i < POST_PIPELINES_SCREEN; i++) {
        device.destroyPipeline(post_pipelines_screen[i]);
    }

    for (u32 i = 0; i < POST_PIPELINES_TEXTURE; i++) {
        device.destroyPipeline(post_pipelines_texture[i]);
    }

    for (u32 i = 0; i < POST_PIPELINES_SCREEN; i++) {
        device.destroyShaderModule(post_vert_shaders_screen[i]);
        device.destroyShaderModule(post_frag_shaders_screen[i]);
    }

    for (u32 i = 0; i < POST_PIPELINES_TEXTURE; i++) {
        device.destroyShaderModule(post_vert_shaders_texture[i]);
        device.destroyShaderModule(post_frag_shaders_texture[i]);
    }

    for (auto& sampler : present_samplers) {
        device.destroySampler(sampler);
    }

    for (auto& info : screen_infos) {
        device.destroyImageView(info.texture.image_view);
        vmaDestroyImage(instance.GetAllocator(), info.texture.image, info.texture.allocation);
    }

    for (int j = 0; j < intermediateTextures.size(); j++) {
        for (int i = 0; i < intermediateTextures[0].size(); i++){
            device.destroyFramebuffer(intermediateTextureFBOs[j][i]);
            device.destroyImageView(intermediateTextures[j][i].image_view);
            vmaDestroyImage(instance.GetAllocator(), intermediateTextures[j][i].image, intermediateTextures[j][i].allocation);

        }
        device.destroyFramebuffer(antialiasTextureFBOs[j]);
        device.destroyImageView(antialiasTextures[j].image_view);
        vmaDestroyImage(instance.GetAllocator(), antialiasTextures[j].image, antialiasTextures[j].allocation);
    }
    device.destroyRenderPass(textureRenderpass);
    device.destroyPipeline(cursor_pipeline);
    device.destroyShaderModule(cursor_vertex_shader);
    device.destroyShaderModule(cursor_fragment_shader);
}

void RendererVulkan::PrepareRendertarget() {
    const auto& framebuffer_config = pica.regs.framebuffer_config;
    const auto& regs_lcd = pica.regs_lcd;
    for (u32 i = 0; i < 3; i++) {
        const u32 fb_id = i == 2 ? 1 : 0;
        const auto& framebuffer = framebuffer_config[fb_id];
        auto& texture = screen_infos[i].texture;

        const auto color_fill = fb_id == 0 ? regs_lcd.color_fill_top : regs_lcd.color_fill_bottom;
        if (color_fill.is_enabled) {
            screen_infos[i].image_view = texture.image_view;
            FillScreen(color_fill.AsVector(), texture);
            continue;
        }

        if (texture.width != framebuffer.width || texture.height != framebuffer.height ||
            texture.format != framebuffer.color_format) {
            ConfigureFramebufferTexture(texture, framebuffer);
        }

        LoadFBToScreenInfo(framebuffer, screen_infos[i], i == 1);
    }
}

void RendererVulkan::CreateTextureRenderPass(){
    const vk::AttachmentReference color_ref = {
        .attachment = 0,
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
    };

    const vk::SubpassDescription subpass = {
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .inputAttachmentCount = 0,
        .pInputAttachments = nullptr,
        .colorAttachmentCount = 1u,
        .pColorAttachments = &color_ref,
        .pResolveAttachments = 0,
        .pDepthStencilAttachment = nullptr,
    };

    const vk::AttachmentDescription color_attachment = {
        .format = vk::Format::eR16G16B16A16Sfloat,
        .samples = vk::SampleCountFlagBits::e1,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = vk::ImageLayout::eUndefined,
        .finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };

    vk::SubpassDependency dependency = {
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .dstStageMask  = vk::PipelineStageFlagBits::eFragmentShader,
        .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
        .dstAccessMask = vk::AccessFlagBits::eShaderRead,
    };

    const vk::RenderPassCreateInfo renderpass_info = {
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };
    textureRenderpass = instance.GetDevice().createRenderPass(renderpass_info);
}

void RendererVulkan::AllocateTexture(TextureInfo& texture, int width, int height, vk::Format colorFormat){
    vk::Device device = instance.GetDevice();
    if (texture.image_view) {
        device.destroyImageView(texture.image_view);
    }
    if (texture.image) {
        vmaDestroyImage(instance.GetAllocator(), texture.image, texture.allocation);
    }

    texture.width = width;
    texture.height = height;

    const vk::Format format = colorFormat;
    const vk::ImageCreateInfo image_info = {
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {texture.width, texture.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment,
    };

    const VmaAllocationCreateInfo alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = 0,
        .preferredFlags = 0,
        .pool = VK_NULL_HANDLE,
        .pUserData = nullptr,
    };

    VkImage unsafe_image{};
    VkImageCreateInfo unsafe_image_info = static_cast<VkImageCreateInfo>(image_info);

    VkResult result = vmaCreateImage(instance.GetAllocator(), &unsafe_image_info, &alloc_info,
                                     &unsafe_image, &texture.allocation, nullptr);
    if (result != VK_SUCCESS) [[unlikely]] {
        LOG_CRITICAL(Render_Vulkan, "Failed allocating regular texture ({}x{}) with error {}", texture.width, texture.height, result);
        UNREACHABLE();
    } else {
        LOG_INFO(Render_Vulkan, "Successfully allocated regular texture");
    }
    texture.image = vk::Image{unsafe_image};

    const vk::ImageViewCreateInfo view_info = {
        .image = texture.image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    texture.image_view = device.createImageView(view_info);
}

void RendererVulkan::AllocateStagedTexture(StagedTextureInfo& texture, int width, int height, vk::Format colorFormat){
    vk::Device device = instance.GetDevice();
    if (texture.image_view) {
        device.destroyImageView(texture.image_view);
    }
    if (texture.image) {
        vmaDestroyImage(instance.GetAllocator(), texture.image, texture.imageAllocation);
    }

    texture.width = width;
    texture.height = height;

    const vk::Format format = colorFormat;
    const vk::ImageCreateInfo image_info = {
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {texture.width, texture.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment,
    };

    const VmaAllocationCreateInfo alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = 0,
        .preferredFlags = 0,
        .pool = VK_NULL_HANDLE,
        .pUserData = nullptr,
    };

    VkImage unsafe_image{};
    VkImageCreateInfo unsafe_image_info = static_cast<VkImageCreateInfo>(image_info);

    VkResult result = vmaCreateImage(instance.GetAllocator(), &unsafe_image_info, &alloc_info,
                                     &unsafe_image, &texture.imageAllocation, nullptr);
    if (result != VK_SUCCESS) [[unlikely]] {
        LOG_CRITICAL(Render_Vulkan, "Failed allocating regular texture ({}x{}) with error {}", texture.width, texture.height, result);
        UNREACHABLE();
    } else {
        LOG_INFO(Render_Vulkan, "Successfully allocated regular texture");
    }
    texture.image = vk::Image{unsafe_image};

    const vk::ImageViewCreateInfo view_info = {
        .image = texture.image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    texture.image_view = device.createImageView(view_info);
}

void RendererVulkan::AllocateSMAATextures(){
    areaTexInfo = {
        .width = AREATEX_WIDTH,
        .height = AREATEX_HEIGHT,
        .size = AREATEX_SIZE,
        .channels = 2,
    };
    searchTexInfo = {
        .width = SEARCHTEX_WIDTH,
        .height = SEARCHTEX_HEIGHT,
        .size = SEARCHTEX_SIZE,
        .channels = 1,
    };
    AllocateStagedTexture(areaTexInfo, areaTexInfo.width, areaTexInfo.height, vk::Format::eR8G8Unorm);
    CreateImageStagingBuffer(areaTexInfo);
    UploadImageDataToBuffer(areaTexInfo, (unsigned char*) areaTexBytes);
    UploadBufferToImage(areaTexInfo);

    AllocateStagedTexture(searchTexInfo, searchTexInfo.width, searchTexInfo.height, vk::Format::eR8Unorm);
    CreateImageStagingBuffer(searchTexInfo);
    UploadImageDataToBuffer(searchTexInfo, (unsigned char*) searchTexBytes);
    UploadBufferToImage(searchTexInfo);
}

void RendererVulkan::CreateImageStagingBuffer(StagedTextureInfo& texture){
    const vk::BufferCreateInfo staging_buffer_info = {
        .size =  texture.size,
        .usage = vk::BufferUsageFlagBits::eTransferDst,
    };

    const VmaAllocationCreateInfo alloc_create_info = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT |
                 VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        .requiredFlags = 0,
        .preferredFlags = 0,
        .pool = VK_NULL_HANDLE,
        .pUserData = nullptr,
    };

    VkBuffer unsafe_buffer{};
    VmaAllocationInfo alloc_info;
    VkBufferCreateInfo unsafe_buffer_info = static_cast<VkBufferCreateInfo>(staging_buffer_info);

    VkResult result = vmaCreateBuffer(instance.GetAllocator(), &unsafe_buffer_info,
                                      &alloc_create_info, &unsafe_buffer, &texture.bufferAllocation, &alloc_info);
    if (result != VK_SUCCESS) [[unlikely]] {
        LOG_CRITICAL(Render_Vulkan, "Failed allocating texture with error {}", result);
        UNREACHABLE();
    }

    texture.buffer = vk::Buffer{unsafe_buffer};
}


void RendererVulkan::UploadImageDataToBuffer(StagedTextureInfo& texture, unsigned char* imageData){
    vmaMapMemory(instance.GetAllocator(), texture.bufferAllocation, &texture.bufferDataPtr);
    std::memcpy(texture.bufferDataPtr, imageData, texture.size);
    vmaUnmapMemory(instance.GetAllocator(), texture.bufferAllocation);
    // Maybe Add FLush Allocation Here
}


void RendererVulkan::UploadBufferToImage(StagedTextureInfo& texture){
    vk::ImageMemoryBarrier pre_barrier = {
        .oldLayout           = vk::ImageLayout::eUndefined,
        .newLayout           = vk::ImageLayout::eTransferDstOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image               = texture.image,
        .subresourceRange    = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .srcAccessMask = vk::AccessFlags{},
        .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
    };

    vk::BufferImageCopy region = {
        .bufferOffset      = 0,
        .bufferRowLength   = 0,
        .bufferImageHeight = 0,
        .imageSubresource  = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = vk::Offset3D{ 0, 0, 0 },
        .imageExtent = vk::Extent3D{ texture.width , texture.height, 1 },
    };

    vk::ImageMemoryBarrier post_barrier = {
        .oldLayout     = vk::ImageLayout::eTransferDstOptimal,
        .newLayout     = vk::ImageLayout::eShaderReadOnlyOptimal,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image               = texture.image,
        .subresourceRange    = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eShaderRead,
    };

    scheduler.Record([texture, pre_barrier, region, post_barrier](vk::CommandBuffer cmdbuf) {
        cmdbuf.pipelineBarrier(
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer,
            vk::DependencyFlags{},
            nullptr,
            nullptr,
            pre_barrier
        );
        cmdbuf.copyBufferToImage(
            texture.buffer,
            texture.image,
            vk::ImageLayout::eTransferDstOptimal,
            region
        );
        cmdbuf.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader,
            vk::DependencyFlags{},
            nullptr,
            nullptr,
            post_barrier
        );
    });

    scheduler.Finish();
}

void RendererVulkan::AllocatePPTextures(){
    int TopWidth = 400;
    int TopHeight = 240;
    int BottomWidth = 320;
    int BottomHeight = 240;

    if (currTopTextureWidth != 0 && currBottomTextureWidth != 0 && currTopTextureHeight != 0 && currBottomTextureHeight != 0){
        TopWidth = currTopTextureWidth;
        TopHeight = currTopTextureHeight;
        BottomWidth = currBottomTextureWidth;
        BottomHeight = currBottomTextureHeight;
    }
    for (int i = 0; i < intermediateTextures[0].size(); i++){
        AllocateTexture(intermediateTextures[0][i], TopWidth, TopHeight, vk::Format::eR16G16B16A16Sfloat);
    }
    for (int i = 0; i < intermediateTextures[1].size(); i++){
        AllocateTexture(intermediateTextures[1][i], BottomWidth, BottomHeight, vk::Format::eR16G16B16A16Sfloat);
    }
    AllocateTexture(antialiasTextures[0], TopWidth, TopHeight, vk::Format::eR16G16B16A16Sfloat);
    AllocateTexture(antialiasTextures[1], BottomWidth, BottomHeight, vk::Format::eR16G16B16A16Sfloat);
};

void RendererVulkan::AllocateOutputSizeTextures(){
    for (int i = 0; i < intermediateOutputSizeTextures.size(); i++){
        if (currOutputScreenRects[i].GetHeight() != 0 && currOutputScreenRects[i].GetWidth() != 0){
            for (int j = 0; j < intermediateOutputSizeTextures[0].size(); j++){
                AllocateTexture(intermediateOutputSizeTextures[i][j], currOutputScreenRects[i].GetWidth(),  currOutputScreenRects[i].GetHeight(), vk::Format::eR16G16B16A16Sfloat);
            }
        }
    }
    LOG_INFO(Render_Vulkan, "Reallocated OutputSize Textures");
};

void RendererVulkan::CreateOutputSizeTextureFramebuffers(){
    for (int i = 0; i < intermediateOutputSizeTextures.size(); i++){
        if (currOutputScreenRects[i].GetHeight() != 0 && currOutputScreenRects[i].GetWidth() != 0){
            for (int j = 0; j < intermediateOutputSizeTextures[0].size(); j++){
                CreateTextureFramebuffer(intermediateOutputSizeTextures[i][j], intermediateOutputSizeTextureFBOs[i][j]);
            }
        }
    }
};


void RendererVulkan::CreateTextureFramebuffer(TextureInfo& texture, vk::Framebuffer& framebuffer) {
    const vk::FramebufferCreateInfo framebuffer_info = {
        .renderPass = textureRenderpass,
        .attachmentCount = 1,
        .pAttachments = &texture.image_view,
        .width = texture.width,
        .height = texture.height,
        .layers = 1,
    };
    framebuffer = instance.GetDevice().createFramebuffer(framebuffer_info);
}

void RendererVulkan::CreatePPTextureFramebuffers(){
    for (int i = 0; i < intermediateTextures.size(); i++){
        for (int j = 0; j < intermediateTextures[0].size(); j++){
            CreateTextureFramebuffer(intermediateTextures[i][j], intermediateTextureFBOs[i][j]);
        }
    }
    for (int i = 0; i < antialiasTextures.size(); i++){
        CreateTextureFramebuffer(antialiasTextures[i], antialiasTextureFBOs[i]);
    }
};

void RendererVulkan::PrepareTextureDrawFromTextureInfo(TextureInfo framebufferTexture, vk::Framebuffer framebuffer, vk::Pipeline shaderPipeline, std::vector<TextureInfo> texturesToSample, int filterMode){
    const auto sampler = present_samplers[filterMode];
    const auto present_set = present_heap.Commit();
    for (u32 i = 0; i < texturesToSample.size(); i++) {
        update_queue.AddImageSampler(present_set, i, 0, texturesToSample[i].image_view, sampler, vk::ImageLayout::eShaderReadOnlyOptimal);
    }

    renderpass_cache.EndRendering();
    scheduler.Record([this, framebufferTexture, framebuffer, shaderPipeline, present_set](vk::CommandBuffer cmdbuf) {
        const vk::Viewport viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(framebufferTexture.width),
            .height = static_cast<float>(framebufferTexture.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };

        const vk::Rect2D scissor = {
            .offset = {0, 0},
            .extent = {framebufferTexture.width, framebufferTexture.height},
        };

        const vk::ClearColorValue clear_color = {
            .float32 =
                std::array{
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                },
        };
        cmdbuf.setViewport(0, viewport);
        cmdbuf.setScissor(0, scissor);

        const vk::ClearValue clear{.color = clear_color};
        const vk::PipelineLayout layout{*present_pipeline_layout};
        const vk::RenderPassBeginInfo renderpass_begin_info = {
            .renderPass = textureRenderpass,
            .framebuffer = framebuffer,
            .renderArea =
                vk::Rect2D{
                    .offset = {0, 0},
                    .extent = {framebufferTexture.width, framebufferTexture.height},
                },
            .clearValueCount = 1,
            .pClearValues = &clear,
        };
        const std::array<float, 4> blendConstants = { 0.0f, 0.0f, 0.0f, 1.0f };
        cmdbuf.setBlendConstants(blendConstants.data());
        cmdbuf.beginRenderPass(renderpass_begin_info, vk::SubpassContents::eInline);
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, shaderPipeline);
        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, present_set, {});
    });
}

void RendererVulkan::PrepareTextureDrawFromScreenInfo(TextureInfo framebufferTexture, vk::Framebuffer framebuffer, vk::Pipeline shaderPipeline, std::vector<u32> screenids, int filterMode){
    const auto sampler = present_samplers[filterMode];
    const auto present_set = present_heap.Commit();
    for (u32 i = 0; i < screenids.size(); i++) {
        update_queue.AddImageSampler(present_set, i, 0, screen_infos[screenids[i]].image_view,
                                     sampler, vk::ImageLayout::eGeneral);
    }

    renderpass_cache.EndRendering();
    scheduler.Record([this, framebufferTexture, framebuffer, shaderPipeline, present_set](vk::CommandBuffer cmdbuf) {
        const vk::Viewport viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(framebufferTexture.width),
            .height = static_cast<float>(framebufferTexture.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };

        const vk::Rect2D scissor = {
            .offset = {0, 0},
            .extent = {framebufferTexture.width, framebufferTexture.height},
        };

        const vk::ClearColorValue clear_color = {
            .float32 =
                std::array{
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                },
        };
        cmdbuf.setViewport(0, viewport);
        cmdbuf.setScissor(0, scissor);

        const vk::ClearValue clear{.color = clear_color};
        const vk::PipelineLayout layout{*present_pipeline_layout};
        const vk::RenderPassBeginInfo renderpass_begin_info = {
            .renderPass = textureRenderpass,
            .framebuffer = framebuffer,
            .renderArea =
                vk::Rect2D{
                    .offset = {0, 0},
                    .extent = {framebufferTexture.width, framebufferTexture.height},
                },
            .clearValueCount = 1,
            .pClearValues = &clear,
        };
        const std::array<float, 4> blendConstants = { 0.0f, 0.0f, 0.0f, 1.0f };
        cmdbuf.setBlendConstants(blendConstants.data());
        cmdbuf.beginRenderPass(renderpass_begin_info, vk::SubpassContents::eInline);
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, shaderPipeline);
        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, present_set, {});
    });
}

void RendererVulkan::PrepareDrawFromScreenInfo(Frame* frame, const Layout::FramebufferLayout& layout,  vk::Pipeline shaderPipeline, std::vector<u32> screenids, int filterMode) {
    const auto sampler = present_samplers[filterMode];
    const auto present_set = present_heap.Commit();
    for (u32 i = 0; i < screenids.size(); i++) {
        update_queue.AddImageSampler(present_set, i, 0, screen_infos[screenids[i]].image_view,
                                     sampler, vk::ImageLayout::eGeneral);
    }

    renderpass_cache.EndRendering();
    vk::RenderPass currentRenderPass;
    if (clearingColorAttachment){
        currentRenderPass = main_present_window.Renderpass();
    } else {
        currentRenderPass = main_present_window.LoadRenderpass();
    }
    scheduler.Record([this, layout, frame, present_set,
                      currentRenderPass,
                      shaderPipeline](vk::CommandBuffer cmdbuf) {
        const vk::Viewport viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(layout.width),
            .height = static_cast<float>(layout.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };

        const vk::Rect2D scissor = {
            .offset = {0, 0},
            .extent = {layout.width, layout.height},
        };

        cmdbuf.setViewport(0, viewport);
        cmdbuf.setScissor(0, scissor);

        const vk::ClearValue clear{.color = clear_color};
        const vk::PipelineLayout layout{*present_pipeline_layout};
        const vk::RenderPassBeginInfo renderpass_begin_info = {
            .renderPass = currentRenderPass,
            .framebuffer = frame->framebuffer,
            .renderArea =
                vk::Rect2D{
                    .offset = {0, 0},
                    .extent = {frame->width, frame->height},
                },
            .clearValueCount = 1,
            .pClearValues = &clear,
        };
        const std::array<float, 4> blendConstants = { 0.0f, 0.0f, 0.0f, 1.0f };
        cmdbuf.setBlendConstants(blendConstants.data());
        cmdbuf.beginRenderPass(renderpass_begin_info, vk::SubpassContents::eInline);
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, shaderPipeline);
        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, present_set, {});
    });
}

void RendererVulkan::PrepareDrawFromTextureInfo(Frame* frame, const Layout::FramebufferLayout& layout, vk::Pipeline shaderPipeline, std::vector<TextureInfo> texturesToSample, int filterMode) {
    const auto sampler = present_samplers[filterMode];
    const auto present_set = present_heap.Commit();
    for (u32 i = 0; i < texturesToSample.size(); i++) {
        update_queue.AddImageSampler(present_set, i, 0, texturesToSample[i].image_view, sampler, vk::ImageLayout::eShaderReadOnlyOptimal);
    }

    renderpass_cache.EndRendering();
    vk::RenderPass currentRenderPass;
    if (clearingColorAttachment){
        currentRenderPass = main_present_window.Renderpass();
    } else {
        currentRenderPass = main_present_window.LoadRenderpass();
    }
    scheduler.Record([this, layout, frame, present_set,
                      currentRenderPass,
                      shaderPipeline](vk::CommandBuffer cmdbuf) {
        const vk::Viewport viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(layout.width),
            .height = static_cast<float>(layout.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };

        const vk::Rect2D scissor = {
            .offset = {0, 0},
            .extent = {layout.width, layout.height},
        };

        cmdbuf.setViewport(0, viewport);
        cmdbuf.setScissor(0, scissor);

        const vk::ClearValue clear{.color = clear_color};
        const vk::PipelineLayout layout{*present_pipeline_layout};
        const vk::RenderPassBeginInfo renderpass_begin_info = {
            .renderPass = currentRenderPass,
            .framebuffer = frame->framebuffer,
            .renderArea =
                vk::Rect2D{
                    .offset = {0, 0},
                    .extent = {frame->width, frame->height},
                },
            .clearValueCount = 1,
            .pClearValues = &clear,
        };
        const std::array<float, 4> blendConstants = { 0.0f, 0.0f, 0.0f, 1.0f };
        cmdbuf.setBlendConstants(blendConstants.data());
        cmdbuf.beginRenderPass(renderpass_begin_info, vk::SubpassContents::eInline);
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, shaderPipeline);
        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 0, present_set, {});
    });
}
void RendererVulkan::RenderToWindow(PresentWindow& window, const Layout::FramebufferLayout& layout,
                                    bool flipped) {
    Frame* frame = window.GetRenderFrame();

    if (layout.width != frame->width || layout.height != frame->height) {
        window.WaitPresent();
        scheduler.Finish();
        window.RecreateFrame(frame, layout.width, layout.height);
    }

    clear_color.float32[0] = Settings::values.bg_red.GetValue();
    clear_color.float32[1] = Settings::values.bg_green.GetValue();
    clear_color.float32[2] = Settings::values.bg_blue.GetValue();
    clear_color.float32[3] = 1.0f;

    DrawScreens(frame, layout, flipped);
    scheduler.Flush(frame->render_ready);

    window.Present(frame);
}

void RendererVulkan::LoadFBToScreenInfo(const Pica::FramebufferConfig& framebuffer,
                                        ScreenInfo& screen_info, bool right_eye) {

    if (framebuffer.address_right1 == 0 || framebuffer.address_right2 == 0) {
        right_eye = false;
    }

    const PAddr framebuffer_addr =
        framebuffer.active_fb == 0
            ? (right_eye ? framebuffer.address_right1 : framebuffer.address_left1)
            : (right_eye ? framebuffer.address_right2 : framebuffer.address_left2);

    LOG_TRACE(Render_Vulkan, "0x{:08x} bytes from 0x{:08x}({}x{}), fmt {:x}",
              framebuffer.stride * framebuffer.height, framebuffer_addr, framebuffer.width.Value(),
              framebuffer.height.Value(), framebuffer.format);

    const u32 bpp = Pica::BytesPerPixel(framebuffer.color_format);
    const std::size_t pixel_stride = framebuffer.stride / bpp;

    ASSERT(pixel_stride * bpp == framebuffer.stride);
    ASSERT(pixel_stride % 4 == 0);

    if (!rasterizer.AccelerateDisplay(framebuffer, framebuffer_addr, static_cast<u32>(pixel_stride),
                                      screen_info)) {
        // Reset the screen info's display texture to its own permanent texture
        screen_info.image_view = screen_info.texture.image_view;
        screen_info.texcoords = {0.f, 0.f, 1.f, 1.f};

        ASSERT(false);
    }
}

void RendererVulkan::CompileShaders() {
    const vk::Device device = instance.GetDevice();
    const std::string_view preamble =
        instance.IsImageArrayDynamicIndexSupported() ? "#define ARRAY_DYNAMIC_INDEX" : "";
    present_vertex_shader =
        Compile(HostShaders::VULKAN_PRESENT_VERT, vk::ShaderStageFlagBits::eVertex, device);
    present_shaders[0] = Compile(HostShaders::VULKAN_PRESENT_FRAG,
                                 vk::ShaderStageFlagBits::eFragment, device, preamble);
    present_shaders[1] = Compile(HostShaders::VULKAN_PRESENT_ANAGLYPH_FRAG,
                                 vk::ShaderStageFlagBits::eFragment, device, preamble);
    present_shaders[2] = Compile(HostShaders::VULKAN_PRESENT_INTERLACED_FRAG,
                                 vk::ShaderStageFlagBits::eFragment, device, preamble);

    cursor_vertex_shader =
        Compile(HostShaders::VULKAN_CURSOR_VERT, vk::ShaderStageFlagBits::eVertex, device);
    cursor_fragment_shader =
        Compile(HostShaders::VULKAN_CURSOR_FRAG, vk::ShaderStageFlagBits::eFragment, device);

    // Simple Present Shader
    post_vert_shaders_texture[0] =
        Compile(HostShaders::VULKAN_SIMPLE_PRESENT_VERT, vk::ShaderStageFlagBits::eVertex, device);
    post_frag_shaders_texture[0] =
        Compile(HostShaders::VULKAN_SIMPLE_PRESENT_FRAG, vk::ShaderStageFlagBits::eFragment, device, preamble);

    // Area Sampling Shader
    post_vert_shaders_screen[0] =
        Compile(HostShaders::VULKAN_AREA_SAMPLING_VERT, vk::ShaderStageFlagBits::eVertex, device);
    post_frag_shaders_screen[0] =
        Compile(HostShaders::VULKAN_AREA_SAMPLING_FRAG, vk::ShaderStageFlagBits::eFragment, device);

    // FXAA Shader
    post_vert_shaders_texture[1] =
        Compile(HostShaders::VULKAN_FXAA_VERT, vk::ShaderStageFlagBits::eVertex, device);
    post_frag_shaders_texture[1] =
        Compile(HostShaders::VULKAN_FXAA_FRAG, vk::ShaderStageFlagBits::eFragment, device);

    // SMAA Pass 0 Shader
    std::string smaa_pass_0_shader_vert_data = std::string(HostShaders::VULKAN_SMAA_PASS0_PRE_VERT);
    smaa_pass_0_shader_vert_data += std::string(HostShaders::VULKAN_SMAA_HLSL);
    smaa_pass_0_shader_vert_data += std::string(HostShaders::VULKAN_SMAA_PASS0_POST_VERT);
    std::string smaa_pass_0_shader_frag_data = std::string(HostShaders::VULKAN_SMAA_PASS0_PRE_FRAG);
    smaa_pass_0_shader_frag_data += std::string(HostShaders::VULKAN_SMAA_HLSL);
    smaa_pass_0_shader_frag_data += std::string(HostShaders::VULKAN_SMAA_PASS0_POST_FRAG);
    post_vert_shaders_texture[2] =
        Compile(smaa_pass_0_shader_vert_data, vk::ShaderStageFlagBits::eVertex, device);
    post_frag_shaders_texture[2] =
        Compile(smaa_pass_0_shader_frag_data, vk::ShaderStageFlagBits::eFragment, device);

    // SMAA Pass 1 Shader
    std::string smaa_pass_1_shader_vert_data = std::string(HostShaders::VULKAN_SMAA_PASS1_PRE_VERT);
    smaa_pass_1_shader_vert_data += std::string(HostShaders::VULKAN_SMAA_HLSL);
    smaa_pass_1_shader_vert_data += std::string(HostShaders::VULKAN_SMAA_PASS1_POST_VERT);
    std::string smaa_pass_1_shader_frag_data = std::string(HostShaders::VULKAN_SMAA_PASS1_PRE_FRAG);
    smaa_pass_1_shader_frag_data += std::string(HostShaders::VULKAN_SMAA_HLSL);
    smaa_pass_1_shader_frag_data += std::string(HostShaders::VULKAN_SMAA_PASS1_POST_FRAG);
    post_vert_shaders_texture[3] =
        Compile(smaa_pass_1_shader_vert_data, vk::ShaderStageFlagBits::eVertex, device);
    post_frag_shaders_texture[3] =
        Compile(smaa_pass_1_shader_frag_data, vk::ShaderStageFlagBits::eFragment, device);

    // SMAA Pass 2 Shader
    std::string smaa_pass_2_shader_vert_data = std::string(HostShaders::VULKAN_SMAA_PASS2_PRE_VERT);
    smaa_pass_2_shader_vert_data += std::string(HostShaders::VULKAN_SMAA_HLSL);
    smaa_pass_2_shader_vert_data += std::string(HostShaders::VULKAN_SMAA_PASS2_POST_VERT);
    std::string smaa_pass_2_shader_frag_data = std::string(HostShaders::VULKAN_SMAA_PASS2_PRE_FRAG);
    smaa_pass_2_shader_frag_data += std::string(HostShaders::VULKAN_SMAA_HLSL);
    smaa_pass_2_shader_frag_data += std::string(HostShaders::VULKAN_SMAA_PASS2_POST_FRAG);
    post_vert_shaders_texture[4] =
        Compile(smaa_pass_2_shader_vert_data, vk::ShaderStageFlagBits::eVertex, device);
    post_frag_shaders_texture[4] =
        Compile(smaa_pass_2_shader_frag_data, vk::ShaderStageFlagBits::eFragment, device);

    
    auto properties = instance.GetPhysicalDevice().getProperties();
    for (std::size_t i = 0; i < present_samplers.size(); i++) {
        const vk::Filter filter_mode = i == 0 ? vk::Filter::eNearest : vk::Filter::eLinear;
        const vk::SamplerCreateInfo sampler_info = {
            .magFilter = filter_mode,
            .minFilter = filter_mode,
            .mipmapMode = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV = vk::SamplerAddressMode::eClampToEdge,
            .anisotropyEnable = instance.IsAnisotropicFilteringSupported(),
            .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
            .compareEnable = false,
            .compareOp = vk::CompareOp::eAlways,
            .borderColor = vk::BorderColor::eIntOpaqueBlack,
            .unnormalizedCoordinates = false,
        };

        present_samplers[i] = device.createSampler(sampler_info);
    }
}

void RendererVulkan::BuildLayouts() {
    const vk::PushConstantRange push_range = {
        .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        .offset = 0,
        .size = sizeof(PresentUniformData),
    };

    const auto descriptor_set_layout = present_heap.Layout();
    const vk::PipelineLayoutCreateInfo layout_info = {
        .setLayoutCount = 1,
        .pSetLayouts = &descriptor_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };
    present_pipeline_layout = instance.GetDevice().createPipelineLayoutUnique(layout_info);

    const vk::PipelineLayoutCreateInfo cursor_layout_info = {};
    cursor_pipeline_layout = instance.GetDevice().createPipelineLayoutUnique(cursor_layout_info);
}

void RendererVulkan::BuildPipelines() {
    const vk::VertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(ScreenRectVertex),
        .inputRate = vk::VertexInputRate::eVertex,
    };

    const std::array attributes = {
        vk::VertexInputAttributeDescription{
            .location = 0,
            .binding = 0,
            .format = vk::Format::eR32G32Sfloat,
            .offset = offsetof(ScreenRectVertex, position),
        },
        vk::VertexInputAttributeDescription{
            .location = 1,
            .binding = 0,
            .format = vk::Format::eR32G32Sfloat,
            .offset = offsetof(ScreenRectVertex, tex_coord),
        },
    };

    const vk::PipelineVertexInputStateCreateInfo vertex_input_info = {
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding,
        .vertexAttributeDescriptionCount = static_cast<u32>(attributes.size()),
        .pVertexAttributeDescriptions = attributes.data(),
    };

    const vk::PipelineInputAssemblyStateCreateInfo input_assembly = {
        .topology = vk::PrimitiveTopology::eTriangleStrip,
        .primitiveRestartEnable = false,
    };

    const vk::PipelineRasterizationStateCreateInfo raster_state = {
        .depthClampEnable = false,
        .rasterizerDiscardEnable = false,
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eClockwise,
        .depthBiasEnable = false,
        .lineWidth = 1.0f,
    };

    const vk::PipelineMultisampleStateCreateInfo multisampling = {
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = false,
    };

    const vk::PipelineColorBlendAttachmentState colorblend_attachment = {
        .blendEnable = true,
        .srcColorBlendFactor = vk::BlendFactor::eConstantAlpha,
        .dstColorBlendFactor = vk::BlendFactor::eOneMinusConstantAlpha,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eConstantAlpha,
        .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusConstantAlpha,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };

    const vk::PipelineColorBlendStateCreateInfo color_blending = {
        .logicOpEnable = false,
        .attachmentCount = 1,
        .pAttachments = &colorblend_attachment,
    };

    const vk::Viewport placeholder_viewport = vk::Viewport{0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
    const vk::Rect2D placeholder_scissor = vk::Rect2D{{0, 0}, {1, 1}};
    const vk::PipelineViewportStateCreateInfo viewport_info = {
        .viewportCount = 1,
        .pViewports = &placeholder_viewport,
        .scissorCount = 1,
        .pScissors = &placeholder_scissor,
    };

    const std::array dynamic_states = {
        vk::DynamicState::eBlendConstants,
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };

    const vk::PipelineDynamicStateCreateInfo dynamic_info = {
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };

    const vk::PipelineDepthStencilStateCreateInfo depth_info = {
        .depthTestEnable = false,
        .depthWriteEnable = false,
        .depthCompareOp = vk::CompareOp::eAlways,
        .depthBoundsTestEnable = false,
        .stencilTestEnable = false,
    };

    for (u32 i = 0; i < PRESENT_PIPELINES; i++) {
        const std::array shader_stages = {
            vk::PipelineShaderStageCreateInfo{
                .stage = vk::ShaderStageFlagBits::eVertex,
                .module = present_vertex_shader,
                .pName = "main",
            },
            vk::PipelineShaderStageCreateInfo{
                .stage = vk::ShaderStageFlagBits::eFragment,
                .module = present_shaders[i],
                .pName = "main",
            },
        };

        const vk::GraphicsPipelineCreateInfo pipeline_info = {
            .stageCount = static_cast<u32>(shader_stages.size()),
            .pStages = shader_stages.data(),
            .pVertexInputState = &vertex_input_info,
            .pInputAssemblyState = &input_assembly,
            .pViewportState = &viewport_info,
            .pRasterizationState = &raster_state,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = &depth_info,
            .pColorBlendState = &color_blending,
            .pDynamicState = &dynamic_info,
            .layout = *present_pipeline_layout,
            .renderPass = main_present_window.Renderpass(),
        };

        const auto [result, pipeline] =
            instance.GetDevice().createGraphicsPipeline({}, pipeline_info);
        ASSERT_MSG(result == vk::Result::eSuccess, "Unable to build present pipelines");
        present_pipelines[i] = pipeline;
    }

    // Build Post Processing Pipelines for RGBA16F textures
    for (u32 i = 0; i < POST_PIPELINES_TEXTURE; i++) {
        const std::array shader_stages = {
            vk::PipelineShaderStageCreateInfo{
                .stage = vk::ShaderStageFlagBits::eVertex,
                .module = post_vert_shaders_texture[i],
                .pName = "main",
            },
            vk::PipelineShaderStageCreateInfo{
                .stage = vk::ShaderStageFlagBits::eFragment,
                .module = post_frag_shaders_texture[i],
                .pName = "main",
            },
        };

        const vk::GraphicsPipelineCreateInfo pipeline_info = {
            .stageCount = static_cast<u32>(shader_stages.size()),
            .pStages = shader_stages.data(),
            .pVertexInputState = &vertex_input_info,
            .pInputAssemblyState = &input_assembly,
            .pViewportState = &viewport_info,
            .pRasterizationState = &raster_state,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = &depth_info,
            .pColorBlendState = &color_blending,
            .pDynamicState = &dynamic_info,
            .layout = *present_pipeline_layout,
            .renderPass = textureRenderpass,
        };

        const auto [result, pipeline] =
            instance.GetDevice().createGraphicsPipeline({}, pipeline_info);
        ASSERT_MSG(result == vk::Result::eSuccess, "Unable to build post processing pipelines");
        post_pipelines_texture[i] = pipeline;
    }

    // Build Post Processing Pipelines for presenting
    for (u32 i = 0; i < POST_PIPELINES_SCREEN; i++) {
        const std::array shader_stages = {
            vk::PipelineShaderStageCreateInfo{
                .stage = vk::ShaderStageFlagBits::eVertex,
                .module = post_vert_shaders_screen[i],
                .pName = "main",
            },
            vk::PipelineShaderStageCreateInfo{
                .stage = vk::ShaderStageFlagBits::eFragment,
                .module = post_frag_shaders_screen[i],
                .pName = "main",
            },
        };

        const vk::GraphicsPipelineCreateInfo pipeline_info = {
            .stageCount = static_cast<u32>(shader_stages.size()),
            .pStages = shader_stages.data(),
            .pVertexInputState = &vertex_input_info,
            .pInputAssemblyState = &input_assembly,
            .pViewportState = &viewport_info,
            .pRasterizationState = &raster_state,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = &depth_info,
            .pColorBlendState = &color_blending,
            .pDynamicState = &dynamic_info,
            .layout = *present_pipeline_layout,
            .renderPass = main_present_window.Renderpass(),
        };

        const auto [result, pipeline] =
            instance.GetDevice().createGraphicsPipeline({}, pipeline_info);
        ASSERT_MSG(result == vk::Result::eSuccess, "Unable to build post processing pipelines");
        post_pipelines_screen[i] = pipeline;
    }

    // Build cursor pipeline (simple position-only, inverted color blending)
    {
        const vk::VertexInputBindingDescription cursor_binding = {
            .binding = 0,
            .stride = sizeof(float) * 2,
            .inputRate = vk::VertexInputRate::eVertex,
        };

        const vk::VertexInputAttributeDescription cursor_attribute = {
            .location = 0,
            .binding = 0,
            .format = vk::Format::eR32G32Sfloat,
            .offset = 0,
        };

        const vk::PipelineVertexInputStateCreateInfo cursor_vertex_input = {
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &cursor_binding,
            .vertexAttributeDescriptionCount = 1,
            .pVertexAttributeDescriptions = &cursor_attribute,
        };

        const vk::PipelineInputAssemblyStateCreateInfo cursor_input_assembly = {
            .topology = vk::PrimitiveTopology::eTriangleList,
            .primitiveRestartEnable = false,
        };

        const vk::PipelineRasterizationStateCreateInfo cursor_raster = {
            .depthClampEnable = false,
            .rasterizerDiscardEnable = false,
            .cullMode = vk::CullModeFlagBits::eNone,
            .frontFace = vk::FrontFace::eClockwise,
            .depthBiasEnable = false,
            .lineWidth = 1.0f,
        };

        const vk::PipelineMultisampleStateCreateInfo cursor_multisample = {
            .rasterizationSamples = vk::SampleCountFlagBits::e1,
            .sampleShadingEnable = false,
        };

        const vk::PipelineColorBlendAttachmentState cursor_blend_attachment = {
            .blendEnable = true,
            .srcColorBlendFactor = vk::BlendFactor::eOneMinusDstColor,
            .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcColor,
            .colorBlendOp = vk::BlendOp::eAdd,
            .srcAlphaBlendFactor = vk::BlendFactor::eOne,
            .dstAlphaBlendFactor = vk::BlendFactor::eZero,
            .alphaBlendOp = vk::BlendOp::eAdd,
            .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                              vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };

        const vk::PipelineColorBlendStateCreateInfo cursor_color_blending = {
            .logicOpEnable = false,
            .attachmentCount = 1,
            .pAttachments = &cursor_blend_attachment,
        };

        const vk::Viewport placeholder_vp = {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f};
        const vk::Rect2D placeholder_sc = {{0, 0}, {1, 1}};
        const vk::PipelineViewportStateCreateInfo cursor_viewport = {
            .viewportCount = 1,
            .pViewports = &placeholder_vp,
            .scissorCount = 1,
            .pScissors = &placeholder_sc,
        };

        const std::array cursor_dynamic_states = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor,
        };

        const vk::PipelineDynamicStateCreateInfo cursor_dynamic = {
            .dynamicStateCount = static_cast<u32>(cursor_dynamic_states.size()),
            .pDynamicStates = cursor_dynamic_states.data(),
        };

        const vk::PipelineDepthStencilStateCreateInfo cursor_depth = {
            .depthTestEnable = false,
            .depthWriteEnable = false,
            .depthCompareOp = vk::CompareOp::eAlways,
            .depthBoundsTestEnable = false,
            .stencilTestEnable = false,
        };

        const std::array cursor_shader_stages = {
            vk::PipelineShaderStageCreateInfo{
                .stage = vk::ShaderStageFlagBits::eVertex,
                .module = cursor_vertex_shader,
                .pName = "main",
            },
            vk::PipelineShaderStageCreateInfo{
                .stage = vk::ShaderStageFlagBits::eFragment,
                .module = cursor_fragment_shader,
                .pName = "main",
            },
        };

        const vk::GraphicsPipelineCreateInfo cursor_pipeline_info = {
            .stageCount = static_cast<u32>(cursor_shader_stages.size()),
            .pStages = cursor_shader_stages.data(),
            .pVertexInputState = &cursor_vertex_input,
            .pInputAssemblyState = &cursor_input_assembly,
            .pViewportState = &cursor_viewport,
            .pRasterizationState = &cursor_raster,
            .pMultisampleState = &cursor_multisample,
            .pDepthStencilState = &cursor_depth,
            .pColorBlendState = &cursor_color_blending,
            .pDynamicState = &cursor_dynamic,
            .layout = *cursor_pipeline_layout,
            .renderPass = main_present_window.Renderpass(),
        };

        const auto [result, pipeline] =
            instance.GetDevice().createGraphicsPipeline({}, cursor_pipeline_info);
        ASSERT_MSG(result == vk::Result::eSuccess, "Unable to build cursor pipeline");
        cursor_pipeline = pipeline;
    }
}

void RendererVulkan::ConfigureFramebufferTexture(TextureInfo& texture,
                                                 const Pica::FramebufferConfig& framebuffer) {
    vk::Device device = instance.GetDevice();
    if (texture.image_view) {
        device.destroyImageView(texture.image_view);
    }
    if (texture.image) {
        vmaDestroyImage(instance.GetAllocator(), texture.image, texture.allocation);
    }

    const VideoCore::PixelFormat pixel_format =
        VideoCore::PixelFormatFromGPUPixelFormat(framebuffer.color_format);
    const vk::Format format = instance.GetTraits(pixel_format).native;
    const vk::ImageCreateInfo image_info = {
        .imageType = vk::ImageType::e2D,
        .format = format,
        .extent = {framebuffer.width, framebuffer.height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .usage = vk::ImageUsageFlagBits::eSampled,
    };

    const VmaAllocationCreateInfo alloc_info = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = 0,
        .preferredFlags = 0,
        .pool = VK_NULL_HANDLE,
        .pUserData = nullptr,
    };

    VkImage unsafe_image{};
    VkImageCreateInfo unsafe_image_info = static_cast<VkImageCreateInfo>(image_info);

    VkResult result = vmaCreateImage(instance.GetAllocator(), &unsafe_image_info, &alloc_info,
                                     &unsafe_image, &texture.allocation, nullptr);
    if (result != VK_SUCCESS) [[unlikely]] {
        LOG_CRITICAL(Render_Vulkan, "Failed allocating framebuffer texture with error {}", result);
        UNREACHABLE();
    }
    texture.image = vk::Image{unsafe_image};

    const vk::ImageViewCreateInfo view_info = {
        .image = texture.image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };
    texture.image_view = device.createImageView(view_info);

    texture.width = framebuffer.width;
    texture.height = framebuffer.height;
    texture.format = framebuffer.color_format;
}

void RendererVulkan::FillScreen(Common::Vec3<u8> color, const TextureInfo& texture) {
    const vk::ClearColorValue clear_color = {
        .float32 =
            std::array{
                color.r() / 255.0f,
                color.g() / 255.0f,
                color.b() / 255.0f,
                1.0f,
            },
    };

    renderpass_cache.EndRendering();
    scheduler.Record([image = texture.image, clear_color](vk::CommandBuffer cmdbuf) {
        const vk::ImageSubresourceRange range = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = VK_REMAINING_MIP_LEVELS,
            .baseArrayLayer = 0,
            .layerCount = VK_REMAINING_ARRAY_LAYERS,
        };

        const vk::ImageMemoryBarrier pre_barrier = {
            .srcAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eTransferRead,
            .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
            .oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = range,
        };

        const vk::ImageMemoryBarrier post_barrier = {
            .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
            .dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eTransferRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = range,
        };

        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader,
                               vk::PipelineStageFlagBits::eTransfer,
                               vk::DependencyFlagBits::eByRegion, {}, {}, pre_barrier);

        cmdbuf.clearColorImage(image, vk::ImageLayout::eTransferDstOptimal, clear_color, range);

        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                               vk::PipelineStageFlagBits::eFragmentShader,
                               vk::DependencyFlagBits::eByRegion, {}, {}, post_barrier);
    });
}

void RendererVulkan::ReloadPipeline(Settings::StereoRenderOption render_3d) {
    switch (render_3d) {
    case Settings::StereoRenderOption::Anaglyph:
        current_pipeline = 1;
        break;
    case Settings::StereoRenderOption::Interlaced:
    case Settings::StereoRenderOption::ReverseInterlaced:
        current_pipeline = 2;
        draw_info.reverse_interlaced = render_3d == Settings::StereoRenderOption::ReverseInterlaced;
        break;
    default:
        current_pipeline = 0;
        break;
    }
}

void RendererVulkan::DrawSingleScreen(u32 screen_id, float screenLeft, float screenTop, float screenWidth, float screenHeight,
                                      Layout::DisplayOrientation orientation) {
    const ScreenInfo& screen_info = screen_infos[screen_id];
    const auto& texcoords = screen_info.texcoords;
    const u32 scale_factor = GetResolutionScaleFactor();
    // Texture Width and Height when correctly rotated to landscape
    float textureWidth = static_cast<float>(screen_info.texture.height * scale_factor);
    float textureHeight = static_cast<float>(screen_info.texture.width * scale_factor);
    int currScreen;
    if (textureWidth == currTopTextureWidth && textureHeight == currTopTextureHeight){
        currScreen = 0;
    } else {
        currScreen = 1;
    }
    bool isDownsampling = false;
    int scalingMode; // 0 is Nearest Neighbor, 1 is Gamma Corrected Bilinear, 2 is Adaptive (Bilinear/Area), 3 is FSR, 4 is Sharp Bilinear
    scalingMode = static_cast<int>(Settings::values.output_scaling.GetValue());
    int antialiasingMode = static_cast<int>(Settings::values.antialiasing_filter.GetValue()); //0 is none, 1 is FXAA, 2 is SMAA
    float fsr_sharpening = 2 - (2 * (Settings::values.fsr_sharpness.GetValue()/ 100.0f));
    if (orientation == Layout::DisplayOrientation::Landscape || orientation == Layout::DisplayOrientation::LandscapeFlipped) {
        if (textureWidth > screenWidth){
            isDownsampling = true;
        }
    } else {
        if (textureWidth > screenHeight){
            isDownsampling = true;
        }
    }
    // Rotate Internal Texture to Landscape (The 3DS stores images rotated 90° internally)
    std::array<ScreenRectVertex, 4> rotate_vertices;
    rotate_vertices = {{
            ScreenRectVertex(-1.f, 1.f, texcoords.top, texcoords.left),    //Left, Top
            ScreenRectVertex(1.f, 1.f, texcoords.top, texcoords.right),     //Right, Top
            ScreenRectVertex(-1.f, -1.f, texcoords.bottom, texcoords.left), //Left, Bottom
            ScreenRectVertex(1.f, -1.f, texcoords.bottom, texcoords.right), //Right, Bottom
    }};

    std::array<ScreenRectVertex, 4> pass_through_vertices;
    pass_through_vertices = {{
            ScreenRectVertex(-1.f, 1.f, 0.f, 0.f),   //Left, Top
            ScreenRectVertex(1.f, 1.f, 1.f, 0.f),    //Right, Top
            ScreenRectVertex(-1.f, -1.f, 0.f, 1.f),  //Left, Bottom
            ScreenRectVertex(1.f, -1.f, 1.f, 1.f),   //Right, Bottom
    }};

    // Vertices for Azahar's Output Layout
    std::array<ScreenRectVertex, 4> output_vertices;
    switch (orientation) {
    case Layout::DisplayOrientation::Landscape:
        output_vertices = {{
                ScreenRectVertex(screenLeft, screenTop, 0.f, 0.f),
                ScreenRectVertex(screenLeft + screenWidth, screenTop, 1.f, 0.f),
                ScreenRectVertex(screenLeft, screenTop + screenHeight, 0.f, 1.f),
                ScreenRectVertex(screenLeft + screenWidth, screenTop + screenHeight, 1.f, 1.f),
        }};
        break;
    case Layout::DisplayOrientation::Portrait:
        output_vertices = {{
                ScreenRectVertex(screenLeft, screenTop, 1.f, 0.f),
                ScreenRectVertex(screenLeft + screenWidth, screenTop, 1.f, 1.f),
                ScreenRectVertex(screenLeft, screenTop + screenHeight, 0.f, 0.f),
                ScreenRectVertex(screenLeft + screenWidth, screenTop + screenHeight, 0.f, 1.f),
        }};
        std::swap(screenHeight, screenWidth);
        break;
    case Layout::DisplayOrientation::LandscapeFlipped:
        output_vertices = {{
                ScreenRectVertex(screenLeft, screenTop, 0.f, 1.f),
                ScreenRectVertex(screenLeft + screenWidth, screenTop, 1.f, 1.f),
                ScreenRectVertex(screenLeft, screenTop + screenHeight, 0.f, 0.f),
                ScreenRectVertex(screenLeft + screenWidth, screenTop + screenHeight, 1.f, 0.f),
        }};
        break;
    case Layout::DisplayOrientation::PortraitFlipped:
        output_vertices = {{
                ScreenRectVertex(screenLeft, screenTop, 0.f, 1.f),
                ScreenRectVertex(screenLeft + screenWidth, screenTop, 0.f, 0.f),
                ScreenRectVertex(screenLeft, screenTop + screenHeight, 1.f, 1.f),
                ScreenRectVertex(screenLeft + screenWidth, screenTop + screenHeight, 1.f, 0.f),
        }};
        std::swap(screenHeight, screenWidth);
        break;
    }
    const u64 size = sizeof(ScreenRectVertex) * output_vertices.size();

    int maxPasses = 5;
    std::vector<VertexBufferPointer> vertexBufferPointers(maxPasses);
    for (auto& vbp : vertexBufferPointers){
        std::tie(vbp.data, vbp.offset, vbp.invalidate) = vertex_buffer.Map(size, 16);
        vertex_buffer.Commit(size);
    }
    std::vector<PresentUniformData> drawInfos(maxPasses);
    for (auto& info : drawInfos){
        info = draw_info;
    }

    //Vectors for sampling
    std::vector<u32> screen_ids;
    std::vector<TextureInfo> texturesToSample;

    // Multipass
    screen_ids.assign({screen_id});
    PrepareTextureDrawFromScreenInfo(intermediateTextures[currScreen][0], intermediateTextureFBOs[currScreen][0], post_pipelines_texture[0], screen_ids, 1);
    UpdateVertexBuffer(rotate_vertices, vertexBufferPointers[0]);
    drawInfos[0].convert_colors = 1;
    Draw(vertexBufferPointers[0], drawInfos[0]);


    // int currentPass;
    // if (antialiasingMode == 1){
    //     screen_ids.assign({screen_id});
    //     PrepareTextureDrawFromScreenInfo(intermediateTextures[currScreen][0], intermediateTextureFBOs[currScreen][0], post_pipelines_texture[0], screen_ids, 1);
    //     UpdateVertexBuffer(rotate_vertices, vertexBufferPointers[currentPass]);
    //     drawInfos[currentPass].convert_colors = 1;
    //     Draw(vertexBufferPointers[currentPass], drawInfos[currentPass]);
    //     currentPass++;

    //     texturesToSample.assign({intermediateTextures[currScreen][0]});
    //     PrepareTextureDrawFromTextureInfo(antialiasTextures[currScreen], antialiasTextureFBOs[currScreen], post_pipelines_texture[1], texturesToSample, 1);
    //     UpdateVertexBuffer(pass_through_vertices, vertexBufferPointers[currentPass]);
    //     if (scalingMode == 3){
    //         drawInfos[currentPass].convert_colors = 0;
    //     } else {
    //         drawInfos[currentPass].convert_colors = 1;
    //     }
    //     drawInfos[currentPass].i_resolution = Common::Vec4f{textureWidth, textureHeight, 1.0f/ textureWidth, 1.0f / textureHeight};
    //     Draw(vertexBufferPointers[currentPass], drawInfos[currentPass]);
    //     currentPass++;
    // }
    // // else if (antialiasingMode == 2) {

    // // } 
    // else {
    //     screen_ids.assign({screen_id});
    //     PrepareTextureDrawFromScreenInfo(antialiasTextures[currScreen], antialiasTextureFBOs[currScreen], post_pipelines_texture[0], screen_ids, 1);
    //     UpdateVertexBuffer(rotate_vertices, vertexBufferPointers[currentPass]);
    //     if (scalingMode == 3){
    //         drawInfos[currentPass].convert_colors = 0;
    //     } else {
    //         drawInfos[currentPass].convert_colors = 1;
    //     }
    //     Draw(vertexBufferPointers[currentPass], drawInfos[currentPass]);
    //     currentPass++;
    // }

    // if (scalingMode == 2){
    //     if (isDownsampling){

    //     } else {

    //     }
    // } else if (scalingMode == 3) {
    //     if (isDownsampling){

    //     } else {

    //     }    
    // } else if (scalingMode == 4) {

    // } else {

    // }

    texturesToSample.assign({intermediateTextures[currScreen][0]});
    PrepareDrawFromTextureInfo(currentFrame, currentFramebufferLayout, present_pipelines[current_pipeline], texturesToSample, 1);
    ApplySecondLayerOpacity();
    UpdateVertexBuffer(output_vertices, vertexBufferPointers[1]);
    drawInfos[1].convert_colors = 2;
    Draw(vertexBufferPointers[1], drawInfos[1]);
}


void RendererVulkan::UpdateVertexBuffer(std::array<ScreenRectVertex, 4> vertices, VertexBufferPointer vbp){
    const u64 size = sizeof(ScreenRectVertex) * vertices.size();
    std::memcpy(vbp.data, vertices.data(), size);
}

void RendererVulkan::Draw(VertexBufferPointer vbp, PresentUniformData pushconstant){
    scheduler.Record([this, vbp, pushconstant](vk::CommandBuffer cmdbuf) {
        const u32 first_vertex = static_cast<u32>(vbp.offset) / sizeof(ScreenRectVertex);
        cmdbuf.pushConstants(*present_pipeline_layout,
                             vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eVertex,
                             0, sizeof(pushconstant), &pushconstant);
        cmdbuf.bindVertexBuffers(0, vertex_buffer.Handle(), {0});
        cmdbuf.draw(4, 1, first_vertex, 0);
        cmdbuf.endRenderPass();
    });
}

void RendererVulkan::ApplySecondLayerOpacity() {
    float alpha;
    if (applyingOpacity){
        if (drawingPrimaryScreen){
            alpha = 1.0;
        } else {
            if (usingTopOpacity){
                if (currentFramebufferLayout.top_opacity < 1) {
                    alpha = currentFramebufferLayout.top_opacity;
                } else {
                    return;
                }
            } else {
                if (currentFramebufferLayout.bottom_opacity < 1) {
                    alpha = currentFramebufferLayout.bottom_opacity;
                } else {
                    return;
                }
            }
        }
        scheduler.Record([alpha](vk::CommandBuffer cmdbuf) {
            const std::array<float, 4> blend_constants = {0.0f, 0.0f, 0.0f, alpha};
            cmdbuf.setBlendConstants(blend_constants.data());
        });
    }
}

void RendererVulkan::DrawSingleScreenStereo(u32 screen_id_l, u32 screen_id_r, float x, float y,
                                            float w, float h,
                                            Layout::DisplayOrientation orientation) {
    const ScreenInfo& screen_info_l = screen_infos[screen_id_l];
    const auto& texcoords = screen_info_l.texcoords;
    const u32 scale_factor = GetResolutionScaleFactor();
    float textureWidth = static_cast<float>(screen_info_l.texture.height * scale_factor);
    float textureHeight = static_cast<float>(screen_info_l.texture.width * scale_factor);

    std::array<ScreenRectVertex, 4> vertices;
    switch (orientation) {
    case Layout::DisplayOrientation::Landscape:
        vertices = {{
            ScreenRectVertex(x, y, texcoords.bottom, texcoords.left),
            ScreenRectVertex(x + w, y, texcoords.bottom, texcoords.right),
            ScreenRectVertex(x, y + h, texcoords.top, texcoords.left),
            ScreenRectVertex(x + w, y + h, texcoords.top, texcoords.right),
        }};
        break;
    case Layout::DisplayOrientation::Portrait:
        vertices = {{
            ScreenRectVertex(x, y, texcoords.bottom, texcoords.right),
            ScreenRectVertex(x + w, y, texcoords.top, texcoords.right),
            ScreenRectVertex(x, y + h, texcoords.bottom, texcoords.left),
            ScreenRectVertex(x + w, y + h, texcoords.top, texcoords.left),
        }};
        std::swap(h, w);
        break;
    case Layout::DisplayOrientation::LandscapeFlipped:
        vertices = {{
            ScreenRectVertex(x, y, texcoords.top, texcoords.right),
            ScreenRectVertex(x + w, y, texcoords.top, texcoords.left),
            ScreenRectVertex(x, y + h, texcoords.bottom, texcoords.right),
            ScreenRectVertex(x + w, y + h, texcoords.bottom, texcoords.left),
        }};
        break;
    case Layout::DisplayOrientation::PortraitFlipped:
        vertices = {{
            ScreenRectVertex(x, y, texcoords.top, texcoords.left),
            ScreenRectVertex(x + w, y, texcoords.bottom, texcoords.left),
            ScreenRectVertex(x, y + h, texcoords.top, texcoords.right),
            ScreenRectVertex(x + w, y + h, texcoords.bottom, texcoords.right),
        }};
        std::swap(h, w);
        break;
    default:
        LOG_ERROR(Render_Vulkan, "Unknown DisplayOrientation: {}", orientation);
        break;
    }
    const u64 size = sizeof(ScreenRectVertex) * vertices.size();
    int passes = 1;
    std::vector<VertexBufferPointer> vertexBufferPointers(passes);
    for (auto& vbp : vertexBufferPointers){
        std::tie(vbp.data, vbp.offset, vbp.invalidate) = vertex_buffer.Map(size, 16);
        vertex_buffer.Commit(size);
    }

    std::vector<u32> screenids = {screen_id_l, screen_id_r};
    PrepareDrawFromScreenInfo(currentFrame, currentFramebufferLayout, present_pipelines[current_pipeline], screenids, 1);
    ApplySecondLayerOpacity(); // Apply the initial default opacity value; Needed to avoid flickering
    UpdateVertexBuffer(vertices, vertexBufferPointers[0]);
    draw_info.i_resolution = Common::MakeVec(static_cast<f32>(textureWidth), static_cast<f32>(textureHeight), 1.0f / static_cast<f32>(textureWidth), 1.0f / static_cast<f32>(textureHeight));
    draw_info.o_resolution = Common::MakeVec(w, h, 1.0f / w, 1.0f / h);
    draw_info.screen_id_l = screen_id_l;
    draw_info.screen_id_r = screen_id_r;
    Draw(vertexBufferPointers[0], draw_info);
}

void RendererVulkan::DrawTopScreen(const Layout::FramebufferLayout& layout,
                                   const Common::Rectangle<u32>& top_screen) {
    if (!layout.top_screen_enabled) {
        return;
    }
    int leftside, rightside;
    leftside = Settings::values.swap_eyes_3d.GetValue() ? 1 : 0;
    rightside = Settings::values.swap_eyes_3d.GetValue() ? 0 : 1;
    const float top_screen_left = static_cast<float>(top_screen.left);
    const float top_screen_top = static_cast<float>(top_screen.top);
    const float top_screen_width = static_cast<float>(top_screen.GetWidth());
    const float top_screen_height = static_cast<float>(top_screen.GetHeight());

    const auto orientation = layout.is_rotated ? Layout::DisplayOrientation::Landscape
                                               : Layout::DisplayOrientation::Portrait;
    switch (layout.render_3d_mode) {
    case Settings::StereoRenderOption::Off: {
        const int eye = static_cast<int>(Settings::values.mono_render_option.GetValue());
        DrawSingleScreen(eye, top_screen_left, top_screen_top, top_screen_width, top_screen_height,
                         orientation);
        break;
    }
    case Settings::StereoRenderOption::SideBySide: {
        DrawSingleScreen(leftside, top_screen_left / 2, top_screen_top, top_screen_width / 2,
                         top_screen_height, orientation);
        draw_info.layer = 1;
        clearingColorAttachment = false;
        DrawSingleScreen(rightside, static_cast<float>((top_screen_left / 2) + (layout.width / 2)),
                         top_screen_top, top_screen_width / 2, top_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::SideBySideFull: {
        DrawSingleScreen(leftside, top_screen_left, top_screen_top, top_screen_width,
                         top_screen_height, orientation);
        draw_info.layer = 1;
        clearingColorAttachment = false;
        DrawSingleScreen(rightside, top_screen_left + layout.width / 2, top_screen_top,
                         top_screen_width, top_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::CardboardVR: {
        DrawSingleScreen(leftside, top_screen_left, top_screen_top, top_screen_width,
                         top_screen_height, orientation);
        draw_info.layer = 1;
        clearingColorAttachment = false;
        DrawSingleScreen(
            rightside,
            static_cast<float>(layout.cardboard.top_screen_right_eye + (layout.width / 2)),
            top_screen_top, top_screen_width, top_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::Anaglyph:
    case Settings::StereoRenderOption::Interlaced:
    case Settings::StereoRenderOption::ReverseInterlaced: {
        DrawSingleScreenStereo(leftside, rightside, top_screen_left, top_screen_top,
                               top_screen_width, top_screen_height, orientation);
        break;
    }
    }
}

void RendererVulkan::DrawBottomScreen(const Layout::FramebufferLayout& layout,
                                      const Common::Rectangle<u32>& bottom_screen) {
    if (!layout.bottom_screen_enabled) {
        return;
    }

    const float bottom_screen_left = static_cast<float>(bottom_screen.left);
    const float bottom_screen_top = static_cast<float>(bottom_screen.top);
    const float bottom_screen_width = static_cast<float>(bottom_screen.GetWidth());
    const float bottom_screen_height = static_cast<float>(bottom_screen.GetHeight());

    const auto orientation = layout.is_rotated ? Layout::DisplayOrientation::Landscape
                                               : Layout::DisplayOrientation::Portrait;

    switch (layout.render_3d_mode) {
    case Settings::StereoRenderOption::Off: {
        DrawSingleScreen(2, bottom_screen_left, bottom_screen_top, bottom_screen_width,
                         bottom_screen_height, orientation);

        break;
    }
    case Settings::StereoRenderOption::SideBySide: // Bottom screen is identical on both sides
    {
        DrawSingleScreen(2, bottom_screen_left / 2, bottom_screen_top, bottom_screen_width / 2,
                         bottom_screen_height, orientation);
        draw_info.layer = 1;
        DrawSingleScreen(2, static_cast<float>((bottom_screen_left / 2) + (layout.width / 2)),
                         bottom_screen_top, bottom_screen_width / 2, bottom_screen_height,
                         orientation);
        break;
    }
    case Settings::StereoRenderOption::SideBySideFull: {
        DrawSingleScreen(2, bottom_screen_left, bottom_screen_top, bottom_screen_width,
                         bottom_screen_height, orientation);
        draw_info.layer = 1;
        DrawSingleScreen(2, bottom_screen_left + layout.width / 2, bottom_screen_top,
                         bottom_screen_width, bottom_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::CardboardVR: {
        DrawSingleScreen(2, bottom_screen_left, bottom_screen_top, bottom_screen_width,
                         bottom_screen_height, orientation);
        draw_info.layer = 1;
        DrawSingleScreen(
            2, static_cast<float>(layout.cardboard.bottom_screen_right_eye + (layout.width / 2)),
            bottom_screen_top, bottom_screen_width, bottom_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::Anaglyph:
    case Settings::StereoRenderOption::Interlaced:
    case Settings::StereoRenderOption::ReverseInterlaced: {
        DrawSingleScreenStereo(2, 2, bottom_screen_left, bottom_screen_top, bottom_screen_width,
                               bottom_screen_height, orientation);
        break;
    }
    }
}

void RendererVulkan::DrawScreens(Frame* frame, const Layout::FramebufferLayout& layout,
                                 bool flipped) {
    if (settings.bg_color_update_requested.exchange(false)) {
        clear_color.float32[0] = Settings::values.bg_red.GetValue();
        clear_color.float32[1] = Settings::values.bg_green.GetValue();
        clear_color.float32[2] = Settings::values.bg_blue.GetValue();
    }
    if (settings.shader_update_requested.exchange(false)) {
        ReloadPipeline(layout.render_3d_mode);
    }

    // Track Texture Changes
    currTopTextureWidth = static_cast<float>(screen_infos[0].texture.height * GetResolutionScaleFactor());
    currTopTextureHeight = static_cast<float>(screen_infos[0].texture.width * GetResolutionScaleFactor());
    currBottomTextureWidth = static_cast<float>(screen_infos[2].texture.height * GetResolutionScaleFactor());
    currBottomTextureHeight = static_cast<float>(screen_infos[2].texture.width * GetResolutionScaleFactor());
    if (currTopTextureWidth != prevTopTextureWidth || currTopTextureHeight != prevTopTextureHeight || currBottomTextureWidth != prevBottomTextureWidth || currBottomTextureHeight != prevBottomTextureHeight){
        AllocatePPTextures();
        CreatePPTextureFramebuffers();
        LOG_INFO(Render_Vulkan, "PrevTopTexture Res: {}x{}, CurrTopTexture Res: {}x{}, PrevBottomTexture Res: {}x{}, CurrBottomTexture Res: {}x{}", prevTopTextureWidth, prevTopTextureHeight, currTopTextureWidth, currTopTextureHeight, prevBottomTextureWidth, prevBottomTextureHeight, currBottomTextureWidth, currBottomTextureHeight);
    }
    prevTopTextureWidth = currTopTextureWidth;
    prevTopTextureHeight = currTopTextureHeight;
    prevBottomTextureWidth = currBottomTextureWidth;
    prevBottomTextureHeight = currBottomTextureHeight;

    //Track Layout Changes
    currOutputScreenRects[0] = layout.top_screen;
    currOutputScreenRects[1] = layout.bottom_screen;
    currOutputScreenRects[2] = layout.additional_screen;
    if (currOutputScreenRects[0] != prevOutputScreenRects[0] || currOutputScreenRects[1] != prevOutputScreenRects[1] || currOutputScreenRects[2] != prevOutputScreenRects[2]){
        AllocateOutputSizeTextures();
        CreateOutputSizeTextureFramebuffers();
    }
    prevOutputScreenRects[0] = currOutputScreenRects[0];
    prevOutputScreenRects[1] = currOutputScreenRects[1];
    prevOutputScreenRects[2] = currOutputScreenRects[2];

    currentFrame = frame;
    currentFramebufferLayout = layout;
    const auto& top_screen = layout.top_screen;
    const auto& bottom_screen = layout.bottom_screen;
    draw_info.modelview = MakeOrthographicMatrix(layout.width, layout.height);
    draw_info.layer = 0;

    clearingColorAttachment = true;
    applyingOpacity = true;
    if (!Settings::values.swap_screen.GetValue()) {
        drawingPrimaryScreen = true;
        DrawTopScreen(layout, top_screen);
        draw_info.layer = 0;
        drawingPrimaryScreen = false;
        usingTopOpacity = false;
        clearingColorAttachment = false;
        DrawBottomScreen(layout, bottom_screen);
    } else {
        drawingPrimaryScreen = true;
        DrawBottomScreen(layout, bottom_screen);
        draw_info.layer = 0;
        drawingPrimaryScreen = false;
        usingTopOpacity = true;
        clearingColorAttachment = false;
        DrawTopScreen(layout, top_screen);
    }

    applyingOpacity = false;
    if (layout.additional_screen_enabled) {
        const auto& additional_screen = layout.additional_screen;
        if (!Settings::values.swap_screen.GetValue()) {
            DrawTopScreen(layout, additional_screen);
        } else {
            DrawBottomScreen(layout, additional_screen);
        }
    }

    // Needs to be fixed
    // DrawCursor(layout);
}

void RendererVulkan::DrawCursor(const Layout::FramebufferLayout& layout) {
    const auto cursor = render_window.GetCursorInfo();
    if (!cursor.visible) {
        return;
    }

    const float buf_w = static_cast<float>(layout.width);
    const float buf_h = static_cast<float>(layout.height);

    // Convert from bottom-screen-local to layout-absolute, then to NDC
    const float abs_x = layout.bottom_screen.left + cursor.projected_x;
    const float abs_y = layout.bottom_screen.top + cursor.projected_y;
    const float cx = (abs_x / buf_w) * 2.0f - 1.0f;
    const float cy = (abs_y / buf_h) * 2.0f - 1.0f;
    const float ratio = static_cast<float>(layout.bottom_screen.GetHeight()) / 30.0f;
    const float rw = ratio / buf_w;
    const float rh = ratio / buf_h;

    // Bottom screen bounds in NDC
    const float bl = (layout.bottom_screen.left / buf_w) * 2.0f - 1.0f;
    const float bt = (layout.bottom_screen.top / buf_h) * 2.0f - 1.0f;
    const float br = (layout.bottom_screen.right / buf_w) * 2.0f - 1.0f;
    const float bb = (layout.bottom_screen.bottom / buf_h) * 2.0f - 1.0f;

    // Crosshair geometry clamped to bottom screen bounds
    const float vl = std::fmax(cx - rw / 5.0f, bl);
    const float vr = std::fmin(cx + rw / 5.0f, br);
    const float vt = std::fmax(cy - rh, bt);
    const float vb = std::fmin(cy + rh, bb);

    const float hl = std::fmax(cx - rw, bl);
    const float hr = std::fmin(cx + rw, br);
    const float ht = std::fmax(cy - rh / 5.0f, bt);
    const float hb = std::fmin(cy + rh / 5.0f, bb);

    // 12 vertices = 4 triangles (2 for vertical bar, 2 for horizontal bar)
    // clang-format off
    const float vertices[] = {
        // Vertical bar
        vl, vt,  vr, vt,  vr, vb,
        vl, vt,  vr, vb,  vl, vb,
        // Horizontal bar
        hl, ht,  hr, ht,  hr, hb,
        hl, ht,  hr, hb,  hl, hb,
    };
    // clang-format on

    const u64 size = sizeof(vertices);
    auto [data, offset, invalidate] = vertex_buffer.Map(size, 16);
    std::memcpy(data, vertices, size);
    vertex_buffer.Commit(size);

    scheduler.Record([this, offset = offset, pipeline = cursor_pipeline](vk::CommandBuffer cmdbuf) {
        cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);
        cmdbuf.bindVertexBuffers(0, vertex_buffer.Handle(), {0});
        const u32 first_vertex = static_cast<u32>(offset) / (sizeof(float) * 2);
        cmdbuf.draw(12, 1, first_vertex, 0);
    });
}

void RendererVulkan::SwapBuffers() {
    system.perf_stats->StartSwap();
    const Layout::FramebufferLayout& layout = render_window.GetFramebufferLayout();
    PrepareRendertarget();
    RenderScreenshot();
    RenderToWindow(main_present_window, layout, false);
#ifndef ANDROID
    if (Settings::values.layout_option.GetValue() == Settings::LayoutOption::SeparateWindows) {
        ASSERT(secondary_window);
        const auto& secondary_layout = secondary_window->GetFramebufferLayout();
        if (!secondary_present_window_ptr) {
            secondary_present_window_ptr = std::make_unique<PresentWindow>(
                *secondary_window, instance, scheduler, IsLowRefreshRate());
        }
        RenderToWindow(*secondary_present_window_ptr, secondary_layout, false);
        secondary_window->PollEvents();
    }
#endif

#ifdef ANDROID
    if (secondary_window) {
        const auto& secondary_layout = secondary_window->GetFramebufferLayout();
        if (!secondary_present_window_ptr) {
            secondary_present_window_ptr = std::make_unique<PresentWindow>(
                *secondary_window, instance, scheduler, IsLowRefreshRate());
        }
        RenderToWindow(*secondary_present_window_ptr, secondary_layout, false);
        secondary_window->PollEvents();
    }
#endif

    system.perf_stats->EndSwap();
    rasterizer.TickFrame();
    EndFrame();
}

void RendererVulkan::RenderScreenshot() {
    if (!settings.screenshot_requested.exchange(false)) {
        return;
    }

    if (!TryRenderScreenshotWithHostMemory()) {
        RenderScreenshotWithStagingCopy();
    }

    settings.screenshot_complete_callback(false);
}

void RendererVulkan::RenderScreenshotWithStagingCopy() {
    const vk::Device device = instance.GetDevice();

    const Layout::FramebufferLayout layout{settings.screenshot_framebuffer_layout};
    const u32 width = layout.width;
    const u32 height = layout.height;

    const vk::BufferCreateInfo staging_buffer_info = {
        .size = width * height * 4,
        .usage = vk::BufferUsageFlagBits::eTransferDst,
    };

    const VmaAllocationCreateInfo alloc_create_info = {
        .flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT |
                 VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        .requiredFlags = 0,
        .preferredFlags = 0,
        .pool = VK_NULL_HANDLE,
        .pUserData = nullptr,
    };

    VkBuffer unsafe_buffer{};
    VmaAllocation allocation{};
    VmaAllocationInfo alloc_info;
    VkBufferCreateInfo unsafe_buffer_info = static_cast<VkBufferCreateInfo>(staging_buffer_info);

    VkResult result = vmaCreateBuffer(instance.GetAllocator(), &unsafe_buffer_info,
                                      &alloc_create_info, &unsafe_buffer, &allocation, &alloc_info);
    if (result != VK_SUCCESS) [[unlikely]] {
        LOG_CRITICAL(Render_Vulkan, "Failed allocating texture with error {}", result);
        UNREACHABLE();
    }

    vk::Buffer staging_buffer{unsafe_buffer};

    Frame frame{};
    main_present_window.RecreateFrame(&frame, width, height);

    DrawScreens(&frame, layout, false);

    scheduler.Record(
        [width, height, source_image = frame.image, staging_buffer](vk::CommandBuffer cmdbuf) {
            const vk::ImageMemoryBarrier read_barrier = {
                .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
                .dstAccessMask = vk::AccessFlagBits::eTransferRead,
                .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
                .newLayout = vk::ImageLayout::eTransferSrcOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = source_image,
                .subresourceRange{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = VK_REMAINING_MIP_LEVELS,
                    .baseArrayLayer = 0,
                    .layerCount = VK_REMAINING_ARRAY_LAYERS,
                },
            };
            const vk::ImageMemoryBarrier write_barrier = {
                .srcAccessMask = vk::AccessFlagBits::eTransferRead,
                .dstAccessMask = vk::AccessFlagBits::eMemoryWrite,
                .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
                .newLayout = vk::ImageLayout::eTransferSrcOptimal,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = source_image,
                .subresourceRange{
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .baseMipLevel = 0,
                    .levelCount = VK_REMAINING_MIP_LEVELS,
                    .baseArrayLayer = 0,
                    .layerCount = VK_REMAINING_ARRAY_LAYERS,
                },
            };
            static constexpr vk::MemoryBarrier memory_write_barrier = {
                .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
                .dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite,
            };

            const vk::BufferImageCopy image_copy = {
                .bufferOffset = 0,
                .bufferRowLength = 0,
                .bufferImageHeight = 0,
                .imageSubresource =
                    {
                        .aspectMask = vk::ImageAspectFlagBits::eColor,
                        .mipLevel = 0,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
                .imageOffset = {0, 0, 0},
                .imageExtent = {width, height, 1},
            };

            cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                                   vk::PipelineStageFlagBits::eTransfer,
                                   vk::DependencyFlagBits::eByRegion, {}, {}, read_barrier);
            cmdbuf.copyImageToBuffer(source_image, vk::ImageLayout::eTransferSrcOptimal,
                                     staging_buffer, image_copy);
            cmdbuf.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands,
                vk::DependencyFlagBits::eByRegion, memory_write_barrier, {}, write_barrier);
        });

    // Ensure the copy is fully completed before saving the screenshot
    scheduler.Finish();

    // Copy backing image data to the QImage screenshot buffer
    std::memcpy(settings.screenshot_bits, alloc_info.pMappedData, staging_buffer_info.size);

    // Destroy allocated resources
    vmaDestroyBuffer(instance.GetAllocator(), staging_buffer, allocation);
    vmaDestroyImage(instance.GetAllocator(), frame.image, frame.allocation);
    device.destroyFramebuffer(frame.framebuffer);
    device.destroyImageView(frame.image_view);
}

bool RendererVulkan::TryRenderScreenshotWithHostMemory() {
    // If the host-memory import alignment matches the allocation granularity of the platform, then
    // the entire span of memory can be trivially imported
    const bool trivial_import =
        instance.IsExternalMemoryHostSupported() &&
        instance.GetMinImportedHostPointerAlignment() == Common::GetPageSize();
    if (!trivial_import) {
        return false;
    }

    const vk::Device device = instance.GetDevice();

    const Layout::FramebufferLayout layout{settings.screenshot_framebuffer_layout};
    const u32 width = layout.width;
    const u32 height = layout.height;

    // For a span of memory [x, x + s], import [AlignDown(x, alignment), AlignUp(x + s, alignment)]
    // and maintain an offset to the start of the data
    const u64 import_alignment = instance.GetMinImportedHostPointerAlignment();
    const uintptr_t address = reinterpret_cast<uintptr_t>(settings.screenshot_bits);
    void* aligned_pointer = reinterpret_cast<void*>(Common::AlignDown(address, import_alignment));
    const u64 offset = address % import_alignment;
    const u64 aligned_size = Common::AlignUp(offset + width * height * 4ull, import_alignment);

    // Buffer<->Image mapping for the imported imported buffer
    const vk::BufferImageCopy buffer_image_copy = {
        .bufferOffset = offset,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource =
            {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1},
    };

    const vk::MemoryHostPointerPropertiesEXT import_properties =
        device.getMemoryHostPointerPropertiesEXT(
            vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT, aligned_pointer);

    if (!import_properties.memoryTypeBits) {
        // Could not import memory
        return false;
    }

    const std::optional<u32> memory_type_index = FindMemoryType(
        instance.GetPhysicalDevice().getMemoryProperties(),
        vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
        import_properties.memoryTypeBits);

    if (!memory_type_index.has_value()) {
        // Could not find memory type index
        return false;
    }

    const vk::StructureChain<vk::MemoryAllocateInfo, vk::ImportMemoryHostPointerInfoEXT>
        allocation_chain = {
            vk::MemoryAllocateInfo{
                .allocationSize = aligned_size,
                .memoryTypeIndex = memory_type_index.value(),
            },
            vk::ImportMemoryHostPointerInfoEXT{
                .handleType = vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT,
                .pHostPointer = aligned_pointer,
            },
        };

    // Import host memory
    const vk::UniqueDeviceMemory imported_memory =
        device.allocateMemoryUnique(allocation_chain.get());

    const vk::StructureChain<vk::BufferCreateInfo, vk::ExternalMemoryBufferCreateInfo> buffer_info =
        {
            vk::BufferCreateInfo{
                .size = aligned_size,
                .usage = vk::BufferUsageFlagBits::eTransferDst,
                .sharingMode = vk::SharingMode::eExclusive,
            },
            vk::ExternalMemoryBufferCreateInfo{
                .handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eHostAllocationEXT,
            },
        };

    // Bind imported memory to buffer
    const vk::UniqueBuffer imported_buffer = device.createBufferUnique(buffer_info.get());
    device.bindBufferMemory(imported_buffer.get(), imported_memory.get(), 0);

    Frame frame{};
    main_present_window.RecreateFrame(&frame, width, height);

    DrawScreens(&frame, layout, false);

    scheduler.Record([buffer_image_copy, source_image = frame.image,
                      imported_buffer = imported_buffer.get()](vk::CommandBuffer cmdbuf) {
        const vk::ImageMemoryBarrier read_barrier = {
            .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
            .dstAccessMask = vk::AccessFlagBits::eTransferRead,
            .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
            .newLayout = vk::ImageLayout::eTransferSrcOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = source_image,
            .subresourceRange{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        };
        const vk::ImageMemoryBarrier write_barrier = {
            .srcAccessMask = vk::AccessFlagBits::eTransferRead,
            .dstAccessMask = vk::AccessFlagBits::eMemoryWrite,
            .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
            .newLayout = vk::ImageLayout::eTransferSrcOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = source_image,
            .subresourceRange{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        };
        static constexpr vk::MemoryBarrier memory_write_barrier = {
            .srcAccessMask = vk::AccessFlagBits::eMemoryWrite,
            .dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite,
        };

        cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                               vk::PipelineStageFlagBits::eTransfer,
                               vk::DependencyFlagBits::eByRegion, {}, {}, read_barrier);
        cmdbuf.copyImageToBuffer(source_image, vk::ImageLayout::eTransferSrcOptimal,
                                 imported_buffer, buffer_image_copy);
        cmdbuf.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eAllCommands,
            vk::DependencyFlagBits::eByRegion, memory_write_barrier, {}, write_barrier);
    });

    // Ensure the copy is fully completed before saving the screenshot
    scheduler.Finish();

    // Image data has been copied directly to host memory
    device.destroyFramebuffer(frame.framebuffer);
    device.destroyImageView(frame.image_view);

    return true;
}

void RendererVulkan::NotifySurfaceChanged(bool is_second_window) {
    if (is_second_window) {
        if (secondary_present_window_ptr) {
            secondary_present_window_ptr->NotifySurfaceChanged();
        }
    } else {
        main_present_window.NotifySurfaceChanged();
    }
}

} // namespace Vulkan
