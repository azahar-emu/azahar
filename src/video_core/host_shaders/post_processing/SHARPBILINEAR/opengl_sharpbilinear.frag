/*
   Author: KojoZero (modified from rsn8887's shader)
   License: Public domain

   This is an integer prescale filter that should be combined
   with a bilinear hardware filtering (GL_BILINEAR filter or some such) to achieve
   a smooth scaling result with minimum blur. This is good for pixelgraphics
   that are scaled by non-integer factors.

   This is a modified version rsn8887's shader which has been modified to scale
   until above the output resolution, rather than right below the output resolution.
   
   The prescale factor and texel coordinates are precalculated
   in the vertex shader for speed.
*/


layout(location = 0) in vec2 frag_tex_coord;
layout(location = 1) in vec2 precalc_texel;
layout(location = 2) in vec2 precalc_scale;
layout(location = 0) out vec4 color;
layout(binding = 0) uniform sampler2D color_texture;
uniform vec4 i_resolution;
uniform vec4 o_resolution;
uniform int convert_colors;

vec3 LinearTosRGB(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0/2.4)) - 0.055, step(0.0031308, c));
}

void main()
{
  vec2 texel = precalc_texel;
  vec2 scale = precalc_scale;
  vec2 texel_floored = floor(texel);
  vec2 s = fract(texel);
  vec2 region_range = 0.5 - 0.5 / scale;

  // Figure out where in the texel to sample to get correct pre-scaled bilinear.
  // Uses the hardware bilinear interpolator to avoid having to sample 4 times manually.

  vec2 center_dist = s - 0.5;
  vec2 f = (center_dist - clamp(center_dist, -region_range, region_range)) * scale + 0.5;

  vec2 mod_texel = texel_floored + f;

  vec4 pixel = vec4(texture(color_texture, mod_texel / i_resolution.xy).rgb, 1.0);
  if (convert_colors == 2){
      pixel = vec4(LinearTosRGB(pixel.rgb), pixel.a);
  }
  color = pixel;
}