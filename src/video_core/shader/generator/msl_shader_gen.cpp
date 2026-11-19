// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <cctype>
#include <cmath>
#include <string_view>
#include <utility>
#include <vector>
#include <fmt/format.h>

#include "common/assert.h"
#include "common/logging/log.h"
#include "video_core/pica/regs_rasterizer.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/shader/generator/msl_shader_decompiler.h"
#include "video_core/shader/generator/msl_shader_gen.h"

namespace Pica::Shader::Generator::MSL {

using ProcTexClamp = TexturingRegs::ProcTexClamp;
using ProcTexShift = TexturingRegs::ProcTexShift;
using ProcTexCombiner = TexturingRegs::ProcTexCombiner;
using ProcTexFilter = TexturingRegs::ProcTexFilter;
using TextureType = Pica::TexturingRegs::TextureConfig::TextureType;
using TevStageConfig = Pica::TexturingRegs::TevStageConfig;

namespace {

constexpr std::size_t RESERVE_SIZE = 64 * 1024;

/// Formats a float as an MSL literal that round-trips and always carries a decimal point.
std::string FloatLiteral(f32 value) {
    if (std::isnan(value)) {
        return "NAN";
    }
    if (std::isinf(value)) {
        return value > 0 ? "INFINITY" : "(-INFINITY)";
    }
    std::string s = fmt::format("{:.9g}", value);
    if (s.find_first_of(".eE") == std::string::npos) {
        s += ".0";
    }
    return s + "f";
}

bool IsPassThroughTevStage(const TevStageConfig& stage) {
    return (stage.color_op == TevStageConfig::Operation::Replace &&
            stage.alpha_op == TevStageConfig::Operation::Replace &&
            stage.color_source1 == TevStageConfig::Source::Previous &&
            stage.alpha_source1 == TevStageConfig::Source::Previous &&
            stage.color_modifier1 == TevStageConfig::ColorModifier::SourceColor &&
            stage.alpha_modifier1 == TevStageConfig::AlphaModifier::SourceAlpha &&
            stage.GetColorMultiplier() == 1 && stage.GetAlphaMultiplier() == 1);
}

constexpr std::string_view CommonHeader = R"(#include <metal_stdlib>
using namespace metal;

#define NUM_TEV_STAGES 6
#define NUM_LIGHTS 8
#define NUM_LIGHTING_SAMPLERS 24
)";

// Must match Pica::Shader::Generator::VSUniformData (and MSL::VSDepthData)
constexpr std::string_view VSUniformDef = R"(
struct VSDepth {
    float scale;
    float offset;
    uint enabled;
    uint pad;
};

struct VSUniforms {
    uint enable_clip1;
    uint flip_viewport;
    float4 clip_coef;
#ifdef ENABLE_FREECAM
    float4x4 freecam_matrix;
    uint freecam_skip_2d;
#endif
};
)";

// Must match Pica::Shader::Generator::FSUniformData (std140-like layout). The spot direction is a
// packed float3 since the following float is packed into its fourth component.
constexpr std::string_view FSUniformDef = R"(
struct LightSrc {
    float3 specular_0;
    float3 specular_1;
    float3 diffuse;
    float3 ambient;
    float3 position;
    packed_float3 spot_direction;
    float dist_atten_bias;
    float dist_atten_scale;
};

struct FSUniforms {
    int framebuffer_scale;
    int alphatest_ref;
    float depth_scale;
    float depth_offset;
    float shadow_bias_constant;
    float shadow_bias_linear;
    int scissor_x1;
    int scissor_y1;
    int scissor_x2;
    int scissor_y2;
    int fog_lut_offset;
    int proctex_noise_lut_offset;
    int proctex_color_map_offset;
    int proctex_alpha_map_offset;
    int proctex_lut_offset;
    int proctex_diff_lut_offset;
    float proctex_bias;
    int shadow_texture_bias;
    int4 lighting_lut_offset[NUM_LIGHTING_SAMPLERS / 4];
    float3 fog_color;
    float2 proctex_noise_f;
    float2 proctex_noise_a;
    float2 proctex_noise_p;
    float3 lighting_global_ambient;
    LightSrc light_src[NUM_LIGHTS];
    float4 const_color[NUM_TEV_STAGES];
    float4 tev_combiner_buffer_color;
    float3 tex_lod_bias;
    float4 tex_border_color[3];
    float4 blend_color;
};
)";

// The color passed from the vertex to the fragment stage: half precision with the fast
// interpolants option (it is a 0 to 1 value that ends up as 8 bits).
constexpr std::string_view InterpColorDef = R"(
#ifdef HALF_INTERPOLANTS
typedef half4 interp_color_t;
#else
typedef float4 interp_color_t;
#endif
)";

// Interface between the vertex and fragment stages. Members are matched by their user() name.
constexpr std::string_view VSOutputDef = R"(
struct VSOutput {
    float4 position [[position, invariant]];
    interp_color_t primary_color [[user(primary_color)]];
    float2 texcoord0 [[user(texcoord0)]];
    float2 texcoord1 [[user(texcoord1)]];
    float2 texcoord2 [[user(texcoord2)]];
    float texcoord0_w [[user(texcoord0_w)]];
    float4 normquat [[user(normquat)]];
    float3 view [[user(view)]];
#ifdef USE_CLIP_PLANES
    float clip_distance [[clip_distance]] [3];
#endif
};
)";

constexpr std::string_view FSInputDef = R"(
struct FSInput {
    float4 position [[position]];
    interp_color_t primary_color [[user(primary_color)]];
    float2 texcoord0 [[user(texcoord0)]];
    float2 texcoord1 [[user(texcoord1)]];
    float2 texcoord2 [[user(texcoord2)]];
    float texcoord0_w [[user(texcoord0_w)]];
    float4 normquat [[user(normquat)]];
    float3 view [[user(view)]];
};
)";

} // Anonymous namespace

const std::string_view QUATERNION_FIX_MSL = R"(
// Interpolating between opposite quaternions breaks the lighting. Like the PICA rasterizer, the
// quaternions of the other corners are flipped to the side of the first corner's. The vertex
// shader cannot (a vertex is shared by several triangles), so the corners are recovered here:
// the interpolated value is exactly q = qa + b.y * (qb - qa) + b.z * (qc - qa) with the
// perspective correct barycentric coordinates b, at this pixel and at its neighbours in the 2x2
// quad (the GPU evaluates the same triangle there), so the differences between the pixels give
// qb - qa and qc - qa. Only used when a flip is needed, otherwise q is returned unchanged. Must
// be called outside of any branch (derivatives).
static float4 FixQuaternion(float4 q, float3 b) {
    const float4 dqx = dfdx(q);
    const float4 dqy = dfdy(q);
    const float3 dbx = dfdx(b);
    const float3 dby = dfdy(b);
    const float det = dbx.y * dby.z - dbx.z * dby.y;
    if (det == 0.0f) {
        return q;
    }
    const float4 eb = (dqx * dby.z - dqy * dbx.z) / det;
    const float4 ec = (dqy * dbx.y - dqx * dby.y) / det;
    const float4 qa = q - b.y * eb - b.z * ec;
    const float4 qb = qa + eb;
    const float4 qc = qa + ec;
    const bool flip_b = dot(qa, qb) < 0.0f;
    const bool flip_c = dot(qa, qc) < 0.0f;
    if (!flip_b && !flip_c) {
        return q;
    }
    return qa * b.x + (flip_b ? -qb : qb) * b.y + (flip_c ? -qc : qc) * b.z;
}
)";

namespace {

constexpr std::string_view VSSanitizeDef = R"(
constant float2 EPSILON_Z = float2(0.000001f, -1.00001f);

static float4 SanitizeVertex(float4 vtx_pos) {
    float ndc_z = vtx_pos.z / vtx_pos.w;
    if (ndc_z > 0.f && ndc_z < EPSILON_Z[0]) {
        vtx_pos.z = 0.f;
    }
    if (ndc_z < -1.f && ndc_z > EPSILON_Z[1]) {
        vtx_pos.z = -vtx_pos.w;
    }
    return vtx_pos;
}

#ifdef ENABLE_FREECAM
static float4 ApplyFreeCam(constant VSUniforms& vs, float4 clip_pos) {
    if (vs.freecam_skip_2d != 0 && abs(clip_pos.w - 1.0f) < 0.0001f) {
        return clip_pos;
    }
    return vs.freecam_matrix * clip_pos;
}
#endif
)";

class FragmentModule {
public:
    FragmentModule(const FSConfig& config_, const UserConfig& user_, const Profile& profile_,
                   const MetalFSConfig& metal_config_)
        : config{config_}, user{user_}, profile{profile_}, metal{metal_config_} {
        config.ApplyProfile(profile);
        out.reserve(RESERVE_SIZE);

        const bool needs_logic_op = config.framebuffer.logic_op != FramebufferRegs::LogicOp::Copy;
        const bool needs_minmax =
            config.framebuffer.requested_rgb_blend.RequiresMinMaxEmulation() ||
            config.framebuffer.requested_alpha_blend.RequiresMinMaxEmulation();
        use_framebuffer_fetch = metal.has_color_attachment &&
                                !config.framebuffer.shadow_rendering &&
                                (needs_logic_op || needs_minmax);
        // A [[depth]] output is only valid when a depth attachment is bound. Shadow rendering
        // writes depth through the shadow buffer image instead.
        write_fragment_depth = metal.has_depth_attachment && !config.framebuffer.shadow_rendering &&
                               !metal.vertex_depth;
        quaternion_fix = metal.fragment_quaternion_fix && config.lighting.enable.Value() != 0;
    }

    std::string Generate() {
        out += CommonHeader;
#ifdef ENABLE_FREECAM
        out += "#define ENABLE_FREECAM 1\n";
#endif
        if (metal.half_interpolants) {
            out += "#define HALF_INTERPOLANTS 1\n";
        }
        out += FSUniformDef;
        out += InterpColorDef;
        if (quaternion_fix) {
            // The barycentric coordinates give back each corner's quaternion, see
            // QUATERNION_FIX_MSL.
            std::string input{FSInputDef};
            const std::string_view plain = "float4 normquat [[user(normquat)]];";
            input.replace(input.find(plain), plain.size(),
                          "float4 normquat [[user(normquat)]];\n"
                          "    float3 barycentric [[barycentric_coord, center_perspective]];");
            out += input;
            out += QUATERNION_FIX_MSL;
        } else {
            out += FSInputDef;
        }
        DefineOutput();
        DefineConstants();

        out += "\nstruct FSMain {\n";
        DefineMembers();
        DefineHelpers();
        DefineShadowHelpers();
        DefineLightingHelpers();
        DefineProcTexSampler();
        for (u32 i = 0; i < 4; i++) {
            DefineTexUnitSampler(i);
        }
        WriteMain();
        out += "};\n";

        DefineEntryPoint();
        return std::move(out);
    }

private:
    bool UsesTextureUnit(u32 unit) const {
        // Texture units are referenced by the TEV, lighting (bump/shadow) and proctex code. To keep
        // things simple every enabled unit gets declared; unused declarations are free in Metal.
        if (unit == 0) {
            return config.texture.texture0_type != TextureType::Disabled;
        }
        return true;
    }

    bool IsShadowTexture0() const {
        return config.texture.texture0_type == TextureType::Shadow2D ||
               config.texture.texture0_type == TextureType::ShadowCube;
    }

    void DefineOutput() {
        out += "\nstruct FSOutput {\n";
        out += "    float4 color [[color(0)]];\n";
        // Shadow rendering writes only through the shadow buffer image, and a [[depth]] output is
        // only legal when a depth attachment is bound.
        if (write_fragment_depth) {
            out += "    float depth [[depth(any)]];\n";
        }
        out += "};\n";
    }

