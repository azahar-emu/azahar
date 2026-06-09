#version 450 core
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec2 vert_position;
layout(location = 1) in vec2 vert_tex_coord;
layout(location = 0) out vec2 frag_tex_coord;
layout(location = 1) out vec2 precalc_texel;
layout(location = 2) out vec2 precalc_scale;
layout (push_constant, std140) uniform DrawInfo {
    mat4 modelview_matrix;
    vec4 i_resolution;
    vec4 o_resolution;
    int screen_id_l;
    int screen_id_r;
    int layer;
    int reverse_interlaced;
    int convert_colors;
};

void main()
{
    vec4 position = vec4(vert_position, 0.0, 1.0) * modelview_matrix;
    gl_Position = vec4(position.x, position.y, 0.0, 1.0);
    frag_tex_coord = vert_tex_coord;
    precalc_scale = ceil(o_resolution.xy / i_resolution.xy);
    precalc_texel = vert_tex_coord.xy * i_resolution.xy;
}