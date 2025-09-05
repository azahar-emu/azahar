// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>
#include <boost/container/static_vector.hpp>
#include <fmt/format.h>

#include "citra_libretro/environment.h"
#include "citra_libretro/libretro_vk.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/frontend/emu_window.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

#include <vk_mem_alloc.h>

static const struct retro_hw_render_interface_vulkan* vulkan_intf;

namespace LibRetro {

void VulkanResetContext() {
    LibRetro::GetHWRenderInterface((void**)&vulkan_intf);

    // Initialize dispatcher with LibRetro's function pointers
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vulkan_intf->get_instance_proc_addr);

    vk::Instance vk_instance{vulkan_intf->instance};
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vk_instance);
}

} // namespace LibRetro

namespace Vulkan {

std::shared_ptr<Common::DynamicLibrary> OpenLibrary(
    [[maybe_unused]] Frontend::GraphicsContext* context) {
    // the frontend takes care of this, we'll get the instance later
    return std::make_shared<Common::DynamicLibrary>();
}

vk::SurfaceKHR CreateSurface(vk::Instance instance, const Frontend::EmuWindow& emu_window) {
    // LibRetro cores don't use surfaces - we render to our own output texture
    // This function should not be called in LibRetro mode
    LOG_WARNING(Render_Vulkan, "CreateSurface called in LibRetro mode - this should not happen");
    return VK_NULL_HANDLE;
}

vk::UniqueInstance CreateInstance([[maybe_unused]] const Common::DynamicLibrary& library,
                                  [[maybe_unused]] Frontend::WindowSystemType window_type,
                                  [[maybe_unused]] bool enable_validation,
                                  [[maybe_unused]] bool dump_command_buffers) {
    // LibRetro cores don't create instances - frontend handles this
    LOG_WARNING(Render_Vulkan, "CreateInstance called in LibRetro mode - this should not happen");
    return vk::UniqueInstance{};
}

DebugCallback CreateDebugCallback(vk::Instance instance, bool& debug_utils_supported) {
    // LibRetro handles debugging, return empty callback
    debug_utils_supported = false;
    return {};
}

LibRetroVKInstance::LibRetroVKInstance(Frontend::EmuWindow& window,
                                       [[maybe_unused]] u32 physical_device_index)
    : Instance(Instance::NoInit{}) {
    // Ensure LibRetro interface is available
    if (!vulkan_intf) {
        LOG_CRITICAL(Render_Vulkan, "LibRetro Vulkan interface not initialized!");
        throw std::runtime_error("LibRetro Vulkan interface not available");
    }

    // Initialize basic Vulkan objects from LibRetro
    physical_device = vulkan_intf->gpu;
    if (!physical_device) {
        LOG_CRITICAL(Render_Vulkan, "LibRetro provided invalid physical device!");
        throw std::runtime_error("Invalid physical device from LibRetro");
    }

    // Get device properties and features
    properties = physical_device.getProperties();

    const std::vector extensions = physical_device.enumerateDeviceExtensionProperties();
    available_extensions.reserve(extensions.size());
    for (const auto& extension : extensions) {
        available_extensions.emplace_back(extension.extensionName.data());
    }

    // Get queues from LibRetro
    graphics_queue = vulkan_intf->queue;
    queue_family_index = vulkan_intf->queue_index;
    present_queue = graphics_queue; // Same queue for LibRetro

    if (!graphics_queue) {
        LOG_CRITICAL(Render_Vulkan, "LibRetro provided invalid graphics queue!");
        throw std::runtime_error("Invalid graphics queue from LibRetro");
    }

    // Initialize Vulkan HPP dispatcher with LibRetro's device
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Device{vulkan_intf->device});

    // Now run device capability detection with dispatcher initialized
    CreateDevice(true);

    // LibRetro-specific: Validate function pointers are actually available
    // LibRetro's device may not have loaded all extension functions even if extensions are
    // available
    if (extended_dynamic_state) {
        if (!VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdSetCullModeEXT ||
            !VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdSetDepthTestEnableEXT ||
            !VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdSetDepthWriteEnableEXT ||
            !VULKAN_HPP_DEFAULT_DISPATCHER.vkCmdSetFrontFaceEXT) {
            LOG_WARNING(Render_Vulkan, "Extended dynamic state function pointers not available in "
                                       "LibRetro context, disabling");
            extended_dynamic_state = false;
        }
    }

