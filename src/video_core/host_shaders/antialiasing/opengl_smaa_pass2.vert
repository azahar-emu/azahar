//? #version 450
// SPDX-License-Identifier: Unlicense
//-----------------------------------------------------------------------------
// Neighborhood Blending Shader (Third Pass)

uniform vec4 i_resolution;
#define SMAA_RT_METRICS vec4(i_resolution.z, i_resolution.w, i_resolution.x, i_resolution.y)
#define SMAA_GLSL_4

layout(location = 0) in vec2 vert_position;
layout(location = 1) in vec2 vert_tex_coord;
layout(location = 0) out vec2 frag_tex_coord;
layout(location = 1) out vec4 offset;

#define SMAA_INCLUDE_PS 0
//#include "SMAA.hlsl"








void main() {
    gl_Position = vec4(vert_position, 0.0, 1.0);
    frag_tex_coord = vert_tex_coord;
    SMAANeighborhoodBlendingVS(vert_tex_coord, offset);
}