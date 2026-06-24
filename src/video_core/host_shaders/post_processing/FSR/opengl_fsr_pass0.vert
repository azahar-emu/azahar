// FSR - [EASU] EDGE ADAPTIVE SPATIAL UPSAMPLING
// SM 4.0 compatible: no textureGather, direct texelFetch of 12 unique texels.
layout(location = 0) in vec2 vert_position;
layout(location = 1) in vec2 vert_tex_coord;
layout(location = 0) out vec2 frag_tex_coord;

void main()
{
   gl_Position = vec4(vert_position, 0.0, 1.0);
   frag_tex_coord = vert_tex_coord;
}

