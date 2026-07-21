// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <limits>
#include <utility>
#include "common/assert.h"
#include "common/bit_set.h"
#include "common/hash.h"
#include "common/logging/log.h"
#include "video_core/pica/regs_shader.h"
#include "video_core/pica/shader_setup.h"

#if defined(CITRA_HAS_SSE42)
#include <nmmintrin.h>
#endif

#if defined(__aarch64__) || defined(__ARM_NEON)
#define CITRA_HAS_NEON
#include <arm_neon.h>
#endif

#if defined(_MSC_VER)
#define DISABLE_VECTORIZE __pragma(loop(no_vector))
#elif defined(__clang__)
#define DISABLE_VECTORIZE _Pragma("clang loop vectorize(disable)")
#elif defined(__GNUC__) && (__GNUC__ >= 14)
#define DISABLE_VECTORIZE _Pragma("GCC novector")
#else
#define DISABLE_VECTORIZE
#endif

namespace Pica {

ShaderSetup::ShaderSetup() = default;

ShaderSetup::~ShaderSetup() = default;

void ShaderSetup::WriteUniformBoolReg(u32 value) {
    const auto bits = BitSet32(value);
    for (u32 i = 0; i < uniforms.b.size(); ++i) {
        const bool prev = std::exchange(uniforms.b[i], bits[i]);
        uniforms_dirty |= prev != bits[i];
    }
}

void ShaderSetup::WriteUniformIntReg(u32 index, const Common::Vec4<u8> values) {
    ASSERT(index < uniforms.i.size());
    const auto prev = std::exchange(uniforms.i[index], values);
    uniforms_dirty |= prev != values;
}

std::optional<u32> ShaderSetup::WriteUniformFloatReg(ShaderRegs& config, u32 value) {
    auto& uniform_setup = config.uniform_setup;
    const bool is_float32 = uniform_setup.IsFloat32();
    if (!uniform_queue.Push(value, is_float32)) {
        return std::nullopt;
    }

    const auto uniform = uniform_queue.Get(is_float32);
    if (uniform_setup.index >= uniforms.f.size()) {
        LOG_ERROR(HW_GPU, "Invalid float uniform index {}", uniform_setup.index.Value());
        return std::nullopt;
    }

    const u32 index = uniform_setup.index.Value();
    const auto prev = std::exchange(uniforms.f[index], uniform);
    uniforms_dirty |= prev != uniform;
    uniform_setup.index.Assign(index + 1);
    return index;
}

std::optional<ShaderSetup::UniformWriteRange> ShaderSetup::WriteUniformFloatRegRange(
    ShaderRegs& config, const u32* values, u32 count) {

    if (count == 0) [[unlikely]] {
        return std::nullopt;
    }

    auto& uniform_setup = config.uniform_setup;
    const bool is_float32 = uniform_setup.IsFloat32();

    std::optional<u32> first_index;
    u32 written = 0;

    for (u32 i = 0; i < count; ++i) {
        if (!uniform_queue.Push(values[i], is_float32)) {
            continue;
        }

        const auto uniform = uniform_queue.Get(is_float32);
        if (uniform_setup.index >= uniforms.f.size()) [[unlikely]] {
            LOG_ERROR(HW_GPU, "Invalid float uniform index {}", uniform_setup.index.Value());
            break;
        }

        const u32 index = uniform_setup.index.Value();
        const auto prev = std::exchange(uniforms.f[index], uniform);
        uniforms_dirty |= prev != uniform;
        uniform_setup.index.Assign(index + 1);

        if (!first_index) {
            first_index = index;
        }
        ++written;
    }

    if (written == 0) {
        return std::nullopt;
    }
    return UniformWriteRange{*first_index, written};
}

u64 ShaderSetup::GetProgramCodeHash() {
    if (program_code_hash_dirty) {
        const auto& prog_code = GetProgramCode();
        program_code_hash =
            Common::ComputeHash64(prog_code.data(), biggest_program_size * sizeof(u32));
        program_code_hash_dirty = false;
    }
    return program_code_hash;
}

u64 ShaderSetup::GetSwizzleDataHash() {
    if (swizzle_data_hash_dirty) {
        swizzle_data_hash =
            Common::ComputeHash64(swizzle_data.data(), biggest_swizzle_size * sizeof(u32));
        swizzle_data_hash_dirty = false;
    }
    return swizzle_data_hash;
}

// Returns the index of the highest most significant bit set.
// 0001 -> 0, 0010 -> 1, 1000 -> 3...
inline u32 HighestSetBitIndex(u32 mask) {
#if defined(_MSC_VER)
    unsigned long index;
    _BitScanReverse(&index, mask);
    return static_cast<u32>(index);
#else
    return 31u - static_cast<u32>(__builtin_clz(mask));
#endif
}

// Process 32 bit words in groups of 4. If any of the words changed,
// store the new value and return true, plus the index of the word
// that changed.
#if defined(CITRA_HAS_SSE42)
static inline u32 ProcessBlockSSE42(u32* dst, const u32* values) {
    // Load 4 old values into old_vals.
    const __m128i old_vals = _mm_loadu_si128(reinterpret_cast<const __m128i*>(dst));
    // Load 4 new values into new_vals.
    const __m128i new_vals = _mm_loadu_si128(reinterpret_cast<const __m128i*>(values));
    // Compare both, eq now holds 4 words where each word X is either
    // 0xFFFFFFFF if words at X are equal or 0x0 if words at X differ.
    const __m128i eq = _mm_cmpeq_epi32(old_vals, new_vals);

    // If eq is all F, old_vals and new_vals are equal, return.
    if (_mm_testc_si128(eq, _mm_set1_epi32(-1))) {
        return std::numeric_limits<u32>::max();
    }

    // Store the new values to the destination.
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst), new_vals);

    // Reduce the four 0x0/0xFFFFFFFF words to 4 bits
    // stored to the 4 LSB of eq_mask.
    const u32 eq_mask = static_cast<u32>(_mm_movemask_ps(_mm_castsi128_ps(eq)));
    // Negate the result to make 0 signify equality instead of 1.
    const u32 neq_mask = (~eq_mask) & 0xFu;
    // Return the index of the highest 1, which corresponds to the index of the
    // last new word that differs (taking into account endianness).
    return HighestSetBitIndex(neq_mask);
}
#endif

