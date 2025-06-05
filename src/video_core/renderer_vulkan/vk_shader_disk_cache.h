// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/common_types.h"
#include "common/file_util.h"
#include "video_core/pica/regs_internal.h"
#include "video_core/shader/generator/shader_gen.h"

namespace Vulkan {

struct ShaderDiskCacheDecompiled;

using RawShaderConfig = Pica::RegsInternal;
using ProgramCode = std::vector<u32>;
using ProgramType = Pica::Shader::Generator::ProgramType;
using ShaderDecompiledMap = std::unordered_map<u64, ShaderDiskCacheDecompiled>;

/// Describes a shader as it's used by the guest GPU
class ShaderDiskCacheRaw {
public:
    explicit ShaderDiskCacheRaw(u64 unique_identifier, ProgramType program_type,
                                RawShaderConfig config, ProgramCode program_code);
    ShaderDiskCacheRaw() = default;
    ~ShaderDiskCacheRaw() = default;

    bool Load(FileUtil::IOFile& file);
    bool Save(FileUtil::IOFile& file) const;

    u64 GetUniqueIdentifier() const {
        return unique_identifier;
    }

    ProgramType GetProgramType() const {
        return program_type;
    }

    const ProgramCode& GetProgramCode() const {
        return program_code;
    }

    const RawShaderConfig& GetRawShaderConfig() const {
        return config;
    }

private:
    u64 unique_identifier{};
    ProgramType program_type{};
    RawShaderConfig config{};
    ProgramCode program_code{};
};

/// Contains decompiled data from a shader
struct ShaderDiskCacheDecompiled {
    std::string code;
    bool sanitize_mul;
};

class ShaderDiskCache {
public:
    explicit ShaderDiskCache(u64 program_id);
    ~ShaderDiskCache() = default;

    /// Loads transferable cache. If file has an old version or fails, it deletes the file.
    std::optional<std::vector<ShaderDiskCacheRaw>> LoadTransferable();

    /// Loads current game's precompiled cache. Invalidates on failure.
    ShaderDecompiledMap LoadPrecompiled();

    /// Removes the transferable (and precompiled) cache file.
    void InvalidateAll();

    /// Removes the precompiled cache file and clears virtual precompiled cache file.
    void InvalidatePrecompiled();

    /// Saves a raw dump to the transferable file. Checks for collisions.
    void SaveRaw(const ShaderDiskCacheRaw& entry);

    /// Saves a decompiled entry to the precompiled file. Does not check for collisions.
    void SaveDecompiled(u64 unique_identifier, const std::string& code, bool sanitize_mul);

    /// Returns if the cache can be used
    bool IsUsable() const;

private:
    /// Loads the transferable cache. Returns empty on failure.
    std::optional<ShaderDecompiledMap> LoadPrecompiledFile(FileUtil::IOFile& file);

    /// Loads a decompiled cache entry from m_precompiled_cache_virtual_file. Returns empty on
    /// failure.
    std::optional<ShaderDiskCacheDecompiled> LoadDecompiledEntry();

    /// Saves a decompiled entry to the passed file. Does not check for collisions.
    void SaveDecompiledToFile(FileUtil::IOFile& file, u64 unique_identifier,
                              const std::string& code, bool sanitize_mul);

    /// Opens current game's transferable file and write its header if it doesn't exist.
    FileUtil::IOFile AppendTransferableFile();

    /// Opens current game's precompiled file and write its header if it doesn't exist.
    FileUtil::IOFile AppendPrecompiledFile();

    /// Create shader disk cache directories. Returns true on success.
    bool EnsureDirectories() const;

    /// Gets current game's transferable file path
    std::string GetTransferablePath();

    /// Gets current game's precompiled file path
    std::string GetPrecompiledPath();

    /// Get user's transferable directory path
    std::string GetTransferableDir() const;

    /// Get user's precompiled directory path
    std::string GetPrecompiledDir() const; /// Get user's shader directory path
    std::string GetBaseDir() const;

    /// Get current game's title id
    std::string GetTitleID();

    /// Get current game's title id as u64
    u64 GetProgramID() const;

    template <typename T>
    bool SaveArrayToPrecompiled(const T* data, std::size_t length) {
        const u8* data_view = reinterpret_cast<const u8*>(data);
        decompressed_precompiled_cache.insert(decompressed_precompiled_cache.end(), &data_view[0],
                                              &data_view[length * sizeof(T)]);
        decompressed_precompiled_cache_offset += length * sizeof(T);
        return true;
    }

    template <typename T>
    bool LoadArrayFromPrecompiled(T* data, std::size_t length) {
        u8* data_view = reinterpret_cast<u8*>(data);
        std::copy_n(decompressed_precompiled_cache.data() + decompressed_precompiled_cache_offset,
                    length * sizeof(T), data_view);
        decompressed_precompiled_cache_offset += length * sizeof(T);
        return true;
    }

    template <typename T>
    bool SaveObjectToPrecompiled(const T& object) {
        return SaveArrayToPrecompiled(&object, 1);
    }

    bool SaveObjectToPrecompiled(bool object);

    template <typename T>
    bool LoadObjectFromPrecompiled(T& object) {
        return LoadArrayFromPrecompiled(&object, 1);
    }

    // Stored transferable shaders
    std::unordered_map<u64, ShaderDiskCacheRaw> transferable;

    // The cache has been loaded at boot
    bool tried_to_load{};

    // Decompressed precompiled cache
    std::vector<u8> decompressed_precompiled_cache;

    // Current offset in precompiled cache
    std::size_t decompressed_precompiled_cache_offset = 0;

    u64 program_id{};
    std::string title_id;

    FileUtil::IOFile transferable_file;
    FileUtil::IOFile precompiled_file;
};

} // namespace Vulkan
