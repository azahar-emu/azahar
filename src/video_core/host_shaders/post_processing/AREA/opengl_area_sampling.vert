//? #version 460
layout(location = 0) in vec2 vert_position;
layout(location = 1) in vec2 vert_tex_coord;
layout(location = 0) out vec2 frag_tex_coord;
uniform mat3x2 modelview_matrix;
void main()
{
   gl_Position = vec4(mat2(modelview_matrix) * vert_position + modelview_matrix[2], 0.0, 1.0);
   frag_tex_coord = vert_tex_coord;
}