#if defined(CITRA_HAS_NEON)
static inline u32 ProcessBlockNEON(u32* dst, const u32* values) {
    // Load 4 old values into old_vals.
    const uint32x4_t old_vals = vld1q_u32(dst);
    // Load 4 new values into new_vals.
    const uint32x4_t new_vals = vld1q_u32(values);
    // Compare both, neq now holds 4 words where each word X is either
    // 0x0 if words at X are equal or 0xFFFFFFFF if words at X differ.
    const uint32x4_t neq = vmvnq_u32(vceqq_u32(old_vals, new_vals));

    // If neq is all 0, old_vals and new_vals are equal, return.
    if (vmaxvq_u32(neq) == 0) {
        return std::numeric_limits<u32>::max();
    }

    // Store the new values to the destination.
    vst1q_u32(dst, new_vals);

    // Extract the value to an array and check which
    // entry is the first that differs. Corresponds to
    // the index of the last new word that differs
    // (taking into account endianness).
    // This needs to be done manually due to missing
    // _mm_movemask_ps equivalent on NEON.
    u32 neq_arr[4];
    vst1q_u32(neq_arr, neq);
    for (int i = 3; i >= 0; --i) {
        if (neq_arr[i] != 0) {
            return static_cast<u32>(i);
        }
    }

    // Should never happen as otherwise we would have
    // returned earlier.
    UNREACHABLE();
}
#endif

