// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/framebuffer_layout.h"
#include "core/memory.h"
#include "gl_state.h"
#include "video_core/pica/pica_core.h"
#include "video_core/renderer_opengl/gl_resource_manager.h"
#include "video_core/renderer_opengl/gl_state.h"
#include "video_core/renderer_opengl/gl_texture_mailbox.h"
#include "video_core/renderer_opengl/post_processing_opengl.h"
#include "video_core/renderer_opengl/renderer_opengl.h"
#include "video_core/shader/generator/glsl_shader_gen.h"

#include "video_core/host_shaders/opengl_present_anaglyph_frag.h"
#include "video_core/host_shaders/opengl_present_frag.h"
#include "video_core/host_shaders/opengl_present_interlaced_frag.h"
#include "video_core/host_shaders/opengl_present_vert.h"
#include "video_core/host_shaders/opengl_simple_present_frag.h"
#include "video_core/host_shaders/opengl_simple_present_vert.h"

#include "video_core/host_shaders/antialiasing/OpenGL/opengl_fxaa_frag.h"
#include "video_core/host_shaders/antialiasing/OpenGL/opengl_fxaa_vert.h"
#include "video_core/host_shaders/antialiasing/OpenGL/opengl_smaa_pass0_pre_frag.h"
#include "video_core/host_shaders/antialiasing/OpenGL/opengl_smaa_pass0_pre_vert.h"
#include "video_core/host_shaders/antialiasing/OpenGL/opengl_smaa_pass0_post_frag.h"
#include "video_core/host_shaders/antialiasing/OpenGL/opengl_smaa_pass0_post_vert.h"
#include "video_core/host_shaders/antialiasing/OpenGL/opengl_smaa_pass1_pre_frag.h"
#include "video_core/host_shaders/antialiasing/OpenGL/opengl_smaa_pass1_pre_vert.h"
#include "video_core/host_shaders/antialiasing/OpenGL/opengl_smaa_pass1_post_frag.h"
#include "video_core/host_shaders/antialiasing/OpenGL/opengl_smaa_pass1_post_vert.h"
#include "video_core/host_shaders/antialiasing/OpenGL/opengl_smaa_pass2_pre_frag.h"
#include "video_core/host_shaders/antialiasing/OpenGL/opengl_smaa_pass2_pre_vert.h"
#include "video_core/host_shaders/antialiasing/OpenGL/opengl_smaa_pass2_post_frag.h"
#include "video_core/host_shaders/antialiasing/OpenGL/opengl_smaa_pass2_post_vert.h"
#include "video_core/host_shaders/antialiasing/OpenGL/opengl_smaa_hlsl.h"
#include "video_core/host_shaders/antialiasing/AreaTex.h"
#include "video_core/host_shaders/antialiasing/SearchTex.h"
#include "video_core/host_shaders/scaling/opengl_area_sampling_frag.h"
#include "video_core/host_shaders/scaling/opengl_area_sampling_vert.h"
#include "video_core/host_shaders/scaling/FSR/OpenGL/opengl_fsr_pass0_vert.h"
#include "video_core/host_shaders/scaling/FSR/OpenGL/opengl_fsr_pass0_part1_frag.h"
#include "video_core/host_shaders/scaling/FSR/OpenGL/opengl_fsr_pass0_part2_frag.h"
#include "video_core/host_shaders/scaling/FSR/OpenGL/opengl_fsr_pass1_vert.h"
#include "video_core/host_shaders/scaling/FSR/OpenGL/opengl_fsr_pass1_part1_frag.h"
#include "video_core/host_shaders/scaling/FSR/OpenGL/opengl_fsr_pass1_part2_frag.h"
#include "video_core/host_shaders/scaling/FSR/OpenGL/opengl_fsr_pass1_part3_frag.h"
#include "video_core/host_shaders/scaling/FSR/ffx_a_h.h"
#include "video_core/host_shaders/scaling/FSR/ffx_fsr1_h.h"
#include "video_core/host_shaders/scaling/SharpBilinear/OpenGL/opengl_sharpbilinear_vert.h"
#include "video_core/host_shaders/scaling/SharpBilinear/OpenGL/opengl_sharpbilinear_frag.h"

namespace OpenGL {

MICROPROFILE_DEFINE(OpenGL_RenderFrame, "OpenGL", "Render Frame", MP_RGB(128, 128, 64));
MICROPROFILE_DEFINE(OpenGL_WaitPresent, "OpenGL", "Wait For Present", MP_RGB(128, 128, 128));

/**
 * Vertex structure that the drawn screen rectangles are composed of.
 */
struct ScreenRectVertex {
    ScreenRectVertex() = default;
    ScreenRectVertex(GLfloat x, GLfloat y, GLfloat u, GLfloat v) {
        position[0] = x;
        position[1] = y;
        tex_coord[0] = u;
        tex_coord[1] = v;
    }