    if (timeline_semaphores) {
        if (!VULKAN_HPP_DEFAULT_DISPATCHER.vkGetSemaphoreCounterValueKHR) {
            LOG_WARNING(Render_Vulkan, "Timeline semaphore function pointers not available in "
                                       "LibRetro context, disabling");
            timeline_semaphores = false;
        }
    }

    // Initialize subsystems
    CreateAllocator();
    CreateFormatTable();
    CollectToolingInfo();
    CreateCustomFormatTable();
    CreateAttribTable();

    LOG_INFO(Render_Vulkan, "LibRetro Vulkan Instance initialized successfully");
    LOG_INFO(Render_Vulkan, "Device: {} ({})", properties.deviceName.data(), GetVendorName());
    LOG_INFO(Render_Vulkan, "Driver: {}", GetDriverVersionName());
}

vk::Instance LibRetroVKInstance::GetInstance() const {
    return vk::Instance{vulkan_intf->instance};
}

vk::Device LibRetroVKInstance::GetDevice() const {
    return vk::Device{vulkan_intf->device};
}

// ============================================================================
// PresentWindow Implementation (LibRetro version)
// ============================================================================

PresentWindow::PresentWindow(Frontend::EmuWindow& emu_window_, const Instance& instance_,
                             Scheduler& scheduler_, [[maybe_unused]] bool low_refresh_rate)
    : emu_window{emu_window_}, instance{instance_}, scheduler{scheduler_},
      graphics_queue{instance.GetGraphicsQueue()} {
    const vk::Device device = instance.GetDevice();

    LOG_INFO(Render_Vulkan, "Initializing LibRetro PresentWindow");

    // Create command pool for frame operations
    const vk::CommandPoolCreateInfo pool_info = {
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
                 vk::CommandPoolCreateFlagBits::eTransient,
        .queueFamilyIndex = instance.GetGraphicsQueueFamilyIndex(),
    };
    command_pool = device.createCommandPool(pool_info);

    // Create render pass for LibRetro output
    present_renderpass = CreateRenderpass();

    // Start with initial dimensions from layout
    const auto& layout = emu_window.GetFramebufferLayout();
    CreateOutputTexture(layout.width, layout.height);
    CreateFrameResources();

    LOG_INFO(Render_Vulkan, "LibRetro PresentWindow initialized with {}x{}", layout.width,
             layout.height);
}

PresentWindow::~PresentWindow() {
    const vk::Device device = instance.GetDevice();

    LOG_DEBUG(Render_Vulkan, "Destroying LibRetro PresentWindow");

    // Wait for any pending operations
    WaitPresent();
    device.waitIdle();

    // Destroy frame resources
    DestroyFrameResources();

    // Destroy output texture
    DestroyOutputTexture();

    // Destroy Vulkan objects
    if (command_pool) {
        device.destroyCommandPool(command_pool);
    }
    if (present_renderpass) {
        device.destroyRenderPass(present_renderpass);
    }
}

