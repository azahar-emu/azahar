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

const VkApplicationInfo* GetVulkanApplicationInfo() {
    static VkApplicationInfo app_info{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app_info.pApplicationName = "Azahar";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "Azahar";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    // Request Vulkan 1.1 for better compatibility (especially on Android)
    // Extensions can be used for features beyond 1.1
    app_info.apiVersion = VK_API_VERSION_1_1;
    return &app_info;
}

void AddExtensionIfAvailable(std::vector<const char*>& enabled_exts,
                             const std::vector<VkExtensionProperties>& available_exts,
                             const char* ext_name) {
    // Check if already in the list
    for (const char* ext : enabled_exts) {
        if (ext && !strcmp(ext, ext_name)) {
            return; // Already enabled
        }
    }

    // Check if available
    for (const auto& ext : available_exts) {
        if (!strcmp(ext.extensionName, ext_name)) {
            enabled_exts.push_back(ext_name);
            LOG_INFO(Render_Vulkan, "Enabling Vulkan extension: {}", ext_name);
            return;
        }
    }

    LOG_DEBUG(Render_Vulkan, "Vulkan extension {} not available", ext_name);
}

bool CreateVulkanDevice(struct retro_vulkan_context* context, VkInstance instance,
                        VkPhysicalDevice gpu, VkSurfaceKHR surface,
                        PFN_vkGetInstanceProcAddr get_instance_proc_addr,
                        const char** required_device_extensions,
                        unsigned num_required_device_extensions,
                        const char** required_device_layers, unsigned num_required_device_layers,
                        const VkPhysicalDeviceFeatures* required_features) {

    LOG_INFO(Render_Vulkan, "CreateDevice callback invoked - negotiating Vulkan device creation");

    // Get available extensions for this physical device
    uint32_t ext_count = 0;
    PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties =
        (PFN_vkEnumerateDeviceExtensionProperties)get_instance_proc_addr(
            instance, "vkEnumerateDeviceExtensionProperties");

    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> available_exts(ext_count);
    if (ext_count > 0) {
        vkEnumerateDeviceExtensionProperties(gpu, nullptr, &ext_count, available_exts.data());
    }

    // Start with frontend's required extensions
    std::vector<const char*> enabled_exts;
    enabled_exts.reserve(num_required_device_extensions + 10);
    for (unsigned i = 0; i < num_required_device_extensions; i++) {
        if (required_device_extensions[i]) {
            enabled_exts.push_back(required_device_extensions[i]);
        }
    }

    // Add extensions we want (if available)
    AddExtensionIfAvailable(enabled_exts, available_exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    AddExtensionIfAvailable(enabled_exts, available_exts, VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME);
    AddExtensionIfAvailable(enabled_exts, available_exts,
                            VK_EXT_SHADER_STENCIL_EXPORT_EXTENSION_NAME);
    AddExtensionIfAvailable(enabled_exts, available_exts,
                            VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
    AddExtensionIfAvailable(enabled_exts, available_exts, VK_EXT_TOOLING_INFO_EXTENSION_NAME);

    // These are beneficial but blacklisted on some platforms due to driver bugs
    // For now, let the Instance class handle these decisions
    // AddExtensionIfAvailable(enabled_exts, available_exts,
    // VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
    // AddExtensionIfAvailable(enabled_exts, available_exts,
    // VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);

    // Merge frontend's required features with our baseline
    VkPhysicalDeviceFeatures merged_features{};
    if (required_features) {
        // Copy all frontend requirements
        for (unsigned i = 0; i < sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32); i++) {
            if (reinterpret_cast<const VkBool32*>(required_features)[i]) {
                reinterpret_cast<VkBool32*>(&merged_features)[i] = VK_TRUE;
            }
        }
    }

    // Request features we need (these will be OR'd with frontend requirements)
    // The Instance class will validate these against actual device capabilities
    merged_features.geometryShader = VK_TRUE;    // Used for certain rendering effects
    merged_features.logicOp = VK_TRUE;           // Used for blending modes
    merged_features.samplerAnisotropy = VK_TRUE; // Used for texture filtering

    // Find queue family with graphics support
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)get_instance_proc_addr(
            instance, "vkGetPhysicalDeviceQueueFamilyProperties");

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &queue_family_count, queue_families.data());

    uint32_t graphics_queue_family = VK_QUEUE_FAMILY_IGNORED;
    for (uint32_t i = 0; i < queue_family_count; i++) {
        if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphics_queue_family = i;
            break;
        }
    }

    if (graphics_queue_family == VK_QUEUE_FAMILY_IGNORED) {
        LOG_CRITICAL(Render_Vulkan, "No graphics queue family found!");
        return false;
    }

    // Create device
    const float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = graphics_queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = static_cast<uint32_t>(enabled_exts.size());
    device_info.ppEnabledExtensionNames = enabled_exts.data();
    device_info.enabledLayerCount = num_required_device_layers;
    device_info.ppEnabledLayerNames = required_device_layers;
    device_info.pEnabledFeatures = &merged_features;

    PFN_vkCreateDevice vkCreateDevice =
        (PFN_vkCreateDevice)get_instance_proc_addr(instance, "vkCreateDevice");

    VkDevice device = VK_NULL_HANDLE;
    VkResult result = vkCreateDevice(gpu, &device_info, nullptr, &device);
    if (result != VK_SUCCESS) {
        LOG_CRITICAL(Render_Vulkan, "vkCreateDevice failed: {}", static_cast<int>(result));
        return false;
    }

    // Get the queue
    PFN_vkGetDeviceQueue vkGetDeviceQueue =
        (PFN_vkGetDeviceQueue)get_instance_proc_addr(instance, "vkGetDeviceQueue");

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, graphics_queue_family, 0, &queue);

    // Fill in the context for the frontend
    context->gpu = gpu;
    context->device = device;
    context->queue = queue;
    context->queue_family_index = graphics_queue_family;
    context->presentation_queue = queue; // Same queue for LibRetro
    context->presentation_queue_family_index = graphics_queue_family;

    LOG_INFO(Render_Vulkan,
             "Vulkan device created successfully via negotiation interface (GPU: {}, Queue "
             "Family: {})",
             static_cast<void*>(gpu), graphics_queue_family);

    return true;
}

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

