// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/common_types.h"
#include "common/math_util.h"
#include "video_core/renderer_base.h"
#ifdef HAVE_LIBRETRO
#include "citra_libretro/libretro_vk.h"
#else
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_present_window.h"
#endif
#include "video_core/renderer_vulkan/vk_rasterizer.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Core {
class System;
}

namespace Memory {
class MemorySystem;
}

namespace Pica {
class PicaCore;
}

namespace Layout {
struct FramebufferLayout;
}

namespace VideoCore {
class GPU;
}

namespace Vulkan {

struct TextureInfo {
    u32 width;
    u32 height;
    Pica::PixelFormat format;
    vk::Image image;
    vk::ImageView image_view;
    VmaAllocation allocation;
};

struct ScreenRectVertex {
    ScreenRectVertex() = default;
    ScreenRectVertex(float x, float y, float u, float v)
        : position{Common::MakeVec(x, y)}, tex_coord{Common::MakeVec(u, v)} {}

    Common::Vec2f position;
    Common::Vec2f tex_coord;
};

struct VertexBufferPointer {
    unsigned char* data;
    unsigned int offset;
    bool invalidate;
};

struct ScreenInfo {
    TextureInfo texture;
    Common::Rectangle<f32> texcoords;
    vk::ImageView image_view;
};

struct PresentUniformData {
    std::array<f32, 4 * 4> modelview;
    Common::Vec4f i_resolution;
    Common::Vec4f o_resolution;
    int screen_id_l = 0;
    int screen_id_r = 0;
    int layer = 0;
    int reverse_interlaced = 0;
    int convert_colors;
};
static_assert(sizeof(PresentUniformData) == 116,
              "PresentUniformData does not structure in shader!");

class RendererVulkan : public VideoCore::RendererBase {
    static constexpr std::size_t PRESENT_PIPELINES = 3;
    static constexpr std::size_t POST_PIPELINES_SCREEN = 1;
    static constexpr std::size_t POST_PIPELINES_TEXTURE = 5;
    static constexpr std::size_t POST_SHADERS = 6;
public:
    explicit RendererVulkan(Core::System& system, Pica::PicaCore& pica, Frontend::EmuWindow& window,
                            Frontend::EmuWindow* secondary_window);
    ~RendererVulkan() override;

    [[nodiscard]] VideoCore::RasterizerInterface* Rasterizer() override {
        return &rasterizer;
    }

    void NotifySurfaceChanged(bool second) override;

    void SwapBuffers() override;
    void TryPresent(int timeout_ms, bool is_secondary) override {}

private:
    void ReloadPipeline(Settings::StereoRenderOption render_3d);
    void CompileShaders();
    void BuildLayouts();
    void BuildPipelines();
    void ConfigureFramebufferTexture(TextureInfo& texture,
                                     const Pica::FramebufferConfig& framebuffer);
    void ConfigureRenderPipeline();
    void PrepareRendertarget();
    void RenderScreenshot();
    void RenderScreenshotWithStagingCopy();
    bool TryRenderScreenshotWithHostMemory();
    // Sets up command buffer for sampling from a screen_info to the screen framebuffer
    void PrepareDrawFromScreenInfo(Frame* frame, const Layout::FramebufferLayout& layout, vk::Pipeline shaderPipeline, std::vector<u32> screenids, int filterMode);
    // Sets up command buffer for sampling from a texture to the screen framebuffer
    void PrepareDrawFromTextureInfo(Frame* frame, const Layout::FramebufferLayout& layout, vk::Pipeline shaderPipeline, std::vector<TextureInfo> texturesToSample, int filterMode);
    // Sets up command buffer for sampling from a texture to an intermediate texture framebuffer
    void PrepareTextureDraw(TextureInfo framebufferTexture, vk::Framebuffer framebuffer, vk::Pipeline shaderPipeline, std::vector<TextureInfo> texturesToSample, int filterMode);
    // Sets up command buffer for sampling from a screen_info to an intermediate texture framebuffer
    void PrepareTextureDrawFromScreenInfo(TextureInfo framebufferTexture, vk::Framebuffer framebuffer, vk::Pipeline shaderPipeline, std::vector<u32> screenids, int filterMode);

    
    void UpdateVertexBuffer(std::array<ScreenRectVertex, 4> vertices, VertexBufferPointer vbp);
    void Draw(VertexBufferPointer vbp, PresentUniformData pushconstant);
    void RenderToWindow(PresentWindow& window, const Layout::FramebufferLayout& layout,
                        bool flipped);

    void DrawScreens(Frame* frame, const Layout::FramebufferLayout& layout, bool flipped);
    void DrawBottomScreen(const Layout::FramebufferLayout& layout,
                          const Common::Rectangle<u32>& bottom_screen);

    void DrawTopScreen(const Layout::FramebufferLayout& layout,
                       const Common::Rectangle<u32>& top_screen);
    void DrawSingleScreen(u32 screen_id, float x, float y, float w, float h,
                          Layout::DisplayOrientation orientation);
    void DrawSingleScreenStereo(u32 screen_id_l, u32 screen_id_r, float x, float y, float w,
                                float h, Layout::DisplayOrientation orientation);

