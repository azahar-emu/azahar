// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <variant>
#include <fmt/format.h>

#include "common/common_types.h"
#include "common/dynamic_library/dynamic_library.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_master_semaphore.h"
#include "video_core/renderer_vulkan/vk_platform.h"

#include "libretro_vulkan.h"

VK_DEFINE_HANDLE(VmaAllocation)

namespace LibRetro {

extern void VulkanResetContext();

} // namespace LibRetro

namespace Vulkan {

class LibRetroVKInstance : public Instance {
public:
    explicit LibRetroVKInstance(Frontend::EmuWindow& window, u32 physical_device_index);

    /// Returns the Vulkan instance
    vk::Instance GetInstance() const override;

    /// Returns the Vulkan device
    vk::Device GetDevice() const override;
};

class Scheduler;
class RenderManager;
class MasterSemaphore;

/// LibRetro-specific MasterSemaphore implementation
class MasterSemaphoreLibRetro : public MasterSemaphore {
public:
    explicit MasterSemaphoreLibRetro(const Instance& instance);
    ~MasterSemaphoreLibRetro() override;

    void Refresh() override;
    void Wait(u64 tick) override;
    void SubmitWork(vk::CommandBuffer cmdbuf, vk::Semaphore wait, vk::Semaphore signal,
                    u64 signal_value) override;
};

/// Factory function for scheduler to create LibRetro MasterSemaphore
std::unique_ptr<MasterSemaphore> CreateLibRetroMasterSemaphore(const Instance& instance);

struct Frame {
    u32 width;
    u32 height;
    VmaAllocation allocation;
    vk::Framebuffer framebuffer;
    vk::Image image;
    vk::ImageView image_view;
    vk::Semaphore render_ready;
    vk::Fence present_done;
    vk::CommandBuffer cmdbuf;
};

/// LibRetro-specific PresentWindow implementation (same interface as desktop version)
class PresentWindow final {
public:
    explicit PresentWindow(Frontend::EmuWindow& emu_window, const Instance& instance,
                           Scheduler& scheduler, bool low_refresh_rate);
    ~PresentWindow();

    /// Waits for all queued frames to finish presenting.
    void WaitPresent();

    /// Returns the last used render frame.
    Frame* GetRenderFrame();

    /// Recreates the render frame to match provided parameters.
    void RecreateFrame(Frame* frame, u32 width, u32 height);

    /// Queues the provided frame for presentation.
    void Present(Frame* frame);

    /// This is called to notify the rendering backend of a surface change
    void NotifySurfaceChanged();

    [[nodiscard]] vk::RenderPass Renderpass() const noexcept {
        return present_renderpass;
    }

    u32 ImageCount() const noexcept {
        return static_cast<u32>(frame_pool.size());
    }

private:
    /// Creates the render pass for LibRetro output
    vk::RenderPass CreateRenderpass();

    /// Creates output texture for LibRetro submission
    void CreateOutputTexture(u32 width, u32 height);

    /// Destroys current output texture
    void DestroyOutputTexture();

    /// Creates frame resources
    void CreateFrameResources();

    /// Destroys frame resources
    void DestroyFrameResources();

private:
    Frontend::EmuWindow& emu_window;
    const Instance& instance;
    [[maybe_unused]] Scheduler& scheduler;

    // LibRetro output texture (replaces swapchain)
    vk::Image output_image{};
    vk::ImageView output_image_view{};
    VmaAllocation output_allocation{};
    vk::Format output_format{vk::Format::eR8G8B8A8Unorm};
    vk::ImageViewCreateInfo output_view_create_info{};

    // Frame management
    vk::RenderPass present_renderpass{};
    vk::CommandPool command_pool{};
    std::vector<Frame> frame_pool;
    u32 current_frame_index{0};

    // Current output dimensions
    u32 output_width{0};
    u32 output_height{0};

    // Vulkan objects
    vk::Queue graphics_queue{};

    // Persistent LibRetro image descriptor, must persist across frames for RetroArch frame duping
    // during pause
    retro_vulkan_image persistent_libretro_image{};
};

} // namespace Vulkan