    void DefineConstants() {
        if (config.proctex.enable && config.proctex.noise_enable) {
            out += R"(
constant int proctex_noise_table1[16] = {0,4,10,8,4,9,7,12,5,15,13,14,11,15,2,11};
constant int proctex_noise_table2[16] = {10,2,15,8,0,7,4,5,5,13,2,6,13,9,3,14};
)";
        }
    }

    /// Declares the members of FSMain together with the list used by its constructor.
    void DefineMembers() {
        std::vector<std::pair<std::string, std::string>> members; // (type, name)
        members.emplace_back("constant FSUniforms&", "fs");
        members.emplace_back(metal.half_luts ? "const device half2*" : "const device float2*", "lut_lf");
        members.emplace_back("const device float2*", "lut_rg");
        members.emplace_back("const device float4*", "lut_rgba");

        const auto texture0_type = config.texture.texture0_type.Value();
        if (UsesTextureUnit(0)) {
            switch (texture0_type) {
            case TextureType::TextureCube:
                members.emplace_back("texturecube<float>", "tex0");
                members.emplace_back("sampler", "samp0");
                break;
            case TextureType::Shadow2D:
                members.emplace_back("texture2d<uint>", "tex0");
                break;
            case TextureType::ShadowCube:
                members.emplace_back("array<texture2d<uint>, 6>", "tex0");
                break;
            default:
                members.emplace_back("texture2d<float>", "tex0");
                members.emplace_back("sampler", "samp0");
                break;
            }
        }
        members.emplace_back("texture2d<float>", "tex1");
        members.emplace_back("sampler", "samp1");
        members.emplace_back("texture2d<float>", "tex2");
        members.emplace_back("sampler", "samp2");
        if (user.use_custom_normal) {
            members.emplace_back("texture2d<float>", "tex_normal");
            members.emplace_back("sampler", "samp_normal");
        }
        if (use_framebuffer_fetch) {
            members.emplace_back("float4", "dest_color");
        }

        // Stage inputs
        members.emplace_back("float4", "frag_coord");
        members.emplace_back("float4", "primary_color");
        members.emplace_back("float2", "texcoord0");
        members.emplace_back("float2", "texcoord1");
        members.emplace_back("float2", "texcoord2");
        members.emplace_back("float", "texcoord0_w");
        members.emplace_back("float4", "normquat");
        members.emplace_back("float3", "view");

        for (const auto& [type, name] : members) {
            out += fmt::format("    {} {};\n", type, name);
        }
        if (config.framebuffer.shadow_rendering) {
            // Written by run(), consumed by the entry point which owns the shadow buffer.
            out += "    bool shadow_valid = false;\n"
                   "    uint shadow_d = 0u;\n"
                   "    uint shadow_s = 0u;\n";
        }

        // Constructor
        out += "\n    FSMain(";
        for (std::size_t i = 0; i < members.size(); i++) {
            const auto& [type, name] = members[i];
            out += fmt::format("{}{} {}_", i == 0 ? "" : ", ", type, name);
        }
        out += ")\n        : ";
        for (std::size_t i = 0; i < members.size(); i++) {
            const auto& name = members[i].second;
            out += fmt::format("{}{}({}_)", i == 0 ? "" : ", ", name, name);
        }
        out += " {}\n";

        ctor_members = std::move(members);
    }

    void DefineEntryPoint() {
        out += fmt::format("\nfragment FSOutput {}(FSInput in [[stage_in]]", FS_ENTRY_POINT);
        out += fmt::format(",\n    constant FSUniforms& fs [[buffer({})]]", +FS_BUFFER_UNIFORMS);
        out += fmt::format(",\n    const device {}* lut_lf [[buffer({})]]",
                           metal.half_luts ? "half2" : "float2", +FS_BUFFER_LUT_LF);
        out += fmt::format(",\n    const device float2* lut_rg [[buffer({})]]", +FS_BUFFER_LUT_RG);
        out +=
            fmt::format(",\n    const device float4* lut_rgba [[buffer({})]]", +FS_BUFFER_LUT_RGBA);

        // Entry point parameters are declared for every member that is not a stage input.
        for (const auto& [type, name] : ctor_members) {
            if (name == "fs" || name == "lut_lf" || name == "lut_rg" || name == "lut_rgba") {
                continue;
            }
            if (name == "tex0") {
                out += fmt::format(",\n    {} tex0 [[texture({})]]", type, +FS_TEXTURE_0);
            } else if (name == "samp0") {
                out += fmt::format(",\n    sampler samp0 [[sampler({})]]", +FS_SAMPLER_0);
            } else if (name == "tex1") {
                out += fmt::format(",\n    texture2d<float> tex1 [[texture({})]]", +FS_TEXTURE_1);
            } else if (name == "samp1") {
                out += fmt::format(",\n    sampler samp1 [[sampler({})]]", +FS_SAMPLER_1);
            } else if (name == "tex2") {
                out += fmt::format(",\n    texture2d<float> tex2 [[texture({})]]", +FS_TEXTURE_2);
            } else if (name == "samp2") {
                out += fmt::format(",\n    sampler samp2 [[sampler({})]]", +FS_SAMPLER_2);
            } else if (name == "tex_normal") {
                out += fmt::format(",\n    texture2d<float> tex_normal [[texture({})]]",
                                   +FS_TEXTURE_NORMAL);
            } else if (name == "samp_normal") {
                out +=
                    fmt::format(",\n    sampler samp_normal [[sampler({})]]", +FS_SAMPLER_NORMAL);
            } else if (name == "dest_color") {
                out += ",\n    float4 dest_color [[color(0)]]";
            }
        }
        if (config.framebuffer.shadow_rendering) {
            // Raster order groups give us the same guarantees as fragment shader interlock.
            out += fmt::format(",\n    texture2d<uint, access::read_write> shadow_buffer "
                               "[[texture({}), raster_order_group(0)]]",
                               +FS_TEXTURE_SHADOW_BUFFER);
        }
        out += ")\n{\n";
        if (quaternion_fix) {
            // Before any branch: it takes derivatives.
            out += "    const float4 normquat = FixQuaternion(in.normquat, in.barycentric);\n";
        }
        out += "    FSMain m(";
        for (std::size_t i = 0; i < ctor_members.size(); i++) {
            const auto& name = ctor_members[i].second;
            std::string arg = name;
            if (name == "frag_coord") {
                arg = "in.position";
            } else if (name == "primary_color") {
                arg = "float4(in.primary_color)";
            } else if (name == "normquat" && quaternion_fix) {
                arg = "normquat";
            } else if (name == "texcoord0" || name == "texcoord1" ||
                       name == "texcoord2" || name == "texcoord0_w" || name == "normquat" ||
                       name == "view") {
                arg = "in." + name;
            }
            out += fmt::format("{}{}", i == 0 ? "" : ", ", arg);
        }
        out += ");\n    FSOutput result = m.run();\n";
        if (config.framebuffer.shadow_rendering) {
            out += R"(    if (m.shadow_valid) {
        uint2 image_coord = uint2(in.position.xy);
        uint old_shadow = shadow_buffer.read(image_coord).x;
        uint new_shadow = m.UpdateShadow(old_shadow, m.shadow_d, m.shadow_s);
        shadow_buffer.write(uint4(new_shadow), image_coord);
    }
)";
        }
        out += "    return result;\n}\n";
    }

    void DefineHelpers() {
        out += R"(
    static float3 quaternion_rotate(float4 q, float3 v) {
        return v + 2.0f * cross(q.xyz, cross(q.xyz, v) + q.w * v);
    }

    static float byteround(float x) {
        return round(x * 255.0f) * (1.0f / 255.0f);
    }

    static float2 byteround(float2 x) {
        return round(x * 255.0f) * (1.0f / 255.0f);
    }

    static float3 byteround(float3 x) {
        return round(x * 255.0f) * (1.0f / 255.0f);
    }

    static float4 byteround(float4 x) {
        return round(x * 255.0f) * (1.0f / 255.0f);
    }

    static half byteround(half x) {
        return round(x * 255.0h) * (1.0h / 255.0h);
    }

    static half3 byteround(half3 x) {
        return round(x * 255.0h) * (1.0h / 255.0h);
    }

    static half4 byteround(half4 x) {
        return round(x * 255.0h) * (1.0h / 255.0h);
    }

    static float getLod(float2 coord) {
        float2 d = max(abs(dfdx(coord)), abs(dfdy(coord)));
        return log2(max(d.x, d.y));
    }

    static uint2 DecodeShadow(uint pixel) {
        return uint2(pixel >> 8, pixel & 0xFFu);
    }
)";
    }

    void DefineLightingHelpers() {
        if (!config.lighting.enable) {
            return;
        }

        out += R"(
    float LookupLightingLUT(int lut_index, int index, float delta) {
        float2 entry = float2(lut_lf[fs.lighting_lut_offset[lut_index >> 2][lut_index & 3] + index]);
        return entry.x + entry.y * delta;
    }

    float LookupLightingLUTUnsigned(int lut_index, float pos) {
        int index = int(clamp(floor(pos * 256.0f), 0.f, 255.f));
        float delta = pos * 256.0f - float(index);
        return LookupLightingLUT(lut_index, index, delta);
    }

    float LookupLightingLUTSigned(int lut_index, float pos) {
        int index = int(clamp(floor(pos * 128.0f), -128.f, 127.f));
        float delta = pos * 128.0f - float(index);
        if (index < 0) index += 256;
        return LookupLightingLUT(lut_index, index, delta);
    }
)";
    }

    void DefineShadowHelpers() {
        if (config.framebuffer.shadow_rendering) {
            out += R"(
    static uint EncodeShadow(uint2 pixel) {
        return (pixel.x << 8) | pixel.y;
    }

    uint UpdateShadow(uint pixel, uint d, uint s) {
        uint2 ref = DecodeShadow(pixel);
        if (d < ref.x) {
            if (s == 0u) {
                ref.x = d;
            } else {
                s = uint(float(s) / (fs.shadow_bias_constant + fs.shadow_bias_linear * float(d) / float(ref.x)));
                ref.y = min(s, ref.y);
            }
        }
        return EncodeShadow(ref);
    }
)";
        }

        if (!IsShadowTexture0()) {
            return;
        }

        out += R"(
    static float CompareShadow(uint pixel, uint z) {
        uint2 p = DecodeShadow(pixel);
        return (p.x <= z) ? 0.0f : float(p.y) * (1.0f / 255.0f);
    }

    static float mix2(float4 s, float2 a) {
        float2 t = mix(s.xy, s.zw, a.yy);
        return mix(t.x, t.y, a.x);
    }
)";

        if (config.texture.texture0_type == TextureType::Shadow2D) {
            out += R"(
    float SampleShadow2D(int2 uv, uint z) {
        int2 size = int2(tex0.get_width(), tex0.get_height());
        if (any(uv < int2(0)) || any(uv >= size))
            return 1.0f;
        return CompareShadow(tex0.read(uint2(uv)).x, z);
    }

    float4 shadowTexture(float2 uv, float w) {
)";
            if (!config.texture.shadow_texture_orthographic) {
                out += "        uv /= w;\n";
            }
            out += R"(
        uint z = uint(max(0, int(min(abs(w), 1.0f) * float(0xFFFFFF)) - fs.shadow_texture_bias));
        float2 coord = float2(tex0.get_width(), tex0.get_height()) * uv - float2(0.5f);
        float2 coord_floor = floor(coord);
        float2 f = coord - coord_floor;
        int2 i = int2(coord_floor);
        float4 s = float4(
            SampleShadow2D(i              , z),
            SampleShadow2D(i + int2(1, 0), z),
            SampleShadow2D(i + int2(0, 1), z),
            SampleShadow2D(i + int2(1, 1), z));
        return float4(mix2(s, f));
    }
)";
        } else {
            out += R"(
    uint4 SampleShadowCube(int face, int2 i00, int2 i10, int2 i01, int2 i11) {
        return uint4(
            tex0[face].read(uint2(i00)).x,
            tex0[face].read(uint2(i10)).x,
            tex0[face].read(uint2(i01)).x,
            tex0[face].read(uint2(i11)).x);
    }

    float4 shadowTextureCube(float2 uv, float w) {
        int2 size = int2(tex0[0].get_width(), tex0[0].get_height());
        float3 c = float3(uv, w);
        float3 a = abs(c);
        if (a.x > a.y && a.x > a.z) {
            w = a.x;
            uv = -c.zy;
            if (c.x < 0.0f) uv.x = -uv.x;
        } else if (a.y > a.z) {
            w = a.y;
            uv = c.xz;
            if (c.y < 0.0f) uv.y = -uv.y;
        } else {
            w = a.z;
            uv = -c.xy;
            if (c.z > 0.0f) uv.x = -uv.x;
        }
        uint z = uint(max(0, int(min(w, 1.0f) * float(0xFFFFFF)) - fs.shadow_texture_bias));
        float2 coord = float2(size) * (uv / w * float2(0.5f) + float2(0.5f)) - float2(0.5f);
        float2 coord_floor = floor(coord);
        float2 f = coord - coord_floor;
        int2 i00 = int2(coord_floor);
        int2 i10 = i00 + int2(1, 0);
        int2 i01 = i00 + int2(0, 1);
        int2 i11 = i00 + int2(1, 1);
        int2 cmin = int2(0), cmax = size - int2(1, 1);
        i00 = clamp(i00, cmin, cmax);
        i10 = clamp(i10, cmin, cmax);
        i01 = clamp(i01, cmin, cmax);
        i11 = clamp(i11, cmin, cmax);
        uint4 pixels;
        if (a.x > a.y && a.x > a.z) {
            pixels = SampleShadowCube(c.x > 0.0f ? 0 : 1, i00, i10, i01, i11);
        } else if (a.y > a.z) {
            pixels = SampleShadowCube(c.y > 0.0f ? 2 : 3, i00, i10, i01, i11);
        } else {
            pixels = SampleShadowCube(c.z > 0.0f ? 4 : 5, i00, i10, i01, i11);
        }
        float4 s = float4(
            CompareShadow(pixels.x, z),
            CompareShadow(pixels.y, z),
            CompareShadow(pixels.z, z),
            CompareShadow(pixels.w, z));
        return float4(mix2(s, f));
    }
)";
        }
    }

    void AppendProcTexShiftOffset(std::string_view v, ProcTexShift mode, ProcTexClamp clamp_mode) {
        const auto offset = (clamp_mode == ProcTexClamp::MirroredRepeat) ? "1.0f" : "0.5f";
        switch (mode) {
        case ProcTexShift::None:
            out += "0.0f";
            break;
        case ProcTexShift::Odd:
            out += fmt::format("{} * float((int({}) / 2) % 2)", offset, v);
            break;
        case ProcTexShift::Even:
            out += fmt::format("{} * float(((int({}) + 1) / 2) % 2)", offset, v);
            break;
        default:
            LOG_CRITICAL(HW_GPU, "Unknown shift mode {}", mode);
            out += "0.0f";
            break;
        }
    }

    void AppendProcTexClamp(std::string_view var, ProcTexClamp mode) {
        switch (mode) {
        case ProcTexClamp::ToZero:
            out += fmt::format("        {0} = {0} > 1.0f ? 0.0f : {0};\n", var);
            break;
        case ProcTexClamp::ToEdge:
            out += fmt::format("        {0} = min({0}, 1.0f);\n", var);
            break;
        case ProcTexClamp::SymmetricalRepeat:
            out += fmt::format("        {0} = fract({0});\n", var);
            break;
        case ProcTexClamp::MirroredRepeat:
            out += fmt::format(
                "        {0} = int({0}) % 2 == 0 ? fract({0}) : 1.0f - fract({0});\n", var);
            break;
        case ProcTexClamp::Pulse:
            out += fmt::format("        {0} = {0} > 0.5f ? 1.0f : 0.0f;\n", var);
            break;
        default:
            LOG_CRITICAL(HW_GPU, "Unknown clamp mode {}", mode);
            out += fmt::format("        {0} = min({0}, 1.0f);\n", var);
            break;
        }
    }

    void AppendProcTexCombineAndMap(ProcTexCombiner combiner, std::string_view offset) {
        const auto combined = [combiner] {
            switch (combiner) {
            case ProcTexCombiner::U:
                return "u";
            case ProcTexCombiner::U2:
                return "(u * u)";
            case ProcTexCombiner::V:
                return "v";
            case ProcTexCombiner::V2:
                return "(v * v)";
            case ProcTexCombiner::Add:
                return "((u + v) * 0.5f)";
            case ProcTexCombiner::Add2:
                return "((u * u + v * v) * 0.5f)";
            case ProcTexCombiner::SqrtAdd2:
                return "min(sqrt(u * u + v * v), 1.0f)";
            case ProcTexCombiner::Min:
                return "min(u, v)";
            case ProcTexCombiner::Max:
                return "max(u, v)";
            case ProcTexCombiner::RMax:
                return "min(((u + v) * 0.5f + sqrt(u * u + v * v)) * 0.5f, 1.0f)";
            default:
                LOG_CRITICAL(HW_GPU, "Unknown combiner {}", combiner);
                return "0.0f";
            }
        }();
        out += fmt::format("ProcTexLookupLUT(fs.{}, {})", offset, combined);
    }

    void DefineProcTexSampler() {
        if (!config.proctex.enable) {
            return;
        }

        // For NoiseLUT/ColorMap/AlphaMap, coord=0.0 is lut[0], coord=127.0/128.0 is lut[127] and
        // coord=1.0 is lut[127]+lut_diff[127]. For other indices, the result is interpolated using
        // value entries and difference entries.
        out += R"(
    float ProcTexLookupLUT(int offset, float coord) {
        coord *= 128.0f;
        float index_i = clamp(floor(coord), 0.0f, 127.0f);
        float index_f = coord - index_i; // fract() cannot be used here because 128.0 needs to be
                                         // extracted as index_i = 127.0 and index_f = 1.0
        float2 entry = lut_rg[int(index_i) + offset];
        return clamp(entry.x + entry.y * index_f, 0.0f, 1.0f);
    }
)";

        if (config.proctex.noise_enable) {
            // See swrasterizer/proctex.cpp for more information about these functions
            out += R"(
    static int ProcTexNoiseRand1D(int v) {
        return ((v % 9 + 2) * 3 & 0xF) ^ proctex_noise_table1[(v / 9) & 0xF];
    }

    static float ProcTexNoiseRand2D(float2 point) {
        int u2 = ProcTexNoiseRand1D(int(point.x));
        int v2 = ProcTexNoiseRand1D(int(point.y));
        v2 += ((u2 & 3) == 1) ? 4 : 0;
        v2 ^= (u2 & 1) * 6;
        v2 += 10 + u2;
        v2 &= 0xF;
        v2 ^= proctex_noise_table2[u2];
        return -1.0f + float(v2) * (2.0f / 15.0f);
    }

    float ProcTexNoiseCoef(float2 x) {
        float2 grid  = 9.0f * fs.proctex_noise_f * abs(x + fs.proctex_noise_p);
        float2 point = floor(grid);
        float2 frac  = grid - point;

        float g0 = ProcTexNoiseRand2D(point) * (frac.x + frac.y);
        float g1 = ProcTexNoiseRand2D(point + float2(1.0f, 0.0f)) * (frac.x + frac.y - 1.0f);
        float g2 = ProcTexNoiseRand2D(point + float2(0.0f, 1.0f)) * (frac.x + frac.y - 1.0f);
        float g3 = ProcTexNoiseRand2D(point + float2(1.0f, 1.0f)) * (frac.x + frac.y - 2.0f);

        float x_noise = ProcTexLookupLUT(fs.proctex_noise_lut_offset, frac.x);
        float y_noise = ProcTexLookupLUT(fs.proctex_noise_lut_offset, frac.y);
        float x0 = mix(g0, g1, x_noise);
        float x1 = mix(g2, g3, x_noise);
        return mix(x0, x1, y_noise);
    }
)";
        }

        out += "\n    float4 SampleProcTexColor(float lut_coord, int level) {\n";
        out += fmt::format("        int lut_width = {} >> level;\n", config.proctex.lut_width);
        // Offsets for level 4-7 seem to be hardcoded
        out += fmt::format("        const int lut_offsets[8] = {{{}, {}, {}, {}, 0xF0, 0xF8, 0xFC, "
                           "0xFE}};\n",
                           config.proctex.lut_offset0, config.proctex.lut_offset1,
                           config.proctex.lut_offset2, config.proctex.lut_offset3);
        out += "        int lut_offset = lut_offsets[level];\n";
        // For the color lut, coord=0.0 is lut[offset] and coord=1.0 is lut[offset+width-1]
        out += "        lut_coord *= float(lut_width - 1);\n";

        switch (config.proctex.lut_filter) {
        case ProcTexFilter::Linear:
        case ProcTexFilter::LinearMipmapLinear:
        case ProcTexFilter::LinearMipmapNearest:
        default: // Undefined filter values (6, 7) behave like linear filtering here.
            out += "        int lut_index_i = int(lut_coord) + lut_offset;\n";
            out += "        float lut_index_f = fract(lut_coord);\n";
            out += "        return lut_rgba[lut_index_i + fs.proctex_lut_offset] + "
                   "lut_index_f * lut_rgba[lut_index_i + fs.proctex_diff_lut_offset];\n";
            break;
        case ProcTexFilter::Nearest:
        case ProcTexFilter::NearestMipmapLinear:
        case ProcTexFilter::NearestMipmapNearest:
            out += "        lut_coord += float(lut_offset);\n";
            out += "        return lut_rgba[int(round(lut_coord)) + fs.proctex_lut_offset];\n";
            break;
        }
        out += "    }\n";

        out += "\n    float4 ProcTex() {\n";
        if (config.proctex.coord < 3) {
            out +=
                fmt::format("        float2 uv = abs(texcoord{});\n", config.proctex.coord.Value());
        } else {
            LOG_CRITICAL(Render, "Unexpected proctex.coord >= 3");
            out += "        float2 uv = abs(texcoord0);\n";
        }

        // This LOD formula is the same as the LOD upper limit defined in OpenGL.
        // f(x, y) <= m_u + m_v + m_w
        // (See OpenGL 4.6 spec, 8.14.1 - Scale Factor and Level-of-Detail)
        // Note: this is different from the one normal 2D textures use.
        out += "        float2 duv = max(abs(dfdx(uv)), abs(dfdy(uv)));\n";
        // unlike normal texture, the bias is inside the log2
        out += fmt::format(
            "        float lod = log2(abs(float({}) * fs.proctex_bias) * (duv.x + duv.y));\n",
            config.proctex.lut_width);
        out += "        if (fs.proctex_bias == 0.0f) lod = 0.0f;\n";
        out += fmt::format("        lod = clamp(lod, {}, {});\n",
                           FloatLiteral(std::max(0.0f, static_cast<f32>(config.proctex.lod_min))),
                           FloatLiteral(std::min(7.0f, static_cast<f32>(config.proctex.lod_max))));

        // Get shift offset before noise generation
        out += "        float u_shift = ";
        AppendProcTexShiftOffset("uv.y", config.proctex.u_shift, config.proctex.u_clamp);
        out += ";\n";
        out += "        float v_shift = ";
        AppendProcTexShiftOffset("uv.x", config.proctex.v_shift, config.proctex.v_clamp);
        out += ";\n";

        // Generate noise
        if (config.proctex.noise_enable) {
            out += "        uv += fs.proctex_noise_a * ProcTexNoiseCoef(uv);\n"
                   "        uv = abs(uv);\n";
        }

        // Shift
        out += "        float u = uv.x + u_shift;\n"
               "        float v = uv.y + v_shift;\n";

        // Clamp
        AppendProcTexClamp("u", config.proctex.u_clamp);
        AppendProcTexClamp("v", config.proctex.v_clamp);

        // Combine and map
        out += "        float lut_coord = ";
        AppendProcTexCombineAndMap(config.proctex.color_combiner, "proctex_color_map_offset");
        out += ";\n";

        switch (config.proctex.lut_filter) {
        case ProcTexFilter::Linear:
        case ProcTexFilter::Nearest:
        default: // Undefined filter values (6, 7) sample the base level.
            out += "        float4 final_color = SampleProcTexColor(lut_coord, 0);\n";
            break;
        case ProcTexFilter::NearestMipmapNearest:
        case ProcTexFilter::LinearMipmapNearest:
            out += "        float4 final_color = SampleProcTexColor(lut_coord, int(round(lod)));\n";
            break;
        case ProcTexFilter::NearestMipmapLinear:
        case ProcTexFilter::LinearMipmapLinear:
            out += "        int lod_i = int(lod);\n"
                   "        float lod_f = fract(lod);\n"
                   "        float4 final_color = mix(SampleProcTexColor(lut_coord, lod_i), "
                   "SampleProcTexColor(lut_coord, lod_i + 1), lod_f);\n";
            break;
        }

        if (config.proctex.separate_alpha) {
            // Note: in separate alpha mode, the alpha channel skips the color LUT look up stage. It
            // uses the output of CombineAndMap directly instead.
            out += "        float final_alpha = ";
            AppendProcTexCombineAndMap(config.proctex.alpha_combiner, "proctex_alpha_map_offset");
            out += ";\n";
            out += "        return float4(final_color.xyz, final_alpha);\n    }\n";
        } else {
            out += "        return final_color;\n    }\n";
        }
    }

    void DefineTexUnitSampler(u32 texture_unit) {
        out += fmt::format("\n    float4 sampleTexUnit{}() {{\n", texture_unit);
        if (texture_unit == 0 && config.texture.texture0_type == TextureType::Disabled) {
            out += "        return float4(0.0f);\n    }\n";
            return;
        }

        if (texture_unit < 3) {
            const u32 texcoord_num =
                texture_unit == 2 && config.texture.texture2_use_coord1 ? 1 : texture_unit;
            if (config.texture.texture_border_color[texture_unit].enable_s) {
                out += fmt::format("        if (texcoord{0}.x < 0.0f || texcoord{0}.x > 1.0f) {{\n"
                                   "            return fs.tex_border_color[{1}];\n"
                                   "        }}\n",
                                   texcoord_num, texture_unit);
            }
            if (config.texture.texture_border_color[texture_unit].enable_t) {
                out += fmt::format("        if (texcoord{0}.y < 0.0f || texcoord{0}.y > 1.0f) {{\n"
                                   "            return fs.tex_border_color[{1}];\n"
                                   "        }}\n",
                                   texcoord_num, texture_unit);
            }
        }

        const auto sample_2d = [&](u32 unit, u32 coord) {
            return fmt::format("        return tex{0}.sample(samp{0}, texcoord{1}, "
                               "level(getLod(texcoord{1} * float2(tex{0}.get_width(), "
                               "tex{0}.get_height())) + fs.tex_lod_bias[{0}]));\n",
                               unit, coord);
        };

        switch (texture_unit) {
        case 0:
            switch (config.texture.texture0_type) {
            case TextureType::Texture2D:
                out += sample_2d(0, 0);
                break;
            case TextureType::Projection2D:
                // TODO (wwylele): find the exact LOD formula for projection texture
                out += "        return tex0.sample(samp0, texcoord0 / texcoord0_w);\n";
                break;
            case TextureType::TextureCube:
                out += "        return tex0.sample(samp0, float3(texcoord0, texcoord0_w));\n";
                break;
            case TextureType::Shadow2D:
                out += "        return shadowTexture(texcoord0, texcoord0_w);\n";
                break;
            case TextureType::ShadowCube:
                out += "        return shadowTextureCube(texcoord0, texcoord0_w);\n";
                break;
            default:
                LOG_CRITICAL(HW_GPU, "Unhandled texture type {:x}",
                             config.texture.texture0_type.Value());
                out += "        return tex0.sample(samp0, texcoord0);\n";
                break;
            }
            break;
        case 1:
            out += sample_2d(1, 1);
            break;
        case 2:
            out += sample_2d(2, config.texture.texture2_use_coord1 ? 1 : 2);
            break;
        case 3:
            if (config.proctex.enable) {
                out += "        return ProcTex();\n";
            } else {
                out += "        return float4(0.0f);\n";
            }
            break;
        default:
            UNREACHABLE();
            break;
        }
        out += "    }\n";
    }

    std::string GetSource(TevStageConfig::Source source, u32 tev_index) {
        using Source = TevStageConfig::Source;
        // With fast color math the texture units are sampled once before the combiners, into
        // half precision values (see WriteMain).
        if (metal.fast_color && source >= Source::Texture0 && source <= Source::Texture3) {
            return fmt::format("tex_h{}",
                               static_cast<u32>(source) - static_cast<u32>(Source::Texture0));
        }
        std::string value = GetSourceValue(source, tev_index);
        // With fast color math the combiners run in half precision (see ToHalfPrecision), the
        // float inputs are converted explicitly (float4 becomes half4 there).
        if (metal.fast_color && source != Source::PreviousBuffer && source != Source::Previous &&
            !value.starts_with("float4(")) {
            value = "float4(" + value + ")";
        }
        return value;
    }

    std::string GetSourceValue(TevStageConfig::Source source, u32 tev_index) {
        using Source = TevStageConfig::Source;
        switch (source) {
        case Source::PrimaryColor:
            return "rounded_primary_color";
        case Source::PrimaryFragmentColor:
            return "primary_fragment_color";
        case Source::SecondaryFragmentColor:
            return "secondary_fragment_color";
        case Source::Texture0:
            return "sampleTexUnit0()";
        case Source::Texture1:
            return "sampleTexUnit1()";
        case Source::Texture2:
            return "sampleTexUnit2()";
        case Source::Texture3:
            return "sampleTexUnit3()";
        case Source::PreviousBuffer:
            return "combiner_buffer";
        case Source::Constant:
            return fmt::format("fs.const_color[{}]", tev_index);
        case Source::Previous:
            return "combiner_output";
        default:
            LOG_CRITICAL(Render, "Unknown source op {}", source);
            return "float4(0.0f)";
        }
    }

    void AppendColorModifier(TevStageConfig::ColorModifier modifier, TevStageConfig::Source source,
                             u32 tev_index) {
        using Source = TevStageConfig::Source;
        using ColorModifier = TevStageConfig::ColorModifier;
        const TevStageConfig stage = config.texture.tev_stages[tev_index];
        const bool force_source3 = tev_index == 0 && source == Source::Previous;
        const auto src = GetSource(force_source3 ? stage.color_source3.Value() : source, tev_index);
        switch (modifier) {
        case ColorModifier::SourceColor:
            out += fmt::format("{}.rgb", src);
            break;
        case ColorModifier::OneMinusSourceColor:
            out += fmt::format("float3(1.0f) - {}.rgb", src);
            break;
        case ColorModifier::SourceAlpha:
            out += fmt::format("{}.aaa", src);
            break;
        case ColorModifier::OneMinusSourceAlpha:
            out += fmt::format("float3(1.0f) - {}.aaa", src);
            break;
        case ColorModifier::SourceRed:
            out += fmt::format("{}.rrr", src);
            break;
        case ColorModifier::OneMinusSourceRed:
            out += fmt::format("float3(1.0f) - {}.rrr", src);
            break;
        case ColorModifier::SourceGreen:
            out += fmt::format("{}.ggg", src);
            break;
        case ColorModifier::OneMinusSourceGreen:
            out += fmt::format("float3(1.0f) - {}.ggg", src);
            break;
        case ColorModifier::SourceBlue:
            out += fmt::format("{}.bbb", src);
            break;
        case ColorModifier::OneMinusSourceBlue:
            out += fmt::format("float3(1.0f) - {}.bbb", src);
            break;
        default:
            out += "float3(0.0f)";
            LOG_CRITICAL(Render, "Unknown color modifier op {}", modifier);
            break;
        }
    }

    void AppendAlphaModifier(TevStageConfig::AlphaModifier modifier, TevStageConfig::Source source,
                             u32 tev_index) {
        using Source = TevStageConfig::Source;
        using AlphaModifier = TevStageConfig::AlphaModifier;
        const TevStageConfig stage = config.texture.tev_stages[tev_index];
        const bool force_source3 = tev_index == 0 && source == Source::Previous;
        const auto src = GetSource(force_source3 ? stage.alpha_source3.Value() : source, tev_index);
        switch (modifier) {
        case AlphaModifier::SourceAlpha:
            out += fmt::format("{}.a", src);
            break;
        case AlphaModifier::OneMinusSourceAlpha:
            out += fmt::format("1.0f - {}.a", src);
            break;
        case AlphaModifier::SourceRed:
            out += fmt::format("{}.r", src);
            break;
        case AlphaModifier::OneMinusSourceRed:
            out += fmt::format("1.0f - {}.r", src);
            break;
        case AlphaModifier::SourceGreen:
            out += fmt::format("{}.g", src);
            break;
        case AlphaModifier::OneMinusSourceGreen:
            out += fmt::format("1.0f - {}.g", src);
            break;
        case AlphaModifier::SourceBlue:
            out += fmt::format("{}.b", src);
            break;
        case AlphaModifier::OneMinusSourceBlue:
            out += fmt::format("1.0f - {}.b", src);
            break;
        default:
            out += "0.0f";
            LOG_CRITICAL(Render, "Unknown alpha modifier op {}", modifier);
            break;
        }
    }

    void AppendColorCombiner(TevStageConfig::Operation operation) {
        const auto get_combiner = [operation] {
            using Operation = TevStageConfig::Operation;
            switch (operation) {
            case Operation::Replace:
                return "color_results_1";
            case Operation::Modulate:
                return "color_results_1 * color_results_2";
            case Operation::Add:
                return "color_results_1 + color_results_2";
            case Operation::AddSigned:
                return "color_results_1 + color_results_2 - float3(0.5f)";
            case Operation::Lerp:
                return "mix(color_results_2, color_results_1, color_results_3)";
            case Operation::Subtract:
                return "color_results_1 - color_results_2";
            case Operation::MultiplyThenAdd:
                return "fma(color_results_1, color_results_2, color_results_3)";
            case Operation::AddThenMultiply:
                return "min(color_results_1 + color_results_2, float3(1.0f)) * color_results_3";
            case Operation::Dot3_RGB:
            case Operation::Dot3_RGBA:
                return "float3(dot(color_results_1 - float3(0.5f), color_results_2 - "
                       "float3(0.5f)) * 4.0f)";
            default:
                LOG_CRITICAL(Render, "Unknown color combiner operation: {}", operation);
                return "float3(0.0f)";
            }
        };
        out += fmt::format("clamp({}, float3(0.0f), float3(1.0f))", get_combiner());
    }

    void AppendAlphaCombiner(TevStageConfig::Operation operation) {
        const auto get_combiner = [operation] {
            using Operation = TevStageConfig::Operation;
            switch (operation) {
            case Operation::Replace:
                return "alpha_results_1";
            case Operation::Modulate:
                return "alpha_results_1 * alpha_results_2";
            case Operation::Add:
                return "alpha_results_1 + alpha_results_2";
            case Operation::AddSigned:
                return "alpha_results_1 + alpha_results_2 - 0.5f";
            case Operation::Lerp:
                return "mix(alpha_results_2, alpha_results_1, alpha_results_3)";
            case Operation::Subtract:
                return "alpha_results_1 - alpha_results_2";
            case Operation::MultiplyThenAdd:
                return "fma(alpha_results_1, alpha_results_2, alpha_results_3)";
            case Operation::AddThenMultiply:
                return "min(alpha_results_1 + alpha_results_2, 1.0f) * alpha_results_3";
            default:
                LOG_CRITICAL(Render, "Unknown alpha combiner operation: {}", operation);
                return "0.0f";
            }
        };
        out += fmt::format("clamp({}, 0.0f, 1.0f)", get_combiner());
    }

    void WriteAlphaTestCondition(FramebufferRegs::CompareFunc func) {
        using CompareFunc = FramebufferRegs::CompareFunc;
        switch (func) {
        case CompareFunc::Never:
            out += "        discard_fragment();\n        return fs_out;\n";
            return;
        case CompareFunc::Always:
            return;
        case CompareFunc::Equal:
        case CompareFunc::NotEqual:
        case CompareFunc::LessThan:
        case CompareFunc::LessThanOrEqual:
        case CompareFunc::GreaterThan:
        case CompareFunc::GreaterThanOrEqual: {
            static constexpr std::array op{"!=", "==", ">=", ">", "<=", "<"};
            const auto index = static_cast<u32>(func) - static_cast<u32>(CompareFunc::Equal);
            out += fmt::format("        if (int(combiner_output.a * 255.0f) {} fs.alphatest_ref) "
                               "{{\n            discard_fragment();\n            return fs_out;\n"
                               "        }}\n",
                               op[index]);
            return;
        }
        default:
            LOG_CRITICAL(Render, "Unknown alpha test condition {}", func);
            return;
        }
    }

    /**
     * Turns combiner code into half precision code: float types become half types, float
     * literals become half literals, and the combiner variables get names of their own so the
     * float combiner_output can be assigned from them afterwards.
     */
    static std::string ToHalfPrecision(const std::string& code) {
        const auto is_ident = [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        };
        std::string result;
        result.reserve(code.size());
        std::size_t i = 0;
        while (i < code.size()) {
            const char c = code[i];
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                std::size_t end = i;
                while (end < code.size() && is_ident(code[end])) {
                    end++;
                }
                std::string word = code.substr(i, end - i);
                if (word == "float" || word == "float2" || word == "float3" || word == "float4") {
                    word = "half" + word.substr(5);
                } else if (word == "next_combiner_buffer") {
                    word = "tev_next";
                } else if (word == "combiner_buffer") {
                    word = "tev_buffer";
                } else if (word == "combiner_output") {
                    word = "tev_out";
                }
                result += word;
                i = end;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c))) {
                std::size_t end = i;
                bool has_dot = false;
                while (end < code.size() &&
                       (std::isdigit(static_cast<unsigned char>(code[end])) || code[end] == '.')) {
                    has_dot |= code[end] == '.';
                    end++;
                }
                result += code.substr(i, end - i);
                if (has_dot && end < code.size() && code[end] == 'f' &&
                    (end + 1 >= code.size() || !is_ident(code[end + 1]))) {
                    result += 'h';
                    end++;
                }
                i = end;
                continue;
            }
            result += c;
            i++;
        }
        return result;
    }

    void WriteTevStage(u32 index) {
        const TevStageConfig stage = config.texture.tev_stages[index];
        if (!IsPassThroughTevStage(stage)) {
            out += "        color_results_1 = ";
            AppendColorModifier(stage.color_modifier1, stage.color_source1, index);
            out += ";\n        color_results_2 = ";
            AppendColorModifier(stage.color_modifier2, stage.color_source2, index);
            out += ";\n        color_results_3 = ";
            AppendColorModifier(stage.color_modifier3, stage.color_source3, index);

            // Round the output of each TEV stage to maintain the PICA's 8 bits of precision
            out += fmt::format(";\n        float3 color_output_{} = byteround(", index);
            AppendColorCombiner(stage.color_op);
            out += ");\n";

            if (stage.color_op == TevStageConfig::Operation::Dot3_RGBA) {
                // result of Dot3_RGBA operation is also placed to the alpha component
                out +=
                    fmt::format("        float alpha_output_{0} = color_output_{0}[0];\n", index);
            } else {
                out += "        alpha_results_1 = ";
                AppendAlphaModifier(stage.alpha_modifier1, stage.alpha_source1, index);
                out += ";\n        alpha_results_2 = ";
                AppendAlphaModifier(stage.alpha_modifier2, stage.alpha_source2, index);
                out += ";\n        alpha_results_3 = ";
                AppendAlphaModifier(stage.alpha_modifier3, stage.alpha_source3, index);

                out += fmt::format(";\n        float alpha_output_{} = byteround(", index);
                AppendAlphaCombiner(stage.alpha_op);
                out += ");\n";
            }

            out +=
                fmt::format("        combiner_output = float4("
                            "clamp(color_output_{} * {}.0f, float3(0.0f), float3(1.0f)), "
                            "clamp(alpha_output_{} * {}.0f, 0.0f, 1.0f));\n",
                            index, stage.GetColorMultiplier(), index, stage.GetAlphaMultiplier());
        }

        out += "        combiner_buffer = next_combiner_buffer;\n";
        if (config.TevStageUpdatesCombinerBufferColor(index)) {
            out += "        next_combiner_buffer.rgb = combiner_output.rgb;\n";
        }
        if (config.TevStageUpdatesCombinerBufferAlpha(index)) {
            out += "        next_combiner_buffer.a = combiner_output.a;\n";
        }
    }

    void WriteScissor() {
        const auto scissor_mode = config.framebuffer.scissor_test_mode.Value();
        if (scissor_mode == RasterizerRegs::ScissorMode::Disabled ||
            (scissor_mode == RasterizerRegs::ScissorMode::Include && metal.hardware_scissor)) {
            return;
        }

        out += "        if (";
        // Negate the condition if we have to keep only the pixels outside the scissor box
        if (scissor_mode == RasterizerRegs::ScissorMode::Include) {
            out += '!';
        }
        out += "(frag_coord.x >= float(fs.scissor_x1) && "
               "frag_coord.y >= float(fs.scissor_y1) && "
               "frag_coord.x < float(fs.scissor_x2) && "
               "frag_coord.y < float(fs.scissor_y2))) {\n"
               "            discard_fragment();\n"
               "            return fs_out;\n"
               "        }\n";
    }

    void WriteDepth() {
        // The PICA depth range is [-1, 0]. The vertex shader outputs the negated Z value. Metal's
        // depth range is [0, 1] so all we need to do is un-negate the value to range [-1, 0].
        // Once we have z_over_w, we can do our own transformation according to PICA spec.
        if (metal.vertex_depth) {
            // The vertex shader already produced the final depth, see VSDepthData.
            out += "        float depth = frag_coord.z;\n";
            return;
        }
        out += "        float z_over_w = -frag_coord.z;\n";
        out += "        float depth = z_over_w * fs.depth_scale + fs.depth_offset;\n";
        if (config.framebuffer.depthmap_enable == RasterizerRegs::DepthBuffering::WBuffering) {
            // Metal's [[position]].w holds 1/w like gl_FragCoord.w.
            out += "        depth /= frag_coord.w;\n";
        }
    }

    void WriteLighting() {
        if (!config.lighting.enable) {
            return;
        }

        const auto& lighting = config.lighting;

        // Define lighting globals
        out += "        float4 diffuse_sum = float4(0.0f, 0.0f, 0.0f, 1.0f);\n"
               "        float4 specular_sum = float4(0.0f, 0.0f, 0.0f, 1.0f);\n"
               "        float3 light_vector = float3(0.0f);\n"
               "        float light_distance = 0.0f;\n"
               "        float3 refl_value = float3(0.0f);\n"
               "        float3 spot_dir = float3(0.0f);\n"
               "        float3 half_vector = float3(0.0f);\n"
               "        float dot_product = 0.0f;\n"
               "        float clamp_highlights = 1.0f;\n"
               "        float geo_factor = 1.0f;\n";

        // Compute fragment normals and tangents
        const auto perturbation = [&] {
            return fmt::format("2.0f * (sampleTexUnit{}()).rgb - 1.0f",
                               lighting.bump_selector.Value());
        };

        if (user.use_custom_normal) {
            out += "        float3 surface_normal = 2.0f * tex_normal.sample(samp_normal, "
                   "texcoord0).rgb - 1.0f;\n";
            out += "        float3 surface_tangent = float3(1.0f, 0.0f, 0.0f);\n";
        } else {
            switch (lighting.bump_mode) {
            case LightingRegs::LightingBumpMode::NormalMap: {
                // Bump mapping is enabled using a normal map
                out += fmt::format("        float3 surface_normal = {};\n", perturbation());

                // Recompute Z-component of perturbation if 'renorm' is enabled, this provides a
                // higher precision result
                if (lighting.bump_renorm) {
                    constexpr std::string_view val = "(1.0f - (surface_normal.x*surface_normal.x + "
                                                     "surface_normal.y*surface_normal.y))";
                    out += fmt::format("        surface_normal.z = sqrt(max({}, 0.0f));\n", val);
                }

                // The tangent vector is not perturbed by the normal map and is just a unit vector.
                out += "        float3 surface_tangent = float3(1.0f, 0.0f, 0.0f);\n";
                break;
            }
            case LightingRegs::LightingBumpMode::TangentMap: {
                // Bump mapping is enabled using a tangent map
                out += fmt::format("        float3 surface_tangent = {};\n", perturbation());
                // Mathematically, recomputing Z-component of the tangent vector won't affect the
                // relevant computation below, which is also confirmed on 3DS. So we don't bother
                // recomputing here even if 'renorm' is enabled.

                // The normal vector is not perturbed by the tangent map and is just a unit vector.
                out += "        float3 surface_normal = float3(0.0f, 0.0f, 1.0f);\n";
                break;
            }
            default:
                // No bump mapping - surface local normal and tangent are just unit vectors
                out += "        float3 surface_normal = float3(0.0f, 0.0f, 1.0f);\n"
                       "        float3 surface_tangent = float3(1.0f, 0.0f, 0.0f);\n";
            }
        }

        // Rotate the surface-local normal by the interpolated normal quaternion to convert it to
        // eyespace. Opposite quaternions were already fixed up per triangle (by the CPU for
        // software vertices, the vertex shader, or the entry point, see quaternion_fix).
        out +=
            "        float4 normalized_normquat = normalize(normquat);\n"
            "        float3 normal = quaternion_rotate(normalized_normquat, surface_normal);\n"
            "        float3 tangent = quaternion_rotate(normalized_normquat, surface_tangent);\n";

        // Fast lighting: the unit vectors the lookup tables are indexed with are kept in half
        // precision (Apple GPUs do half math at twice the rate) and normalized once per light
        // with the fast functions. Positions and distances stay in float.
        const bool fast = metal.fast_lighting;
        if (fast) {
            out += "        half3 normal_h = half3(normal);\n"
                   "        half3 tangent_h = half3(tangent);\n"
                   "        float3 view_dir = fast::normalize(view);\n"
                   "        half3 view_h = half3(view_dir);\n"
                   "        half3 light_h = half3(0.0h);\n"
                   "        half3 half_h = half3(0.0h);\n";
        }

        if (lighting.enable_shadow) {
            const std::string shadow_texture =
                fmt::format("sampleTexUnit{}()", lighting.shadow_selector.Value());
            if (lighting.shadow_invert) {
                out += fmt::format("        float4 shadow = float4(1.0f) - {};\n", shadow_texture);
            } else {
                out += fmt::format("        float4 shadow = {};\n", shadow_texture);
            }
        } else {
            out += "        float4 shadow = float4(1.0f);\n";
        }

        // Samples the specified lookup table for specular lighting
        const auto get_lut_value = [&lighting, fast](LightingRegs::LightingSampler sampler,
                                                     u32 light_num,
                                                     LightingRegs::LightingLutInput input,
                                                     bool abs) {
            std::string index;
            switch (input) {
            case LightingRegs::LightingLutInput::NH:
                index = fast ? "float(dot(normal_h, half_h))"
                             : "dot(normal, normalize(half_vector))";
                break;
            case LightingRegs::LightingLutInput::VH:
                index = fast ? "float(dot(view_h, half_h))"
                             : "dot(normalize(view), normalize(half_vector))";
                break;
            case LightingRegs::LightingLutInput::NV:
                index = fast ? "float(dot(normal_h, view_h))" : "dot(normal, normalize(view))";
                break;
            case LightingRegs::LightingLutInput::LN:
                index = fast ? "float(dot(light_h, normal_h))" : "dot(light_vector, normal)";
                break;
            case LightingRegs::LightingLutInput::SP:
                index = "dot(light_vector, spot_dir)";
                break;
            case LightingRegs::LightingLutInput::CP:
                // CP input is only available with configuration 7
                if (lighting.config == LightingRegs::LightingConfig::Config7) {
                    // Note: even if the normal vector is modified by normal map, which is not the
                    // normal of the tangent plane anymore, the half angle vector is still
                    // projected using the modified normal vector.
                    constexpr std::string_view half_angle_proj =
                        "normalize(half_vector) - normal * dot(normal, normalize(half_vector))";
                    // Note: the half angle vector projection is confirmed not normalized before
                    // the dot product. The result is in fact not cos(phi) as the name suggested.
                    index = fast ? std::string{"float(dot(half_h - normal_h * dot(normal_h, "
                                               "half_h), tangent_h))"}
                                 : fmt::format("dot({}, tangent)", half_angle_proj);
                } else {
                    index = "0.0f";
                }
                break;
            default:
                LOG_CRITICAL(HW_GPU, "Unknown lighting LUT input {}", static_cast<int>(input));
                index = "0.0f";
                break;
            }

            const auto sampler_index = static_cast<u32>(sampler);

            if (abs) {
                // LUT index is in the range of (0.0, 1.0)
                index = lighting.lights[light_num].two_sided_diffuse
                            ? fmt::format("abs({})", index)
                            : fmt::format("max({}, 0.0f)", index);
                return fmt::format("LookupLightingLUTUnsigned({}, {})", sampler_index, index);
            } else {
                // LUT index is in the range of (-1.0, 1.0)
                return fmt::format("LookupLightingLUTSigned({}, {})", sampler_index, index);
            }
        };

        // Write the code to emulate each enabled light
        for (u32 light_index = 0; light_index < lighting.src_num; ++light_index) {
            const auto& light_config = lighting.lights[light_index];
            const std::string light_src = fmt::format("fs.light_src[{}]", light_config.num.Value());

            // Compute light vector (directional or positional)
            if (light_config.directional) {
                out += fmt::format("        light_vector = {}.position;\n", light_src);
            } else {
                out += fmt::format("        light_vector = {}.position + view;\n", light_src);
            }
            if (fast) {
                out += "        light_distance = fast::length(light_vector);\n"
                       "        light_vector = light_vector / max(light_distance, 1e-20f);\n"
                       "        light_h = half3(light_vector);\n";
            } else {
                out += "        light_distance = length(light_vector);\n";
                out += "        light_vector = normalize(light_vector);\n";
            }

            out += fmt::format("        spot_dir = float3({}.spot_direction);\n", light_src);
            if (fast) {
                out += "        half_vector = view_dir + light_vector;\n"
                       "        half_h = half3(fast::normalize(half_vector));\n";
            } else {
                out += "        half_vector = normalize(view) + light_vector;\n";
            }

            // Compute dot product of light_vector and normal, adjust if lighting is one-sided or
            // two-sided
            out += "        dot_product = ";
            if (fast) {
                out += light_config.two_sided_diffuse ? "abs(float(dot(light_h, normal_h)));\n"
                                                      : "max(float(dot(light_h, normal_h)), 0.0f);\n";
            } else {
                out += light_config.two_sided_diffuse ? "abs(dot(light_vector, normal));\n"
                                                      : "max(dot(light_vector, normal), 0.0f);\n";
            }

            // If enabled, clamp specular component if lighting result is zero
            if (lighting.clamp_highlights) {
                out += "        clamp_highlights = sign(dot_product);\n";
            }

            // If enabled, compute spot light attenuation value
            std::string spot_atten = "1.0f";
            if (light_config.spot_atten_enable &&
                LightingRegs::IsLightingSamplerSupported(
                    lighting.config, LightingRegs::LightingSampler::SpotlightAttenuation)) {
                const std::string value = get_lut_value(
                    LightingRegs::SpotlightAttenuationSampler(light_config.num), light_config.num,
                    lighting.lut_sp.type, lighting.lut_sp.abs_input);
                spot_atten =
                    fmt::format("({} * {})", FloatLiteral(lighting.lut_sp.GetScale()), value);
            }

            // If enabled, compute distance attenuation value
            std::string dist_atten = "1.0f";
            if (light_config.dist_atten_enable) {
                const std::string index = fmt::format("clamp({0}.dist_atten_scale * light_distance "
                                                      "+ {0}.dist_atten_bias, 0.0f, 1.0f)",
                                                      light_src);
                const auto sampler = LightingRegs::DistanceAttenuationSampler(light_config.num);
                dist_atten = fmt::format("LookupLightingLUTUnsigned({}, {})",
                                         static_cast<u32>(sampler), index);
            }

            if (light_config.geometric_factor_0 || light_config.geometric_factor_1) {
                out += "        geo_factor = dot(half_vector, half_vector);\n"
                       "        geo_factor = geo_factor == 0.0f ? 0.0f : min("
                       "dot_product / geo_factor, 1.0f);\n";
            }

            // Specular 0 component
            std::string d0_lut_value = "1.0f";
            if (lighting.lut_d0.enable &&
                LightingRegs::IsLightingSamplerSupported(
                    lighting.config, LightingRegs::LightingSampler::Distribution0)) {
                // Lookup specular "distribution 0" LUT value
                const std::string value =
                    get_lut_value(LightingRegs::LightingSampler::Distribution0, light_config.num,
                                  lighting.lut_d0.type, lighting.lut_d0.abs_input);
                d0_lut_value =
                    fmt::format("({} * {})", FloatLiteral(lighting.lut_d0.GetScale()), value);
            }
            std::string specular_0 = fmt::format("({} * {}.specular_0)", d0_lut_value, light_src);
            if (light_config.geometric_factor_0) {
                specular_0 = fmt::format("({} * geo_factor)", specular_0);
            }

            const auto write_reflect = [&](const LutConfig& lut,
                                           LightingRegs::LightingSampler sampler, char component,
                                           std::string_view fallback) {
                if (lut.enable &&
                    LightingRegs::IsLightingSamplerSupported(lighting.config, sampler)) {
                    std::string value =
                        get_lut_value(sampler, light_config.num, lut.type, lut.abs_input);
                    value = fmt::format("({} * {})", FloatLiteral(lut.GetScale()), value);
                    out += fmt::format("        refl_value.{} = {};\n", component, value);
                } else {
                    out += fmt::format("        refl_value.{} = {};\n", component, fallback);
                }
            };
            // If enabled, lookup ReflectRed value, otherwise, 1.0 is used
            write_reflect(lighting.lut_rr, LightingRegs::LightingSampler::ReflectRed, 'r', "1.0f");
            // If enabled, lookup ReflectGreen value, otherwise, ReflectRed value is used
            write_reflect(lighting.lut_rg, LightingRegs::LightingSampler::ReflectGreen, 'g',
                          "refl_value.r");
            // If enabled, lookup ReflectBlue value, otherwise, ReflectRed value is used
            write_reflect(lighting.lut_rb, LightingRegs::LightingSampler::ReflectBlue, 'b',
                          "refl_value.r");

            // Specular 1 component
            std::string d1_lut_value = "1.0f";
            if (lighting.lut_d1.enable &&
                LightingRegs::IsLightingSamplerSupported(
                    lighting.config, LightingRegs::LightingSampler::Distribution1)) {
                // Lookup specular "distribution 1" LUT value
                const std::string value =
                    get_lut_value(LightingRegs::LightingSampler::Distribution1, light_config.num,
                                  lighting.lut_d1.type, lighting.lut_d1.abs_input);
                d1_lut_value =
                    fmt::format("({} * {})", FloatLiteral(lighting.lut_d1.GetScale()), value);
            }
            std::string specular_1 =
                fmt::format("({} * refl_value * {}.specular_1)", d1_lut_value, light_src);
            if (light_config.geometric_factor_1) {
                specular_1 = fmt::format("({} * geo_factor)", specular_1);
            }

            // Fresnel
            // Note: only the last entry in the light slots applies the Fresnel factor
            if (light_index == lighting.src_num - 1 && lighting.lut_fr.enable &&
                LightingRegs::IsLightingSamplerSupported(lighting.config,
                                                         LightingRegs::LightingSampler::Fresnel)) {
                // Lookup fresnel LUT value
                std::string value =
                    get_lut_value(LightingRegs::LightingSampler::Fresnel, light_config.num,
                                  lighting.lut_fr.type, lighting.lut_fr.abs_input);
                value = fmt::format("({} * {})", FloatLiteral(lighting.lut_fr.GetScale()), value);

                // Enabled for diffuse lighting alpha component
                if (lighting.enable_primary_alpha) {
                    out += fmt::format("        diffuse_sum.a = {};\n", value);
                }

                // Enabled for the specular lighting alpha component
                if (lighting.enable_secondary_alpha) {
                    out += fmt::format("        specular_sum.a = {};\n", value);
                }
            }

            const bool shadow_primary_enable =
                lighting.shadow_primary && light_config.shadow_enable;
            const bool shadow_secondary_enable =
                lighting.shadow_secondary && light_config.shadow_enable;
            const auto shadow_primary = shadow_primary_enable ? " * shadow.rgb" : "";
            const auto shadow_secondary = shadow_secondary_enable ? " * shadow.rgb" : "";

            // Compute primary fragment color (diffuse lighting) function
            out += fmt::format("        diffuse_sum.rgb += (({0}.diffuse * dot_product{1}) + "
                               "{0}.ambient) * {2} * {3};\n",
                               light_src, shadow_primary, dist_atten, spot_atten);

            // Compute secondary fragment color (specular lighting) function
            out += fmt::format(
                "        specular_sum.rgb += ({} + {}) * clamp_highlights * {} * {}{};\n",
                specular_0, specular_1, dist_atten, spot_atten, shadow_secondary);
        }

        // Apply shadow attenuation to alpha components if enabled
        if (lighting.shadow_alpha) {
            if (lighting.enable_primary_alpha) {
                out += "        diffuse_sum.a *= shadow.a;\n";
            }
            if (lighting.enable_secondary_alpha) {
                out += "        specular_sum.a *= shadow.a;\n";
            }
        }

        // Sum final lighting result
        out += "        diffuse_sum.rgb += fs.lighting_global_ambient;\n"
               "        primary_fragment_color = clamp(diffuse_sum, float4(0.0f), float4(1.0f));\n"
               "        secondary_fragment_color = clamp(specular_sum, float4(0.0f), "
               "float4(1.0f));\n";
    }

    void WriteFog() {
        // Get index into fog LUT
        if (config.texture.fog_flip) {
            out += "        float fog_index = (1.0f - float(depth)) * 128.0f;\n";
        } else {
            out += "        float fog_index = depth * 128.0f;\n";
        }

        // Generate clamped fog factor from LUT for given fog index
        out += "        float fog_i = clamp(floor(fog_index), 0.0f, 127.0f);\n"
               "        float fog_f = fog_index - fog_i;\n"
               "        float2 fog_lut_entry = float2(lut_lf[int(fog_i) + fs.fog_lut_offset]);\n"
               "        float fog_factor = fog_lut_entry.x + fog_lut_entry.y * fog_f;\n"
               "        fog_factor = clamp(fog_factor, 0.0f, 1.0f);\n";

        // Blend the fog. With fast color math the blend is done in half precision (the fog
        // position above stays in float, it comes from the depth).
        if (metal.fast_color) {
            out += "        combiner_output.rgb = float3(mix(half3(fs.fog_color.rgb), "
                   "half3(combiner_output.rgb), half(fog_factor)));\n";
            return;
        }
        out += "        combiner_output.rgb = mix(fs.fog_color.rgb, combiner_output.rgb, "
               "fog_factor);\n";
    }

    void WriteShadow() {
        // The read-modify-write of the shadow buffer happens in the entry point, which owns the
        // raster-ordered shadow buffer binding.
        out += "        shadow_d = uint(clamp(depth, 0.0f, 1.0f) * float(0xFFFFFF));\n"
               "        shadow_s = uint(combiner_output.g * float(0xFF));\n"
               "        shadow_valid = true;\n";
    }

    void WriteBlending() {
        const bool rgb_minmax = config.framebuffer.requested_rgb_blend.RequiresMinMaxEmulation();
        const bool alpha_minmax =
            config.framebuffer.requested_alpha_blend.RequiresMinMaxEmulation();
        if (!rgb_minmax && !alpha_minmax) [[likely]] {
            return;
        }
        if (!use_framebuffer_fetch) {
            return;
        }

        using BlendFactor = Pica::FramebufferRegs::BlendFactor;
        out += "        float4 source_color = combiner_output;\n";
        const auto get_factor = [&](BlendFactor factor) -> std::string {
            switch (factor) {
            case BlendFactor::Zero:
                return "float4(0.f)";
            case BlendFactor::One:
                return "float4(1.f)";
            case BlendFactor::SourceColor:
                return "source_color";
            case BlendFactor::OneMinusSourceColor:
                return "float4(1.f) - source_color";
            case BlendFactor::DestColor:
                return "dest_color";
            case BlendFactor::OneMinusDestColor:
                return "float4(1.f) - dest_color";
            case BlendFactor::SourceAlpha:
                return "source_color.aaaa";
            case BlendFactor::OneMinusSourceAlpha:
                return "float4(1.f) - source_color.aaaa";
            case BlendFactor::DestAlpha:
                return "dest_color.aaaa";
            case BlendFactor::OneMinusDestAlpha:
                return "float4(1.f) - dest_color.aaaa";
            case BlendFactor::ConstantColor:
                return "fs.blend_color";
            case BlendFactor::OneMinusConstantColor:
                return "float4(1.f) - fs.blend_color";
            case BlendFactor::ConstantAlpha:
                return "fs.blend_color.aaaa";
            case BlendFactor::OneMinusConstantAlpha:
                return "float4(1.f) - fs.blend_color.aaaa";
            default:
                LOG_CRITICAL(Render, "Unknown blend factor {}", factor);
                return "float4(1.f)";
            }
        };

        // At this point, the blend equation can only be min or max.
        const auto get_func = [](Pica::FramebufferRegs::BlendEquation eq) {
            return eq == Pica::FramebufferRegs::BlendEquation::Min ? "min" : "max";
        };

        if (rgb_minmax) {
            out += fmt::format("        combiner_output.rgb = {}(source_color.rgb * ({}).rgb, "
                               "dest_color.rgb * ({}).rgb);\n",
                               get_func(config.framebuffer.requested_rgb_blend.eq),
                               get_factor(config.framebuffer.requested_rgb_blend.src_factor),
                               get_factor(config.framebuffer.requested_rgb_blend.dst_factor));
        }
        if (alpha_minmax) {
            out += fmt::format("        combiner_output.a = {}(source_color.a * ({}).a, "
                               "dest_color.a * ({}).a);\n",
                               get_func(config.framebuffer.requested_alpha_blend.eq),
                               get_factor(config.framebuffer.requested_alpha_blend.src_factor),
                               get_factor(config.framebuffer.requested_alpha_blend.dst_factor));
        }
    }

    void WriteLogicOp() {
        using LogicOp = FramebufferRegs::LogicOp;
        const auto logic_op = config.framebuffer.logic_op.Value();
        if (logic_op == LogicOp::Copy) {
            return;
        }
        if (!use_framebuffer_fetch) {
            // Without a color attachment the color output is irrelevant.
            return;
        }

        // Metal has no fixed function logic ops, but every Apple GPU supports framebuffer fetch.
        // The color attachments are always 8-bit unorm, so we can emulate them exactly.
        out += "        uint4 s_bits = uint4(round(clamp(fs_out.color, 0.0f, 1.0f) * 255.0f));\n"
               "        uint4 d_bits = uint4(round(clamp(dest_color, 0.0f, 1.0f) * 255.0f));\n";
        const auto op = [logic_op]() -> std::string_view {
            switch (logic_op) {
            case LogicOp::Clear:
                return "uint4(0u)";
            case LogicOp::And:
                return "s_bits & d_bits";
            case LogicOp::AndReverse:
                return "s_bits & ~d_bits";
            case LogicOp::Set:
                return "uint4(0xFFu)";
            case LogicOp::CopyInverted:
                return "~s_bits";
            case LogicOp::NoOp:
                return "d_bits";
            case LogicOp::Invert:
                return "~d_bits";
            case LogicOp::Nand:
                return "~(s_bits & d_bits)";
            case LogicOp::Or:
                return "s_bits | d_bits";
            case LogicOp::Nor:
                return "~(s_bits | d_bits)";
            case LogicOp::Xor:
                return "s_bits ^ d_bits";
            case LogicOp::Equiv:
                return "~(s_bits ^ d_bits)";
            case LogicOp::AndInverted:
                return "~s_bits & d_bits";
            case LogicOp::OrReverse:
                return "s_bits | ~d_bits";
            case LogicOp::OrInverted:
                return "~s_bits | d_bits";
            default:
                return "s_bits";
            }
        }();
        out += fmt::format("        fs_out.color = float4(({}) & uint4(0xFFu)) / 255.0f;\n", op);
    }

    void WriteMain() {
        out += R"(
    FSOutput run() {
        FSOutput fs_out;
        fs_out.color = float4(0.0f);
        float4 rounded_primary_color = byteround(primary_color);
        float4 primary_fragment_color = float4(0.0f);
        float4 secondary_fragment_color = float4(0.0f);
)";
        if (write_fragment_depth) {
            out += "        fs_out.depth = frag_coord.z;\n";
        }

        // Do not do any sort of processing if it's obvious we're not going to pass the alpha test
        if (config.framebuffer.alpha_test_func == FramebufferRegs::CompareFunc::Never) {
            out += "        discard_fragment();\n        return fs_out;\n    }\n";
            return;
        }

        // Append the scissor and depth tests
        WriteScissor();
        WriteDepth();

        // Write shader source to emulate all enabled PICA lights
        WriteLighting();

        // With fast color math every texture unit the combiners read is sampled once, straight
        // into half precision, instead of at every use.
        if (metal.fast_color) {
            using Source = TevStageConfig::Source;
            std::array<bool, 4> used{};
            for (u32 index = 0; index < config.texture.tev_stages.size(); index++) {
                const TevStageConfig stage = config.texture.tev_stages[index];
                if (IsPassThroughTevStage(stage)) {
                    continue;
                }
                for (const Source source :
                     {stage.color_source1.Value(), stage.color_source2.Value(),
                      stage.color_source3.Value(), stage.alpha_source1.Value(),
                      stage.alpha_source2.Value(), stage.alpha_source3.Value()}) {
                    if (source >= Source::Texture0 && source <= Source::Texture3) {
                        used[static_cast<u32>(source) - static_cast<u32>(Source::Texture0)] = true;
                    }
                }
            }
            for (u32 unit = 0; unit < used.size(); unit++) {
                if (used[unit]) {
                    out += fmt::format("        half4 tex_h{0} = half4(sampleTexUnit{0}());\n", unit);
                }
            }
        }

        // With fast color math the combiner code below is turned into half precision code in a
        // block of its own, which hands its result back as the float combiner_output.
        const std::size_t tev_start = out.size();
        out += "        float4 combiner_buffer = float4(0.0f);\n"
               "        float4 next_combiner_buffer = float4(fs.tev_combiner_buffer_color);\n"
               "        float4 combiner_output = float4(0.0f);\n"
               "        float3 color_results_1 = float3(0.0f);\n"
               "        float3 color_results_2 = float3(0.0f);\n"
               "        float3 color_results_3 = float3(0.0f);\n"
               "        float alpha_results_1 = 0.0f;\n"
               "        float alpha_results_2 = 0.0f;\n"
               "        float alpha_results_3 = 0.0f;\n";

        // Write shader source to emulate PICA TEV stages
        for (u32 index = 0; index < config.texture.tev_stages.size(); index++) {
            WriteTevStage(index);
        }
        if (metal.fast_color) {
            const std::string block = ToHalfPrecision(out.substr(tev_start));
            out.resize(tev_start);
            out += "        float4 combiner_output;\n        {\n";
            out += block;
            out += "        combiner_output = float4(tev_out);\n        }\n";
        }

        // Append the alpha test condition
        WriteAlphaTestCondition(config.framebuffer.alpha_test_func);

        // Emulate the fog
        switch (config.texture.fog_mode) {
        case TexturingRegs::FogMode::Fog:
            WriteFog();
            break;
        case TexturingRegs::FogMode::Gas:
            // TODO: Implement me
            LOG_CRITICAL(Render, "Unimplemented gas mode");
            // Replace the output color with a transparent pixel, (just discarding the pixel
            // causes graphical issues in some MH games).
            out += "        fs_out.color = float4(0.0f);\n";
            if (write_fragment_depth) {
                out += "        fs_out.depth = depth;\n";
            }
            out += "        return fs_out;\n    }\n";
            return;
        default:
            break;
        }

        if (config.framebuffer.shadow_rendering) {
            WriteShadow();
        } else {
            if (write_fragment_depth) {
                out += "        fs_out.depth = depth;\n";
            }
            // Round the final fragment color to maintain the PICA's 8 bits of precision
            out += "        combiner_output = byteround(combiner_output);\n";
            WriteBlending();
            out += "        fs_out.color = combiner_output;\n";
            WriteLogicOp();
        }

        out += "        return fs_out;\n    }\n";
    }

