//? #version 460 core
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
};

/***** Area Sampling *****/

// By Sam Belliveau and Filippo Tarpini. Public Domain license.
// Effectively a more accurate sharp bilinear filter when upscaling,
// that also works as a mathematically perfect downscale filter.
// https://entropymine.com/imageworsener/pixelmixing/
// https://github.com/obsproject/obs-studio/pull/1715
// https://legacy.imagemagick.org/Usage/filter/
vec4 AreaSampling(sampler2D textureSampler, vec2 texCoords) {
    // Determine the sizes of the source and target images.
    vec2 source_size = i_resolution.xy;
    vec2 inverted_target_size = o_resolution.zw;

    // Determine the range of the source image that the target pixel will cover.
    vec2 range = source_size * inverted_target_size;
    vec2 beg = (texCoords.xy * source_size) - (range * 0.5);
    vec2 end = beg + range;

    // Compute the top-left and bottom-right corners of the pixel box.
    ivec2 f_beg = ivec2(floor(beg));
    ivec2 f_end = ivec2(floor(end));

    // Compute how much of the start and end pixels are covered horizontally & vertically.
    float area_w = 1.0 - fract(beg.x);
    float area_n = 1.0 - fract(beg.y);
    float area_e = fract(end.x);
    float area_s = fract(end.y);

    // Compute the areas of the corner pixels in the pixel box.
    float area_nw = area_n * area_w;
    float area_ne = area_n * area_e;
    float area_sw = area_s * area_w;
    float area_se = area_s * area_e;

    // Initialize the color accumulator.
    vec4 avg_color = vec4(0.0, 0.0, 0.0, 0.0);

    // Accumulate corner pixels.
    avg_color += area_nw * texelFetch(textureSampler, ivec2(f_beg.x, f_beg.y), 0);
    avg_color += area_ne * texelFetch(textureSampler, ivec2(f_end.x, f_beg.y), 0);
    avg_color += area_sw * texelFetch(textureSampler, ivec2(f_beg.x, f_end.y), 0);
    avg_color += area_se * texelFetch(textureSampler, ivec2(f_end.x, f_end.y), 0);

    // Determine the size of the pixel box.
    int x_range = int(f_end.x - f_beg.x - 0.5);
    int y_range = int(f_end.y - f_beg.y - 0.5);

    // Accumulate top and bottom edge pixels.
    for (int x = f_beg.x + 1; x <= f_beg.x + x_range; ++x) {
        avg_color += area_n * texelFetch(textureSampler, ivec2(x, f_beg.y), 0);
        avg_color += area_s * texelFetch(textureSampler, ivec2(x, f_end.y), 0);
    }

    // Accumulate left and right edge pixels and all the pixels in between.
    for (int y = f_beg.y + 1; y <= f_beg.y + y_range; ++y) {
        avg_color += area_w * texelFetch(textureSampler, ivec2(f_beg.x, y), 0);
        avg_color += area_e * texelFetch(textureSampler, ivec2(f_end.x, y), 0);

        for (int x = f_beg.x + 1; x <= f_beg.x + x_range; ++x) {
            avg_color += texelFetch(textureSampler, ivec2(x, y), 0);
        }
    }

    // Compute the area of the pixel box that was sampled.
    float area_corners = area_nw + area_ne + area_sw + area_se;
    float area_edges = float(x_range) * (area_n + area_s) + float(y_range) * (area_w + area_e);
    float area_center = float(x_range) * float(y_range);

    // Return the normalized average color.
    return avg_color / (area_corners + area_edges + area_center);
}

vec3 LinearTosRGB(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0/2.4)) - 0.055, step(0.0031308, c));
}

void main() {
    vec4 pixel = AreaSampling(color_texture, frag_tex_coord);
    if (convert_colors == 2){
        pixel = vec4(LinearTosRGB(pixel.rgb), pixel.a);
    }
    color = pixel;
}