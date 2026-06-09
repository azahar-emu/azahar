layout(location = 0) in vec2 vert_position;
layout(location = 1) in vec2 vert_tex_coord;
layout(location = 0) out vec2 frag_tex_coord;
layout(location = 1) out vec2 precalc_texel;
layout(location = 2) out vec2 precalc_scale;
uniform mat3x2 modelview_matrix;
uniform vec4 i_resolution;
uniform vec4 o_resolution;

void main()
{
    gl_Position = vec4(mat2(modelview_matrix) * vert_position + modelview_matrix[2], 0.0, 1.0);
    frag_tex_coord = vert_tex_coord;
    precalc_scale = ceil(o_resolution.xy / i_resolution.xy);
    precalc_texel = vert_tex_coord.xy * i_resolution.xy;
}