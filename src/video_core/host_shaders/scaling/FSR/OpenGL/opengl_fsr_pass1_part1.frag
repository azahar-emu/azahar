// FSR - [RCAS] ROBUST CONTRAST ADAPTIVE SHARPENING
layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 color;
layout(binding = 0) uniform sampler2D color_texture;
uniform float FSR_SHARPENING;
uniform vec4 o_resolution;

#define A_GPU 1
#define A_GLSL 1
// #include "ffx_a.h"