void ShaderSetup::UpdateProgramCodeRange(size_t offset, const u32* __restrict values, u32 count) {
    if (count == 0) [[unlikely]] {
        return;
    }

    u32* __restrict dst = &program_code[offset];
    bool any_changed = false;
    size_t last_changed_offset = offset;
    u32 i = 0;

    // SIMD block.
#if defined(CITRA_HAS_SSE42) || defined(CITRA_HAS_NEON)
    for (; i + 4 <= count; i += 4) {
#if defined(CITRA_HAS_SSE42)
        u32 index = ProcessBlockSSE42(dst + i, values + i);
#else
        u32 index = ProcessBlockNEON(dst + i, values + i);
#endif
        if (index != std::numeric_limits<u32>::max()) {
            any_changed = true;
            last_changed_offset = offset + i + index;
        }
    }
#endif

    // Scalar remainder (and whole loop when SIMD is not available).
    DISABLE_VECTORIZE
    for (; i < count; ++i) {
        u32& inst = dst[i];
        const u32 value = values[i];
        if (inst != value) {
            inst = value;
            any_changed = true;
            last_changed_offset = offset + i;
        }
    }

    if (any_changed) {
        if (!program_code_hash_dirty) {
            MakeProgramCodeDirty();
        }
        if ((last_changed_offset + 1) > biggest_program_size) {
            biggest_program_size = last_changed_offset + 1;
        }
    }
}

void ShaderSetup::UpdateSwizzleDataRange(size_t offset, const u32* __restrict values, u32 count) {
    if (count == 0) [[unlikely]] {
        return;
    }

    u32* __restrict dst = &swizzle_data[offset];
    bool any_changed = false;
    size_t last_changed_offset = offset;
    u32 i = 0;

    // SIMD block.
#if defined(CITRA_HAS_SSE42) || defined(CITRA_HAS_NEON)
    for (; i + 4 <= count; i += 4) {
#if defined(CITRA_HAS_SSE42)
        u32 index = ProcessBlockSSE42(dst + i, values + i);
#else
        u32 index = ProcessBlockNEON(dst + i, values + i);
#endif
        if (index != std::numeric_limits<u32>::max()) {
            any_changed = true;
            last_changed_offset = offset + i + index;
        }
    }
#endif

    // Scalar remainder (and whole loop when SIMD is not available).
    DISABLE_VECTORIZE
    for (; i < count; ++i) {
        u32& inst = dst[i];
        const u32 value = values[i];
        if (inst != value) {
            inst = value;
            any_changed = true;
            last_changed_offset = offset + i;
        }
    }

    if (any_changed) {
        if (!swizzle_data_hash_dirty) {
            MakeSwizzleDataDirty();
        }
        if ((last_changed_offset + 1) > biggest_swizzle_size) {
            biggest_swizzle_size = last_changed_offset + 1;
        }
    }
}