    std::array<GLfloat, 2> position{};
    std::array<GLfloat, 2> tex_coord{};
};

/**
 * Defines a 1:1 pixel ortographic projection matrix with (0,0) on the top-left
 * corner and (width, height) on the lower-bottom.
 *
 * The projection part of the matrix is trivial, hence these operations are represented
 * by a 3x2 matrix.
 *
 * @param flipped Whether the frame should be flipped upside down.
 */
static std::array<GLfloat, 3 * 2> MakeOrthographicMatrix(const float width, const float height,
                                                         bool flipped) {

    std::array<GLfloat, 3 * 2> matrix; // Laid out in column-major order

    // Last matrix row is implicitly assumed to be [0, 0, 1].
    if (flipped) {
        // clang-format off
        matrix[0] = 2.f / width; matrix[2] = 0.f;           matrix[4] = -1.f;
        matrix[1] = 0.f;         matrix[3] = 2.f / height;  matrix[5] = -1.f;
        // clang-format on
    } else {
        // clang-format off
        matrix[0] = 2.f / width; matrix[2] = 0.f;           matrix[4] = -1.f;
        matrix[1] = 0.f;         matrix[3] = -2.f / height; matrix[5] = 1.f;
        // clang-format on
    }

    return matrix;
}

RendererOpenGL::RendererOpenGL(Core::System& system, Pica::PicaCore& pica_,
                               Frontend::EmuWindow& window, Frontend::EmuWindow* secondary_window)
    : VideoCore::RendererBase{system, window, secondary_window}, pica{pica_},
      rasterizer{system.Memory(), pica, system.CustomTexManager(), *this, driver},
      frame_dumper{system, window} {
    const bool has_debug_tool = driver.HasDebugTool();
    window.mailbox = std::make_unique<OGLTextureMailbox>(has_debug_tool);
    if (secondary_window) {
        secondary_window->mailbox = std::make_unique<OGLTextureMailbox>(has_debug_tool);
    }
    frame_dumper.mailbox = std::make_unique<OGLVideoDumpingMailbox>();
    InitOpenGLObjects();
}

RendererOpenGL::~RendererOpenGL() = default;

void RendererOpenGL::SwapBuffers() {
    system.perf_stats->StartSwap();
    // Maintain the rasterizer's state as a priority
    OpenGLState prev_state = OpenGLState::GetCurState();
    state.Apply();

    render_window.SetupFramebuffer();

    PrepareRendertarget();
    RenderScreenshot();
#ifdef HAVE_LIBRETRO
    DrawScreens(render_window.GetFramebufferLayout(), false);
    render_window.SwapBuffers();
#else
    const auto& main_layout = render_window.GetFramebufferLayout();
    RenderToMailbox(main_layout, render_window.mailbox, false);

#ifdef ANDROID
    // On Android, if secondary_window is defined at all,
    // it means we have a second display
    if (secondary_window) {
        const auto& secondary_layout = secondary_window->GetFramebufferLayout();
        RenderToMailbox(secondary_layout, secondary_window->mailbox, false);
        secondary_window->PollEvents();
    }
#else
    if (Settings::values.layout_option.GetValue() == Settings::LayoutOption::SeparateWindows) {
        ASSERT(secondary_window);
        const auto& secondary_layout = secondary_window->GetFramebufferLayout();
        RenderToMailbox(secondary_layout, secondary_window->mailbox, false);
        secondary_window->PollEvents();
    }
#endif

    if (frame_dumper.IsDumping()) {
        try {
            RenderToMailbox(frame_dumper.GetLayout(), frame_dumper.mailbox, true);
        } catch (const OGLTextureMailboxException& exception) {
            LOG_DEBUG(Render_OpenGL, "Frame dumper exception caught: {}", exception.what());
        }
    }
#endif

    system.perf_stats->EndSwap();
    EndFrame();
    prev_state.Apply();
    rasterizer.TickFrame();
}

void RendererOpenGL::RenderScreenshot() {
    if (settings.screenshot_requested.exchange(false)) {
        // Draw this frame to the screenshot framebuffer
        screenshot_framebuffer.Create();
        GLuint old_read_fb = state.draw.read_framebuffer;
        GLuint old_draw_fb = state.draw.draw_framebuffer;
        state.draw.read_framebuffer = state.draw.draw_framebuffer = screenshot_framebuffer.handle;
        state.Apply();

        const Layout::FramebufferLayout layout{settings.screenshot_framebuffer_layout};

        GLuint renderbuffer;
        glGenRenderbuffers(1, &renderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB8, layout.width, layout.height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                  renderbuffer);

        DrawScreens(layout, false);

        glReadPixels(0, 0, layout.width, layout.height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
                     settings.screenshot_bits);

        screenshot_framebuffer.Release();
        state.draw.read_framebuffer = old_read_fb;
        state.draw.draw_framebuffer = old_draw_fb;
        state.Apply();
        glDeleteRenderbuffers(1, &renderbuffer);

        settings.screenshot_complete_callback(true);
    }
}

void RendererOpenGL::PrepareRendertarget() {
    const auto& framebuffer_config = pica.regs.framebuffer_config;
    const auto& regs_lcd = pica.regs_lcd;
    for (u32 i = 0; i < 3; i++) {
        const u32 fb_id = i == 2 ? 1 : 0;
        const auto& framebuffer = framebuffer_config[fb_id];
        auto& texture = screen_infos[i].texture;

        const auto color_fill = fb_id == 0 ? regs_lcd.color_fill_top : regs_lcd.color_fill_bottom;
        if (color_fill.is_enabled) {
            // Resize the texture to let it be reconfigured
            texture.width = 1;
            texture.height = 1;
        }

        if (texture.width != framebuffer.width || texture.height != framebuffer.height ||
            texture.format != framebuffer.color_format) {
            ConfigureFramebufferTexture(texture, framebuffer, color_fill);
        }
        LoadFBToScreenInfo(framebuffer, screen_infos[i], i == 1, color_fill);
    }
}

void RendererOpenGL::RenderToMailbox(const Layout::FramebufferLayout& layout,
                                     std::unique_ptr<Frontend::TextureMailbox>& mailbox,
                                     bool flipped) {

    Frontend::Frame* frame;
    {
        MICROPROFILE_SCOPE(OpenGL_WaitPresent);

        frame = mailbox->GetRenderFrame();

        // Clean up sync objects before drawing

        // INTEL driver workaround. We can't delete the previous render sync object until we are
        // sure that the presentation is done
        if (frame->present_fence) {
            glClientWaitSync(frame->present_fence, 0, GL_TIMEOUT_IGNORED);
        }

        // delete the draw fence if the frame wasn't presented
        if (frame->render_fence) {
            glDeleteSync(frame->render_fence);
            frame->render_fence = nullptr;
        }

        // wait for the presentation to be done
        if (frame->present_fence) {
            glWaitSync(frame->present_fence, 0, GL_TIMEOUT_IGNORED);
            glDeleteSync(frame->present_fence);
            frame->present_fence = nullptr;
        }
    }

    {
        MICROPROFILE_SCOPE(OpenGL_RenderFrame);
        // Recreate the frame if the size of the window has changed
        if (layout.width != frame->width || layout.height != frame->height) {
            LOG_DEBUG(Render_OpenGL, "Reloading render frame");
            mailbox->ReloadRenderFrame(frame, layout.width, layout.height);
        }

        state.draw.draw_framebuffer = frame->render.handle;
        state.Apply();
        DrawScreens(layout, flipped);
        // Create a fence for the frontend to wait on and swap this frame to OffTex
        frame->render_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();
        mailbox->ReleaseRenderFrame(frame);
    }
}

/**
 * Loads framebuffer from emulated memory into the active OpenGL texture.
 */
void RendererOpenGL::LoadFBToScreenInfo(const Pica::FramebufferConfig& framebuffer,
                                        ScreenInfo& screen_info, bool right_eye,
                                        const Pica::ColorFill& color_fill) {

    if (framebuffer.address_right1 == 0 || framebuffer.address_right2 == 0)
        right_eye = false;

    const PAddr framebuffer_addr =
        framebuffer.active_fb == 0
            ? (!right_eye ? framebuffer.address_left1 : framebuffer.address_right1)
            : (!right_eye ? framebuffer.address_left2 : framebuffer.address_right2);

    LOG_TRACE(Render_OpenGL, "0x{:08x} bytes from 0x{:08x}({}x{}), fmt {:x}",
              framebuffer.stride * framebuffer.height, framebuffer_addr, framebuffer.width.Value(),
              framebuffer.height.Value(), framebuffer.format);

    int bpp = Pica::BytesPerPixel(framebuffer.color_format);
    std::size_t pixel_stride = framebuffer.stride / bpp;

    // OpenGL only supports specifying a stride in units of pixels, not bytes, unfortunately
    ASSERT(pixel_stride * bpp == framebuffer.stride);

    // Ensure no bad interactions with GL_UNPACK_ALIGNMENT, which by default
    // only allows rows to have a memory alignement of 4.
    ASSERT(pixel_stride % 4 == 0);

    if (color_fill.is_enabled ||
        !rasterizer.AccelerateDisplay(framebuffer, framebuffer_addr, static_cast<u32>(pixel_stride),
                                      screen_info)) {
        u32 width = framebuffer.width;
        u32 height = framebuffer.height;
        u8 fill_pixel[3];
        // Reset the screen info's display texture to its own permanent texture
        screen_info.display_texture = screen_info.texture.resource.handle;
        screen_info.display_texcoords = Common::Rectangle<f32>(0.f, 0.f, 1.f, 1.f);

        rasterizer.FlushRegion(framebuffer_addr, framebuffer.stride * framebuffer.height);

        u8* framebuffer_data = system.Memory().GetPhysicalPointer(framebuffer_addr);

        if (color_fill.is_enabled) {
            memcpy(fill_pixel, color_fill.AsVector().AsArray(), sizeof(fill_pixel));
            framebuffer_data = fill_pixel;
            width = 1;
            height = 1;
            pixel_stride = 0;
        }

        state.texture_units[0].texture_2d = screen_info.texture.resource.handle;
        state.Apply();

        glActiveTexture(GL_TEXTURE0);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)pixel_stride);

        // Update existing texture
        // TODO: Test what happens on hardware when you change the framebuffer dimensions so that
        //       they differ from the LCD resolution.
        // TODO: Applications could theoretically crash Citra here by specifying too large
        //       framebuffer sizes. We should make sure that this cannot happen.
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, screen_info.texture.gl_format,
                        screen_info.texture.gl_type, framebuffer_data);

        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

        state.texture_units[0].texture_2d = 0;
        state.Apply();
    }
}

std::vector<unsigned char> flipVertically(const unsigned char* data, int width, int height, int channels)
{
    int rowSize = width * channels;
    std::vector<unsigned char> flipped(width * height * channels);

    for (int y = 0; y < height; y++)
    {
        const unsigned char* src = data + (height - 1 - y) * rowSize;
        unsigned char* dst       = flipped.data() + y * rowSize;
        memcpy(dst, src, rowSize);
    }

    return flipped;
}


void RendererOpenGL::AllocateSMAATextures(){
    //Load AreaTex and SearchTex  to OGLTexture Objects 
    areatex.Create();
    searchtex.Create();
    std::vector<unsigned char> areaTexBytes_Flipped = flipVertically(areaTexBytes, AREATEX_WIDTH, AREATEX_HEIGHT, 2);
    std::vector<unsigned char> searchTexBytes_Flipped = flipVertically(searchTexBytes, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1);
    GLuint old_tex = OpenGLState::GetCurState().texture_units[0].texture_2d;
    
    glBindTexture(GL_TEXTURE_2D, areatex.handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, AREATEX_WIDTH, AREATEX_HEIGHT, 0, GL_RG, GL_UNSIGNED_BYTE, areaTexBytes_Flipped.data());

    glBindTexture(GL_TEXTURE_2D, searchtex.handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 0, GL_RED, GL_UNSIGNED_BYTE, searchTexBytes_Flipped.data());

    glBindTexture(GL_TEXTURE_2D, old_tex);  
}

