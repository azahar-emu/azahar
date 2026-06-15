// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include "gl_resource_manager.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_opengl/frame_dumper_opengl.h"
#include "video_core/renderer_opengl/gl_driver.h"
#include "video_core/renderer_opengl/gl_rasterizer.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_state.h"

namespace Layout {
struct FramebufferLayout;
}

namespace Core {
class System;
}

namespace OpenGL {

/// Structure used for storing information about the textures for each 3DS screen
struct TextureInfo {
    OGLTexture resource;
    u32 width;
    u32 height;
    Pica::PixelFormat format;
    GLenum gl_format;
    GLenum gl_type;
};

/// Structure used for storing information about the display target for each 3DS screen
struct ScreenInfo {
    GLuint display_texture;
    Common::Rectangle<float> display_texcoords;
    TextureInfo texture;
};

class RendererOpenGL : public VideoCore::RendererBase {
public:
    explicit RendererOpenGL(Core::System& system, Pica::PicaCore& pica, Frontend::EmuWindow& window,
                            Frontend::EmuWindow* secondary_window);
    ~RendererOpenGL() override;

    [[nodiscard]] VideoCore::RasterizerInterface* Rasterizer() override {
        return &rasterizer;
    }

    void SwapBuffers() override;
    void TryPresent(int timeout_ms, bool is_secondary) override;
    void PrepareVideoDumping() override;
    void CleanupVideoDumping() override;

private:
    void InitOpenGLObjects();
    void ReloadShader(Settings::StereoRenderOption render_3d);
    void AllocateSMAATextures();
    void AllocatePPTextures();
    void AllocateOutputSizeTextures();
    void PrepareRendertarget();
    void RenderScreenshot();
    void RenderToMailbox(const Layout::FramebufferLayout& layout,
                         std::unique_ptr<Frontend::TextureMailbox>& mailbox, bool flipped);
    void ConfigureFramebufferTexture(TextureInfo& texture,
                                     const Pica::FramebufferConfig& framebuffer,
                                     const Pica::ColorFill& color_fill);
    void DrawScreens(const Layout::FramebufferLayout& layout, bool flipped);
    void ApplySecondLayerOpacity(float opacity = 1.0f);
    void ResetSecondLayerOpacity();
    void DrawBottomScreen(const Layout::FramebufferLayout& layout,
                          const Common::Rectangle<u32>& bottom_screen);
    void DrawTopScreen(const Layout::FramebufferLayout& layout,
                       const Common::Rectangle<u32>& top_screen);
    void DrawSingleScreen(const ScreenInfo& screen_info, float x, float y, float w, float h,
                          Layout::DisplayOrientation orientation);
    void DrawSingleScreenStereo(const ScreenInfo& screen_info_l, const ScreenInfo& screen_info_r,
                                float x, float y, float w, float h,
                                Layout::DisplayOrientation orientation);

    // Loads framebuffer from emulated memory into the display information structure
    void LoadFBToScreenInfo(const Pica::FramebufferConfig& framebuffer, ScreenInfo& screen_info,
                            bool right_eye, const Pica::ColorFill& color_fill);
    // Attach Uniforms to the current shader
    void AttachUniforms();
    // Shader #include function. Modified from Yuzu
    void ReplaceInclude(std::string& shader_source, std::string_view include_name, std::string_view include_content);

private:
    Pica::PicaCore& pica;
    Driver driver;
    RasterizerOpenGL rasterizer;
    OpenGLState state;

    // OpenGL object IDs
    OGLVertexArray vertex_array;
    OGLBuffer vertex_buffer;
    OGLProgram Present_shader;
    OGLProgram SimplePresent_shader;
    OGLProgram FXAA_shader;
    OGLProgram SMAA_PASS_0_shader;
    OGLProgram SMAA_PASS_1_shader;
    OGLProgram SMAA_PASS_2_shader;
    OGLProgram AREA_SAMPLING_shader;
    OGLProgram FSR_PASS_0_shader;
    OGLProgram FSR_PASS_1_shader;
    OGLProgram SGSR_shader;
    OGLProgram SharpBilinear_shader;
    OGLFramebuffer screenshot_framebuffer;
    std::array<OGLSampler, 2> samplers;

    // OpenGL objects for post processing
    OGLFramebuffer textureFBO;
    // Textures for Top and Bottom Screen Respectively
    std::array<std::array<OGLTexture, 7>, 2> intermediateTextures;
    std::array<OGLTexture, 2> antialiasFBOTexture;

    // Intermediate Textures at output size. These are 3 textures for each Main/Secondary Display + Top/Bottom/Additional Screen combo 
    std::array<std::array<std::array<OGLTexture, 3>, 3>, 2> intermediateOutputSizeTextures;
    std::array<std::array<Common::Rectangle<u32>, 3>, 2> prevOutputScreenRects;
    std::array<std::array<Common::Rectangle<u32>, 3>, 2> currOutputScreenRects;
    int currOutputScreen;
    OGLTexture areatex;
    OGLTexture searchtex;

    // Display information for top and bottom screens respectively
    std::array<ScreenInfo, 3> screen_infos;
    std::array<GLfloat, 3 * 2> ortho_matrix;
    // Shader uniform location indices
    GLuint uniform_modelview_matrix;
    GLuint uniform_color_texture;
    GLuint uniform_color_texture_r;
    GLuint uniform_reverse_interlaced;

    // Shader Uniform for converting colors. 0 is no conversion, 1 is sRGB -> linear, 2 is Linear -> sRGB
    GLuint uniform_convert_colors;
    
    GLuint uniform_fsr_sharpening;
    GLuint uniform_sgsr_sharpening;
    // Shader uniform for Dolphin compatibility
    GLuint uniform_i_resolution;
    GLuint uniform_o_resolution;
    GLuint uniform_layer;

    // Shader attribute input indices
    GLuint attrib_position;
    GLuint attrib_tex_coord;

    FrameDumperOpenGL frame_dumper;

    // Variables tracking texture changes
    float prevTopTextureWidth;
    float prevTopTextureHeight;
    float prevBottomTextureWidth;
    float prevBottomTextureHeight;
    float currTopTextureWidth;
    float currTopTextureHeight;
    float currBottomTextureWidth;
    float currBottomTextureHeight;
    std::array<int, 4> originalViewport;

    // Secondary Layout Fix
    bool isSecondaryWindow;

    // Fix External Shader
    bool usingExternalShader;
};

} // namespace OpenGL
