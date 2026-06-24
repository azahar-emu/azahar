// FSR - [RCAS] ROBUST CONTRAST ADAPTIVE SHARPENING
layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 color;
layout(binding = 0) uniform sampler2D color_texture;
uniform float FSR_SHARPENING;
uniform vec4 o_resolution;

#define A_GPU 1
#define A_GLSL 1
#include "ffx_a.h"

#define FSR_RCAS_F 1
AU4 con0;

AF4 FsrRcasLoadF(ASU2 p) { return AF4(texelFetch(color_texture, p, 0)); }
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}

#include "ffx_fsr1.h"

void main() {
    FsrRcasCon(con0, FSR_SHARPENING);

    AU2 gxy = AU2(frag_tex_coord.xy * o_resolution.xy); // Integer pixel position in output.
    AF3 Gamma2Color = AF3(0, 0, 0);
    FsrRcasF(Gamma2Color.r, Gamma2Color.g, Gamma2Color.b, gxy, con0);

    color = vec4(Gamma2Color, 1.0);
}