void RendererOpenGL::AllocatePPTextures(){


    for (int j = 0; j < intermediateTextures[0].size(); j++){
        intermediateTextures[0][j].Release();
        intermediateTextures[0][j].Create();
        intermediateTextures[0][j].Allocate(GL_TEXTURE_2D, 1, GL_RGBA16F, currTopTextureWidth,  currTopTextureHeight);
        intermediateTextures[1][j].Release();
        intermediateTextures[1][j].Create();
        intermediateTextures[1][j].Allocate(GL_TEXTURE_2D, 1, GL_RGBA16F, currBottomTextureWidth,  currBottomTextureHeight);
    }
    antialiasFBOTexture[0].Release();
    antialiasFBOTexture[0].Create();
    antialiasFBOTexture[0].Allocate(GL_TEXTURE_2D, 1, GL_RGBA16F, currTopTextureWidth,  currTopTextureHeight);
    antialiasFBOTexture[1].Release();
    antialiasFBOTexture[1].Create();
    antialiasFBOTexture[1].Allocate(GL_TEXTURE_2D, 1, GL_RGBA16F, currBottomTextureWidth,  currBottomTextureHeight);
    LOG_INFO(Render_OpenGL, "Reallocated Textures");
}

void RendererOpenGL::AllocateOutputSizeTextures(){
    for (int i = 0; i < intermediateOutputSizeTextures.size(); i++){
        if (currOutputScreenRects[i].GetHeight() != 0 && currOutputScreenRects[i].GetWidth() != 0){
            for (int j = 0; j < intermediateOutputSizeTextures[0].size(); j++){
                intermediateOutputSizeTextures[i][j].Release();
                intermediateOutputSizeTextures[i][j].Create();
                intermediateOutputSizeTextures[i][j].Allocate(GL_TEXTURE_2D, 1, GL_RGBA16F, currOutputScreenRects[i].GetWidth(),  currOutputScreenRects[i].GetHeight());
            }
        }
    }

    LOG_INFO(Render_OpenGL, "Reallocated OutputSize Textures");
}

/**
 * Initializes the OpenGL state and creates persistent objects.
 */
