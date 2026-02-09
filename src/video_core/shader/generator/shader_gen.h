// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/hash.h"
#include "video_core/pica/regs_rasterizer.h"

namespace Pica {
struct RegsInternal;
struct ShaderSetup;
} // namespace Pica

namespace Pica::Shader::Generator {

// NOTE: Changing the order impacts shader transferable and precompiled cache loading.
enum ProgramType : u32 {
    VS = 0,
    FS = 1,
    GS = 2,
};

enum Attributes {
    ATTRIBUTE_POSITION,
    ATTRIBUTE_COLOR,
    ATTRIBUTE_TEXCOORD0,
    ATTRIBUTE_TEXCOORD1,
    ATTRIBUTE_TEXCOORD2,
    ATTRIBUTE_TEXCOORD0_W,
    ATTRIBUTE_NORMQUAT,
    ATTRIBUTE_VIEW,
};

enum class AttribLoadFlags : u32 {
    Float = 1 << 0,
    Sint = 1 << 1,
    Uint = 1 << 2,
    ZeroW = 1 << 3,
};
DECLARE_ENUM_FLAG_OPERATORS(AttribLoadFlags)

/**
 * WARNING!
 *
 * The following structs are saved to the disk as cache entries!
 * Any modification to their members will invalidate the cache, breaking their
 * transferable properties.
 *
 * Only modify the entries if such modifications are justified.
 * If the struct is modified in a way that results in the exact same layout
 * (for example, replacing an u8 with another u8 in the same place), then bump
 * the struct's STRUCT_VERSION value.
 */

/**
 * This struct contains common information to identify a GLSL geometry shader generated from
 * PICA geometry shader.
 */
struct PicaGSConfigState {
    void Init(const Pica::RegsInternal& regs);

    u32 vs_output_attributes_count;
    u32 gs_output_attributes_count;
    u32 vs_output_total;

    std::array<Pica::RasterizerRegs::VSOutputAttributes, 7> vs_output_attributes;

    // semantic_maps[semantic name] -> GS output attribute index + component index
    struct SemanticMap {
        u32 attribute_index;
        u32 component_index;
    };
    std::array<SemanticMap, 24> GetSemanticMaps() const;

    static constexpr u64 StructHash() {
        constexpr u64 STRUCT_VERSION = 0;

        using T = PicaGSConfigState;
        return Common::HashCombine(
            STRUCT_VERSION,

            // layout
            sizeof(T), alignof(T),

            // fields
            offsetof(T, vs_output_attributes_count), sizeof(vs_output_attributes_count),
            offsetof(T, gs_output_attributes_count), sizeof(gs_output_attributes_count),
            offsetof(T, vs_output_total), sizeof(vs_output_total),

            offsetof(T, vs_output_attributes), sizeof(vs_output_attributes));
    }
};

/**
 * This struct contains common information to identify a GLSL vertex shader generated from
 * PICA vertex shader.
 */
struct PicaVSConfigState {
    void Init(const Pica::RegsInternal& regs, Pica::ShaderSetup& setup);

    u8 lighting_disable;
    u64 program_hash;
    u64 swizzle_hash;
    u32 main_offset;

    u32 num_outputs;

    // output_map[output register index] -> output attribute index
    std::array<u32, 16> output_map;

    // These represent relevant input vertex attributes
    struct VAttr {
        u8 location;
        u8 type;
        u8 size;
    };
    u8 used_input_vertex_attributes;
    std::array<VAttr, 16> input_vertex_attributes;

    PicaGSConfigState gs_state;

    static constexpr u64 StructHash() {
        constexpr u64 STRUCT_VERSION = 0;

        using T = PicaVSConfigState;
        return Common::HashCombine(
            STRUCT_VERSION,

            // layout
            sizeof(T), alignof(T),

            // fields
            offsetof(T, lighting_disable), sizeof(lighting_disable), offsetof(T, program_hash),
            sizeof(program_hash), offsetof(T, swizzle_hash), sizeof(swizzle_hash),
            offsetof(T, main_offset), sizeof(main_offset),

            offsetof(T, num_outputs), sizeof(num_outputs),

            offsetof(T, output_map), sizeof(output_map),

            offsetof(T, used_input_vertex_attributes), sizeof(used_input_vertex_attributes),
            offsetof(T, input_vertex_attributes), sizeof(input_vertex_attributes),

            offsetof(T, gs_state), sizeof(gs_state),

            // nested layout
            PicaGSConfigState::StructHash());
    }
};

/**
 * This struct contains information to identify a GL vertex shader generated from PICA vertex
 * shader.
 */
struct PicaVSConfig : Common::HashableStruct<PicaVSConfigState> {
    PicaVSConfig() = default;
    explicit PicaVSConfig(const Pica::RegsInternal& regs, Pica::ShaderSetup& setup);
};

/**
 * This struct contains complementary user/driver information to generate a vertex shader.
 */
struct ExtraVSConfig {
    u8 use_clip_planes;
    u8 use_geometry_shader;
    u8 sanitize_mul;
    u8 separable_shader;

    // Load operations to apply to the input vertex data
    std::array<AttribLoadFlags, 16> load_flags;
};

/**
 * This struct contains information to identify a GL geometry shader generated from PICA no-geometry
 * shader pipeline
 */
struct PicaFixedGSConfig : Common::HashableStruct<PicaGSConfigState> {
    explicit PicaFixedGSConfig(const Pica::RegsInternal& regs);
};

struct ExtraFixedGSConfig {
    u8 use_clip_planes;
    u8 separable_shader;
};

} // namespace Pica::Shader::Generator

namespace std {
template <>
struct hash<Pica::Shader::Generator::PicaVSConfig> {
    std::size_t operator()(const Pica::Shader::Generator::PicaVSConfig& k) const noexcept {
        return k.Hash();
    }
};

template <>
struct hash<Pica::Shader::Generator::PicaFixedGSConfig> {
    std::size_t operator()(const Pica::Shader::Generator::PicaFixedGSConfig& k) const noexcept {
        return k.Hash();
    }
};
} // namespace std