constexpr u64 FENCE_RESERVE = 8;

MasterSemaphoreLibRetro::MasterSemaphoreLibRetro(const Instance& instance_) : instance{instance_} {
    const vk::Device device{instance.GetDevice()};
    // Pre-allocate fence pool
    for (u64 i = 0; i < FENCE_RESERVE; i++) {
        free_queue.push_back(device.createFence({}));
    }
    // Start background wait thread
    wait_thread = std::jthread([this](std::stop_token token) { WaitThread(token); });
}

MasterSemaphoreLibRetro::~MasterSemaphoreLibRetro() {
    // wait_thread will be automatically stopped by jthread destructor
    // Clean up remaining fences
    const vk::Device device{instance.GetDevice()};
    for (const auto& fence : free_queue) {
        device.destroyFence(fence);
    }
}

void MasterSemaphoreLibRetro::Refresh() {}

void MasterSemaphoreLibRetro::Wait(u64 tick) {
    std::unique_lock lock{free_mutex};
    free_cv.wait(lock, [this, tick] { return gpu_tick.load(std::memory_order_relaxed) >= tick; });
}

void MasterSemaphoreLibRetro::SubmitWork(vk::CommandBuffer cmdbuf, vk::Semaphore wait,
                                         vk::Semaphore signal, u64 signal_value) {
    if (!vulkan_intf) {
        LOG_ERROR(Render_Vulkan, "LibRetro Vulkan interface not available for command submission");
        return;
    }

    cmdbuf.end();

    // Get a fence from the pool
    const vk::Fence fence = GetFreeFence();

    // Strip semaphores - RetroArch handles frame sync, we track resources internally
    const vk::SubmitInfo submit_info = {
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1u,
        .pCommandBuffers = &cmdbuf,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };

    // Use LibRetro's queue coordination
    if (vulkan_intf->lock_queue) {
        vulkan_intf->lock_queue(vulkan_intf->handle);
    }

    try {
        // Submit with fence for internal resource tracking
        vk::Queue queue{vulkan_intf->queue};
        queue.submit(submit_info, fence);

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

    // Enqueue fence for wait thread to process
    {
        std::scoped_lock lock{wait_mutex};
        wait_queue.emplace(fence, signal_value);
        wait_cv.notify_one();
    }
}

void MasterSemaphoreLibRetro::WaitThread(std::stop_token token) {
    const vk::Device device{instance.GetDevice()};

    while (!token.stop_requested()) {
        vk::Fence fence;
        u64 signal_value;

        // Wait for work
        {
            std::unique_lock lock{wait_mutex};
            Common::CondvarWait(wait_cv, lock, token, [this] { return !wait_queue.empty(); });
            if (token.stop_requested()) {
                return;
            }
            std::tie(fence, signal_value) = wait_queue.front();
            wait_queue.pop();
        }

        // Wait for fence (blocks only this background thread)
        const vk::Result result = device.waitForFences(fence, true, UINT64_MAX);
        if (result != vk::Result::eSuccess) {
            LOG_ERROR(Render_Vulkan, "Fence wait failed: {}", vk::to_string(result));
        }

        // Reset fence and return to pool
        device.resetFences(fence);

        // Update GPU tick - signals main thread's Wait()
        gpu_tick.store(signal_value, std::memory_order_release);

        // Return fence to pool
        {
            std::scoped_lock lock{free_mutex};
            free_queue.push_back(fence);
            free_cv.notify_all();
        }
    }
}

vk::Fence MasterSemaphoreLibRetro::GetFreeFence() {
    std::scoped_lock lock{free_mutex};
    if (free_queue.empty()) {
        // Pool exhausted - create new fence
        return instance.GetDevice().createFence({});
    }

    const vk::Fence fence = free_queue.front();
    free_queue.pop_front();
    return fence;
}

// Factory function for scheduler to create LibRetro MasterSemaphore
std::unique_ptr<MasterSemaphore> CreateLibRetroMasterSemaphore(const Instance& instance) {
    return std::make_unique<MasterSemaphoreLibRetro>(instance);
}

} // namespace Vulkan