    void ApplySecondLayerOpacity();

    void DrawCursor(const Layout::FramebufferLayout& layout);

    void LoadFBToScreenInfo(const Pica::FramebufferConfig& framebuffer, ScreenInfo& screen_info,
                            bool right_eye);
    void FillScreen(Common::Vec3<u8> color, const TextureInfo& texture);

    void AllocateTexture(TextureInfo& texture, int width, int height, vk::Format colorFormat);
    void CreateTextureFramebuffer(TextureInfo& texture, vk::Framebuffer& framebuffer);

    // Create Renderpass used for Textures
    void CreateTextureRenderPass();
    // Allocate Post Processing Textures
    void AllocatePPTextures();
    // Create Framebuffers that are attached to the Post Processing Textures
    void CreatePPTextureFramebuffers();
    void AllocateSMAATextures();
private:
    Memory::MemorySystem& memory;
    Pica::PicaCore& pica;

#ifdef HAVE_LIBRETRO
    LibRetroVKInstance instance;
#else
    Instance instance;
#endif
    Scheduler scheduler;
    RenderManager renderpass_cache;
    PresentWindow main_present_window;
    StreamBuffer vertex_buffer;
    DescriptorUpdateQueue update_queue;
    RasterizerVulkan rasterizer;
    std::unique_ptr<PresentWindow> secondary_present_window_ptr;

    DescriptorHeap present_heap;
    vk::UniquePipelineLayout present_pipeline_layout;
    std::array<vk::Pipeline, PRESENT_PIPELINES> present_pipelines;
    // Post Processing Pipelines for use with RGBA16F Textures. Contains: Simple Present, FXAA, SMAA Pass 0, SMAA Pass 1, SMAA Pass 2
    std::array<vk::Pipeline, POST_PIPELINES_TEXTURE> post_pipelines_texture;
    // Post Processing Pipelines for presenting to screen. Contains: Area
    std::array<vk::Pipeline, POST_PIPELINES_SCREEN> post_pipelines_screen;
    std::array<vk::ShaderModule, PRESENT_PIPELINES> present_shaders;
    // Post Processing Shaders for use with RGBA16F Textures. Contains: Simple Present, FXAA, SMAA Pass 0, SMAA Pass 1, SMAA Pass 2
    std::array<vk::ShaderModule, POST_PIPELINES_TEXTURE> post_vert_shaders_texture;
    std::array<vk::ShaderModule, POST_PIPELINES_TEXTURE> post_frag_shaders_texture;
    // Post Processing Shaders for presenting to screen. Contains: Area
    std::array<vk::ShaderModule, POST_PIPELINES_SCREEN> post_vert_shaders_screen;
    std::array<vk::ShaderModule, POST_PIPELINES_SCREEN> post_frag_shaders_screen;
    // Linear and Nearest Sampler Respectively
    std::array<vk::Sampler, 2> present_samplers;
    vk::ShaderModule present_vertex_shader;
    vk::ShaderModule simplepresent_vertex_shader;
    vk::ShaderModule simplepresent_frag_shader;
    vk::ShaderModule area_sampling_vertex_shader;
    vk::ShaderModule area_sampling_frag_shader;
    vk::ShaderModule fxaa_vertex_shader;
    vk::ShaderModule fxaa_frag_shader;
    vk::ShaderModule smaa_pass_0_vertex_shader;
    vk::ShaderModule smaa_pass_0_frag_shader;
    vk::ShaderModule smaa_pass_1_vertex_shader;
    vk::ShaderModule smaa_pass_1_frag_shader;
    vk::ShaderModule smaa_pass_2_vertex_shader;
    vk::ShaderModule smaa_pass_2_frag_shader;

    // Renderpass for RGBA16F Textures
    vk::RenderPass textureRenderpass;

    // Array of textures. 0 is top screen, 1 is bottom screen.
    std::array<std::array<TextureInfo, 5>, 2> intermediateTextures;
    std::array<TextureInfo, 2> antialiasTextures;

    // Array of framebuffer objects. 0 is top screen, 1 is bottom screen.
    std::array<std::array<vk::Framebuffer, 5>, 2> intermediateTextureFBOs;
    std::array<vk::Framebuffer, 2 > antialiasTextureFBOs;
    float currTopTextureWidth;
    float currTopTextureHeight;
    float currBottomTextureWidth;
    float currBottomTextureHeight;
    float prevTopTextureWidth;
    float prevTopTextureHeight;
    float prevBottomTextureWidth;
    float prevBottomTextureHeight;
    u32 current_pipeline = 0;
    Frame* currentFrame;
    Layout::FramebufferLayout currentFramebufferLayout;
    bool clearingColorAttachment = true;
    bool applyingOpacity = true;
    bool drawingPrimaryScreen = false;
    bool usingTopOpacity = false;
    std::array<ScreenInfo, 3> screen_infos{};
    PresentUniformData draw_info{};
    vk::ClearColorValue clear_color{};
    vk::ShaderModule cursor_vertex_shader{};
    vk::ShaderModule cursor_fragment_shader{};
    vk::Pipeline cursor_pipeline{};
    vk::UniquePipelineLayout cursor_pipeline_layout{};
};

} // namespace Vulkan