private:
    FSConfig config;
    const UserConfig& user;
    const Profile& profile;
    const MetalFSConfig& metal;
    std::string out;
    std::vector<std::pair<std::string, std::string>> ctor_members;
    bool use_framebuffer_fetch{};
    bool write_fragment_depth{};
    /// The lighting quaternion is corrected per triangle here, see MetalFSConfig.
    bool quaternion_fix = false;
};

} // Anonymous namespace

std::string GenerateTrivialVertexShader(bool use_clip_planes, bool half_interpolants) {
    std::string out;
    out += CommonHeader;
#ifdef ENABLE_FREECAM
    out += "#define ENABLE_FREECAM 1\n";
#endif
    if (use_clip_planes) {
        out += "#define USE_CLIP_PLANES 1\n";
    }
    if (half_interpolants) {
        out += "#define HALF_INTERPOLANTS 1\n";
    }
    out += VSUniformDef;
    out += InterpColorDef;
    out += VSOutputDef;
    out += VSSanitizeDef;

    // Matches RasterizerAccelerated::HardwareVertex, see the Metal rasterizer vertex descriptor.
    out += R"(
struct VSInput {
    float4 position [[attribute(0)]];
    float4 color [[attribute(1)]];
    float2 texcoord0 [[attribute(2)]];
    float2 texcoord1 [[attribute(3)]];
    float2 texcoord2 [[attribute(4)]];
    float texcoord0_w [[attribute(5)]];
    float4 normquat [[attribute(6)]];
    float3 view [[attribute(7)]];
};
)";

    out += fmt::format(R"(
vertex VSOutput {}(VSInput in [[stage_in]], constant VSUniforms& vs [[buffer({})]],
                    constant VSDepth& vs_depth [[buffer({})]]) {{
    VSOutput out;
    out.primary_color = interp_color_t(in.color);
    out.texcoord0 = in.texcoord0;
    out.texcoord1 = in.texcoord1;
    out.texcoord2 = in.texcoord2;
    out.texcoord0_w = in.texcoord0_w;
    out.normquat = in.normquat;
    out.view = in.view;
    float4 vtx_pos = SanitizeVertex(in.position);
    if (vs.flip_viewport != 0) {{
        vtx_pos.y = -vtx_pos.y;
    }}
#ifdef ENABLE_FREECAM
    float4 clip_pos = ApplyFreeCam(vs, float4(vtx_pos.x, vtx_pos.y, -vtx_pos.z, vtx_pos.w));
#else
    float4 clip_pos = float4(vtx_pos.x, vtx_pos.y, -vtx_pos.z, vtx_pos.w);
#endif
    // Metal NDC has +Y pointing up while Vulkan's points down. Negate Y so the rendered image has
    // exactly the same memory layout as the one produced by the Vulkan backend.
    clip_pos.y = -clip_pos.y;
    if (vs_depth.enabled != 0u) {{
        clip_pos.z = vtx_pos.z * vs_depth.scale + vs_depth.offset * vtx_pos.w;
    }}
    out.position = clip_pos;
#ifdef USE_CLIP_PLANES
    out.clip_distance[0] = -vtx_pos.z; // fixed PICA clipping plane z <= 0
    out.clip_distance[1] = vs.enable_clip1 != 0 ? dot(vs.clip_coef, vtx_pos) : 0.0f;
    out.clip_distance[2] = vtx_pos.z + vtx_pos.w; // PICA far plane z >= -w, see VSDepthData
#endif
    return out;
}}
)",
                       VS_ENTRY_POINT, +VS_BUFFER_UNIFORMS, +VS_BUFFER_DEPTH);
    return out;
}

