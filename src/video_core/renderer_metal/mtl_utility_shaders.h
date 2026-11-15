// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <string_view>

// Metal Shading Language sources of the fixed utility programs used by the Metal renderer. Kept
// in a plain C++ header so they can be validated offline by CI (see tests/msl_corpus.cpp).

namespace Metal::Shaders {

/// Blits, partial clears and format reinterpretation.
constexpr std::string_view UTILITY = R"(
#include <metal_stdlib>
using namespace metal;

struct BlitParams {
    float4 src_rect; // Source coordinates (x0, y0, x1, y1) of the destination corners.
    float4 color;
    float depth;
    float lod;
    float2 pad;
};

struct BlitVOut {
    float4 position [[position]];
    float2 uv;
};

vertex BlitVOut blit_vs(uint vid [[vertex_id]], constant BlitParams& p [[buffer(0)]]) {
    float2 corner = float2(float(vid & 1u), float(vid >> 1u));
    BlitVOut o;
    // corner (0, 0) is the top-left of the viewport.
    o.position = float4(corner.x * 2.0f - 1.0f, 1.0f - corner.y * 2.0f, 0.0f, 1.0f);
    o.uv = mix(p.src_rect.xy, p.src_rect.zw, corner);
    return o;
}

fragment float4 blit_color_fs(BlitVOut in [[stage_in]], texture2d<float> src [[texture(0)]],
                              sampler s [[sampler(0)]]) {
    return src.sample(s, in.uv, level(0.0f));
}

struct DepthOut {
    float depth [[depth(any)]];
};

fragment DepthOut blit_depth_fs(BlitVOut in [[stage_in]], depth2d<float> src [[texture(0)]],
                                sampler s [[sampler(0)]]) {
    DepthOut o;
    o.depth = src.sample(s, in.uv, level(0.0f));
    return o;
}

fragment float4 clear_color_fs(constant BlitParams& p [[buffer(0)]]) {
    return p.color;
}

fragment DepthOut clear_depth_fs(constant BlitParams& p [[buffer(0)]]) {
    DepthOut o;
    o.depth = p.depth;
    return o;
}

// Reinterprets a D24S8 depth-stencil surface as RGBA8, matching the Vulkan backend's compute
// shader. src_rect.xy holds the offset to add to the destination pixel to get the source pixel.
fragment float4 d24s8_to_rgba8_fs(BlitVOut in [[stage_in]], depth2d<float> depth [[texture(0)]],
                                  texture2d<uint> stencil [[texture(1)]],
                                  constant BlitParams& p [[buffer(0)]]) {
    uint2 coord = uint2(int2(in.position.xy) + int2(p.src_rect.xy));
    uint depth_val = uint(depth.read(coord) * (exp2(32.0f) - 1.0f));
    uint stencil_val = stencil.read(coord).x;
    uint4 components = uint4(stencil_val, (uint3(depth_val) >> uint3(24u, 16u, 8u)) & 0xFFu);
    return float4(components) / 255.0f;
}
)";

/// Depth-stencil blits, requires stencil export support ([[stencil]]).
constexpr std::string_view STENCIL = R"(
#include <metal_stdlib>
using namespace metal;

struct BlitVOut {
    float4 position [[position]];
    float2 uv;
};

struct BlitParams {
    float4 src_rect;
    float4 color;
    float depth;
    float lod;
    float2 pad;
};

struct DepthStencilOut {
    float depth [[depth(any)]];
    uint stencil [[stencil]];
};

fragment DepthStencilOut blit_depth_stencil_fs(BlitVOut in [[stage_in]],
                                               depth2d<float> src [[texture(0)]],
                                               texture2d<uint> stencil [[texture(1)]],
                                               sampler s [[sampler(0)]]) {
    DepthStencilOut o;
    o.depth = src.sample(s, in.uv, level(0.0f));
    uint2 size = uint2(stencil.get_width(), stencil.get_height());
    uint2 coord = min(uint2(clamp(in.uv, 0.0f, 1.0f) * float2(size)), size - 1u);
    o.stencil = stencil.read(coord).x;
    return o;
}
)";

/// Presentation of the 3DS screens to the CAMetalLayer.
constexpr std::string_view PRESENT = R"(
#include <metal_stdlib>
using namespace metal;

struct PresentUniforms {
    float4 o_resolution; // layout width, height, 1/width, 1/height
    float4 fill_color;
    int use_fill;
    int reverse_interlaced;
    float interlace_rows; // stereo screens: the extent the interlacing alternates over
    int interlace_use_y;  // 1 when the rows run along the texture's y axis (upscaled screens)
    int effect;           // post-processing: 0 none, 1 sharpen, 2 LCD grid, 3 scanlines
    int anaglyph_mode;    // 0 Dubois, 1 gray, 2 half color, 3 color
    int sharp_pixels;     // 1: the screen's pixels stay crisp when it is enlarged ("sharp bilinear")
    int sharpness;        // 0 off (effect 1 uses its own amount), 1-100: sharpening amount in percent
    int sharpen_method;   // 0 unsharp mask, 1 contrast adaptive sharpening
    int scale_filter;     // when a screen is enlarged: 0 bilinear, 1 bicubic (Catmull-Rom), 2 Lanczos
    int grid_strength;    // LCD grid: 0 the default depth, 1-100 how dark the gaps are
    int saturation;       // color adjustment, -100 to 100 (0 none)
    int contrast;
    int brightness;
    int pad0;
    int pad1;
};

struct PresentVertex {
    float2 position;
    float2 tex_coord;
};

struct PresentVOut {
    float4 position [[position]];
    float2 tex_coord;
};

vertex PresentVOut present_vs(uint vid [[vertex_id]],
                              constant PresentVertex* vertices [[buffer(0)]],
                              constant PresentUniforms& u [[buffer(1)]]) {
    PresentVOut o;
    float2 pos = vertices[vid].position;
    // Layout coordinates have the origin at the top-left corner and +Y pointing down.
    o.position = float4(pos.x * 2.0f * u.o_resolution.z - 1.0f,
                        1.0f - pos.y * 2.0f * u.o_resolution.w, 0.0f, 1.0f);
    o.tex_coord = vertices[vid].tex_coord;
    return o;
}

