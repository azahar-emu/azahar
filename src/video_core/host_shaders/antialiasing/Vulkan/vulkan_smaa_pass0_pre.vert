#version 450 core
#extension GL_ARB_separate_shader_objects : enable

// SPDX-License-Identifier: Unlicense
//-----------------------------------------------------------------------------
// Edge Detection Shaders (First Pass)
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

layout(location = 0) in vec2 vert_position;
layout(location = 1) in vec2 vert_tex_coord;
layout(location = 0) out vec2 frag_tex_coord;
layout(location = 1) out vec4 offset[3];

#define SMAA_INCLUDE_PS 0
//#include "SMAA.hlsl"