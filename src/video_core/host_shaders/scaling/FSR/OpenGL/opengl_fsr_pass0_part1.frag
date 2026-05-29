// FSR - [EASU] EDGE ADAPTIVE SPATIAL UPSAMPLING
// SM 4.0 compatible: no textureGather, direct texelFetch of 12 unique texels.
layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 color;
layout(binding = 0) uniform sampler2D color_texture;
uniform vec4 i_resolution;
uniform vec4 o_resolution;

#define A_GPU 1
#define A_GLSL 1
// #include "ffx_a.h"

// // We intentionally do NOT define FSR_EASU_F here.
// // We only need FsrEasuCon (which compiles under A_GPU alone),
// // and we inline the EASU filter logic below to avoid the
// // textureGather-based callback system entirely.
// // This yields 12 texelFetch calls instead of the original
// // 12 textureGather calls (4 gathers x 3 channels), and is
// // faster than emulating gathers with 48 individual fetches.
// #include "ffx_fsr1.h"
