//? #version 460
layout(location = 0) in vec2 vert_position;
layout(location = 1) in vec2 vert_tex_coord;
layout(location = 0) out vec2 frag_tex_coord;
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
}

