// FSR - [RCAS] ROBUST CONTRAST ADAPTIVE SHARPENING
#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 color;
layout (set = 0, binding = 0) uniform sampler2D color_texture;
layout (push_constant, std140) uniform DrawInfo {
    mat4 modelview_matrix;
    vec4 i_resolution;
    vec4 o_resolution;
    int screen_id_l;
    int screen_id_r;
    int layer;
    int reverse_interlaced;
    int convert_colors;
    float FSR_SHARPENING;
};

#define A_GPU 1
#define A_GLSL 1
// #include "ffx_a.h"