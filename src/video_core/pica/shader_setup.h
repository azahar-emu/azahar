// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <optional>
#include "common/vector_math.h"
#include "video_core/pica/packed_attribute.h"
#include "video_core/pica_types.h"

namespace Pica {

constexpr u32 MAX_PROGRAM_CODE_LENGTH = 4096;
constexpr u32 MAX_SWIZZLE_DATA_LENGTH = 4096;

using ProgramCode = std::array<u32, MAX_PROGRAM_CODE_LENGTH>;
using SwizzleData = std::array<u32, MAX_SWIZZLE_DATA_LENGTH>;

struct Uniforms {
    alignas(16) std::array<Common::Vec4<f24>, 96> f;
    std::array<bool, 16> b;
    std::array<Common::Vec4<u8>, 4> i;

    static std::size_t GetFloatUniformOffset(u32 index) {
        return offsetof(Uniforms, f) + index * sizeof(Common::Vec4<f24>);
    }

    static std::size_t GetBoolUniformOffset(u32 index) {
        return offsetof(Uniforms, b) + index * sizeof(bool);
    }

    static std::size_t GetIntUniformOffset(u32 index) {
        return offsetof(Uniforms, i) + index * sizeof(Common::Vec4<u8>);
    }

private:
    friend class boost::serialization::access;
    template <class Archive>
    void serialize(Archive& ar, const u32 file_version) {
        ar & f;
        ar & b;
        ar & i;
    }
};

struct ShaderRegs;

/**
 * This structure contains the state information common for all shader units such as uniforms.
 * The geometry shaders has a unique configuration so when enabled it has its own setup.
 */
struct ShaderSetup {
private:
    void MakeProgramCodeDirty() {
        program_code_hash_dirty = true;
        program_code_pending_fixup = true;
        has_fixup = false;
    }

    void MakeSwizzleDataDirty() {
        swizzle_data_hash_dirty = true;
    }

public:
    explicit ShaderSetup();
    ~ShaderSetup();

    void WriteUniformBoolReg(u32 value);

    void WriteUniformIntReg(u32 index, const Common::Vec4<u8> values);

    std::optional<u32> WriteUniformFloatReg(ShaderRegs& config, u32 value);

    u64 GetProgramCodeHash();

    u64 GetSwizzleDataHash();

    void DoProgramCodeFixup();

    inline void UpdateProgramCode(size_t offset, u32 value) {
        u32& inst = program_code[offset];
        u32 old = inst;

        if (old == value)
            return;

        inst = value;
        if (!program_code_hash_dirty) {
            MakeProgramCodeDirty();
        }
        if ((offset + 1) > biggest_program_size) {
            biggest_program_size = offset + 1;
        }
    }

    void UpdateProgramCode(const ProgramCode& other) {
        program_code = other;
        biggest_program_size = program_code.size();
        MakeProgramCodeDirty();
    }

    inline void UpdateSwizzleData(size_t offset, u32 value) {
        u32& data = swizzle_data[offset];
        u32 old = data;

        if (old == value)
            return;

        data = value;
        if (!swizzle_data_hash_dirty) {
            MakeSwizzleDataDirty();
        }
        if ((offset + 1) > biggest_swizzle_size) {
            biggest_swizzle_size = offset + 1;
        }
    }

    void UpdateSwizzleData(const SwizzleData& other) {
        swizzle_data = other;
        biggest_swizzle_size = swizzle_data.size();
        MakeSwizzleDataDirty();
    }

    const ProgramCode& GetProgramCode() const {
        return (has_fixup) ? program_code_fixup : program_code;
    }

    const SwizzleData& GetSwizzleData() const {
        return swizzle_data;
    }

public:
    Uniforms uniforms;
    PackedAttribute uniform_queue;
    u32 entry_point{};
    const void* cached_shader{};
    bool uniforms_dirty = true;
    bool requires_fixup = false;
    bool has_fixup = false;

private:
    ProgramCode program_code{};
    ProgramCode program_code_fixup{};
    SwizzleData swizzle_data{};
    bool program_code_hash_dirty{true};
    bool swizzle_data_hash_dirty{true};
    bool program_code_pending_fixup{true};
    u32 biggest_program_size = 0;
    u32 biggest_swizzle_size = 0;
    u64 program_code_hash{0};
    u64 swizzle_data_hash{0};

    friend class boost::serialization::access;
    template <class Archive>
    void serialize(Archive& ar, const u32 file_version) {
        ar & uniforms;
        ar & uniform_queue;
        ar & program_code;
        ar & program_code_fixup;
        ar & swizzle_data;
        ar & program_code_hash_dirty;
        ar & swizzle_data_hash_dirty;
        ar & program_code_pending_fixup;
        ar & biggest_program_size;
        ar & biggest_swizzle_size;
        ar & program_code_hash;
        ar & swizzle_data_hash;
        ar & requires_fixup;
        ar & has_fixup;
    }
};

} // namespace Pica