void ShaderSetup::DoProgramCodeFixup() {
    // This function holds shader fixups that are required for proper emulation of some games. As
    // emulation gets more accurate the goal is to have this function as empty as possible.
    // WARNING: If the hashing method is changed, the hashes of the different shaders will need
    // adjustment.

    if (!requires_fixup || !program_code_pending_fixup) {
        return;
    }
    program_code_pending_fixup = false;

    auto prepare_for_fixup = [this]() {
        memcpy(program_code_fixup.data(), program_code.data(), program_code.size());
        has_fixup = true;
        program_code_hash_dirty = true;
    };

    /**
     * Affected games:
     * Some Sega 3D Classics games.
     *
     * Problem:
     * The geometry shaders used by some of the Sega 3D Classics have a shader
     * (gf2_five_color_gshader.vsh) that presents two separate issues.
     *
     * - The shader does not have an "end" instruction. This causes the execution
     *   to be unbounded and start running whatever is in the code buffer after the end of the
     *   program. Usually this is filled with zeroes, which are add instructions that have no effect
     *   due to no more vertices being emitted. It's not clear what happens when the PC reaches
     *   0x3FFC and is incremented. The most likely scenario is that it overflows back to the start,
     *   where there is an end instruction close by. This causes the game to execute around 4K
     *   instructions per vertex, which is very slow on emulator.
     *
     * - The shader relies on a HW bug or quirk that we do not currently understand or implement.
     *   The game builds a quad (4 vertices) using two inputs, the upper left coordinate, and the
     *   bottom right coordinate. The generated vertex coordinates are put in output register o0
     *   before being emitted. Here is the pseudocode of the shader:
     *       o0.xyzw = leftTop.xyzw
     *       emit                       <- Emits the top left vertex
     *       o0._yzw = leftTop.xyzw
     *       o0.x___ = rightBottom.xyzw
     *       emit                       <- Emits the top right vertex
     *       o0._y__ = rightBottom.xyzw
     *       emit                       <- Emits the bottom left vertex (!)
     *       o0.xyzw = rightBottom.xyzw
     *       emit                       <- Emits the bottom right vertex
     *
     *   This shader code has a bug. When the bottom left vertex is emitted, the y element is
     *   updated to the bottom coordinate, but the x element is left untouched. One would say that
     *   since the x element was last set to the RIGHT coordinate, the vertex would end up being
     *   drawn to the bottom RIGHT instead of the intended bottom LEFT (which is what we observe on
     *   the emulator). But on real HW, the vertex is drawn to the bottom LEFT instead. This
     *   suggests a HW bug or quirk that is triggered whenever some elements of an output register
     *   are not written to between emits. In order for the quad to look proper, the xzw elements
     *   should somehow keep the contents from the first emit, where the top left coordinate was
     *   written. The specifics of the HW bug that causes this are unknown.
     *
     * Solution:
     * The following patches are made to fix the shaders:
     *
     * - An end instruction is inserted at the end of the shader to prevent unbounded execution.
     *
     * - Before the third vertex is emited and the y element of o0 is adjusted, a mov o0.xywz
     *   leftTop.xywz instruction is inserted to update the xzw elements of the output register to
     *   the expected values. This requires making room in the shader code, but luckily there are no
     *   jumps that need relocation.
     *
     */
    constexpr u64 SEGA_3D_CLASSICS_THUNDER_BLADE = 0x0797513756f2c8c9;
    constexpr u64 SEGA_3D_CLASSICS_AFTER_BURNER = 0x188e959fbe31324d;
    constexpr u64 SEGA_3D_CLASSICS_COLLECTION_TB = 0x5954c8e4d13cdd86;
    constexpr u64 SEGA_3D_CLASSICS_COLLECTION_PD = 0x27496993b307355b;

    const auto fix_3d_classics_common = [this](u32 offset_base, u32 mov_swizzle) {
        offset_base /= 4;

        // Make some room to insert an instruction
        std::memmove(program_code_fixup.data() + offset_base + 1,
                     program_code_fixup.data() + offset_base, 0x1C);

        // mov o0.xyzw v0.xyzw (out.pos <- vLeftTop)
        program_code_fixup[offset_base] = 0x4c000000 | mov_swizzle;

        // end
        program_code_fixup[offset_base + 0x20] = 0x88000000;

        // Adjust biggest program size
        if (biggest_program_size <= offset_base + 0x20) {
            biggest_program_size = offset_base + 0x20 + 1;
        }
    };

    // Select shader fixup
    switch (GetProgramCodeHash()) {
    case SEGA_3D_CLASSICS_THUNDER_BLADE: {
        prepare_for_fixup();
        fix_3d_classics_common(0x510, 0xA);
    } break;
    case SEGA_3D_CLASSICS_AFTER_BURNER: {
        prepare_for_fixup();
        fix_3d_classics_common(0x50C, 0xA);
    } break;
    case SEGA_3D_CLASSICS_COLLECTION_TB: {
        prepare_for_fixup();
        fix_3d_classics_common(0xAE0, 0xC);
    } break;
    case SEGA_3D_CLASSICS_COLLECTION_PD: {
        prepare_for_fixup();
        fix_3d_classics_common(0xAF0, 0xC);
    } break;
    default:
        break;
    }
}

} // namespace Pica