// Bicubic (Catmull-Rom) filtering in nine bilinear reads. Keeps edges crisper than bilinear when
// a screen is enlarged. It can ring a little at hard edges, so the result is clamped.
float3 present_catmull_rom(texture2d<float> tex, sampler s, float2 uv) {
    float2 size = float2(tex.get_width(), tex.get_height());
    float2 pos = uv * size;
    float2 center = floor(pos - 0.5f) + 0.5f;
    float2 f = pos - center;
    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);
    float2 w12 = w1 + w2;
    float2 t0 = (center - 1.0f) / size;
    float2 t12 = (center + w2 / w12) / size;
    float2 t3 = (center + 2.0f) / size;
    float3 result =
        (tex.sample(s, float2(t0.x, t0.y)).rgb * w0.x + tex.sample(s, float2(t12.x, t0.y)).rgb * w12.x +
         tex.sample(s, float2(t3.x, t0.y)).rgb * w3.x) * w0.y +
        (tex.sample(s, float2(t0.x, t12.y)).rgb * w0.x + tex.sample(s, float2(t12.x, t12.y)).rgb * w12.x +
         tex.sample(s, float2(t3.x, t12.y)).rgb * w3.x) * w12.y +
        (tex.sample(s, float2(t0.x, t3.y)).rgb * w0.x + tex.sample(s, float2(t12.x, t3.y)).rgb * w12.x +
         tex.sample(s, float2(t3.x, t3.y)).rgb * w3.x) * w3.y;
    return clamp(result, 0.0f, 1.0f);
}

// The four weights of Lanczos (two lobes) for the texels at -1, 0, 1 and 2 around a sample point
// that is f (0 to 1) past the center of texel 0. The sines are the same for all four texels up to
// a sign (sin(pi (f - i)) is sin(pi f) with the sign of i), so three sine and cosine calls give all
// four weights instead of eight.
float4 present_lanczos_weights(float f) {
    const float pi = 3.14159265f;
    const float a = sin(pi * f);
    const float b = sin(pi * 0.5f * f);
    const float c = cos(pi * 0.5f * f);
    const float k = 2.0f / (pi * pi);
    float4 w;
    w.x = -k * a * c / ((f + 1.0f) * (f + 1.0f));
    w.y = f < 1e-4f ? 1.0f : k * a * b / (f * f);
    w.z = (1.0f - f) < 1e-4f ? 1.0f : k * a * c / ((f - 1.0f) * (f - 1.0f));
    w.w = -k * a * b / ((f - 2.0f) * (f - 2.0f));
    return w;
}

// Lanczos (two lobes) over the 4x4 nearest texels: sharper than bicubic, a little more ringing.
// The weights are separable: four for x and four for y, multiplied per texel.
float3 present_lanczos(texture2d<float> tex, float2 uv) {
    constexpr sampler nearest_sampler(filter::nearest, address::clamp_to_edge);
    float2 size = float2(tex.get_width(), tex.get_height());
    float2 pos = uv * size;
    float2 base = floor(pos - 0.5f);
    float2 f = pos - base - 0.5f;
    float4 wx = present_lanczos_weights(f.x);
    float4 wy = present_lanczos_weights(f.y);
    half3 sum = half3(0.0h);
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 4; i++) {
            float2 texel = base + float2(i - 1, j - 1);
            sum += half3(tex.sample(nearest_sampler, (texel + 0.5f) / size).rgb) *
                   half(wx[i] * wy[j]);
        }
    }
    const float total = (wx.x + wx.y + wx.z + wx.w) * (wy.x + wy.y + wy.z + wy.w);
    return clamp(float3(sum) / max(total, 1e-4f), 0.0f, 1.0f);
}

fragment float4 present_fs(PresentVOut in [[stage_in]], texture2d<float> screen [[texture(0)]],
                           sampler s [[sampler(0)]], constant PresentUniforms& u [[buffer(1)]]) {
    if (u.use_fill != 0) {
        return u.fill_color;
    }
    float2 uv = in.tex_coord;
    if (u.sharp_pixels != 0) {
        // Sharp bilinear: inside a source pixel the color is flat, and only about one display
        // pixel at its edge blends with the next one. That keeps pixels crisp at any scale
        // without the unevenly wide pixels of plain nearest filtering.
        float2 size = float2(screen.get_width(), screen.get_height());
        float2 p = uv * size;
        float2 scale = 1.0f / max(fwidth(p), float2(1e-5f));
        float2 base = floor(p);
        float2 blend = clamp((p - base - 0.5f) * scale + 0.5f, 0.0f, 1.0f);
        uv = (base + blend) / size;
    }
    float3 color;
    if (u.sharp_pixels == 0 && u.scale_filter == 1) {
        color = present_catmull_rom(screen, s, uv);
    } else if (u.sharp_pixels == 0 && u.scale_filter == 2) {
        color = present_lanczos(screen, uv);
    } else {
        color = screen.sample(s, uv).rgb;
    }
    // Sharpening. The effect "Sharpen" has a fixed amount, the sharpness setting an adjustable
    // one (and works with the other effects and MetalFX).
    const float amount = u.sharpness > 0 ? float(u.sharpness) * 0.015f
                                         : (u.effect == 1 ? 0.8f : 0.0f);
    if (amount > 0.0f) {
        float2 texel = 1.0f / float2(screen.get_width(), screen.get_height());
        // The arithmetic is half precision (twice the rate on this GPU), the coordinates stay
        // full precision.
        half3 center = half3(color);
        half3 right = half3(screen.sample(s, uv + float2(texel.x, 0.0f)).rgb);
        half3 left = half3(screen.sample(s, uv - float2(texel.x, 0.0f)).rgb);
        half3 down = half3(screen.sample(s, uv + float2(0.0f, texel.y)).rgb);
        half3 up = half3(screen.sample(s, uv - float2(0.0f, texel.y)).rgb);
        if (u.sharpen_method == 1) {
            // Contrast adaptive sharpening (AMD FidelityFX CAS, MIT): sharpens where the picture
            // is soft and leaves strong edges alone, so there are fewer halos than with the
            // unsharp mask. The amount (0.015 per percent) maps to its strength 0 to 1.
            half3 mn = min(min(min(left, right), min(up, down)), center);
            half3 mx = max(max(max(left, right), max(up, down)), center);
            half3 amp = sqrt(clamp(min(mn, half(2.0f) - mx) / max(mx, half(0.001f)), half(0.0f),
                                   half(1.0f)));
            float strength = clamp(amount / 1.2f, 0.0f, 1.0f);
            half peak = half(-1.0f / mix(8.0f, 5.0f, strength));
            half3 w = amp * peak;
            center = clamp((center + (left + right + up + down) * w) / (half(1.0f) + half(4.0f) * w),
                           half(0.0f), half(1.0f));
        } else {
            // Unsharp mask: add the difference to the average of the four neighbours.
            half3 around = right + left + down + up;
            center = clamp(center + (center - around * half(0.25f)) * half(amount), half(0.0f),
                           half(1.0f));
        }
        color = float3(center);
    }
    if (u.effect == 2) {
        // LCD grid: darken the gaps between the pixels of the console's screen.
        float2 cell = fract(in.tex_coord * float2(screen.get_width(), screen.get_height()));
        float2 edge = smoothstep(0.0f, 0.18f, cell) * smoothstep(0.0f, 0.18f, 1.0f - cell);
        const float depth = u.grid_strength > 0 ? float(u.grid_strength) * 0.008f : 0.45f;
        color *= mix(1.0f - depth, 1.0f, edge.x * edge.y);
    } else if (u.effect == 3) {
        // Scanlines: every other row of the console's screen is a little darker.
        float row = fract(in.tex_coord.y * float(screen.get_height()));
        color *= mix(0.6f, 1.0f, smoothstep(0.0f, 0.5f, row) * smoothstep(0.0f, 0.5f, 1.0f - row));
    }
    // Color adjustment (half precision).
    if (u.saturation != 0 || u.contrast != 0 || u.brightness != 0) {
        half3 c = half3(color);
        const half luma = dot(c, half3(0.2126h, 0.7152h, 0.0722h));
        c = mix(half3(luma), c, half(1.0f + float(u.saturation) * 0.01f));
        c = (c - half(0.5f)) * half(1.0f + float(u.contrast) * 0.01f) + half(0.5f);
        c += half(float(u.brightness) * 0.005f);
        color = float3(clamp(c, half(0.0f), half(1.0f)));
    }
    return float4(color, 1.0f);
}