void RendererOpenGL::InitOpenGLObjects() {
    glClearColor(Settings::values.bg_red.GetValue(), Settings::values.bg_green.GetValue(),
                 Settings::values.bg_blue.GetValue(), 0.0f);

    for (std::size_t i = 0; i < samplers.size(); i++) {
        samplers[i].Create();
        glSamplerParameteri(samplers[i].handle, GL_TEXTURE_MIN_FILTER,
                            i == 0 ? GL_NEAREST : GL_LINEAR);
        glSamplerParameteri(samplers[i].handle, GL_TEXTURE_MAG_FILTER,
                            i == 0 ? GL_NEAREST : GL_LINEAR);
        glSamplerParameteri(samplers[i].handle, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(samplers[i].handle, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    ReloadShader(Settings::values.render_3d.GetValue());

    // Generate VBO handle for drawing
    vertex_buffer.Create();

    // Generate VAO
    vertex_array.Create();

    state.draw.vertex_array = vertex_array.handle;
    state.draw.vertex_buffer = vertex_buffer.handle;
    state.draw.uniform_buffer = 0;
    state.Apply();

    // Attach vertex data to VAO
    glBufferData(GL_ARRAY_BUFFER, sizeof(ScreenRectVertex) * 4, nullptr, GL_STREAM_DRAW);
    glVertexAttribPointer(attrib_position, 2, GL_FLOAT, GL_FALSE, sizeof(ScreenRectVertex),
                          (GLvoid*)offsetof(ScreenRectVertex, position));
    glVertexAttribPointer(attrib_tex_coord, 2, GL_FLOAT, GL_FALSE, sizeof(ScreenRectVertex),
                          (GLvoid*)offsetof(ScreenRectVertex, tex_coord));
    glEnableVertexAttribArray(attrib_position);
    glEnableVertexAttribArray(attrib_tex_coord);

    // Allocate textures for Post Processing
    AllocateSMAATextures();
    textureFBO.Create();
    
    // Allocate textures for each screen
    for (auto& screen_info : screen_infos) {
        screen_info.texture.resource.Create();

        // Allocation of storage is deferred until the first frame, when we
        // know the framebuffer size.

        state.texture_units[0].texture_2d = screen_info.texture.resource.handle;
        state.Apply();

        glActiveTexture(GL_TEXTURE0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        screen_info.display_texture = screen_info.texture.resource.handle;
    }
    AllocatePPTextures();
    state.texture_units[0].texture_2d = 0;
    state.Apply();
}

void RendererOpenGL::ReloadShader(Settings::StereoRenderOption render_3d) {
    // Link shaders and get variable locations
    std::string shader_data = fragment_shader_precision_OES;
    if (render_3d == Settings::StereoRenderOption::Anaglyph) {
        if (Settings::values.anaglyph_shader_name.GetValue() == "Dubois (builtin)") {
            shader_data += HostShaders::OPENGL_PRESENT_ANAGLYPH_FRAG;
        } else {
            std::string shader_text = OpenGL::GetPostProcessingShaderCode(
                true, Settings::values.anaglyph_shader_name.GetValue());
            if (shader_text.empty()) {
                // Should probably provide some information that the shader couldn't load
                shader_data += HostShaders::OPENGL_PRESENT_ANAGLYPH_FRAG;
            } else {
                shader_data += shader_text;
            }
        }
    } else if (render_3d == Settings::StereoRenderOption::Interlaced ||
               render_3d == Settings::StereoRenderOption::ReverseInterlaced) {
        shader_data += HostShaders::OPENGL_PRESENT_INTERLACED_FRAG;
    } else {
        if (Settings::values.pp_shader_name.GetValue() == "None (builtin)") {
            shader_data += HostShaders::OPENGL_PRESENT_FRAG;
        } else {
            std::string shader_text = OpenGL::GetPostProcessingShaderCode(
                false, Settings::values.pp_shader_name.GetValue());
            if (shader_text.empty()) {
                // Should probably provide some information that the shader couldn't load
                shader_data += HostShaders::OPENGL_PRESENT_FRAG;
            } else {
                shader_data += shader_text;
            }
        }
    }
    Present_shader.Create(HostShaders::OPENGL_PRESENT_VERT, shader_data);
    state.draw.shader_program = Present_shader.handle;
    AttachUniforms();


    // Setup FXAA, SMAA and Simple Present Shaders
    std::string FXAA_shader_data = fragment_shader_precision_OES;
    FXAA_shader_data += HostShaders::OPENGL_FXAA_FRAG;
    FXAA_shader.Create(HostShaders::OPENGL_FXAA_VERT, FXAA_shader_data);

    std::string AREA_SAMPLING_shader_data = fragment_shader_precision_OES;
    AREA_SAMPLING_shader_data += HostShaders::OPENGL_AREA_SAMPLING_FRAG;
    AREA_SAMPLING_shader.Create(HostShaders::OPENGL_AREA_SAMPLING_VERT, AREA_SAMPLING_shader_data);
        
    std::string SimplePresent_shader_data = fragment_shader_precision_OES;
    SimplePresent_shader_data += HostShaders::OPENGL_SIMPLE_PRESENT_FRAG;
    SimplePresent_shader.Create(HostShaders::OPENGL_SIMPLE_PRESENT_VERT, SimplePresent_shader_data);

    std::string SMAA_PASS_0_shader_frag_data = fragment_shader_precision_OES;
    SMAA_PASS_0_shader_frag_data += HostShaders::OPENGL_SMAA_PASS0_PRE_FRAG;
    SMAA_PASS_0_shader_frag_data += HostShaders::OPENGL_SMAA_HLSL;
    SMAA_PASS_0_shader_frag_data += HostShaders::OPENGL_SMAA_PASS0_POST_FRAG;
    std::string SMAA_PASS_0_shader_vert_data;
    SMAA_PASS_0_shader_vert_data += HostShaders::OPENGL_SMAA_PASS0_PRE_VERT;
    SMAA_PASS_0_shader_vert_data += HostShaders::OPENGL_SMAA_HLSL;
    SMAA_PASS_0_shader_vert_data += HostShaders::OPENGL_SMAA_PASS0_POST_VERT;
    SMAA_PASS_0_shader.Create(SMAA_PASS_0_shader_vert_data, SMAA_PASS_0_shader_frag_data);

    std::string SMAA_PASS_1_shader_frag_data = fragment_shader_precision_OES;
    SMAA_PASS_1_shader_frag_data += HostShaders::OPENGL_SMAA_PASS1_PRE_FRAG;
    SMAA_PASS_1_shader_frag_data += HostShaders::OPENGL_SMAA_HLSL;
    SMAA_PASS_1_shader_frag_data += HostShaders::OPENGL_SMAA_PASS1_POST_FRAG;
    std::string SMAA_PASS_1_shader_vert_data;
    SMAA_PASS_1_shader_vert_data += HostShaders::OPENGL_SMAA_PASS1_PRE_VERT;
    SMAA_PASS_1_shader_vert_data += HostShaders::OPENGL_SMAA_HLSL;
    SMAA_PASS_1_shader_vert_data += HostShaders::OPENGL_SMAA_PASS1_POST_VERT;
    SMAA_PASS_1_shader.Create(SMAA_PASS_1_shader_vert_data, SMAA_PASS_1_shader_frag_data);

    std::string SMAA_PASS_2_shader_frag_data = fragment_shader_precision_OES;
    SMAA_PASS_2_shader_frag_data += HostShaders::OPENGL_SMAA_PASS2_PRE_FRAG;
    SMAA_PASS_2_shader_frag_data += HostShaders::OPENGL_SMAA_HLSL;
    SMAA_PASS_2_shader_frag_data += HostShaders::OPENGL_SMAA_PASS2_POST_FRAG;
    std::string SMAA_PASS_2_shader_vert_data;
    SMAA_PASS_2_shader_vert_data += HostShaders::OPENGL_SMAA_PASS2_PRE_VERT;
    SMAA_PASS_2_shader_vert_data += HostShaders::OPENGL_SMAA_HLSL;
    SMAA_PASS_2_shader_vert_data += HostShaders::OPENGL_SMAA_PASS2_POST_VERT;
    SMAA_PASS_2_shader.Create(SMAA_PASS_2_shader_vert_data, SMAA_PASS_2_shader_frag_data);
    

    std::string FSR_PASS_0_shader_frag_data =  fragment_shader_precision_OES;
    FSR_PASS_0_shader_frag_data += HostShaders::OPENGL_FSR_PASS0_PART1_FRAG;
    FSR_PASS_0_shader_frag_data += HostShaders::FFX_A_H;
    FSR_PASS_0_shader_frag_data += HostShaders::FFX_FSR1_H;
    FSR_PASS_0_shader_frag_data += HostShaders::OPENGL_FSR_PASS0_PART2_FRAG;
    FSR_PASS_0_shader.Create(HostShaders::OPENGL_FSR_PASS0_VERT, FSR_PASS_0_shader_frag_data);

    std::string FSR_PASS_1_shader_frag_data =  fragment_shader_precision_OES;
    FSR_PASS_1_shader_frag_data += HostShaders::OPENGL_FSR_PASS1_PART1_FRAG;
    FSR_PASS_1_shader_frag_data += HostShaders::FFX_A_H;
    FSR_PASS_1_shader_frag_data += HostShaders::OPENGL_FSR_PASS1_PART2_FRAG;
    FSR_PASS_1_shader_frag_data += HostShaders::FFX_FSR1_H;
    FSR_PASS_1_shader_frag_data += HostShaders::OPENGL_FSR_PASS1_PART3_FRAG;
    FSR_PASS_1_shader.Create(HostShaders::OPENGL_FSR_PASS1_VERT, FSR_PASS_1_shader_frag_data);

    SharpBilinear_shader.Create(HostShaders::OPENGL_SHARPBILINEAR_VERT, HostShaders::OPENGL_SHARPBILINEAR_FRAG);
    
    state.Apply();
    if (render_3d == Settings::StereoRenderOption::Anaglyph ||
        render_3d == Settings::StereoRenderOption::Interlaced ||
        render_3d == Settings::StereoRenderOption::ReverseInterlaced) {
    }
    if (render_3d == Settings::StereoRenderOption::Interlaced ||
        render_3d == Settings::StereoRenderOption::ReverseInterlaced) {
        if (render_3d == Settings::StereoRenderOption::ReverseInterlaced)
            glUniform1i(uniform_reverse_interlaced, 1);
        else
            glUniform1i(uniform_reverse_interlaced, 0);
    }
}

void RendererOpenGL::ConfigureFramebufferTexture(TextureInfo& texture,
                                                 const Pica::FramebufferConfig& framebuffer,
                                                 const Pica::ColorFill& color_fill) {
    Pica::PixelFormat format = framebuffer.color_format;
    GLint internal_format{};
    u32 width, height;

    texture.format = format;
    width = texture.width = framebuffer.width;
    height = texture.height = framebuffer.height;
    if (color_fill.is_enabled) {
        width = 1;
        height = 1;
        format = Pica::PixelFormat::RGB8;
    }

    switch (format) {
    case Pica::PixelFormat::RGBA8:
        internal_format = GL_RGBA;
        texture.gl_format = GL_RGBA;
        texture.gl_type = driver.IsOpenGLES() ? GL_UNSIGNED_BYTE : GL_UNSIGNED_INT_8_8_8_8;
        break;

    case Pica::PixelFormat::RGB8:
        // This pixel format uses BGR since GL_UNSIGNED_BYTE specifies byte-order, unlike every
        // specific OpenGL type used in this function using native-endian (that is, little-endian
        // mostly everywhere) for words or half-words.
        // TODO: check how those behave on big-endian processors.
        internal_format = GL_RGB;

        // GLES Dosen't support BGR , Use RGB instead
        texture.gl_format = driver.IsOpenGLES() ? GL_RGB : GL_BGR;
        texture.gl_type = GL_UNSIGNED_BYTE;
        break;

    case Pica::PixelFormat::RGB565:
        internal_format = GL_RGB;
        texture.gl_format = GL_RGB;
        texture.gl_type = GL_UNSIGNED_SHORT_5_6_5;
        break;

    case Pica::PixelFormat::RGB5A1:
        internal_format = GL_RGBA;
        texture.gl_format = GL_RGBA;
        texture.gl_type = GL_UNSIGNED_SHORT_5_5_5_1;
        break;

    case Pica::PixelFormat::RGBA4:
        internal_format = GL_RGBA;
        texture.gl_format = GL_RGBA;
        texture.gl_type = GL_UNSIGNED_SHORT_4_4_4_4;
        break;

    default:
        UNIMPLEMENTED();
    }

    state.texture_units[0].texture_2d = texture.resource.handle;
    state.Apply();

    glActiveTexture(GL_TEXTURE0);
    glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, texture.gl_format,
                 texture.gl_type, nullptr);

    state.texture_units[0].texture_2d = 0;
    state.Apply();
}

void RendererOpenGL::AttachUniforms(){
    uniform_modelview_matrix = glGetUniformLocation(state.draw.shader_program, "modelview_matrix");
    uniform_color_texture = glGetUniformLocation(state.draw.shader_program, "color_texture");
    uniform_color_texture_r = glGetUniformLocation(state.draw.shader_program, "color_texture_r");
    uniform_reverse_interlaced = glGetUniformLocation(state.draw.shader_program, "reverse_interlaced");
    uniform_i_resolution = glGetUniformLocation(state.draw.shader_program, "i_resolution");
    uniform_o_resolution = glGetUniformLocation(state.draw.shader_program, "o_resolution");
    uniform_convert_colors = glGetUniformLocation(state.draw.shader_program, "convert_colors");
    uniform_fsr_sharpening = glGetUniformLocation(state.draw.shader_program, "FSR_SHARPENING");
    uniform_layer = glGetUniformLocation(state.draw.shader_program, "layer");
    attrib_position = glGetAttribLocation(state.draw.shader_program, "vert_position");
    attrib_tex_coord = glGetAttribLocation(state.draw.shader_program, "vert_tex_coord");
}

/**
 * Draws a single texture to the emulator window, rotating the texture to correct for the 3DS's LCD
 * rotation.
 */
void RendererOpenGL::DrawSingleScreen(const ScreenInfo& screen_info, float screenLeft, float screenTop, float screenWidth,
                                      float screenHeight, Layout::DisplayOrientation orientation) {
    const auto& texcoords = screen_info.display_texcoords;
    const u32 scale_factor = GetResolutionScaleFactor();
    float textureWidth = static_cast<float>(screen_info.texture.height * scale_factor);
    float textureHeight = static_cast<float>(screen_info.texture.width * scale_factor);
    int currScreen;
    if (textureWidth == currTopTextureWidth && textureHeight == currTopTextureHeight){
        currScreen = 0;
    } else {
        currScreen = 1;
    }
   
    // Texture Width and Height when correctly rotated to landscape
    bool isDownsampling = false;
    int scalingMode; //0 is Nearest Neighbor, 1 is Gamma Corrected Bilinear, 2 is Adaptive (Bilinear/Area), 3 is FSR, 4 is Sharp Bilinear
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
            ScreenRectVertex(-1.f, 1.f, texcoords.bottom, texcoords.left),   //Left, Top
            ScreenRectVertex(1.f, 1.f, texcoords.bottom, texcoords.right),    //Right, Top
            ScreenRectVertex(-1.f, -1.f, texcoords.top, texcoords.left),  //Left, Bottom
            ScreenRectVertex(1.f, -1.f, texcoords.top, texcoords.right),   //Right, Bottom
    }};

    // Vertices for 1:1 Texture Mapping.
    std::array<ScreenRectVertex, 4> pass_through_vertices;
    pass_through_vertices = {{
            ScreenRectVertex(-1.f, 1.f, 0.f, 1.f),   //Left, Top
            ScreenRectVertex(1.f, 1.f, 1.f, 1.f),    //Right, Top
            ScreenRectVertex(-1.f, -1.f, 0.f, 0.f),  //Left, Bottom
            ScreenRectVertex(1.f, -1.f, 1.f, 0.f),   //Right, Bottom
    }};

    // Vertices for Azahar's Output Layout
    std::array<ScreenRectVertex, 4> output_vertices;
    switch (orientation) {
    case Layout::DisplayOrientation::Landscape:
        output_vertices = {{
                ScreenRectVertex(screenLeft, screenTop, 0.f, 1.f),                               //Left, Top
                ScreenRectVertex(screenLeft + screenWidth, screenTop, 1.f, 1.f),                 //Right, Top
                ScreenRectVertex(screenLeft, screenTop + screenHeight, 0.f, 0.f),                //Left, Bottom
                ScreenRectVertex(screenLeft + screenWidth, screenTop + screenHeight, 1.f, 0.f),  //Right, Bottom
        }};
        break;
    case Layout::DisplayOrientation::Portrait:
        output_vertices = {{
                ScreenRectVertex(screenLeft, screenTop, 1.f, 1.f),                               //Left, Top
                ScreenRectVertex(screenLeft + screenWidth, screenTop, 1.f, 0.f),                 //Right, Top
                ScreenRectVertex(screenLeft, screenTop + screenHeight, 0.f, 1.f),                //Left, Bottom
                ScreenRectVertex(screenLeft + screenWidth, screenTop + screenHeight, 0.f, 0.f),  //Right, Bottom
        }};
        std::swap(screenHeight, screenWidth);
        break;
    case Layout::DisplayOrientation::LandscapeFlipped:
        output_vertices = {{
                ScreenRectVertex(screenLeft, screenTop, 0.f, 0.f),                               //Left, Top
                ScreenRectVertex(screenLeft + screenWidth, screenTop, 1.f, 0.f),                 //Right, Top
                ScreenRectVertex(screenLeft, screenTop + screenHeight, 0.f, 1.f),                //Left, Bottom
                ScreenRectVertex(screenLeft + screenWidth, screenTop + screenHeight, 1.f, 1.f),  //Right, Bottom
        }};
        break;
    case Layout::DisplayOrientation::PortraitFlipped:
        output_vertices = {{
                ScreenRectVertex(screenLeft, screenTop, 0.f, 0.f),                               //Left, Top
                ScreenRectVertex(screenLeft + screenWidth, screenTop, 0.f, 1.f),                 //Right, Top
                ScreenRectVertex(screenLeft, screenTop + screenHeight, 1.f, 0.f),                //Left, Bottom
                ScreenRectVertex(screenLeft + screenWidth, screenTop + screenHeight, 1.f, 1.f),  //Right, Bottom
        }};
        std::swap(screenHeight, screenWidth);
        break;
    default:
        LOG_ERROR(Render_OpenGL, "Unknown DisplayOrientation: {}", orientation);
        break;
    }

    GLuint originalReadFramebuffer = state.draw.read_framebuffer;
    GLuint originalDrawFramebuffer = state.draw.draw_framebuffer;
    if (antialiasingMode == 1){
        //Pass 1
        state.draw.read_framebuffer = textureFBO.handle;
        state.draw.draw_framebuffer = textureFBO.handle;
        state.Apply();
        state.viewport.x = 0;
        state.viewport.y = 0;
        state.viewport.width = textureWidth;
        state.viewport.height = textureHeight;
        state.Apply();
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, intermediateTextures[currScreen][0].handle, 0);  
        glClear(GL_COLOR_BUFFER_BIT);
        state.draw.shader_program = SimplePresent_shader.handle;
        state.Apply();
        AttachUniforms();
        state.texture_units[0].texture_2d = screen_info.display_texture;
        state.texture_units[0].sampler = samplers[1].handle;
        glUniform1i(uniform_color_texture, 0);
        glUniform1i(uniform_convert_colors, 0);
        state.Apply();
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(rotate_vertices), rotate_vertices.data());
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        //Pass 2
        state.viewport.x = 0;
        state.viewport.y = 0;
        state.viewport.width = textureWidth;
        state.viewport.height = textureHeight;
        state.Apply();
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, antialiasFBOTexture[currScreen].handle, 0);  
        glClear(GL_COLOR_BUFFER_BIT);
        state.draw.shader_program = FXAA_shader.handle;
        state.Apply();
        AttachUniforms();
        state.texture_units[0].texture_2d = intermediateTextures[currScreen][0].handle;
        state.texture_units[0].sampler = samplers[1].handle;
        glUniform1i(uniform_color_texture, 0);
        if (scalingMode == 3){
            glUniform1i(uniform_convert_colors, 0);
        } else {
            glUniform1i(uniform_convert_colors, 1);
        }
        glUniform4f(uniform_i_resolution, textureWidth, textureHeight, 1.0f / textureWidth, 1.0f / textureHeight);
        state.Apply();
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(pass_through_vertices), pass_through_vertices.data());
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    } else if (antialiasingMode == 2){  
        // Landscape Gamma Space Texture
        state.draw.read_framebuffer = textureFBO.handle;
        state.draw.draw_framebuffer = textureFBO.handle;
        state.Apply();
        state.viewport.x = 0;
        state.viewport.y = 0;
        state.viewport.width = textureWidth;
        state.viewport.height = textureHeight;
        state.Apply();
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, intermediateTextures[currScreen][0].handle, 0);  
        glClear(GL_COLOR_BUFFER_BIT);
        state.draw.shader_program = SimplePresent_shader.handle;
        state.Apply();
        AttachUniforms();
        state.texture_units[0].texture_2d = screen_info.display_texture;
        state.texture_units[0].sampler = samplers[1].handle;
        glUniform1i(uniform_color_texture, 0);
        glUniform1i(uniform_convert_colors, 0);
        state.Apply();
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(rotate_vertices), rotate_vertices.data());
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);        

        // Landscape Linear Space Texture
        state.viewport.x = 0;
        state.viewport.y = 0;
        state.viewport.width = textureWidth;
        state.viewport.height = textureHeight;
        state.Apply();
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, intermediateTextures[currScreen][3].handle, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        state.draw.shader_program = SimplePresent_shader.handle;
        state.Apply();
        AttachUniforms();
        state.texture_units[0].texture_2d = intermediateTextures[currScreen][0].handle;
        state.texture_units[0].sampler = samplers[1].handle;
        glUniform1i(uniform_color_texture, 0);
        glUniform1i(uniform_convert_colors, 1);
        state.Apply();
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(pass_through_vertices), pass_through_vertices.data());
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // Edge Detection
        state.viewport.x = 0;
        state.viewport.y = 0;
        state.viewport.width = textureWidth;
        state.viewport.height = textureHeight;
        state.Apply();
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, intermediateTextures[currScreen][1].handle, 0);  
        glClear(GL_COLOR_BUFFER_BIT);
        state.draw.shader_program = SMAA_PASS_0_shader.handle;
        state.Apply();
        AttachUniforms();
        state.texture_units[0].texture_2d = intermediateTextures[currScreen][0].handle;
        state.texture_units[0].sampler = samplers[1].handle;
        glUniform1i(uniform_color_texture, 0);
        glUniform4f(uniform_i_resolution, textureWidth, textureHeight, 1.0f / textureWidth, 1.0f / textureHeight);
        state.Apply();
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(pass_through_vertices), pass_through_vertices.data());
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // Blending Weight Calculation
        state.viewport.x = 0;
        state.viewport.y = 0;
        state.viewport.width = textureWidth;
        state.viewport.height = textureHeight;
        state.Apply();
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, intermediateTextures[currScreen][2].handle, 0);  
        glClear(GL_COLOR_BUFFER_BIT);
        state.draw.shader_program = SMAA_PASS_1_shader.handle;
        state.Apply();
        AttachUniforms();
        state.texture_units[0].texture_2d = intermediateTextures[currScreen][1].handle;
        state.texture_units[0].sampler = samplers[1].handle;
        state.texture_units[1].texture_2d = areatex.handle;
        state.texture_units[1].sampler = samplers[1].handle;
        state.texture_units[2].texture_2d = searchtex.handle;
        state.texture_units[2].sampler = samplers[1].handle;
        GLuint uniform_areatex = glGetUniformLocation(state.draw.shader_program, "areaTex");
        GLuint uniform_searchtex = glGetUniformLocation(state.draw.shader_program, "searchTex");
        glUniform1i(uniform_color_texture, 0);
        glUniform1i(uniform_areatex, 1);
        glUniform1i(uniform_searchtex, 2);
        glUniform4f(uniform_i_resolution, textureWidth, textureHeight, 1.0f / textureWidth, 1.0f / textureHeight);
        state.Apply();
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(pass_through_vertices), pass_through_vertices.data());
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // Neighborhood Blending
        state.viewport.x = 0;
        state.viewport.y = 0;
        state.viewport.width = textureWidth;
        state.viewport.height = textureHeight;
        state.Apply();
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, antialiasFBOTexture[currScreen].handle, 0);  
        glClear(GL_COLOR_BUFFER_BIT);
        state.draw.shader_program = SMAA_PASS_2_shader.handle;
        state.Apply();
        AttachUniforms();
        state.texture_units[0].texture_2d = intermediateTextures[currScreen][2].handle;
        state.texture_units[0].sampler = samplers[1].handle;
        state.texture_units[1].texture_2d = intermediateTextures[currScreen][3].handle;
        state.texture_units[1].sampler = samplers[1].handle;
        GLuint uniform_smaa_input = glGetUniformLocation(state.draw.shader_program, "SMAA_Input");
        glUniform1i(uniform_color_texture, 0);
        glUniform1i(uniform_smaa_input, 1);
        if (scalingMode == 3){
            glUniform1i(uniform_convert_colors, 2);
        } else {
            glUniform1i(uniform_convert_colors, 0);
        }
        glUniform4f(uniform_i_resolution, textureWidth, textureHeight, 1.0f / textureWidth, 1.0f / textureHeight);
        state.Apply();
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(pass_through_vertices), pass_through_vertices.data());
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    } else {
        state.draw.read_framebuffer = textureFBO.handle;
        state.draw.draw_framebuffer = textureFBO.handle;
        state.Apply();
        state.viewport.x = 0;
        state.viewport.y = 0;
        state.viewport.width = textureWidth;
        state.viewport.height = textureHeight;
        state.Apply();
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, antialiasFBOTexture[currScreen].handle, 0);  
        glClear(GL_COLOR_BUFFER_BIT);
        state.draw.shader_program = SimplePresent_shader.handle;
        state.Apply();
        AttachUniforms();
        state.texture_units[0].texture_2d = screen_info.display_texture;
        state.texture_units[0].sampler = samplers[1].handle;
        glUniform1i(uniform_color_texture, 0);
        if (scalingMode == 3){
            glUniform1i(uniform_convert_colors, 0);
        } else {
            glUniform1i(uniform_convert_colors, 1);
        }
        state.Apply();
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(rotate_vertices), rotate_vertices.data());
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    if (scalingMode == 2){
        if (isDownsampling){
            //Output
            state.draw.read_framebuffer = originalReadFramebuffer;
            state.draw.draw_framebuffer = originalDrawFramebuffer;
            state.Apply();
            state.viewport.x = originalViewport[0];
            state.viewport.y = originalViewport[1];
            state.viewport.width = originalViewport[2];
            state.viewport.height = originalViewport[3];
            state.Apply();
            state.draw.shader_program = AREA_SAMPLING_shader.handle;
            state.Apply();
            AttachUniforms();
            state.texture_units[0].texture_2d = antialiasFBOTexture[currScreen].handle;
            state.texture_units[0].sampler = samplers[0].handle;
            glUniform1i(uniform_color_texture, 0);
            glUniform1i(uniform_convert_colors, 2);
            glUniform4f(uniform_i_resolution, textureWidth, textureHeight, 1.0f / textureWidth, 1.0f / textureHeight);
            glUniform4f(uniform_o_resolution, screenWidth, screenHeight, 1.0f / screenWidth, 1.0f / screenHeight);
            glUniformMatrix3x2fv(uniform_modelview_matrix, 1, GL_FALSE, ortho_matrix.data());
            state.Apply();
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(output_vertices), output_vertices.data());
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        } else {
            //Output
            state.draw.read_framebuffer = originalReadFramebuffer;
            state.draw.draw_framebuffer = originalDrawFramebuffer;
            state.Apply();
            state.viewport.x = originalViewport[0];
            state.viewport.y = originalViewport[1];
            state.viewport.width = originalViewport[2];
            state.viewport.height = originalViewport[3];
            state.Apply();
            state.draw.shader_program = Present_shader.handle;
            state.Apply();
            AttachUniforms();
            state.texture_units[0].texture_2d = antialiasFBOTexture[currScreen].handle;
            state.texture_units[0].sampler = samplers[1].handle;
            glUniform1i(uniform_color_texture, 0);
            glUniform1i(uniform_convert_colors, 2);
            glUniformMatrix3x2fv(uniform_modelview_matrix, 1, GL_FALSE, ortho_matrix.data());
            state.Apply();
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(output_vertices), output_vertices.data());
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    } else if (scalingMode == 3){
            //Use intermiedatetextures[currScreen]
            if (isDownsampling){
                // EASU (1x)
                state.viewport.x = 0;
                state.viewport.y = 0;
                state.viewport.width = textureWidth;
                state.viewport.height = textureHeight;
                state.Apply();
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, intermediateTextures[currScreen][4].handle, 0);  
                glClear(GL_COLOR_BUFFER_BIT);
                state.draw.shader_program = FSR_PASS_0_shader.handle;
                state.Apply();
                AttachUniforms();
                state.texture_units[0].texture_2d = antialiasFBOTexture[currScreen].handle;
                state.texture_units[0].sampler = samplers[0].handle;
                glUniform4f(uniform_i_resolution, textureWidth, textureHeight, 1.0f / textureWidth, 1.0f / textureHeight);
                glUniform4f(uniform_o_resolution, textureWidth, textureHeight, 1.0f / textureWidth, 1.0f / textureHeight);
                state.Apply();
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(pass_through_vertices), pass_through_vertices.data());
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

                // RCAS
                state.viewport.x = 0;
                state.viewport.y = 0;
                state.viewport.width = textureWidth;
                state.viewport.height = textureHeight;
                state.Apply();
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, intermediateTextures[currScreen][5].handle, 0);  
                glClear(GL_COLOR_BUFFER_BIT);
                state.draw.shader_program = FSR_PASS_1_shader.handle;
                state.Apply();
                AttachUniforms();
                state.texture_units[0].texture_2d = intermediateTextures[currScreen][4].handle;
                state.texture_units[0].sampler = samplers[1].handle;
                glUniform1f(uniform_fsr_sharpening, fsr_sharpening);
                glUniform4f(uniform_o_resolution, textureWidth, textureHeight, 1.0f / textureWidth, 1.0f / textureHeight);
                state.Apply();
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(pass_through_vertices), pass_through_vertices.data());
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

                // Area Sampling
                state.draw.read_framebuffer = originalReadFramebuffer;
                state.draw.draw_framebuffer = originalDrawFramebuffer;
                state.Apply();
                state.viewport.x = originalViewport[0];
                state.viewport.y = originalViewport[1];
                state.viewport.width = originalViewport[2];
                state.viewport.height = originalViewport[3];
                state.Apply();
                state.draw.shader_program = AREA_SAMPLING_shader.handle;
                state.Apply();
                AttachUniforms();
                state.texture_units[0].texture_2d = intermediateTextures[currScreen][5].handle;
                state.texture_units[0].sampler = samplers[0].handle;
                glUniform1i(uniform_color_texture, 0);
                glUniform1i(uniform_convert_colors, 0);
                glUniform4f(uniform_i_resolution, textureWidth, textureHeight, 1.0f / textureWidth, 1.0f / textureHeight);
                glUniform4f(uniform_o_resolution, screenWidth, screenHeight, 1.0f / screenWidth, 1.0f / screenHeight);
                glUniformMatrix3x2fv(uniform_modelview_matrix, 1, GL_FALSE, ortho_matrix.data());
                state.Apply();
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(output_vertices), output_vertices.data());
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            } else {
                // EASU (to output resolution)
                state.viewport.x = 0;
                state.viewport.y = 0;
                state.viewport.width = screenWidth;
                state.viewport.height = screenHeight;
                state.Apply();
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, intermediateOutputSizeTextures[currOutputScreen][0].handle, 0);  
                glClear(GL_COLOR_BUFFER_BIT);
                state.draw.shader_program = FSR_PASS_0_shader.handle;
                state.Apply();
                AttachUniforms();
                state.texture_units[0].texture_2d = antialiasFBOTexture[currScreen].handle;
                state.texture_units[0].sampler = samplers[0].handle;
                glUniform4f(uniform_i_resolution, textureWidth, textureHeight, 1.0f / textureWidth, 1.0f / textureHeight);
                glUniform4f(uniform_o_resolution, screenWidth, screenHeight, 1.0f / screenWidth, 1.0f / screenHeight);
                state.Apply();
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(pass_through_vertices), pass_through_vertices.data());
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

                // RCAS
                state.viewport.x = 0;
                state.viewport.y = 0;
                state.viewport.width = screenWidth;
                state.viewport.height = screenHeight;
                state.Apply();
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, intermediateOutputSizeTextures[currOutputScreen][1].handle, 0);  
                glClear(GL_COLOR_BUFFER_BIT);
                state.draw.shader_program = FSR_PASS_1_shader.handle;
                state.Apply();
                AttachUniforms();
                state.texture_units[0].texture_2d = intermediateOutputSizeTextures[currOutputScreen][0].handle;
                state.texture_units[0].sampler = samplers[1].handle;
                glUniform1f(uniform_fsr_sharpening, fsr_sharpening);
                glUniform4f(uniform_o_resolution, screenWidth, screenHeight, 1.0f / screenWidth, 1.0f / screenHeight);
                state.Apply();
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(pass_through_vertices), pass_through_vertices.data());
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

                // Normal Present
                state.draw.read_framebuffer = originalReadFramebuffer;
                state.draw.draw_framebuffer = originalDrawFramebuffer;
                state.Apply();
                state.viewport.x = originalViewport[0];
                state.viewport.y = originalViewport[1];
                state.viewport.width = originalViewport[2];
                state.viewport.height = originalViewport[3];
                state.Apply();
                state.draw.shader_program = Present_shader.handle;
                state.Apply();
                AttachUniforms();
                state.texture_units[0].texture_2d = intermediateOutputSizeTextures[currOutputScreen][1].handle;
                state.texture_units[0].sampler = samplers[1].handle;
                glUniform1i(uniform_color_texture, 0);
                glUniform1i(uniform_convert_colors, 0);
                glUniformMatrix3x2fv(uniform_modelview_matrix, 1, GL_FALSE, ortho_matrix.data());
                state.Apply();
                glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(output_vertices), output_vertices.data());
                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }
    } else if (scalingMode == 4){
        //Output
        state.draw.read_framebuffer = originalReadFramebuffer;
        state.draw.draw_framebuffer = originalDrawFramebuffer;
        state.Apply();
        state.viewport.x = originalViewport[0];
        state.viewport.y = originalViewport[1];
        state.viewport.width = originalViewport[2];
        state.viewport.height = originalViewport[3];
        state.Apply();
        state.draw.shader_program = SharpBilinear_shader.handle;
        state.Apply();
        AttachUniforms();
        state.texture_units[0].texture_2d = antialiasFBOTexture[currScreen].handle;
        state.texture_units[0].sampler = samplers[1].handle;
        glUniform1i(uniform_color_texture, 0);
        glUniform1i(uniform_convert_colors, 2);
        glUniform4f(uniform_i_resolution, textureWidth, textureHeight, 1.0f / textureWidth, 1.0f / textureHeight);
        glUniform4f(uniform_o_resolution, screenWidth, screenHeight, 1.0f / screenWidth, 1.0f / screenHeight);
        glUniformMatrix3x2fv(uniform_modelview_matrix, 1, GL_FALSE, ortho_matrix.data());
        state.Apply();
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(output_vertices), output_vertices.data());
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    } else {
        //Output
        state.draw.read_framebuffer = originalReadFramebuffer;
        state.draw.draw_framebuffer = originalDrawFramebuffer;
        state.Apply();
        state.viewport.x = originalViewport[0];
        state.viewport.y = originalViewport[1];
        state.viewport.width = originalViewport[2];
        state.viewport.height = originalViewport[3];
        state.Apply();
        state.draw.shader_program = Present_shader.handle;
        state.Apply();
        AttachUniforms();
        state.texture_units[0].texture_2d = antialiasFBOTexture[currScreen].handle;
        if (scalingMode == 1){
            state.texture_units[0].sampler = samplers[1].handle;
        } else {
            state.texture_units[0].sampler = samplers[0].handle;
        }
        glUniform1i(uniform_color_texture, 0);
        glUniform1i(uniform_convert_colors, 2);
        glUniformMatrix3x2fv(uniform_modelview_matrix, 1, GL_FALSE, ortho_matrix.data());
        state.Apply();
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(output_vertices), output_vertices.data());
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    state.texture_units[0].texture_2d = 0;
    state.texture_units[0].sampler = 0;
    state.Apply();
}

