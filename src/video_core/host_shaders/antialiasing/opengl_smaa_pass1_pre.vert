//? #version 450
// SPDX-License-Identifier: Unlicense
//-----------------------------------------------------------------------------
// Blending Weight Calculation Shader (Second Pass)

uniform vec4 i_resolution;
#define SMAA_RT_METRICS vec4(i_resolution.z, i_resolution.w, i_resolution.x, i_resolution.y)
#define SMAA_GLSL_4
#define SMAA_PRESET_ULTRA
#define SMAA_EDT 1.0

layout(location = 0) in vec2 vert_position;
layout(location = 1) in vec2 vert_tex_coord;
layout(location = 0) out vec2 frag_tex_coord;
layout(location = 1) out vec2 pixcoord;
layout(location = 2) out vec4 offset[3];

#define SMAA_INCLUDE_PS 0
//#include "SMAA.hlsl"