// Red/cyan anaglyph from the left (texture 0) and right (texture 1) eye, same matrices as the
// other backends.
fragment float4 present_anaglyph_fs(PresentVOut in [[stage_in]], texture2d<float> left [[texture(0)]],
                                    texture2d<float> right [[texture(1)]],
                                    sampler s [[sampler(0)]],
                                    constant PresentUniforms& u [[buffer(1)]]) {
    const float3x3 l = float3x3(float3(0.437, 0.449, 0.164), float3(-0.062, -0.062, -0.024),
                                float3(-0.048, -0.050, -0.017));
    const float3x3 r = float3x3(float3(-0.011, -0.032, -0.007), float3(0.377, 0.761, 0.009),
                                float3(-0.026, -0.093, 1.234));
    float4 color_l = left.sample(s, in.tex_coord);
    float4 color_r = right.sample(s, in.tex_coord);
    const float3 luma = float3(0.299f, 0.587f, 0.114f);
    if (u.anaglyph_mode == 1) {
        // Gray: both eyes as luminance, red from the left and cyan from the right.
        return float4(dot(color_l.rgb, luma), dot(color_r.rgb, luma), dot(color_r.rgb, luma),
                      1.0f);
    } else if (u.anaglyph_mode == 2) {
        // Half color: the left eye as luminance, the right eye in color.
        return float4(dot(color_l.rgb, luma), color_r.g, color_r.b, 1.0f);
    } else if (u.anaglyph_mode == 3) {
        // Color: red from the left eye, green and blue from the right eye.
        return float4(color_l.r, color_r.g, color_r.b, 1.0f);
    }
    return float4(color_l.rgb * l + color_r.rgb * r, 1.0f);
}

// Interleaves the eyes row by row.
fragment float4 present_interlaced_fs(PresentVOut in [[stage_in]],
                                      texture2d<float> left [[texture(0)]],
                                      texture2d<float> right [[texture(1)]],
                                      sampler s [[sampler(0)]],
                                      constant PresentUniforms& u [[buffer(1)]]) {
    float screen_row = u.interlace_rows * (u.interlace_use_y != 0 ? in.tex_coord.y : in.tex_coord.x);
    float3 color = (int(screen_row) % 2 == u.reverse_interlaced)
                       ? left.sample(s, in.tex_coord).rgb
                       : right.sample(s, in.tex_coord).rgb;
    return float4(color, 1.0f);
}
)";


/// Appended to PRESENT and the user's post-processing shader (which defines post_effect).
constexpr std::string_view PRESENT_POST_WRAPPER = R"(
fragment float4 present_post_fs(PresentVOut in [[stage_in]], texture2d<float> screen [[texture(0)]],
                                sampler s [[sampler(0)]], constant PresentUniforms& u [[buffer(1)]]) {
    if (u.use_fill != 0) {
        return u.fill_color;
    }
    return float4(post_effect(in.tex_coord, screen, s, u.o_resolution).rgb, 1.0f);
}
)";

/// A post-processing shader as the user writes one, also used by the shader tests.
constexpr std::string_view POST_EXAMPLE = R"(
// A post-processing shader for Azahar. Define post_effect: it gets the position in the screen
// (0 to 1), the picture of the screen, a sampler and the size of the window (width, height,
// 1/width, 1/height), and returns the color of that point.
float4 post_effect(float2 uv, texture2d<float> screen, sampler s, float4 resolution) {
    float3 color = screen.sample(s, uv).rgb;
    float gray = dot(color, float3(0.299f, 0.587f, 0.114f));
    // A little more color than the original.
    return float4(mix(float3(gray), color, 1.25f), 1.0f);
}
)";

/// Texture upscaling filters, run when a texture is uploaded at an internal resolution above 1x.
/// They are ports of the shaders in host_shaders/texture_filtering. The vertex stage and the
/// parameters are the ones of the utility blits: `uv` is the source position of the pixel and
/// BlitParams::lod carries the resolution scale for xBRZ.
constexpr std::string_view FILTERS = R"(
#include <metal_stdlib>
using namespace metal;

