//? #version 450
// SPDX-License-Identifier: Unlicense
//-----------------------------------------------------------------------------
// Edge Detection Shaders (First Pass)

uniform vec4 i_resolution;
#define SMAA_RT_METRICS vec4(i_resolution.z, i_resolution.w, i_resolution.x, i_resolution.y)
#define SMAA_GLSL_4
#define SMAA_FLIP_Y 1
#define SMAA_PRESET_ULTRA
#define SMAA_EDT 1.0

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 1) in vec4 offset[3];
layout(location = 0) out vec4 color;
layout(binding = 0) uniform sampler2D color_texture;

#define SMAA_INCLUDE_VS 0
#include "SMAA.hlsl"

void main() {
    if (SMAA_EDT == 0.0) {
        color = vec4(SMAALumaEdgeDetectionPS(frag_tex_coord, offset, color_texture), 0.0, 0.0);
    } else if (SMAA_EDT <= 1.0) {
        color = vec4(SMAAColorEdgeDetectionPS(frag_tex_coord, offset, color_texture), 0.0, 0.0);
    }
}