namespace {

// Must match VSPicaUniformData and VSVertexLayout.
constexpr std::string_view VSHardwareDefs = R"(
struct PicaUniforms {
    uint b;
    uint4 i[4];
    float4 f[96];
};

struct VertexAttribute {
    uint offset;
    uint stride;
    uint format;
    uint size;
};

struct VertexLayout {
    VertexAttribute attributes[16];
    float4 defaults[16];
    uint vertex_min;
    uint vertex_base;
    uint index_offset;
    uint index_format;
    uint topology;
    uint direct;
    uint pad1;
    uint pad2;
};

// PICA attributes are little endian and only aligned to their element size, read them bytewise.
static float LoadComponent(device const uchar* data, uint address, uint format) {
    switch (format) {
    case 0u: // BYTE
        return float(as_type<char>(data[address]));
    case 1u: // UBYTE
        return float(data[address]);
    case 2u: // SHORT
        return float(as_type<short>(ushort(uint(data[address]) | (uint(data[address + 1u]) << 8u))));
    default: // FLOAT
        return as_type<float>(uint(data[address]) | (uint(data[address + 1u]) << 8u) |
                              (uint(data[address + 2u]) << 16u) | (uint(data[address + 3u]) << 24u));
    }
}

// Note: `vertex` is a keyword in MSL.
static float4 LoadAttribute(constant VertexLayout& layout, device const uchar* data, uint reg,
                            uint vertex_index) {
    const uint format = layout.attributes[reg].format;
    if (format > 3u) {
        return layout.defaults[reg];
    }
    const uint element = format == 3u ? 4u : (format == 2u ? 2u : 1u);
    const uint size = min(layout.attributes[reg].size, 4u);
    const uint address =
        layout.attributes[reg].offset +
        (vertex_index - layout.vertex_min) * layout.attributes[reg].stride;
    float4 value = float4(0.0, 0.0, 0.0, 1.0);
    if (format == 3u && (address & 3u) == 0u) {
        // Floats on a word boundary (the usual case): one 32 bit load a component instead of
        // four byte loads. The data is little endian, like the GPU.
        device const uint* words = (device const uint*)(data + address);
        for (uint i = 0u; i < size; ++i) {
            value[i] = as_type<float>(words[i]);
        }
    } else if (format == 2u && (address & 1u) == 0u) {
        device const ushort* halves = (device const ushort*)(data + address);
        for (uint i = 0u; i < size; ++i) {
            value[i] = float(as_type<short>(halves[i]));
        }
    } else {
        for (uint i = 0u; i < size; ++i) {
            value[i] = LoadComponent(data, address + i * element, format);
        }
    }
    return value;
}

// Position of a corner of a triangle in the PICA vertex stream, matching the primitive assembler.
static uint StreamIndex(uint topology, uint triangle, uint corner) {
    switch (topology) {
    case 1u: // Strip, odd triangles swap their first two vertices
        if ((triangle & 1u) != 0u && corner < 2u) {
            return triangle + 1u - corner;
        }
        return triangle + corner;
    case 2u: // Fan
        return corner == 0u ? 0u : triangle + corner;
    default: // List
        return triangle * 3u + corner;
    }
}

static uint FetchVertexIndex(constant VertexLayout& layout, device const uchar* data,
                             uint triangle, uint corner) {
    const uint index = StreamIndex(layout.topology, triangle, corner);
    if (layout.index_format == 1u) {
        return uint(data[layout.index_offset + index]);
    }
    if (layout.index_format == 2u) {
        const uint address = layout.index_offset + index * 2u;
        return uint(data[address]) | (uint(data[address + 1u]) << 8u);
    }
    return layout.vertex_base + index;
}
)";

} // Anonymous namespace