struct BlitParams {
    float4 src_rect;
    float4 color;
    float depth;
    float lod;
    float2 pad;
};

struct BlitVOut {
    float4 position [[position]];
    float2 uv;
};

// ---- Bicubic

static float4 cubic4(float v) {
    float4 n = float4(1.0, 2.0, 3.0, 4.0) - v;
    float4 s = n * n * n;
    float x = s.x;
    float y = s.y - 4.0 * s.x;
    float z = s.z - 4.0 * s.y + 6.0 * s.x;
    float w = 6.0 - x - y - z;
    return float4(x, y, z, w) * (1.0 / 6.0);
}

static float4 texture_bicubic(texture2d<float> tex, sampler smp, float2 tex_coords) {
    float2 tex_size = float2(tex.get_width(), tex.get_height());
    float2 inv_tex_size = 1.0 / tex_size;

    tex_coords = tex_coords * tex_size - 0.5;

    float2 fxy = fract(tex_coords);
    tex_coords -= fxy;

    float4 xcubic = cubic4(fxy.x);
    float4 ycubic = cubic4(fxy.y);

    float4 c = tex_coords.xxyy + float4(-0.5, +1.5, -0.5, +1.5);

    float4 s = float4(xcubic.xz + xcubic.yw, ycubic.xz + ycubic.yw);
    float4 offset = c + float4(xcubic.yw, ycubic.yw) / s;

    offset *= inv_tex_size.xxyy;

    float4 sample0 = tex.sample(smp, offset.xz, level(0.0f));
    float4 sample1 = tex.sample(smp, offset.yz, level(0.0f));
    float4 sample2 = tex.sample(smp, offset.xw, level(0.0f));
    float4 sample3 = tex.sample(smp, offset.yw, level(0.0f));

    float sx = s.x / (s.x + s.y);
    float sy = s.z / (s.z + s.w);

    return mix(mix(sample3, sample2, sx), mix(sample1, sample0, sx), sy);
}

fragment float4 filter_bicubic_fs(BlitVOut in [[stage_in]], texture2d<float> tex [[texture(0)]],
                                  sampler smp [[sampler(0)]]) {
    return texture_bicubic(tex, smp, in.uv);
}

// ---- ScaleForce (from https://github.com/BreadFish64/ScaleFish/tree/master/scale_force, MIT)

static float4 sf_color_dist(float4 A, float4 B, float4 C, float4 D, float4 cc) {
    // https://en.wikipedia.org/wiki/YCbCr#ITU-R_BT.2020_conversion
    const float3 K = float3(0.2627, 0.6780, 0.0593);
    const float LUMINANCE_WEIGHT = .6;
    // The sum of the components of YCBCR_MATRIX * (color - center) is the dot product of the
    // color difference with the sums of the columns of the matrix.
    const float3 col0 = K * LUMINANCE_WEIGHT;
    const float3 col1 = float3(-.5 * K.x / (1.0 - K.z), -.5 * K.y / (1.0 - K.z), .5);
    const float3 col2 = float3(.5, -.5 * K.y / (1.0 - K.x), -.5 * K.z / (1.0 - K.x));
    const float3 w = float3(col0.x + col0.y + col0.z, col1.x + col1.y + col1.z,
                            col2.x + col2.y + col2.z);

    float4 color_dist = float4(dot(w, A.rgb - cc.rgb), dot(w, B.rgb - cc.rgb),
                               dot(w, C.rgb - cc.rgb), dot(w, D.rgb - cc.rgb));
    color_dist *= color_dist;
    float4 alpha = float4(A.a, B.a, C.a, D.a);
    float4 center_alpha = float4(cc.a);

    return sqrt((color_dist + abs(center_alpha - alpha)) * alpha * center_alpha);
}

#define SF_TAP(x, y) tex.sample(smp, in.uv, level(0.0f), int2(x, y))

fragment float4 filter_scale_force_fs(BlitVOut in [[stage_in]], texture2d<float> tex [[texture(0)]],
                                      sampler smp [[sampler(0)]]) {
    float4 bl = SF_TAP(-1, -1);
    float4 bc = SF_TAP(0, -1);
    float4 br = SF_TAP(1, -1);
    float4 cl = SF_TAP(-1, 0);
    float4 cc = tex.sample(smp, in.uv, level(0.0f));
    float4 cr = SF_TAP(1, 0);
    float4 tl = SF_TAP(-1, 1);
    float4 tc = SF_TAP(0, 1);
    float4 tr = SF_TAP(1, 1);

    float2 tex_size = float2(tex.get_width(), tex.get_height());
    float2 inv_tex_size = 1.0 / tex_size;

    float4 offset_tl = sf_color_dist(tl, tc, tr, cr, cc);
    float4 offset_br = sf_color_dist(br, bc, bl, cl, cc);

    // Calculate how different cc is from the texels around it
    float total_dist = dot(offset_tl + offset_br, float4(1.0));

    // Add together all the distances with direction taken into account
    float4 tmp = offset_tl - offset_br;
    float2 total_offset = tmp.wy + tmp.zz + float2(-tmp.x, tmp.x);

    if (total_dist == 0.0) {
        // Doing bicubic filtering just past the edges where the offset is 0 causes black floaters
        // and it doesn't really matter which filter is used when the colors aren't changing.
        return cc;
    }
    // When the image has thin points, they tend to split apart. This keeps the offset in bounds.
    float clamp_val = length(total_offset) / total_dist;
    float2 final_offset = clamp(total_offset, -clamp_val, clamp_val) * inv_tex_size;
    return texture_bicubic(tex, smp, in.uv - final_offset);
}

// ---- Anime4K refinement (MIT, bloc97). Like the Vulkan backend, the red channel stands in for
// the luminance texture.

#define A4K_TAP(x, y) tex.sample(smp, in.uv, level(0.0f), int2(x, y))

struct RGBAL {
    float4 c;
    float l;
};

static float4 a4k_average(float4 cc, float4 a, float4 b, float4 c) {
    const float STRENGTH = 0.6;
    return cc * (1.0 - STRENGTH) + ((a + b + c) / 3.0) * STRENGTH;
}

#define GetRGBAL(x_offset, y_offset)                                                               \
    RGBAL{A4K_TAP(x_offset, y_offset), A4K_TAP(x_offset, y_offset).x}

