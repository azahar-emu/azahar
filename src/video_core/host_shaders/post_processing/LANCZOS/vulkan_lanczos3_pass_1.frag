/*
   Lanczos3 - passX 

   Multipass code by Hyllian 2025.

*/


/*
   Copyright (C) 2010 Team XBMC
   http://www.xbmc.org
   Copyright (C) 2011 Stefanos A.
   http://www.opentk.com

This Program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

This Program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with XBMC; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
http://www.gnu.org/copyleft/gpl.html
*/
#version 450 core
#extension GL_ARB_separate_shader_objects : enable

// Configuration
#define OutputGamma   2.2
#define ANTI_RINGING  1.0 // Lanczos3 Anti-Ringing [ OFF | ON ]
#define LINEAR_GAMMA  0.0 // Use Linear Gamma [ YES | NO ]

#define GAMMA_OUT(color)    pow(color, vec3(1.0 / OutputGamma, 1.0 / OutputGamma, 1.0 / OutputGamma))
#define FIX(c) (max(abs(c), 1e-5))
const float PI     = 3.1415926535897932384626433832795;
const float radius = 3.0;

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

layout(location = 0) in vec2 frag_tex_coord;
layout(location = 0) out vec4 fragColor;
layout(set = 0, binding = 0) uniform sampler2D color_texture;

vec3 weight3(float x)
{
   // Looks like "sample" is a reserved word in slang.
   vec3 Sample = FIX(2.0 * PI * vec3(x - 1.5, x - 0.5, x + 0.5));

   // Lanczos3. Note: we normalize outside this function, so no point in multiplying by radius.
   return sin(Sample) * sin(Sample / radius) / (Sample * Sample);
}
    

void main()
{
    vec2 ps = i_resolution.zw;
    vec2 pos = frag_tex_coord.xy + ps * vec2(0.5, 0.0);
    vec2 fp = fract(pos / ps);

    vec2 xystart = (-2.5 - fp) * ps + pos;

    float ypos = xystart.y  + ps.y * 3.0;

    vec3 C0 = texture(color_texture, vec2(xystart.x             , ypos)).rgb;
    vec3 C1 = texture(color_texture, vec2(xystart.x + ps.x      , ypos)).rgb;
    vec3 C2 = texture(color_texture, vec2(xystart.x + ps.x * 2.0, ypos)).rgb;
    vec3 C3 = texture(color_texture, vec2(xystart.x + ps.x * 3.0, ypos)).rgb;
    vec3 C4 = texture(color_texture, vec2(xystart.x + ps.x * 4.0, ypos)).rgb;
    vec3 C5 = texture(color_texture, vec2(xystart.x + ps.x * 5.0, ypos)).rgb; 

    vec3 w1 = weight3(0.5 - fp.x * 0.5);
    vec3 w2 = weight3(1.0 - fp.x * 0.5);

    vec3 color = mat3( C0, C2, C4 ) * w1 +  mat3( C1, C3, C5) * w2;

    color /= dot(w1 + w2, vec3(1));

    // Anti-ringing
    if (ANTI_RINGING == 1.0)
    {
        vec3 aux = color;
        vec3 min_sample = min(min(C1, C2), min(C3, C4));
        vec3 max_sample = max(max(C1, C2), max(C3, C4));
        color = clamp(color, min_sample, max_sample);
        color = mix(aux, color, step(0.0, (C1-C2)*(C3-C4)));
    }

    color = mix(color, GAMMA_OUT(color), LINEAR_GAMMA);

    fragColor = vec4(color, 1.0);
}