std::string GenerateVertexShader(const Pica::ShaderSetup& setup, const PicaVSConfig& config,
                                 bool use_clip_planes, bool sanitize_mul, bool half_interpolants,
                                 bool fragment_quaternion_fix) {
    using VSOutputAttributes = Pica::RasterizerRegs::VSOutputAttributes;
    const auto& state = config.state;

    std::array<bool, 16> used_regs{};
    const auto get_input_reg = [&used_regs](u32 reg) {
        ASSERT(reg < 16);
        used_regs[reg] = true;
        return fmt::format("vs_in_reg{}", reg);
    };
    const auto get_output_reg = [&state](u32 reg) -> std::string {
        ASSERT(reg < 16);
        if (state.output_map[reg] < state.num_outputs) {
            return fmt::format("vs_out_attr{}", state.output_map[reg]);
        }
        return "";
    };

    const std::string program_source =
        DecompileProgram(setup.GetProgramCode(), setup.GetSwizzleData(), state.main_offset,
                         get_input_reg, get_output_reg, sanitize_mul);
    if (program_source.empty()) {
        return "";
    }

    std::string out;
    out += CommonHeader;
#ifdef ENABLE_FREECAM
    out += "#define ENABLE_FREECAM 1\n";
#endif
    if (use_clip_planes) {
        out += "#define USE_CLIP_PLANES 1\n";
    }
    if (half_interpolants) {
        out += "#define HALF_INTERPOLANTS 1\n";
    }
    out += VSUniformDef;
    out += InterpColorDef;
    out += VSOutputDef;
    out += VSSanitizeDef;
    out += VSHardwareDefs;

    // The PICA program, with its registers as members so the subroutines can share them.
    out += "\nstruct PicaShader {\n";
    out += "    constant PicaUniforms* uniforms;\n";
    for (u32 i = 0; i < used_regs.size(); ++i) {
        if (used_regs[i]) {
            out += fmt::format("    float4 vs_in_reg{};\n", i);
        }
    }
    for (u32 i = 0; i < state.num_outputs; ++i) {
        out += fmt::format("    float4 vs_out_attr{};\n", i);
    }
    out += "\n";
    out += program_source;

    out += "    void Load(constant VertexLayout& layout, device const uchar* data, "
           "uint vertex_index) {\n";
    for (u32 i = 0; i < used_regs.size(); ++i) {
        if (used_regs[i]) {
            out += fmt::format(
                "        vs_in_reg{0} = LoadAttribute(layout, data, {0}u, vertex_index);\n", i);
        }
    }
    out += "    }\n\n";

    out += "    void Run() {\n";
    out += "        ResetRegisters();\n";
    for (u32 i = 0; i < state.num_outputs; ++i) {
        out += fmt::format("        vs_out_attr{} = float4(0.0, 0.0, 0.0, 1.0);\n", i);
    }
    out += "        exec_shader();\n";
    out += "    }\n";

    // Output attributes to rasterizer semantics. Unmapped semantics read as 1.0, like on hardware.
    const auto semantic_maps = state.gs_state.GetSemanticMaps();
    const auto semantic = [&](VSOutputAttributes::Semantic slot_semantic) -> std::string {
        const u32 slot = static_cast<u32>(slot_semantic);
        const u32 attrib = semantic_maps[slot].attribute_index;
        const u32 comp = semantic_maps[slot].component_index;
        if (attrib < state.gs_state.gs_output_attributes_count && attrib < state.num_outputs) {
            return fmt::format("vs_out_attr{}.{}", attrib, "xyzw"[comp]);
        }
        return "1.0";
    };
    const auto vec = [&](std::string_view type, std::initializer_list<VSOutputAttributes::Semantic>
                                                     semantics) {
        std::string result = fmt::format("{}(", type);
        bool first = true;
        for (const auto s : semantics) {
            result += fmt::format("{}{}", first ? "" : ", ", semantic(s));
            first = false;
        }
        return result + ")";
    };

    out += "\n    float4 Quaternion() {\n";
    out += fmt::format("        return {};\n",
                       vec("float4", {VSOutputAttributes::QUATERNION_X,
                                      VSOutputAttributes::QUATERNION_Y,
                                      VSOutputAttributes::QUATERNION_Z,
                                      VSOutputAttributes::QUATERNION_W}));
    out += "    }\n";
    out += "};\n";

    // Per-triangle quaternion correction, only needed when the fragment shader does lighting, and
    // done there instead when it can read every corner (MetalFSConfig::fragment_quaternion_fix).
    const bool fix_quaternions = !state.lighting_disable && !fragment_quaternion_fix;

    out += fmt::format(R"(
vertex VSOutput {}(uint vid [[vertex_id]], constant VSUniforms& vs [[buffer({})]],
                    constant PicaUniforms& pica_uniforms [[buffer({})]],
                    constant VertexLayout& layout [[buffer({})]],
                    device const uchar* vertex_data [[buffer({})]],
                    constant VSDepth& vs_depth [[buffer({})]]) {{
    const uint triangle = vid / 3u;
    const uint corner = vid - triangle * 3u;

    PicaShader sh;
    sh.uniforms = &pica_uniforms;
    // A native draw hands over the PICA vertex index as the vertex id (see VSVertexLayout::direct).
    const uint vertex_index =
        layout.direct != 0u ? vid : FetchVertexIndex(layout, vertex_data, triangle, corner);
    sh.Load(layout, vertex_data, vertex_index);
    sh.Run();
)",
                       VS_ENTRY_POINT, +VS_BUFFER_UNIFORMS, +VS_BUFFER_PICA_UNIFORMS,
                       +VS_BUFFER_VERTEX_LAYOUT, +VS_BUFFER_VERTEX_DATA, +VS_BUFFER_DEPTH);

    const auto sh_vec = [&](std::string_view type,
                            std::initializer_list<VSOutputAttributes::Semantic> semantics) {
        std::string result = vec(type, semantics);
        // Prefix the output registers with the shader instance.
        std::string prefixed;
        std::size_t pos = 0;
        while (true) {
            const std::size_t found = result.find("vs_out_attr", pos);
            if (found == std::string::npos) {
                prefixed += result.substr(pos);
                break;
            }
            prefixed += result.substr(pos, found - pos);
            prefixed += "sh.";
            prefixed += "vs_out_attr";
            pos = found + 11;
        }
        return prefixed;
    };

    out += "    VSOutput out;\n";
    out += fmt::format("    float4 vtx_pos = {};\n",
                       sh_vec("float4", {VSOutputAttributes::POSITION_X,
                                         VSOutputAttributes::POSITION_Y,
                                         VSOutputAttributes::POSITION_Z,
                                         VSOutputAttributes::POSITION_W}));
    out += fmt::format("    float4 vtx_color = {};\n",
                       sh_vec("float4", {VSOutputAttributes::COLOR_R, VSOutputAttributes::COLOR_G,
                                         VSOutputAttributes::COLOR_B,
                                         VSOutputAttributes::COLOR_A}));
    out += "    out.primary_color = interp_color_t(min(abs(vtx_color), float4(1.0)));\n";
    out += fmt::format("    out.texcoord0 = {};\n",
                       sh_vec("float2", {VSOutputAttributes::TEXCOORD0_U,
                                         VSOutputAttributes::TEXCOORD0_V}));
    out += fmt::format("    out.texcoord1 = {};\n",
                       sh_vec("float2", {VSOutputAttributes::TEXCOORD1_U,
                                         VSOutputAttributes::TEXCOORD1_V}));
    out += fmt::format("    out.texcoord2 = {};\n",
                       sh_vec("float2", {VSOutputAttributes::TEXCOORD2_U,
                                         VSOutputAttributes::TEXCOORD2_V}));
    out += fmt::format("    out.texcoord0_w = {};\n",
                       sh_vec("float", {VSOutputAttributes::TEXCOORD0_W}));
    out += fmt::format("    out.view = {};\n",
                       sh_vec("float3", {VSOutputAttributes::VIEW_X, VSOutputAttributes::VIEW_Y,
                                         VSOutputAttributes::VIEW_Z}));
    out += "    float4 quat = sh.Quaternion();\n";
    if (fix_quaternions) {
        out += R"(
    // Interpolating between opposite quaternions breaks the lighting. Like the PICA rasterizer,
    // flip the quaternions of the other corners to the side of the first corner's.
    if (corner != 0u) {
        PicaShader first;
        first.uniforms = &pica_uniforms;
        first.Load(layout, vertex_data, FetchVertexIndex(layout, vertex_data, triangle, 0u));
        first.Run();
        if (dot(first.Quaternion(), quat) < 0.0) {
            quat = -quat;
        }
    }
)";
    }
    out += "    out.normquat = quat;\n";

    out += R"(
    vtx_pos = SanitizeVertex(vtx_pos);
    if (vs.flip_viewport != 0) {
        vtx_pos.y = -vtx_pos.y;
    }
#ifdef ENABLE_FREECAM
    float4 clip_pos = ApplyFreeCam(vs, float4(vtx_pos.x, vtx_pos.y, -vtx_pos.z, vtx_pos.w));
#else
    float4 clip_pos = float4(vtx_pos.x, vtx_pos.y, -vtx_pos.z, vtx_pos.w);
#endif
    // Metal NDC has +Y pointing up, see GenerateTrivialVertexShader.
    clip_pos.y = -clip_pos.y;
    if (vs_depth.enabled != 0u) {
        clip_pos.z = vtx_pos.z * vs_depth.scale + vs_depth.offset * vtx_pos.w;
    }
    out.position = clip_pos;
#ifdef USE_CLIP_PLANES
    out.clip_distance[0] = -vtx_pos.z; // fixed PICA clipping plane z <= 0
    out.clip_distance[1] = vs.enable_clip1 != 0 ? dot(vs.clip_coef, vtx_pos) : 0.0f;
    out.clip_distance[2] = vtx_pos.z + vtx_pos.w; // PICA far plane z >= -w, see VSDepthData
#endif
    return out;
}
)";
    return out;
}

std::string GenerateFragmentShader(const FSConfig& config, const UserConfig& user,
                                   const Profile& profile, const MetalFSConfig& metal_config) {
    FragmentModule module{config, user, profile, metal_config};
    return module.Generate();
}

} // namespace Pica::Shader::Generator::MSL