static float min3v(float a, float b, float c) {
    return min(min(a, b), c);
}

static float max3v(float a, float b, float c) {
    return max(max(a, b), c);
}

fragment float4 filter_anime4k_fs(BlitVOut in [[stage_in]], texture2d<float> tex [[texture(0)]],
                                  sampler smp [[sampler(0)]]) {
    const float LINE_DETECT_THRESHOLD = 0.4;
    RGBAL cc = RGBAL{tex.sample(smp, in.uv, level(0.0f)), tex.sample(smp, in.uv, level(0.0f)).x};
    if (cc.l > LINE_DETECT_THRESHOLD) {
        return cc.c;
    }

    RGBAL tl = GetRGBAL(-1, -1);
    RGBAL t = GetRGBAL(0, -1);
    RGBAL tr = GetRGBAL(1, -1);
    RGBAL l = GetRGBAL(-1, 0);
    RGBAL r = GetRGBAL(1, 0);
    RGBAL bl = GetRGBAL(-1, 1);
    RGBAL b = GetRGBAL(0, 1);
    RGBAL br = GetRGBAL(1, 1);

    // Kernel 0 and 4
    float maxDark = max3v(br.l, b.l, bl.l);
    float minLight = min3v(tl.l, t.l, tr.l);

    if (minLight > cc.l && minLight > maxDark) {
        return a4k_average(cc.c, tl.c, t.c, tr.c);
    } else {
        maxDark = max3v(tl.l, t.l, tr.l);
        minLight = min3v(br.l, b.l, bl.l);
        if (minLight > cc.l && minLight > maxDark) {
            return a4k_average(cc.c, br.c, b.c, bl.c);
        }
    }

    // Kernel 1 and 5
    maxDark = max3v(cc.l, l.l, b.l);
    minLight = min3v(r.l, t.l, tr.l);

    if (minLight > maxDark) {
        return a4k_average(cc.c, r.c, t.c, tr.c);
    } else {
        maxDark = max3v(cc.l, r.l, t.l);
        minLight = min3v(bl.l, l.l, b.l);
        if (minLight > maxDark) {
            return a4k_average(cc.c, bl.c, l.c, b.c);
        }
    }

    // Kernel 2 and 6
    maxDark = max3v(l.l, tl.l, bl.l);
    minLight = min3v(r.l, br.l, tr.l);

    if (minLight > cc.l && minLight > maxDark) {
        return a4k_average(cc.c, r.c, br.c, tr.c);
    } else {
        maxDark = max3v(r.l, br.l, tr.l);
        minLight = min3v(l.l, tl.l, bl.l);
        if (minLight > cc.l && minLight > maxDark) {
            return a4k_average(cc.c, l.c, tl.c, bl.c);
        }
    }

    // Kernel 3 and 7
    maxDark = max3v(cc.l, l.l, t.l);
    minLight = min3v(r.l, br.l, b.l);

    if (minLight > maxDark) {
        return a4k_average(cc.c, r.c, br.c, b.c);
    } else {
        maxDark = max3v(cc.l, r.l, b.l);
        minLight = min3v(t.l, l.l, tl.l);
        if (minLight > maxDark) {
            return a4k_average(cc.c, t.c, l.c, tl.c);
        }
    }

    return cc.c;
}

// ---- MMPX

#define src(x, y) tex.sample(smp, coord + float2(x, y) * 1.0 / source_size, level(0.0f))

static float luma(float4 col) {
    return dot(col.rgb, float3(0.2126, 0.7152, 0.0722)) * (1.0 - col.a);
}
static bool same(float4 B, float4 A0) { return all(B == A0); }
static bool notsame(float4 B, float4 A0) { return any(B != A0); }
static bool all_eq2(float4 B, float4 A0, float4 A1) { return (same(B,A0) && same(B,A1)); }
static bool all_eq3(float4 B, float4 A0, float4 A1, float4 A2) {
    return (same(B,A0) && same(B,A1) && same(B,A2));
}
static bool all_eq4(float4 B, float4 A0, float4 A1, float4 A2, float4 A3) {
    return (same(B,A0) && same(B,A1) && same(B,A2) && same(B,A3));
}
static bool any_eq3(float4 B, float4 A0, float4 A1, float4 A2) {
    return (same(B,A0) || same(B,A1) || same(B,A2));
}
static bool none_eq2(float4 B, float4 A0, float4 A1) { return (notsame(B,A0) && notsame(B,A1)); }
static bool none_eq4(float4 B, float4 A0, float4 A1, float4 A2, float4 A3) {
    return (notsame(B,A0) && notsame(B,A1) && notsame(B,A2) && notsame(B,A3));
}

