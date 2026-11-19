// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <array>
#include <string>
#include "common/common_types.h"
#include "common/vector_math.h"
#include "video_core/shader/generator/pica_fs_config.h"
#include "video_core/shader/generator/shader_gen.h"

namespace Pica {
struct ShaderSetup;
}

/**
 * Metal Shading Language (MSL) shader generators used by the native Metal renderer.
 *
 * The Metal renderer follows the Vulkan conventions of the other backends (top-left framebuffer
 * origin, [0, 1] depth range, same memory layout of the rasterizer cache surfaces). The only
 * difference is that Metal's normalized device coordinates have +Y pointing up while Vulkan's
 * point down, so the vertex shader negates Y compared to the Vulkan backend.
 *
 * Resource binding model (shared between the generators and the Metal rasterizer):
 */
namespace Pica::Shader::Generator::MSL {

/// Vertex stage buffer indices.
enum VSBuffer : u32 {
    VS_BUFFER_UNIFORMS = 0,      ///< VSUniformData
    VS_BUFFER_PICA_UNIFORMS = 1, ///< VSPicaUniformData (hardware vertex shaders)
    VS_BUFFER_VERTEX_LAYOUT = 2, ///< VSVertexLayout (hardware vertex shaders)
    VS_BUFFER_VERTEX_DATA = 3,   ///< Raw PICA vertex and index data (hardware vertex shaders)
    VS_BUFFER_DEPTH = 4,         ///< VSDepthData
    /// Vertex buffers are placed after the uniform buffers in the argument table.
    VS_BUFFER_VERTEX_BASE = 16,
};

/// Fragment stage buffer indices.
enum FSBuffer : u32 {
    FS_BUFFER_UNIFORMS = 0, ///< FSUniformData
    FS_BUFFER_LUT_LF = 1,   ///< float2 lighting + fog LUT
    FS_BUFFER_LUT_RG = 2,   ///< float2 proctex noise/color-map/alpha-map LUT
    FS_BUFFER_LUT_RGBA = 3, ///< float4 proctex color LUT
};

/// Fragment stage texture indices. Shadow cube maps use slots [0, 5] for texture unit 0.
enum FSTexture : u32 {
    FS_TEXTURE_0 = 0,
    FS_TEXTURE_1 = 6,
    FS_TEXTURE_2 = 7,
    FS_TEXTURE_NORMAL = 8,
    FS_TEXTURE_SHADOW_BUFFER = 9,
    FS_TEXTURE_LIGHTING_LUT = 10, ///< R16Float view of the lighting LUT rows (lut_texture)
};

/// Bytes of one row of the lighting LUT texture: 256 half values.
constexpr u32 LIGHTING_LUT_ROW_BYTES = 256 * 2;

/// Fragment stage sampler indices.
enum FSSampler : u32 {
    FS_SAMPLER_0 = 0,
    FS_SAMPLER_1 = 1,
    FS_SAMPLER_2 = 2,
    FS_SAMPLER_NORMAL = 3,
};

/// Name of the entry points of the generated programs.
constexpr const char* VS_ENTRY_POINT = "vs_main";
constexpr const char* FS_ENTRY_POINT = "fs_main";

/**
 * Additional state the Metal backend needs to generate a fragment shader that is not part of the
 * PICA configuration.
 */
struct MetalFSConfig {
    /// True if a color attachment is bound, so framebuffer fetch ([[color(0)]]) can be used to
    /// emulate logic ops and min/max blending with factors.
    bool has_color_attachment = true;
    /// True if a depth attachment is bound. Metal rejects a pipeline whose fragment shader declares
    /// a [[depth]] output when no depth attachment is present, so the output is only emitted when a
    /// depth buffer will be attached.
    bool has_depth_attachment = true;
    /// Fragment lighting with half precision directions and fast normalization, see
    /// QualityOptions::fast_lighting.
    bool fast_lighting = false;
    /// The vertex shader already maps the depth to the PICA depth range (VSDepthData::enabled),
    /// so the fragment shader does not write [[depth]]. Not for W-buffering.
    bool vertex_depth = false;
    /// The texture combiners and the fog compute in half precision, see
    /// QualityOptions::fast_color.
    bool fast_color = false;
    /// The lighting and fog lookup tables (FS_BUFFER_LUT_LF) hold half2 instead of float2
    /// entries. Set for every shader while fast lighting is on, the buffer is shared.
    bool half_luts = false;
    /// The vertex color goes from the vertex to the fragment stage as half4. The vertex shaders
    /// are generated with the same setting.
    bool half_interpolants = false;
    /// Lit shaders do the per-triangle sign correction of the normal quaternions themselves
    /// (QUATERNION_FIX_MSL), so the vertex shaders can share vertices between triangles. Needs
    /// barycentric coordinates. Only used when lighting is enabled.
    bool fragment_quaternion_fix = false;
    /// The scissor test that keeps the pixels inside the box is the GPU's scissor rectangle
    /// (set by the rasterizer), the shader only does the one keeping the pixels outside it.
    bool hardware_scissor = false;
    /// The lighting lookup tables are read through a texture with hardware linear filtering
    /// (FS_TEXTURE_LIGHTING_LUT, one row per table and one more in signed order), one sample per
    /// lookup instead of a load and the interpolation in the shader. fs.lighting_lut_offset holds
    /// the row of each table. The ends of a table are clamped instead of extrapolated.
    bool lut_texture = false;
};

/// MSL of FixQuaternion(float4 interpolated, float3 barycentric), which corrects opposite
/// quaternions per triangle in the fragment shader (MetalFSConfig::fragment_quaternion_fix).
extern const std::string_view QUATERNION_FIX_MSL;

/// Depth mapping done in the vertex shader instead of the fragment shader. Writing [[depth]] in a
/// fragment shader turns off the hidden surface removal of Apple GPUs, so every hidden fragment
/// is shaded too. With `enabled` the clip space Z becomes z * scale + offset * w, which gives the
/// same depth as the fragment shader computes, and clip distances keep the PICA clip volume.
struct VSDepthData {
    float scale;
    float offset;
    u32 enabled;
    u32 pad;
};

/**
 * Where the hardware vertex shader finds one PICA vertex shader input register. The vertex data is
 * fetched by the shader itself ("vertex pulling") from the raw PICA attribute buffers.
 */
struct VSVertexAttribute {
    /// Byte offset in VS_BUFFER_VERTEX_DATA of the attribute of the vertex `vertex_min`.
    u32 offset;
    /// Distance in bytes between two vertices.
    u32 stride;
    /// Pica::PipelineRegs::VertexAttributeFormat, or VS_ATTRIBUTE_DEFAULT to use
    /// VSVertexLayout::defaults.
    u32 format;
    /// Number of components, the missing ones read as (0, 0, 0, 1).
    u32 size;
};

constexpr u32 VS_ATTRIBUTE_DEFAULT = 4;

/// How VSVertexLayout::topology numbers the vertices of each triangle.
enum VSTopology : u32 {
    VS_TOPOLOGY_LIST = 0,
    VS_TOPOLOGY_STRIP = 1,
    VS_TOPOLOGY_FAN = 2,
};

/// Index buffer format of VSVertexLayout::index_format.
enum VSIndexFormat : u32 {
    VS_INDEX_NONE = 0,
    VS_INDEX_U8 = 1,
    VS_INDEX_U16 = 2,
};

/**
 * Describes how a hardware vertex shader loads its inputs. Hardware vertex shaders are drawn as
 * non-indexed triangle lists: vertex N is corner N % 3 of triangle N / 3 of the PICA primitive
 * stream, which lets the shader apply the per-triangle quaternion correction that would otherwise
 * need a geometry shader (Metal has none). Must match VertexLayout in the generated MSL.
 */
struct VSVertexLayout {
    std::array<VSVertexAttribute, 16> attributes;
    std::array<Common::Vec4f, 16> defaults;
    /// Lowest vertex index referenced by the draw, the attribute offsets are relative to it.
    u32 vertex_min;
    /// First vertex of a non-indexed draw.
    u32 vertex_base;
    /// Byte offset of the indices in VS_BUFFER_VERTEX_DATA.
    u32 index_offset;
    u32 index_format; ///< VSIndexFormat
    u32 topology;     ///< VSTopology
    /// 1 when the GPU assembles the triangles (a native, possibly indexed draw): the vertex id is
    /// then the PICA vertex index itself, and a vertex used by several triangles is shaded once.
    u32 direct;
    u32 pad[2];
};
static_assert(sizeof(VSVertexLayout) == 544, "VSVertexLayout must match the MSL structure");

/**
 * Generates a hardware vertex shader from the PICA vertex shader program. Returns an empty string
 * if the program cannot be translated, in which case the draw must use the software path.
 */
std::string GenerateVertexShader(const Pica::ShaderSetup& setup, const PicaVSConfig& config,
                                 bool use_clip_planes, bool sanitize_mul,
                                 bool half_interpolants = false,
                                 bool fragment_quaternion_fix = false);

/// Returns the vertex shader used by the software (CPU) vertex processing path.
std::string GenerateTrivialVertexShader(bool use_clip_planes, bool half_interpolants = false);

/**
 * Generates the MSL fragment shader program source code for the current Pica state.
 * @param config Configuration generated from the current Pica state. Only use the state in this
 *               struct, not the Pica registers.
 */
std::string GenerateFragmentShader(const FSConfig& config, const UserConfig& user,
                                   const Profile& profile, const MetalFSConfig& metal_config);

} // namespace Pica::Shader::Generator::MSL