void PresentWindow::CreateOutputTexture(u32 width, u32 height) {
    if (width == 0 || height == 0) {
        LOG_ERROR(Render_Vulkan, "Invalid output texture dimensions: {}x{}", width, height);
        return;
    }

    // Destroy existing texture if dimensions changed
    if (output_image && (output_width != width || output_height != height)) {
        DestroyOutputTexture();
    }

    // Skip if already created with correct dimensions
    if (output_image && output_width == width && output_height == height) {
        return;
    }

    const vk::Device device = instance.GetDevice();
    output_width = width;
    output_height = height;

    // Create output image with LibRetro requirements
    const vk::ImageCreateInfo image_info = {
        .imageType = vk::ImageType::e2D,
        .format = output_format,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eColorAttachment | // For rendering
                 vk::ImageUsageFlagBits::eTransferSrc |     // Required by LibRetro
                 vk::ImageUsageFlagBits::eSampled |         // Required by LibRetro
                 vk::ImageUsageFlagBits::eTransferDst,      // For clearing
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    };

    // Create image with VMA - using budget-aware allocation like standalone version
    VmaAllocationCreateInfo alloc_info = {};
    alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    alloc_info.flags = VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;

    VkImage vk_image;
    const VkResult result = vmaCreateImage(instance.GetAllocator(),
                                           reinterpret_cast<const VkImageCreateInfo*>(&image_info),
                                           &alloc_info, &vk_image, &output_allocation, nullptr);

    if (result != VK_SUCCESS) {
        LOG_CRITICAL(Render_Vulkan, "Failed to create output image: {}", static_cast<int>(result));
        throw std::runtime_error("Failed to create LibRetro output texture");
    }

    output_image = vk::Image{vk_image};

    // Create image view
    output_view_create_info = {
        .image = output_image,
        .viewType = vk::ImageViewType::e2D,
        .format = output_format,
        .components =
            {
                .r = vk::ComponentSwizzle::eIdentity,
                .g = vk::ComponentSwizzle::eIdentity,
                .b = vk::ComponentSwizzle::eIdentity,
                .a = vk::ComponentSwizzle::eIdentity,
            },
        .subresourceRange =
            {
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };
    output_image_view = device.createImageView(output_view_create_info);

    LOG_DEBUG(Render_Vulkan, "Created LibRetro output texture: {}x{}", width, height);
}

void PresentWindow::DestroyOutputTexture() {
    if (!output_image) {
        return;
    }

    const vk::Device device = instance.GetDevice();

    if (output_image_view) {
        device.destroyImageView(output_image_view);
        output_image_view = nullptr;
    }

    if (output_allocation) {
        vmaDestroyImage(instance.GetAllocator(), static_cast<VkImage>(output_image),
                        output_allocation);
        output_allocation = {};
    }

    output_image = nullptr;
    output_width = 0;
    output_height = 0;
}

vk::RenderPass PresentWindow::CreateRenderpass() {
    const vk::AttachmentDescription color_attachment = {
        .format = output_format,
        .samples = vk::SampleCountFlagBits::e1,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = vk::ImageLayout::eUndefined,
        .finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal, // Ready for LibRetro
    };

    const vk::AttachmentReference color_ref = {
        .attachment = 0,
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
    };

    const vk::SubpassDescription subpass = {
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_ref,
    };

    const vk::SubpassDependency dependency = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .srcAccessMask = {},
        .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
    };

    const vk::RenderPassCreateInfo renderpass_info = {
        .attachmentCount = 1,
        .pAttachments = &color_attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = 1,
        .pDependencies = &dependency,
    };

    return instance.GetDevice().createRenderPass(renderpass_info);
}

void PresentWindow::CreateFrameResources() {
    const vk::Device device = instance.GetDevice();
    const u32 frame_count = 2; // Double buffering for LibRetro

    // Destroy existing frames
    DestroyFrameResources();

    // Create frame pool
    frame_pool.resize(frame_count);

    // Allocate command buffers
    const vk::CommandBufferAllocateInfo alloc_info = {
        .commandPool = command_pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = frame_count,
    };
    const std::vector command_buffers = device.allocateCommandBuffers(alloc_info);

    // Initialize frames
    for (u32 i = 0; i < frame_count; i++) {
        Frame& frame = frame_pool[i];
        frame.width = output_width;
        frame.height = output_height;
        frame.image = output_image; // All frames use the same output texture
        frame.image_view = output_image_view;
        frame.allocation = {}; // VMA allocation handled separately
        frame.cmdbuf = command_buffers[i];
        frame.render_ready = device.createSemaphore({});
        frame.present_done = device.createFence({.flags = vk::FenceCreateFlagBits::eSignaled});

        // Create framebuffer for this frame
        const vk::FramebufferCreateInfo fb_info = {
            .renderPass = present_renderpass,
            .attachmentCount = 1,
            .pAttachments = &output_image_view,
            .width = output_width,
            .height = output_height,
            .layers = 1,
        };
        frame.framebuffer = device.createFramebuffer(fb_info);
    }

    LOG_DEBUG(Render_Vulkan, "Created {} frame resources for LibRetro", frame_count);
}

