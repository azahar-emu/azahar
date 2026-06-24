//? #version 450
// SPDX-License-Identifier: Unlicense
//-----------------------------------------------------------------------------
// Neighborhood Blending Shader (Third Pass)

uniform vec4 i_resolution;
uniform int convert_colors;
#define SMAA_RT_METRICS vec4(i_resolution.z, i_resolution.w, i_resolution.x, i_resolution.y)
#define SMAA_GLSL_4
#define SMAA_FLIP_Y 1

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 1) in vec4 offset;
layout(location = 0) out vec4 color;
layout(binding = 0) uniform sampler2D color_texture;
uniform sampler2D SMAA_Input;

#define SMAA_INCLUDE_VS 0
#include "SMAA.hlsl"

vec3 LinearTosRGB(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0/2.4)) - 0.055, step(0.0031308, c));
}

void main() {
    vec4 pixel = SMAANeighborhoodBlendingPS(frag_tex_coord, offset, SMAA_Input, color_texture);
    if (convert_colors == 2){
        pixel = vec4(LinearTosRGB(pixel.rgb), pixel.a);
    }
    color = pixel;
}
