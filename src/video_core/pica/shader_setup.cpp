// Copyright 2023-2026 Citra Emulator Project / Azahar Emulator Project
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
        // Some games may submit OOB indexes (mainly 0x7F), which is
        // ignored on real HW.
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
            // Some games may submit OOB indexes (mainly 0x7F), which is
            // ignored on real HW.
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

    // Load all 0xF values
    const __m128i ones = _mm_set1_epi32(-1);

    // If eq is all 0xF, old_vals and new_vals are equal, return.
    if (_mm_testc_si128(eq, ones)) {
        return std::numeric_limits<u32>::max();
    }

    // Store the new values to the destination.
    _mm_storeu_si128(reinterpret_cast<__m128i*>(dst), new_vals);

    // First negate the result to make 0 signify equality instead of 1
    // (using a XOR of all F).
    // Then reduce the four 0x0/0xFFFFFFFF words to 4 bits
    // stored to the 4 LSB of res.
    const u32 res = static_cast<u32>(_mm_movemask_ps(_mm_castsi128_ps(_mm_xor_si128(eq, ones))));
    // Return the index of the highest 1, which corresponds to the index of the
    // last new word that differs (taking into account endianness).
    return HighestSetBitIndex(res);
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

    // Extract the index of the highest value that was different.
    // This is achieved by ANDing the neq array with (0,1,2,3).
    // If word is equal, 0 is produced, otherwise the index
    // is produced. Then the max index is selected from the
    // result.
    static constexpr u32 index_arr[4] = {0, 1, 2, 3};
    const uint32x4_t indices = vld1q_u32(index_arr);
    return vmaxvq_u32(vandq_u32(neq, indices));
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

    /*
    if (!requires_fixup || !program_code_pending_fixup) {
        return;
    }
    program_code_pending_fixup = false;

    auto prepare_for_fixup = [this]() {
        memcpy(program_code_fixup.data(), program_code.data(), program_code.size());
        has_fixup = true;
        program_code_hash_dirty = true;
    };
    */

    // No games require shader fixups right now
}

} // namespace Pica