fragment float4 filter_mmpx_fs(BlitVOut in [[stage_in]], texture2d<float> tex [[texture(0)]],
                               sampler smp [[sampler(0)]]) {

    float2 source_size = float2(tex.get_width(), tex.get_height());
    float2 pos = fract(in.uv * source_size) - float2(0.5, 0.5);
    float2 coord = in.uv - pos / source_size;

    float4 E = src(0.0,0.0);

    float4 A = src(-1.0,-1.0);
    float4 B = src(0.0,-1.0);
    float4 C = src(1.0,-1.0);

    float4 D = src(-1.0,0.0);
    float4 F = src(1.0,0.0);

    float4 G = src(-1.0,1.0);
    float4 H = src(0.0,1.0);
    float4 I = src(1.0,1.0);

    float4 J = E;
    float4 K = E;
    float4 L = E;
    float4 M = E;

    if(same(E,A) && same(E,B) && same(E,C) && same(E,D) && same(E,F) && same(E,G) && same(E,H) && same(E,I)) return E;

    float4 P  = src(0.0,2.0);
    float4 Q  = src(-2.0,0.0);
    float4 R  = src(2.0,0.0);
    float4 S  = src(0.0,2.0);

    float Bl = luma(B);
    float Dl = luma(D);
    float El = luma(E);
    float Fl = luma(F);
    float Hl = luma(H);

    if (((same(D,B) && notsame(D,H) && notsame(D,F))) && ((El>=Dl) || same(E,A)) && any_eq3(E,A,C,G) && ((El<Dl) || notsame(A,D) || notsame(E,P) || notsame(E,Q))) J=mix(D, J, 0.5);
    if (((same(B,F) && notsame(B,D) && notsame(B,H))) && ((El>=Bl) || same(E,C)) && any_eq3(E,A,C,I) && ((El<Bl) || notsame(C,B) || notsame(E,P) || notsame(E,R))) K=mix(B, K, 0.5);
    if (((same(H,D) && notsame(H,F) && notsame(H,B))) && ((El>=Hl) || same(E,G)) && any_eq3(E,A,G,I) && ((El<Hl) || notsame(G,H) || notsame(E,S) || notsame(E,Q))) L=mix(H, L, 0.5);
    if (((same(F,H) && notsame(F,B) && notsame(F,D))) && ((El>=Fl) || same(E,I)) && any_eq3(E,C,G,I) && ((El<Fl) || notsame(I,H) || notsame(E,R) || notsame(E,S))) M=mix(F, M, 0.5);

    if ((notsame(E,F) && all_eq4(E,C,I,D,Q) && all_eq2(F,B,H)) && notsame(F,src(3.0,0.0))) {M=mix(M, F, 0.5); K=mix(K, M, 0.5);};
    if ((notsame(E,D) && all_eq4(E,A,G,F,R) && all_eq2(D,B,H)) && notsame(D,src(-3.0,0.0))) {L=mix(L, D, 0.5); J=mix(J, L, 0.5);};
    if ((notsame(E,H) && all_eq4(E,G,I,B,P) && all_eq2(H,D,F)) && notsame(H,src(0.0,3.0))) {M=mix(M, H, 0.5); L=mix(L, M, 0.5);};
    if ((notsame(E,B) && all_eq4(E,A,C,H,S) && all_eq2(B,D,F)) && notsame(B,src(0.0,-3.0))) {K=mix(K, B, 0.5); J=mix(J, K, 0.5);};

    if ((Bl<El) && all_eq4(E,G,H,I,S) && none_eq4(E,A,D,C,F)) {K=mix(K, B, 0.5); J=mix(J, K, 0.5);}
    if ((Hl<El) && all_eq4(E,A,B,C,P) && none_eq4(E,D,G,I,F)) {M=mix(M, H, 0.5); L=mix(L, M, 0.5);}
    if ((Fl<El) && all_eq4(E,A,D,G,Q) && none_eq4(E,B,C,I,H)) {M=mix(M, F, 0.5); K=mix(K, M, 0.5);}
    if ((Dl<El) && all_eq4(E,C,F,I,R) && none_eq4(E,B,A,G,H)) {L=mix(L, D, 0.5); J=mix(J, L, 0.5);}

    if (notsame(H,B)) {
        if (notsame(H,A) && notsame(H,E) && notsame(H,C)) {
            if (all_eq3(H,G,F,R) && none_eq2(H,D,src(2.0,-1.0))) L=mix(M, L, 0.5);
            if (all_eq3(H,I,D,Q) && none_eq2(H,F,src(-2.0,-1.0))) M=mix(L, M, 0.5);
        }

        if (notsame(B,I) && notsame(B,G) && notsame(B,E)) {
            if (all_eq3(B,A,F,R) && none_eq2(B,D,src(2.0,1.0))) J=mix(K, L, 0.5);
            if (all_eq3(B,C,D,Q) && none_eq2(B,F,src(-2.0,1.0))) K=mix(J, K, 0.5);
        }
    }

    if (notsame(F,D)) {
        if (notsame(D,I) && notsame(D,E) && notsame(D,C)) {
            if (all_eq3(D,A,H,S) && none_eq2(D,B,src(1.0,2.0))) J=mix(L, J, 0.5);
            if (all_eq3(D,G,B,P) && none_eq2(D,H,src(1.0,2.0))) L=mix(J, L, 0.5);
        }

        if (notsame(F,E) && notsame(F,A) && notsame(F,G)) {
            if (all_eq3(F,C,H,S) && none_eq2(F,B,src(-1.0,2.0))) K=mix(M, K, 0.5);
            if (all_eq3(F,I,B,P) && none_eq2(F,H,src(-1.0,-2.0))) M=mix(K, M, 0.5);
        }
    }

    float2 a = fract(in.uv * source_size);
    float4 colour = (a.x < 0.5) ? (a.y < 0.5 ? J : L) : (a.y < 0.5 ? K : M);
    return colour;
}

// ---- xBRZ (freescale)

constant int BLEND_NONE = 0;
constant int BLEND_NORMAL = 1;
constant int BLEND_DOMINANT = 2;
constant float LUMINANCE_WEIGHT = 1.0;
constant float EQUAL_COLOR_TOLERANCE = 30.0 / 255.0;
constant float STEEP_DIRECTION_THRESHOLD = 2.2;
constant float DOMINANT_DIRECTION_THRESHOLD = 3.6;

static bool eq(float4 a, float4 b) {
    return all(a == b);
}

static float xbrz_dist(float4 a, float4 b) {
    // https://en.wikipedia.org/wiki/YCbCr#ITU-R_BT.2020_conversion
    const float3 K = float3(0.2627, 0.6780, 0.0593);
    const float3x3 MATRIX = float3x3(K, float3(-.5 * K.x / (1.0 - K.z), -.5 * K.y / (1.0 - K.z), .5),
                                     float3(.5, -.5 * K.y / (1.0 - K.x), -.5 * K.z / (1.0 - K.x)));
    float4 diff = a - b;
    float3 YCbCr = diff.rgb * MATRIX;
    float d = length(YCbCr);
    return sqrt(a.a * b.a * d * d + diff.a * diff.a);
}

static bool IsPixEqual(float4 pixA, float4 pixB) {
    return xbrz_dist(pixA, pixB) < EQUAL_COLOR_TOLERANCE;
}

static float GetLeftRatio(float2 center, float2 origin, float2 direction, float scale) {
    float2 P0 = center - origin;
    float2 proj = direction * (dot(P0, direction) / dot(direction, direction));
    float2 distv = P0 - proj;
    float2 orth = float2(-direction.y, direction.x);
    float side = sign(dot(P0, orth));
    float v = side * length(distv * scale);
    return smoothstep(-sqrt(2.0) / 2.0, sqrt(2.0) / 2.0, v);
}

