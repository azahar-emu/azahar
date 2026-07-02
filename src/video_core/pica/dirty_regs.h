// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "video_core/pica/regs_internal.h"

namespace Pica {

#define M_R(base, num_bits) (((1ULL << num_bits) - 1) << (PICA_REG_INDEX(base) & 0x3f))
#define M(base) M_R(base, 1)

union DirtyRegs {
    void Set(u32 reg_id) {
        qwords[reg_id >> 6] |= 1ULL << (reg_id & 0x3f);
    }

    void SetRange(u32 start_reg_id, u32 count) {
        if (count == 0) [[unlikely]] {
            return;
        }

        const u32 end = start_reg_id + count;
        const u32 first_word = start_reg_id >> 6;
        const u32 last_word = (end - 1) >> 6;
        const u32 start_bit = start_reg_id & 0x3f;

        if (first_word == last_word) {
            // Entire range fits in one qword.
            const u64 mask = (count >= 64) ? ~0ULL : (((1ULL << count) - 1ULL) << start_bit);
            qwords[first_word] |= mask;
            return;
        }

        // Partial first qword, set from start_bit until bit 63.
        qwords[first_word] |= (~0ULL << start_bit);

        // Set all middle qwords.
        for (u32 w = first_word + 1; w < last_word; ++w) {
            qwords[w] = ~0ULL;
        }

        // Partial last qword, set bits from 0 to end_bit.
        const u32 end_bit = end & 0x3f;
        const u64 last_mask = (end_bit == 0) ? ~0ULL : ((1ULL << end_bit) - 1ULL);
        qwords[last_word] |= last_mask;
    }

    void SetAllDirty() {
        qwords.fill(UINT64_MAX);
    }

    void Reset() {
        qwords.fill(0ULL);
    }

    bool CheckClipping() const {
        // Checks if GPUREG_FRAGOP_CLIP or GPUREG_FRAGOP_CLIP_DATAi are dirty
        static constexpr u64 ClipMask = M_R(rasterizer.clip_enable, 5);
        return rasterizer & ClipMask;
    }

    bool CheckDepth() const {
        // Checks if GPUREG_DEPTHMAP_SCALE or GPUREG_DEPTHMAP_OFFSET are dirty
        static constexpr u64 DepthMask =
            M(rasterizer.viewport_depth_range) | M(rasterizer.viewport_depth_near_plane);
        return rasterizer & DepthMask;
    }

    bool CheckLight(u32 index) const {
        // Checks if any GPUREG_LIGHTi_* is dirty
        return lights[index];
    }

    bool CheckFogColor() const {
        // Checks if GPUREG_FOG_COLOR is dirty
        static constexpr u64 FogColorMask = M(texturing.fog_color);
        return texenv & FogColorMask;
    }

    bool CheckTexUnits() const {
        // Checks if GPUREG_TEXUNITi_BORDER_COLOR or GPUREG_TEXUNITi_LOD are dirty
        static constexpr u64 TexUnitMask =
            M(texturing.texture0.border_color) | M(texturing.texture0.lod) |
            M(texturing.texture1.border_color) | M(texturing.texture1.lod) |
            M(texturing.texture2.border_color) | M(texturing.texture2.lod);
        return tex_units & TexUnitMask;
    }

    bool CheckProctex() const {
        // Checks if any GPUREG_TEXUNIT3_PROCTEXi reg is dirty
        static constexpr u64 ProctexMask = M_R(texturing.proctex, 6);
        return tex_units & ProctexMask;
    }

    bool CheckTexEnv() const {
        // Checks if GPUREG_TEXENV_BUFFER_COLOR or any GPUREG_TEXENVi_COLOR reg is dirty
        static constexpr u64 TexEnvMask =
            M(texturing.tev_combiner_buffer_color) | M(texturing.tev_stage0.const_color) |
            M(texturing.tev_stage1.const_color) | M(texturing.tev_stage2.const_color) |
            M(texturing.tev_stage3.const_color) | M(texturing.tev_stage4.const_color) |
            M(texturing.tev_stage5.const_color);
        return texenv & TexEnvMask;
    }

    bool CheckLightingAmbient() const {
        // Checks if GPUREG_LIGHTING_AMBIENT is dirty
        static constexpr u64 LightingMask = M(lighting.global_ambient);
        return light_lut & LightingMask;
    }

    bool CheckBlend() const {
        // Checks if GPUREG_BLEND_COLOR or GPUREG_FRAGOP_ALPHA_TEST are dirty
        static constexpr u64 BlendMask =
            M(framebuffer.output_merger.blend_const) | M(framebuffer.output_merger.alpha_test);
        return framebuffer & BlendMask;
    }

    bool CheckShadow() const {
        // Checks if GPUREG_FRAGOP_SHADOW or GPUREG_TEXUNIT0_SHADOW are dirty
        static constexpr u64 ShadowMask1 = M(framebuffer.shadow);
        static constexpr u64 ShadowMask2 = M(texturing.shadow);
        return (framebuffer & ShadowMask1) || (tex_units & ShadowMask2);
    }

    struct {
        u64 misc;
        u64 rasterizer;
        u64 tex_units;
        u64 texenv;
        u64 framebuffer;
        std::array<u16, 8> lights;
        u64 light_lut;
        u128 geo_pipeline;
        u128 shader;
    };
    std::array<u64, 12> qwords;
};
static_assert(sizeof(DirtyRegs) == 12 * sizeof(u64));

#undef M
#undef M_R

} // namespace Pica
