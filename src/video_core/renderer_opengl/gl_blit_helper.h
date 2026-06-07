// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/math_util.h"
#include "video_core/rasterizer_cache/utils.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_state.h"

namespace VideoCore {
struct Extent;
struct TextureBlit;
struct TextureCopy;
} // namespace VideoCore

namespace OpenGL {

class Driver;
class Surface;

class BlitHelper {
public:
    explicit BlitHelper(const Driver& driver);
    ~BlitHelper();

    bool Filter(Surface& surface, const VideoCore::TextureBlit& blit);

    bool ConvertDS24S8ToRGBA8(Surface& source, Surface& dest, const VideoCore::TextureCopy& copy);

    bool ConvertRGBA4ToRGB5A1(Surface& source, Surface& dest, const VideoCore::TextureCopy& copy);

    void ResolveTexture(Surface& surface, u32 level = 0, u32 layer = 0);

private:
    void FilterAnime4K(Surface& surface, const VideoCore::TextureBlit& blit);
    void FilterBicubic(Surface& surface, const VideoCore::TextureBlit& blit);
    void FilterScaleForce(Surface& surface, const VideoCore::TextureBlit& blit);
    void FilterXbrz(Surface& surface, const VideoCore::TextureBlit& blit);
    void FilterMMPX(Surface& surface, const VideoCore::TextureBlit& blit);

    void SetParams(OGLProgram& program, const VideoCore::Extent& src_extent,
                   Common::Rectangle<u32> src_rect);
    void Draw(OGLProgram& program, GLuint dst_tex, GLuint dst_fbo, u32 dst_level,
              Common::Rectangle<u32> dst_rect, bool multisample = false);

private:
    const Driver& driver;
    OGLVertexArray vao;
    OpenGLState state;
    OGLFramebuffer read_fbo;
    OGLFramebuffer draw_fbo;
    OGLSampler linear_sampler;
    OGLSampler nearest_sampler;

    OGLProgram bicubic_program;
    OGLProgram scale_force_program;
    OGLProgram xbrz_program;
    OGLProgram mmpx_program;
    OGLProgram gradient_x_program;
    OGLProgram gradient_y_program;
    OGLProgram refine_program;
    OGLProgram d24s8_to_rgba8;
    OGLProgram d24s8_to_rgba8_ms;
    OGLProgram rgba4_to_rgb5a1;
    OGLProgram rgba4_to_rgb5a1_ms;

    OGLTexture temp_tex;
    VideoCore::Extent temp_extent{};
    bool use_texture_view{true};
};

} // namespace OpenGL
