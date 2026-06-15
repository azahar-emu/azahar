# Copyright 2023 Citra Emulator Project
# Licensed under GPLv2 or any later version
# Refer to the license.txt file included.

set(SHADER_FILES
    format_reinterpreter/d24s8_to_rgba8.frag
    format_reinterpreter/rgba4_to_rgb5a1.frag
    format_reinterpreter/vulkan_d24s8_to_rgba8.comp
    texture_filtering/bicubic.frag
    texture_filtering/refine.frag
    texture_filtering/scale_force.frag
    texture_filtering/xbrz_freescale.frag
    texture_filtering/mmpx.frag
    texture_filtering/x_gradient.frag
    texture_filtering/y_gradient.frag
    post_processing/AREA/opengl_area_sampling.frag
    post_processing/AREA/opengl_area_sampling.vert
    post_processing/AREA/vulkan_area_sampling.frag
    post_processing/AREA/vulkan_area_sampling.vert
    post_processing/FSR/ffx_a.h
    post_processing/FSR/ffx_fsr1.h
    post_processing/FSR/opengl_fsr_pass0.frag
    post_processing/FSR/opengl_fsr_pass0.vert
    post_processing/FSR/opengl_fsr_pass1.frag
    post_processing/FSR/opengl_fsr_pass1.vert
    post_processing/FSR/vulkan_fsr_pass0.frag
    post_processing/FSR/vulkan_fsr_pass0.vert
    post_processing/FSR/vulkan_fsr_pass1.frag
    post_processing/FSR/vulkan_fsr_pass1.vert
    post_processing/SGSR/opengl_sgsr.frag
    post_processing/SGSR/opengl_sgsr.vert
    post_processing/SGSR/vulkan_sgsr.frag
    post_processing/SGSR/vulkan_sgsr.vert
    post_processing/FXAA/opengl_fxaa.frag
    post_processing/FXAA/opengl_fxaa.vert
    post_processing/FXAA/vulkan_fxaa.frag
    post_processing/FXAA/vulkan_fxaa.vert
    post_processing/SHARPBILINEAR/opengl_sharpbilinear.frag
    post_processing/SHARPBILINEAR/opengl_sharpbilinear.vert
    post_processing/SHARPBILINEAR/vulkan_sharpbilinear.frag
    post_processing/SHARPBILINEAR/vulkan_sharpbilinear.vert
    post_processing/SMAA/textures/AreaTex.h
    post_processing/SMAA/textures/SearchTex.h
    post_processing/SMAA/opengl_smaa_pass0.frag
    post_processing/SMAA/opengl_smaa_pass0.vert
    post_processing/SMAA/opengl_smaa_pass1.frag
    post_processing/SMAA/opengl_smaa_pass1.vert
    post_processing/SMAA/opengl_smaa_pass2.frag
    post_processing/SMAA/opengl_smaa_pass2.vert
    post_processing/SMAA/smaa.hlsl
    post_processing/SMAA/vulkan_smaa_pass0.frag
    post_processing/SMAA/vulkan_smaa_pass0.vert
    post_processing/SMAA/vulkan_smaa_pass1.frag
    post_processing/SMAA/vulkan_smaa_pass1.vert
    post_processing/SMAA/vulkan_smaa_pass2.frag
    post_processing/SMAA/vulkan_smaa_pass2.vert
    full_screen_triangle.vert
    opengl_present.frag
    opengl_present.vert
    opengl_present_anaglyph.frag
    opengl_present_interlaced.frag
    opengl_simple_present.frag
    opengl_simple_present.vert
    vulkan_depth_to_buffer.comp
    vulkan_present.frag
    vulkan_present.vert
    vulkan_present_anaglyph.frag
    vulkan_present_interlaced.frag
    vulkan_blit_depth_stencil.frag
    vulkan_simple_present.frag
    vulkan_simple_present.vert
    vulkan_cursor.frag
    vulkan_cursor.vert
)

set(SHADER_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/include)
set(SHADER_DIR ${SHADER_INCLUDE}/video_core/host_shaders)
set(HOST_SHADERS_INCLUDE ${SHADER_INCLUDE} PARENT_SCOPE)

set(INPUT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/source_shader.h.in)
set(HEADER_GENERATOR ${CMAKE_CURRENT_SOURCE_DIR}/StringShaderHeader.cmake)

foreach(FILENAME IN ITEMS ${SHADER_FILES})
    string(REPLACE "." "_" SHADER_NAME ${FILENAME})
    set(SOURCE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/${FILENAME})
    set(SOURCE_HEADER_FILE ${SHADER_DIR}/${SHADER_NAME}.h)
    add_custom_command(
        OUTPUT
            ${SOURCE_HEADER_FILE}
        COMMAND
            ${CMAKE_COMMAND} -P ${HEADER_GENERATOR} ${SOURCE_FILE} ${SOURCE_HEADER_FILE} ${INPUT_FILE}
        MAIN_DEPENDENCY
            ${SOURCE_FILE}
        DEPENDS
            ${INPUT_FILE}
            # HEADER_GENERATOR should be included here but msbuild seems to assume it's always modified
    )
    set(SHADER_HEADERS ${SHADER_HEADERS} ${SOURCE_HEADER_FILE})
endforeach()

set(SHADER_SOURCES ${SHADER_FILES})
list(APPEND SHADER_SOURCES ${GLSL_INCLUDES})

add_custom_target(host_shaders
    DEPENDS
        ${SHADER_HEADERS}
    SOURCES
        ${SHADER_SOURCES}
)