/**
 * Draws a single texture to the emulator window, rotating the texture to correct for the 3DS's LCD
 * rotation.
 */
void RendererOpenGL::DrawSingleScreenStereo(const ScreenInfo& screen_info_l,
                                            const ScreenInfo& screen_info_r, float x, float y,
                                            float w, float h,
                                            Layout::DisplayOrientation orientation) {
    const auto& texcoords = screen_info_l.display_texcoords;

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
        LOG_ERROR(Render_OpenGL, "Unknown DisplayOrientation: {}", orientation);
        break;
    }

    const u32 scale_factor = GetResolutionScaleFactor();
    int scalingMode = static_cast<int>(Settings::values.output_scaling.GetValue());
    const GLuint sampler = samplers[scalingMode > 0 ? 1 : 0].handle;
    glUniform4f(uniform_i_resolution,
                static_cast<float>(screen_info_l.texture.width * scale_factor),
                static_cast<float>(screen_info_l.texture.height * scale_factor),
                1.0f / static_cast<float>(screen_info_l.texture.width * scale_factor),
                1.0f / static_cast<float>(screen_info_l.texture.height * scale_factor));
    glUniform4f(uniform_o_resolution, h, w, 1.0f / h, 1.0f / w);
    glUniformMatrix3x2fv(uniform_modelview_matrix, 1, GL_FALSE, ortho_matrix.data());
    state.texture_units[0].texture_2d = screen_info_l.display_texture;
    state.texture_units[1].texture_2d = screen_info_r.display_texture;
    state.texture_units[0].sampler = sampler;
    state.texture_units[1].sampler = sampler;
    state.Apply();

    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices.data());
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    state.texture_units[0].texture_2d = 0;
    state.texture_units[1].texture_2d = 0;
    state.texture_units[0].sampler = 0;
    state.texture_units[1].sampler = 0;
    state.Apply();
}