#define P(x, y) tex.sample(smp, coord, level(0.0f), int2(x, y))

fragment float4 filter_xbrz_fs(BlitVOut in [[stage_in]], texture2d<float> tex [[texture(0)]],
                               sampler smp [[sampler(0)]], constant BlitParams& params [[buffer(0)]]) {
    const float scale = params.lod;

    float2 source_size = float2(tex.get_width(), tex.get_height());
    float2 pos = fract(in.uv * source_size) - float2(0.5, 0.5);
    float2 coord = in.uv - pos / source_size;

    //---------------------------------------
    // Input Pixel Mapping:  -|x|x|x|-
    //                       x|A|B|C|x
    //                       x|D|E|F|x
    //                       x|G|H|I|x
    //                       -|x|x|x|-
    float4 A = P(-1, -1);
    float4 B = P(0, -1);
    float4 C = P(1, -1);
    float4 D = P(-1, 0);
    float4 E = P(0, 0);
    float4 F = P(1, 0);
    float4 G = P(-1, 1);
    float4 H = P(0, 1);
    float4 I = P(1, 1);
    // blendResult Mapping: x|y|
    //                      w|z|
    int4 blendResult = int4(BLEND_NONE, BLEND_NONE, BLEND_NONE, BLEND_NONE);
    // Preprocess corners
    // Pixel Tap Mapping: -|-|-|-|-
    //                    -|-|B|C|-
    //                    -|D|E|F|x
    //                    -|G|H|I|x
    //                    -|-|x|x|-
    if (!((eq(E, F) && eq(H, I)) || (eq(E, H) && eq(F, I)))) {
        float dist_H_F = xbrz_dist(G, E) + xbrz_dist(E, C) + xbrz_dist(P(0, 2), I) +
                         xbrz_dist(I, P(2, 0)) + (4.0 * xbrz_dist(H, F));
        float dist_E_I = xbrz_dist(D, H) + xbrz_dist(H, P(1, 2)) + xbrz_dist(B, F) +
                         xbrz_dist(F, P(2, 1)) + (4.0 * xbrz_dist(E, I));
        bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_H_F) < dist_E_I;
        blendResult.z = ((dist_H_F < dist_E_I) && !eq(E, F) && !eq(E, H))
                            ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL)
                            : BLEND_NONE;
    }
    // Pixel Tap Mapping: -|-|-|-|-
    //                    -|A|B|-|-
    //                    x|D|E|F|-
    //                    x|G|H|I|-
    //                    -|x|x|-|-
    if (!((eq(D, E) && eq(G, H)) || (eq(D, G) && eq(E, H)))) {
        float dist_G_E = xbrz_dist(P(-2, 1), D) + xbrz_dist(D, B) + xbrz_dist(P(-1, 2), H) +
                         xbrz_dist(H, F) + (4.0 * xbrz_dist(G, E));
        float dist_D_H = xbrz_dist(P(-2, 0), G) + xbrz_dist(G, P(0, 2)) + xbrz_dist(A, E) +
                         xbrz_dist(E, I) + (4.0 * xbrz_dist(D, H));
        bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_D_H) < dist_G_E;
        blendResult.w = ((dist_G_E > dist_D_H) && !eq(E, D) && !eq(E, H))
                            ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL)
                            : BLEND_NONE;
    }
    // Pixel Tap Mapping: -|-|x|x|-
    //                    -|A|B|C|x
    //                    -|D|E|F|x
    //                    -|-|H|I|-
    //                    -|-|-|-|-
    if (!((eq(B, C) && eq(E, F)) || (eq(B, E) && eq(C, F)))) {
        float dist_E_C = xbrz_dist(D, B) + xbrz_dist(B, P(1, -2)) + xbrz_dist(H, F) +
                         xbrz_dist(F, P(2, -1)) + (4.0 * xbrz_dist(E, C));
        float dist_B_F = xbrz_dist(A, E) + xbrz_dist(E, I) + xbrz_dist(P(0, -2), C) +
                         xbrz_dist(C, P(2, 0)) + (4.0 * xbrz_dist(B, F));
        bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_B_F) < dist_E_C;
        blendResult.y = ((dist_E_C > dist_B_F) && !eq(E, B) && !eq(E, F))
                            ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL)
                            : BLEND_NONE;
    }
    // Pixel Tap Mapping: -|x|x|-|-
    //                    x|A|B|C|-
    //                    x|D|E|F|-
    //                    -|G|H|-|-
    //                    -|-|-|-|-
    if (!((eq(A, B) && eq(D, E)) || (eq(A, D) && eq(B, E)))) {
        float dist_D_B = xbrz_dist(P(-2, 0), A) + xbrz_dist(A, P(0, -2)) + xbrz_dist(G, E) +
                         xbrz_dist(E, C) + (4.0 * xbrz_dist(D, B));
        float dist_A_E = xbrz_dist(P(-2, -1), D) + xbrz_dist(D, H) + xbrz_dist(P(-1, -2), B) +
                         xbrz_dist(B, F) + (4.0 * xbrz_dist(A, E));
        bool dominantGradient = (DOMINANT_DIRECTION_THRESHOLD * dist_D_B) < dist_A_E;
        blendResult.x = ((dist_D_B < dist_A_E) && !eq(E, D) && !eq(E, B))
                            ? ((dominantGradient) ? BLEND_DOMINANT : BLEND_NORMAL)
                            : BLEND_NONE;
    }
    float4 res = E;
    // Pixel Tap Mapping: -|-|-|-|-
    //                    -|-|B|C|-
    //                    -|D|E|F|x
    //                    -|G|H|I|x
    //                    -|-|x|x|-
    if (blendResult.z != BLEND_NONE) {
        float dist_F_G = xbrz_dist(F, G);
        float dist_H_C = xbrz_dist(H, C);
        bool doLineBlend = (blendResult.z == BLEND_DOMINANT ||
                            !((blendResult.y != BLEND_NONE && !IsPixEqual(E, G)) ||
                              (blendResult.w != BLEND_NONE && !IsPixEqual(E, C)) ||
                              (IsPixEqual(G, H) && IsPixEqual(H, I) && IsPixEqual(I, F) &&
                               IsPixEqual(F, C) && !IsPixEqual(E, I))));
        float2 origin = float2(0.0, 1.0 / sqrt(2.0));
        float2 direction = float2(1.0, -1.0);
        if (doLineBlend) {
            bool haveShallowLine =
                (STEEP_DIRECTION_THRESHOLD * dist_F_G <= dist_H_C) && !eq(E, G) && !eq(D, G);
            bool haveSteepLine =
                (STEEP_DIRECTION_THRESHOLD * dist_H_C <= dist_F_G) && !eq(E, C) && !eq(B, C);
            origin = haveShallowLine ? float2(0.0, 0.25) : float2(0.0, 0.5);
            direction.x += haveShallowLine ? 1.0 : 0.0;
            direction.y -= haveSteepLine ? 1.0 : 0.0;
        }
        float4 blendPix = mix(H, F, step(xbrz_dist(E, F), xbrz_dist(E, H)));
        res = mix(res, blendPix, GetLeftRatio(pos, origin, direction, scale));
    }
    // Pixel Tap Mapping: -|-|-|-|-
    //                    -|A|B|-|-
    //                    x|D|E|F|-
    //                    x|G|H|I|-
    //                    -|x|x|-|-
    if (blendResult.w != BLEND_NONE) {
        float dist_H_A = xbrz_dist(H, A);
        float dist_D_I = xbrz_dist(D, I);
        bool doLineBlend = (blendResult.w == BLEND_DOMINANT ||
                            !((blendResult.z != BLEND_NONE && !IsPixEqual(E, A)) ||
                              (blendResult.x != BLEND_NONE && !IsPixEqual(E, I)) ||
                              (IsPixEqual(A, D) && IsPixEqual(D, G) && IsPixEqual(G, H) &&
                               IsPixEqual(H, I) && !IsPixEqual(E, G))));
        float2 origin = float2(-1.0 / sqrt(2.0), 0.0);
        float2 direction = float2(1.0, 1.0);
        if (doLineBlend) {
            bool haveShallowLine =
                (STEEP_DIRECTION_THRESHOLD * dist_H_A <= dist_D_I) && !eq(E, A) && !eq(B, A);
            bool haveSteepLine =
                (STEEP_DIRECTION_THRESHOLD * dist_D_I <= dist_H_A) && !eq(E, I) && !eq(F, I);
            origin = haveShallowLine ? float2(-0.25, 0.0) : float2(-0.5, 0.0);
            direction.y += haveShallowLine ? 1.0 : 0.0;
            direction.x += haveSteepLine ? 1.0 : 0.0;
        }
        origin = origin;
        direction = direction;
        float4 blendPix = mix(H, D, step(xbrz_dist(E, D), xbrz_dist(E, H)));
        res = mix(res, blendPix, GetLeftRatio(pos, origin, direction, scale));
    }
    // Pixel Tap Mapping: -|-|x|x|-
    //                    -|A|B|C|x
    //                    -|D|E|F|x
    //                    -|-|H|I|-
    //                    -|-|-|-|-
    if (blendResult.y != BLEND_NONE) {
        float dist_B_I = xbrz_dist(B, I);
        float dist_F_A = xbrz_dist(F, A);
        bool doLineBlend = (blendResult.y == BLEND_DOMINANT ||
                            !((blendResult.x != BLEND_NONE && !IsPixEqual(E, I)) ||
                              (blendResult.z != BLEND_NONE && !IsPixEqual(E, A)) ||
                              (IsPixEqual(I, F) && IsPixEqual(F, C) && IsPixEqual(C, B) &&
                               IsPixEqual(B, A) && !IsPixEqual(E, C))));
        float2 origin = float2(1.0 / sqrt(2.0), 0.0);
        float2 direction = float2(-1.0, -1.0);
        if (doLineBlend) {
            bool haveShallowLine =
                (STEEP_DIRECTION_THRESHOLD * dist_B_I <= dist_F_A) && !eq(E, I) && !eq(H, I);
            bool haveSteepLine =
                (STEEP_DIRECTION_THRESHOLD * dist_F_A <= dist_B_I) && !eq(E, A) && !eq(D, A);
            origin = haveShallowLine ? float2(0.25, 0.0) : float2(0.5, 0.0);
            direction.y -= haveShallowLine ? 1.0 : 0.0;
            direction.x -= haveSteepLine ? 1.0 : 0.0;
        }
        float4 blendPix = mix(F, B, step(xbrz_dist(E, B), xbrz_dist(E, F)));
        res = mix(res, blendPix, GetLeftRatio(pos, origin, direction, scale));
    }
    // Pixel Tap Mapping: -|x|x|-|-
    //                    x|A|B|C|-
    //                    x|D|E|F|-
    //                    -|G|H|-|-
    //                    -|-|-|-|-
    if (blendResult.x != BLEND_NONE) {
        float dist_D_C = xbrz_dist(D, C);
        float dist_B_G = xbrz_dist(B, G);
        bool doLineBlend = (blendResult.x == BLEND_DOMINANT ||
                            !((blendResult.w != BLEND_NONE && !IsPixEqual(E, C)) ||
                              (blendResult.y != BLEND_NONE && !IsPixEqual(E, G)) ||
                              (IsPixEqual(C, B) && IsPixEqual(B, A) && IsPixEqual(A, D) &&
                               IsPixEqual(D, G) && !IsPixEqual(E, A))));
        float2 origin = float2(0.0, -1.0 / sqrt(2.0));
        float2 direction = float2(-1.0, 1.0);
        if (doLineBlend) {
            bool haveShallowLine =
                (STEEP_DIRECTION_THRESHOLD * dist_D_C <= dist_B_G) && !eq(E, C) && !eq(F, C);
            bool haveSteepLine =
                (STEEP_DIRECTION_THRESHOLD * dist_B_G <= dist_D_C) && !eq(E, G) && !eq(H, G);
            origin = haveShallowLine ? float2(0.0, -0.25) : float2(0.0, -0.5);
            direction.x -= haveShallowLine ? 1.0 : 0.0;
            direction.y += haveSteepLine ? 1.0 : 0.0;
        }
        float4 blendPix = mix(D, B, step(xbrz_dist(E, B), xbrz_dist(E, D)));
        res = mix(res, blendPix, GetLeftRatio(pos, origin, direction, scale));
    }
    return res;
}
)";

} // namespace Metal::Shaders
