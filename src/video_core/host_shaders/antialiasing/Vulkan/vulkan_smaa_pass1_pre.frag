#version 450 core
#extension GL_ARB_separate_shader_objects : enable

// SPDX-License-Identifier: Unlicense
//-----------------------------------------------------------------------------
// Blending Weight Calculation Shader (Second Pass)
layout (push_constant, std140) uniform DrawInfo {
    mat4 modelview_matrix;
    vec4 i_resolution;
    vec4 o_resolution;
    int screen_id_l;
    int screen_id_r;
    int layer;
    int reverse_interlaced;
    int convert_colors;
    int areatex;
    int searchtex;
    int smaa_input;
};

#define SMAA_RT_METRICS vec4(i_resolution.z, i_resolution.w, i_resolution.x, i_resolution.y)
#define SMAA_GLSL_4
#define SMAA_PRESET_ULTRA
#define SMAA_EDT 1.0

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 1) in vec2 pixcoord;
layout(location = 2) in vec4 offset[3];
layout(location = 0) out vec4 color;
layout (set = 0, binding = 0) uniform sampler2D screen_textures[3];
/*
screen_textures[0] = color_texture
screen_textures[1] = areaTex;
screen_textures[2] = searchTex;
*/
#define SMAA_INCLUDE_VS 0
//#include "SMAA.hlsl"