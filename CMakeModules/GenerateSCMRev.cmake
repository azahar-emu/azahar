list(APPEND CMAKE_MODULE_PATH "${SRC_DIR}/CMakeModules")
include(GenerateBuildInfo)
generate_build_info()

# The variable SRC_DIR must be passed into the script (since it uses the current build directory for all values of CMAKE_*_DIR)
set(VIDEO_CORE "${SRC_DIR}/src/video_core")
set(HASH_FILES
    "${VIDEO_CORE}/renderer_opengl/gl_shader_disk_cache.cpp"
    "${VIDEO_CORE}/renderer_opengl/gl_shader_disk_cache.h"
    "${VIDEO_CORE}/renderer_opengl/gl_shader_util.cpp"
    "${VIDEO_CORE}/renderer_opengl/gl_shader_util.h"
    "${VIDEO_CORE}/renderer_vulkan/vk_shader_util.cpp"
    "${VIDEO_CORE}/renderer_vulkan/vk_shader_util.h"
    "${VIDEO_CORE}/shader/generator/glsl_fs_shader_gen.cpp"
    "${VIDEO_CORE}/shader/generator/glsl_fs_shader_gen.h"
    "${VIDEO_CORE}/shader/generator/glsl_shader_decompiler.cpp"
    "${VIDEO_CORE}/shader/generator/glsl_shader_decompiler.h"
    "${VIDEO_CORE}/shader/generator/glsl_shader_gen.cpp"
    "${VIDEO_CORE}/shader/generator/glsl_shader_gen.h"
    "${VIDEO_CORE}/shader/generator/pica_fs_config.cpp"
    "${VIDEO_CORE}/shader/generator/pica_fs_config.h"
    "${VIDEO_CORE}/shader/generator/shader_gen.cpp"
    "${VIDEO_CORE}/shader/generator/shader_gen.h"
    "${VIDEO_CORE}/shader/generator/shader_uniforms.cpp"
    "${VIDEO_CORE}/shader/generator/shader_uniforms.h"
    "${VIDEO_CORE}/shader/generator/spv_fs_shader_gen.cpp"
    "${VIDEO_CORE}/shader/generator/spv_fs_shader_gen.h"
    "${VIDEO_CORE}/shader/shader.cpp"
    "${VIDEO_CORE}/shader/shader.h"
    "${VIDEO_CORE}/pica/regs_framebuffer.h"
    "${VIDEO_CORE}/pica/regs_lighting.h"
    "${VIDEO_CORE}/pica/regs_pipeline.h"
    "${VIDEO_CORE}/pica/regs_rasterizer.h"
    "${VIDEO_CORE}/pica/regs_shader.h"
    "${VIDEO_CORE}/pica/regs_texturing.h"
    "${VIDEO_CORE}/pica/regs_internal.cpp"
    "${VIDEO_CORE}/pica/regs_internal.h"
)
set(COMBINED "")
foreach (F IN LISTS HASH_FILES)
    file(READ ${F} TMP)
    set(COMBINED "${COMBINED}${TMP}")
endforeach()
string(MD5 SHADER_CACHE_VERSION "${COMBINED}")
configure_file("${SRC_DIR}/src/common/scm_rev.cpp.in" "scm_rev.cpp" @ONLY)
