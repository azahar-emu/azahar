// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>

#include "audio_core/audio_types.h"
#include "audio_core/libretro_sink.h"
#include "common/scm_rev.h"
#include "environment.h"

#ifdef HAVE_LIBRETRO_VFS
#include "streams/file_stream.h"
#endif

using namespace LibRetro;

namespace LibRetro {

namespace {

static retro_video_refresh_t video_cb;
// static retro_audio_sample_t audio_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

} // namespace

bool GetSoftwareFramebuffer(retro_framebuffer* fb, int width, int height) {
    fb->data = nullptr;
    fb->width = width;
    fb->height = height;
    fb->pitch = 0;
    fb->format = RETRO_PIXEL_FORMAT_XRGB8888;
    fb->access_flags = RETRO_MEMORY_ACCESS_WRITE;
    fb->memory_flags = 0;
    return environ_cb(RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER, fb);
}

void UploadVideoFrame(const void* data, unsigned width, unsigned height, size_t pitch) {
    return video_cb(data, width, height, pitch);
}

bool SetHWSharedContext() {
    return environ_cb(RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT, NULL);
}

void PollInput() {
    return input_poll_cb();
}

Settings::GraphicsAPI GetPreferredRenderer() {
    // try and maintain the current driver
    retro_hw_context_type context_type = RETRO_HW_CONTEXT_OPENGL;
    environ_cb(RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER, &context_type);
    switch (context_type) {
#ifdef ENABLE_OPENGL
    case RETRO_HW_CONTEXT_OPENGL:
    case RETRO_HW_CONTEXT_OPENGL_CORE:
    case RETRO_HW_CONTEXT_OPENGLES2:
    case RETRO_HW_CONTEXT_OPENGLES3:
    case RETRO_HW_CONTEXT_OPENGLES_VERSION:
        return Settings::GraphicsAPI::OpenGL;
#endif
#ifdef ENABLE_VULKAN
    case RETRO_HW_CONTEXT_VULKAN:
        return Settings::GraphicsAPI::Vulkan;
#endif
    default:
        break;
    }
    // we can't maintain the current driver, need to switch
#if defined(ENABLE_VULKAN)
    return Settings::GraphicsAPI::Vulkan;
#elif defined(ENABLE_OPENGL)
    return Settings::GraphicsAPI::OpenGL;
#else
    return Settings::GraphicsAPI::Software;
#endif
}

bool SetVariables(const retro_variable vars[]) {
    return environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void*)vars);
}

bool SetCoreOptionsV2(const retro_core_options_v2* options) {
    return environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, (void*)options);
}

bool SetCoreOptionsV1(const retro_core_option_definition* options) {
    return environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, (void*)options);
}

bool GetCoreOptionsVersion(unsigned* version) {
    return environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, version);
}

bool SetControllerInfo(const retro_controller_info info[]) {
    return environ_cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void*)info);
}

bool SetPixelFormat(const retro_pixel_format fmt) {
    return environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, (void*)&fmt);
}

bool SetHWRenderer(retro_hw_render_callback* cb) {
    return environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER, cb);
}

bool GetHWRenderInterface(void** interface) {
    return environ_cb(RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE, interface) && !!*interface;
}

bool SetHWRenderContextNegotiationInterface(void** interface) {
    return environ_cb(RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE, interface) &&
           !!*interface;
}

bool SetAudioCallback(retro_audio_callback* cb) {
    return environ_cb(RETRO_ENVIRONMENT_SET_AUDIO_CALLBACK, cb);
}

bool SetFrameTimeCallback(retro_frame_time_callback* cb) {
    return environ_cb(RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK, cb);
}

bool SetGeometry(retro_system_av_info* cb) {
    return environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, cb);
}

bool SetInputDescriptors(const retro_input_descriptor desc[]) {
    return environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void*)desc);
}

bool HasUpdatedConfig() {
    bool updated = false;
    return environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated;
}

bool Shutdown() {
    return environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
}

/// Displays the specified message to the screen.
bool DisplayMessage(const char* sg) {
    LOG_CRITICAL(Frontend, "{}", sg);
    retro_message msg;
    msg.msg = sg;
    msg.frames = 60 * 10;
    return environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
}

std::string FetchVariable(std::string key, std::string def) {
    struct retro_variable var = {nullptr};
    var.key = key.c_str();
    if (!environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) || var.value == nullptr) {
        // Fetching variable failed.
        LOG_ERROR(Frontend, "Fetching variable {} failed.", key);
        return def;
    }
    return std::string(var.value);
}

std::string GetSaveDir() {
    char* var = nullptr;
    if (!environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &var) || var == nullptr) {
        // Fetching variable failed.
        LOG_ERROR(Frontend, "No save directory provided by LibRetro.");
        return std::string();
    }
    return std::string(var);
}

std::string GetSystemDir() {
    char* var = nullptr;
    if (!environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &var) || var == nullptr) {
        // Fetching variable failed.
        LOG_ERROR(Frontend, "No system directory provided by LibRetro.");
        return std::string();
    }
    return std::string(var);
}

retro_log_printf_t GetLoggingBackend() {
    retro_log_callback callback{};
    if (!environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &callback)) {
        return nullptr;
    }
    return callback.log;
}

int16_t CheckInput(unsigned port, unsigned device, unsigned index, unsigned id) {
    return input_state_cb(port, device, index, id);
}

#ifdef HAVE_LIBRETRO_VFS
void SetVFSCallback(struct retro_vfs_interface_info* vfs_iface_info) {
    if (environ_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, vfs_iface_info))
        filestream_vfs_init(vfs_iface_info);
}
#endif

#ifdef IOS
bool CanUseJIT() {
    bool can_jit = false;
    return environ_cb(RETRO_ENVIRONMENT_GET_JIT_CAPABLE, &can_jit) && can_jit;
}
#endif

}; // namespace LibRetro

void retro_get_system_info(struct retro_system_info* info) {
    memset(info, 0, sizeof(*info));
    info->library_name = "Azahar";
    info->library_version = Common::g_scm_desc;
    info->need_fullpath = true;
    info->valid_extensions = "3ds|3dsx|cia|elf";
}

void retro_set_audio_sample(retro_audio_sample_t cb) {
    // We don't need single audio sample callbacks.
}

void retro_set_input_poll(retro_input_poll_t cb) {
    LibRetro::input_poll_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb) {
    LibRetro::video_cb = cb;
}
void retro_set_environment(retro_environment_t cb) {
    LibRetro::environ_cb = cb;
    LibRetro::OnConfigureEnvironment();
}

void retro_set_controller_port_device(unsigned port, unsigned device) {}

void retro_set_input_state(retro_input_state_t cb) {
    input_state_cb = cb;
}

void retro_get_system_av_info(struct retro_system_av_info* info) {
    // These are placeholders until we get control.
    info->timing.fps = 60.0;
    info->timing.sample_rate = AudioCore::native_sample_rate;
    info->geometry.base_width = 400;
    info->geometry.base_height = 480;
    info->geometry.max_width = 400 * 10;
    info->geometry.max_height = 480 * 10;
    info->geometry.aspect_ratio = 0;
}