void PresentWindow::DestroyFrameResources() {
    if (frame_pool.empty()) {
        return;
    }

    const vk::Device device = instance.GetDevice();

    for (auto& frame : frame_pool) {
        if (frame.framebuffer) {
            device.destroyFramebuffer(frame.framebuffer);
        }
        if (frame.render_ready) {
            device.destroySemaphore(frame.render_ready);
        }
        if (frame.present_done) {
            device.destroyFence(frame.present_done);
        }
    }

    frame_pool.clear();
    current_frame_index = 0;
}

Frame* PresentWindow::GetRenderFrame() {
    if (frame_pool.empty()) {
        LOG_ERROR(Render_Vulkan, "No frames available in LibRetro PresentWindow");
        return nullptr;
    }

    // RetroArch may not call context_reset during fullscreen toggle, leaving us
    // with a stale interface pointer that can crash
    const struct retro_hw_render_interface_vulkan* current_intf = nullptr;
    if (!LibRetro::GetHWRenderInterface((void**)&current_intf) || !current_intf) {
        LOG_ERROR(Render_Vulkan, "Failed to get current Vulkan interface");
        return &frame_pool[current_frame_index];
    }

    // Update global interface if it changed
    if (current_intf != vulkan_intf) {
        LOG_INFO(Render_Vulkan, "Vulkan interface changed during runtime from {} to {}",
                 static_cast<const void*>(vulkan_intf), static_cast<const void*>(current_intf));
        vulkan_intf = current_intf;
    }

    // LibRetro synchronization: Use LibRetro's wait mechanism instead of fences
    if (vulkan_intf && vulkan_intf->wait_sync_index && vulkan_intf->handle) {
        vulkan_intf->wait_sync_index(vulkan_intf->handle);
    }

    // Use LibRetro's sync index for frame selection if available
    u32 frame_index = current_frame_index;
    if (vulkan_intf && vulkan_intf->get_sync_index && vulkan_intf->handle) {
        LOG_TRACE(Render_Vulkan, "Calling get_sync_index with handle: {}",
                  static_cast<void*>(vulkan_intf->handle));

        const u32 sync_index = vulkan_intf->get_sync_index(vulkan_intf->handle);
        frame_index = sync_index % frame_pool.size();
        LOG_TRACE(Render_Vulkan, "LibRetro sync index: {}, using frame: {}", sync_index,
                  frame_index);
    }

    return &frame_pool[frame_index];
}

void PresentWindow::RecreateFrame(Frame* frame, u32 width, u32 height) {
    if (!frame) {
        LOG_ERROR(Render_Vulkan, "Invalid frame for recreation");
        return;
    }

    if (frame->width == width && frame->height == height) {
        return; // No change needed
    }

    LOG_DEBUG(Render_Vulkan, "Recreating LibRetro frame: {}x{} -> {}x{}", frame->width,
              frame->height, width, height);

    // Wait for frame to be idle
    const vk::Device device = instance.GetDevice();
    [[maybe_unused]] const vk::Result wait_result =
        device.waitForFences(frame->present_done, VK_TRUE, UINT64_MAX);

    // Recreate output texture with new dimensions
    CreateOutputTexture(width, height);

    // Recreate frame resources
    CreateFrameResources();

    LOG_INFO(Render_Vulkan, "LibRetro frame recreated for {}x{}", width, height);
}

void PresentWindow::Present(Frame* frame) {
    if (!frame) {
        LOG_ERROR(Render_Vulkan, "Cannot present null frame");
        return;
    }

    if (!vulkan_intf) {
        LOG_ERROR(Render_Vulkan, "LibRetro Vulkan interface not available for presentation");
        return;
    }

    // CRITICAL: Use persistent struct to avoid stack lifetime issues!
    // RetroArch may cache this pointer for frame duping during pause
    persistent_libretro_image.image_view = static_cast<VkImageView>(frame->image_view);
    persistent_libretro_image.image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    persistent_libretro_image.create_info =
        static_cast<VkImageViewCreateInfo>(output_view_create_info);

    vulkan_intf->set_image(vulkan_intf->handle, &persistent_libretro_image, 0, nullptr,
                           instance.GetGraphicsQueueFamilyIndex());

    // Call EmuWindow SwapBuffers to trigger LibRetro video frame submission
    emu_window.SwapBuffers();

    // LibRetro manages frame indices via sync_index, so we don't manually increment
    // current_frame_index = (current_frame_index + 1) % frame_pool.size();

    LOG_TRACE(Render_Vulkan, "Frame presented to LibRetro: {}x{}", frame->width, frame->height);
}