/**
 * Draws the emulated screens to the emulator window.
 */
void RendererOpenGL::DrawScreens(const Layout::FramebufferLayout& layout, bool flipped) {
    if (settings.bg_color_update_requested.exchange(false)) {
        // Update background color before drawing
        glClearColor(Settings::values.bg_red.GetValue(), Settings::values.bg_green.GetValue(),
                     Settings::values.bg_blue.GetValue(), 0.0f);
    }

    if (settings.shader_update_requested.exchange(false)) {
        // Update fragment shader before drawing
        Present_shader.Release();
        // Link shaders and get variable locations
        ReloadShader(layout.render_3d_mode);
    }

    const auto& top_screen = layout.top_screen;
    const auto& bottom_screen = layout.bottom_screen;

    // Track Texture Changes
    currTopTextureWidth = static_cast<float>(screen_infos[0].texture.height * GetResolutionScaleFactor());
    currTopTextureHeight = static_cast<float>(screen_infos[0].texture.width * GetResolutionScaleFactor());
    currBottomTextureWidth = static_cast<float>(screen_infos[2].texture.height * GetResolutionScaleFactor());
    currBottomTextureHeight = static_cast<float>(screen_infos[2].texture.width * GetResolutionScaleFactor());
    if (currTopTextureWidth != prevTopTextureWidth || currTopTextureHeight != prevTopTextureHeight || currBottomTextureWidth != prevBottomTextureWidth || currBottomTextureHeight != prevBottomTextureHeight){
        AllocatePPTextures();
        // LOG_INFO(Render_OpenGL, "PrevTopTexture Res: {}x{}, CurrTopTexture Res: {}x{}, PrevBottomTexture Res: {}x{}, CurrBottomTexture Res: {}x{}", prevTopTextureWidth, prevTopTextureHeight, currTopTextureWidth, currTopTextureHeight, prevBottomTextureWidth, prevBottomTextureHeight, currBottomTextureWidth, currBottomTextureHeight);
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
    }
    prevOutputScreenRects[0] = currOutputScreenRects[0];
    prevOutputScreenRects[1] = currOutputScreenRects[1];
    prevOutputScreenRects[2] = currOutputScreenRects[2];

    //Set the Viewport
    state.viewport.x = 0;
    state.viewport.y = 0;
    state.viewport.width = layout.width;
    state.viewport.height = layout.height;
    originalViewport = {0, 0, static_cast<int>(layout.width), static_cast<int>(layout.height)};

    state.Apply();
    
    if (render_window.NeedsClearing()) {
        glClear(GL_COLOR_BUFFER_BIT);
    }

    // Set projection matrix
    ortho_matrix = MakeOrthographicMatrix((float)layout.width, (float)layout.height, flipped);

    // Bind texture in Texture Unit 0
    glUniform1i(uniform_color_texture, 0);

    const bool stereo_single_screen =
        layout.render_3d_mode == Settings::StereoRenderOption::Anaglyph ||
        layout.render_3d_mode == Settings::StereoRenderOption::Interlaced ||
        layout.render_3d_mode == Settings::StereoRenderOption::ReverseInterlaced;

    // Bind a second texture for the right eye if in Anaglyph mode
    if (stereo_single_screen) {
        glUniform1i(uniform_color_texture_r, 1);
    }

    glUniform1i(uniform_layer, 0);
    if (!Settings::values.swap_screen.GetValue()) {
        currOutputScreen = 0;
        DrawTopScreen(layout, top_screen);
        glUniform1i(uniform_layer, 0);
        ApplySecondLayerOpacity(layout.bottom_opacity);
        currOutputScreen = 1;
        DrawBottomScreen(layout, bottom_screen);
    } else {
        currOutputScreen = 1;
        DrawBottomScreen(layout, bottom_screen);
        glUniform1i(uniform_layer, 0);
        ApplySecondLayerOpacity(layout.top_opacity);
        currOutputScreen = 0;
        DrawTopScreen(layout, top_screen);
    }

    if (layout.additional_screen_enabled) {
        currOutputScreen = 2;
        const auto& additional_screen = layout.additional_screen;
        if (!Settings::values.swap_screen.GetValue()) {
            DrawTopScreen(layout, additional_screen);
        } else {
            DrawBottomScreen(layout, additional_screen);
        }
    }
    ResetSecondLayerOpacity();
}