void PresentWindow::WaitPresent() {
    if (frame_pool.empty()) {
        return;
    }

    const vk::Device device = instance.GetDevice();

    // Wait for all frames to complete
    std::vector<vk::Fence> fences;
    fences.reserve(frame_pool.size());

    for (const auto& frame : frame_pool) {
        fences.push_back(frame.present_done);
    }

    if (!fences.empty()) {
        [[maybe_unused]] const vk::Result wait_result =
            device.waitForFences(fences, VK_TRUE, UINT64_MAX);
    }
}

void PresentWindow::NotifySurfaceChanged() {
    // LibRetro doesn't use surfaces, so this is a no-op
    LOG_DEBUG(Render_Vulkan, "Surface change notification ignored in LibRetro mode");
}

// ============================================================================
// MasterSemaphoreLibRetro Implementation
// ============================================================================

MasterSemaphoreLibRetro::MasterSemaphoreLibRetro(const Instance& instance) {
    // No internal synchronization objects needed - RetroArch handles everything
}

MasterSemaphoreLibRetro::~MasterSemaphoreLibRetro() = default;

void MasterSemaphoreLibRetro::Refresh() {
    // No internal state to refresh - RetroArch manages synchronization
    // Simply advance our tick counter to match submissions
    gpu_tick.store(current_tick.load(std::memory_order_acquire), std::memory_order_release);
}

void MasterSemaphoreLibRetro::Wait(u64 tick) {
    // No waiting needed - RetroArch handles synchronization through its own mechanisms
    // We trust that RetroArch's frame pacing will handle synchronization properly
    // Simply mark the tick as completed
    gpu_tick.store(std::max(gpu_tick.load(std::memory_order_acquire), tick),
                   std::memory_order_release);
}

void MasterSemaphoreLibRetro::SubmitWork(vk::CommandBuffer cmdbuf, vk::Semaphore wait,
                                         vk::Semaphore signal, u64 signal_value) {
    if (!vulkan_intf) {
        LOG_ERROR(Render_Vulkan, "LibRetro Vulkan interface not available for command submission");
        return;
    }

    cmdbuf.end();

    // CRITICAL: Following PPSSPP pattern - strip out ALL semaphores!
    // RetroArch handles synchronization entirely through its own mechanisms
    const vk::SubmitInfo submit_info = {
        .waitSemaphoreCount = 0, // No wait semaphores - RetroArch manages this
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1u,
        .pCommandBuffers = &cmdbuf,
        .signalSemaphoreCount = 0, // No signal semaphores - RetroArch manages this
        .pSignalSemaphores = nullptr,
    };

    // Use LibRetro's queue coordination directly
    if (vulkan_intf->lock_queue) {
        vulkan_intf->lock_queue(vulkan_intf->handle);
    }

    try {
        // Submit without fence or semaphores - RetroArch handles all synchronization
        vk::Queue queue{vulkan_intf->queue};
        queue.submit(submit_info);

        if (vulkan_intf->unlock_queue) {
            vulkan_intf->unlock_queue(vulkan_intf->handle);
        }
    } catch (vk::DeviceLostError& err) {
        if (vulkan_intf->unlock_queue) {
            vulkan_intf->unlock_queue(vulkan_intf->handle);
        }
        UNREACHABLE_MSG("Device lost during submit: {}", err.what());
    } catch (...) {
        if (vulkan_intf->unlock_queue) {
            vulkan_intf->unlock_queue(vulkan_intf->handle);
        }
        throw;
    }

    // Mark the work as completed immediately - RetroArch handles real synchronization
    gpu_tick.store(signal_value, std::memory_order_release);
}

// Factory function for scheduler to create LibRetro MasterSemaphore
std::unique_ptr<MasterSemaphore> CreateLibRetroMasterSemaphore(const Instance& instance) {
    return std::make_unique<MasterSemaphoreLibRetro>(instance);
}

} // namespace Vulkan