void RendererOpenGL::ApplySecondLayerOpacity(float opacity) {
    state.blend.src_rgb_func = GL_CONSTANT_ALPHA;
    state.blend.src_a_func = GL_CONSTANT_ALPHA;
    state.blend.dst_a_func = GL_ONE_MINUS_CONSTANT_ALPHA;
    state.blend.dst_rgb_func = GL_ONE_MINUS_CONSTANT_ALPHA;
    state.blend.color.alpha = opacity;
}

void RendererOpenGL::ResetSecondLayerOpacity() {
    state.blend.src_rgb_func = GL_ONE;
    state.blend.dst_rgb_func = GL_ZERO;
    state.blend.src_a_func = GL_ONE;
    state.blend.dst_a_func = GL_ZERO;
    state.blend.color.alpha = 0.0f;
}

void RendererOpenGL::DrawTopScreen(const Layout::FramebufferLayout& layout,
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
        DrawSingleScreen(screen_infos[eye], top_screen_left, top_screen_top, top_screen_width,
                         top_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::SideBySide: {
        DrawSingleScreen(screen_infos[leftside], top_screen_left / 2, top_screen_top,
                         top_screen_width / 2, top_screen_height, orientation);
        glUniform1i(uniform_layer, 1);
        DrawSingleScreen(screen_infos[rightside],
                         static_cast<float>((top_screen_left / 2) + (layout.width / 2)),
                         top_screen_top, top_screen_width / 2, top_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::SideBySideFull: {
        DrawSingleScreen(screen_infos[leftside], top_screen_left, top_screen_top, top_screen_width,
                         top_screen_height, orientation);
        glUniform1i(uniform_layer, 1);
        DrawSingleScreen(screen_infos[rightside],
                         static_cast<float>(top_screen_left + layout.width / 2), top_screen_top,
                         top_screen_width, top_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::CardboardVR: {
        DrawSingleScreen(screen_infos[leftside], top_screen_left, top_screen_top, top_screen_width,
                         top_screen_height, orientation);
        glUniform1i(uniform_layer, 1);
        DrawSingleScreen(
            screen_infos[rightside],
            static_cast<float>(layout.cardboard.top_screen_right_eye + (layout.width / 2)),
            top_screen_top, top_screen_width, top_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::Anaglyph:
    case Settings::StereoRenderOption::Interlaced:
    case Settings::StereoRenderOption::ReverseInterlaced: {
        DrawSingleScreenStereo(screen_infos[leftside], screen_infos[rightside], top_screen_left,
                               top_screen_top, top_screen_width, top_screen_height, orientation);
        break;
    }
    }
}

void RendererOpenGL::DrawBottomScreen(const Layout::FramebufferLayout& layout,
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
        DrawSingleScreen(screen_infos[2], bottom_screen_left, bottom_screen_top,
                         bottom_screen_width, bottom_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::SideBySide: // Bottom screen is identical on both sides
    {

        DrawSingleScreen(screen_infos[2], bottom_screen_left / 2, bottom_screen_top,
                         bottom_screen_width / 2, bottom_screen_height, orientation);
        glUniform1i(uniform_layer, 1);
        DrawSingleScreen(
            screen_infos[2], static_cast<float>((bottom_screen_left / 2) + (layout.width / 2)),
            bottom_screen_top, bottom_screen_width / 2, bottom_screen_height, orientation);

        break;
    }
    case Settings::StereoRenderOption::SideBySideFull: {
        DrawSingleScreen(screen_infos[2], bottom_screen_left, bottom_screen_top,
                         bottom_screen_width, bottom_screen_height, orientation);
        glUniform1i(uniform_layer, 1);
        DrawSingleScreen(screen_infos[2], bottom_screen_left + layout.width / 2, bottom_screen_top,
                         bottom_screen_width, bottom_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::CardboardVR: {
        DrawSingleScreen(screen_infos[2], bottom_screen_left, bottom_screen_top,
                         bottom_screen_width, bottom_screen_height, orientation);
        glUniform1i(uniform_layer, 1);
        DrawSingleScreen(
            screen_infos[2],
            static_cast<float>(layout.cardboard.bottom_screen_right_eye + (layout.width / 2)),
            bottom_screen_top, bottom_screen_width, bottom_screen_height, orientation);
        break;
    }
    case Settings::StereoRenderOption::Anaglyph:
    case Settings::StereoRenderOption::Interlaced:
    case Settings::StereoRenderOption::ReverseInterlaced: {
        DrawSingleScreenStereo(screen_infos[2], screen_infos[2], bottom_screen_left,
                               bottom_screen_top, bottom_screen_width, bottom_screen_height,
                               orientation);
        break;
    }
    }
}

void RendererOpenGL::TryPresent(int timeout_ms, bool is_secondary) {
    const auto& window = is_secondary ? *secondary_window : render_window;
    const auto& layout = window.GetFramebufferLayout();
    auto frame = window.mailbox->TryGetPresentFrame(timeout_ms);
    if (!frame) {
        LOG_DEBUG(Render_OpenGL, "TryGetPresentFrame returned no frame to present");
        return;
    }

    // Clearing before a full overwrite of a fbo can signal to drivers that they can avoid a
    // readback since we won't be doing any blending
    glClear(GL_COLOR_BUFFER_BIT);

    // Recreate the presentation FBO if the color attachment was changed
    if (frame->color_reloaded) {
        LOG_DEBUG(Render_OpenGL, "Reloading present frame");
        window.mailbox->ReloadPresentFrame(frame, layout.width, layout.height);
    }
    glWaitSync(frame->render_fence, 0, GL_TIMEOUT_IGNORED);
    // INTEL workaround.
    // Normally we could just delete the draw fence here, but due to driver bugs, we can just delete
    // it on the emulation thread without too much penalty
    // glDeleteSync(frame.render_sync);
    // frame.render_sync = 0;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, frame->present.handle);
    glBlitFramebuffer(0, 0, frame->width, frame->height, 0, 0, layout.width, layout.height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // Delete the fence if we're re-presenting to avoid leaking fences
    if (frame->present_fence) {
        glDeleteSync(frame->present_fence);
    }

    /* insert fence for the main thread to block on */
    frame->present_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

void RendererOpenGL::PrepareVideoDumping() {
    auto* mailbox = static_cast<OGLVideoDumpingMailbox*>(frame_dumper.mailbox.get());
    {
        std::scoped_lock lock{mailbox->swap_chain_lock};
        mailbox->quit = false;
    }
    frame_dumper.StartDumping();
}

void RendererOpenGL::CleanupVideoDumping() {
    frame_dumper.StopDumping();
    auto* mailbox = static_cast<OGLVideoDumpingMailbox*>(frame_dumper.mailbox.get());
    {
        std::scoped_lock lock{mailbox->swap_chain_lock};
        mailbox->quit = true;
    }
    mailbox->free_cv.notify_one();
}

} // namespace OpenGL
