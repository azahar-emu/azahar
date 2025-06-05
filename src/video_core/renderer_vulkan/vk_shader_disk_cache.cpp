// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include <fmt/format.h>

#include "common/common_paths.h"
#include "common/common_types.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "video_core/renderer_vulkan/vk_shader_disk_cache.h"

namespace Vulkan {

constexpr std::size_t HASH_LENGTH = 64;
using ShaderCacheVersionHash = std::array<u8, HASH_LENGTH>;

enum class TransferableEntryKind : u32 {
    Raw,
};

enum class PrecompiledEntryKind : u32 {
    Decompiled,
};

constexpr u32 NativeVersion = 1;

// The hash is based on relevant files. The list of files can be found at src/common/CMakeLists.txt
// and CMakeModules/GenerateSCMRev.cmake
ShaderCacheVersionHash GetShaderCacheVersionHash() {
    ShaderCacheVersionHash hash{};
    const std::size_t length = std::min(std::strlen(Common::g_shader_cache_version), hash.size());
    std::memcpy(hash.data(), Common::g_shader_cache_version, length);
    return hash;
}

ShaderDiskCacheRaw::ShaderDiskCacheRaw(u64 unique_identifier, ProgramType program_type,
                                       RawShaderConfig config, ProgramCode program_code)
    : unique_identifier{unique_identifier}, program_type{program_type}, config{config},
      program_code{std::move(program_code)} {}

bool ShaderDiskCacheRaw::Load(FileUtil::IOFile& file) {
    if (file.ReadBytes(&unique_identifier, sizeof(u64)) != sizeof(u64) ||
        file.ReadBytes(&program_type, sizeof(u32)) != sizeof(u32)) {
        return false;
    }

    u64 reg_array_len{};
    if (file.ReadBytes(&reg_array_len, sizeof(u64)) != sizeof(u64)) {
        return false;
    }

    if (file.ReadArray(config.reg_array.data(), reg_array_len) != reg_array_len) {
        return false;
    }

    // Read in type specific configuration
    if (program_type == ProgramType::VS) {
        u64 code_len{};
        if (file.ReadBytes(&code_len, sizeof(u64)) != sizeof(u64)) {
            return false;
        }
        program_code.resize(code_len);
        if (file.ReadArray(program_code.data(), code_len) != code_len) {
            return false;
        }
    }

    return true;
}

bool ShaderDiskCacheRaw::Save(FileUtil::IOFile& file) const {
    if (file.WriteObject(unique_identifier) != 1 ||
        file.WriteObject(static_cast<u32>(program_type)) != 1) {
        return false;
    }

    // Just for future proofing, save the sizes of the array to the file
    const std::size_t reg_array_len = Pica::RegsInternal::NUM_REGS;
    if (file.WriteObject(static_cast<u64>(reg_array_len)) != 1) {
        return false;
    }
    if (file.WriteArray(config.reg_array.data(), reg_array_len) != reg_array_len) {
        return false;
    }

    if (program_type == ProgramType::VS) {
        const std::size_t code_len = program_code.size();
        if (file.WriteObject(static_cast<u64>(code_len)) != 1) {
            return false;
        }
        if (file.WriteArray(program_code.data(), code_len) != code_len) {
            return false;
        }
    }

    return true;
}

ShaderDiskCache::ShaderDiskCache(u64 program_id_)
    : program_id{program_id_}, transferable_file(AppendTransferableFile()),
      precompiled_file(AppendPrecompiledFile()) {}

std::optional<std::vector<ShaderDiskCacheRaw>> ShaderDiskCache::LoadTransferable() {
    const bool has_title_id = GetProgramID() != 0;
    if (!Settings::values.use_hw_shader || !Settings::values.use_disk_shader_cache ||
        !has_title_id) {
        return std::nullopt;
    }
    tried_to_load = true;

    if (transferable_file.GetSize() == 0) {
        LOG_INFO(Render_Vulkan, "No transferable shader cache found for game with title id={}",
                 GetTitleID());
        return std::nullopt;
    }

    u32 version{};
    if (transferable_file.ReadBytes(&version, sizeof(version)) != sizeof(version)) {
        LOG_ERROR(Render_Vulkan,
                  "Failed to get transferable cache version for title id={} - removing",
                  GetTitleID());
        InvalidateAll();
        return std::nullopt;
    }

    if (version < NativeVersion) {
        LOG_INFO(Render_Vulkan, "Transferable shader cache is old - removing");
        InvalidateAll();
        return std::nullopt;
    }
    if (version > NativeVersion) {
        LOG_WARNING(Render_Vulkan, "Transferable shader cache was generated with a newer version "
                                   "of the emulator - skipping");
        return std::nullopt;
    } // Version is valid, load the shaders
    std::vector<ShaderDiskCacheRaw> raws;
    u32 entry_index = 0;
    u32 invalid_entries = 0;

    while (transferable_file.Tell() < transferable_file.GetSize()) {
        TransferableEntryKind kind{};
        if (transferable_file.ReadBytes(&kind, sizeof(u32)) != sizeof(u32)) {
            LOG_ERROR(Render_Vulkan, "Failed to read transferable file at entry {} - removing",
                      entry_index);
            InvalidateAll();
            return std::nullopt;
        }

        switch (kind) {
        case TransferableEntryKind::Raw: {
            ShaderDiskCacheRaw entry;
            if (!entry.Load(transferable_file)) {
                LOG_ERROR(Render_Vulkan, "Failed to load transferable raw entry {} - skipping",
                          entry_index);
                invalid_entries++;
                // Try to continue with the next entry rather than failing the whole cache
                // But if too many errors, abort
                if (invalid_entries > 10) {
                    LOG_ERROR(Render_Vulkan,
                              "Too many invalid entries in transferable cache - removing");
                    InvalidateAll();
                    return std::nullopt;
                }
                continue;
            }

            // Validate entry - make sure the identifier is not zero and the program type is valid
            const u64 id = entry.GetUniqueIdentifier();
            const auto program_type = entry.GetProgramType();

            if (id == 0 || (program_type != ProgramType::VS && program_type != ProgramType::GS &&
                            program_type != ProgramType::FS)) {
                LOG_ERROR(Render_Vulkan, "Invalid shader entry (id: {}, type: {}) - skipping", id,
                          static_cast<u32>(program_type));
                invalid_entries++;
                continue;
            }

            transferable.emplace(id, ShaderDiskCacheRaw{});
            raws.push_back(std::move(entry));
            break;
        }
        default:
            LOG_ERROR(Render_Vulkan,
                      "Unknown transferable shader cache entry kind={} at index {} - removing",
                      kind, entry_index);
            InvalidateAll();
            return std::nullopt;
        }
        entry_index++;
    }

    if (invalid_entries > 0) {
        LOG_WARNING(
            Render_Vulkan,
            "Found a transferable disk cache with {} valid entries, skipped {} invalid entries",
            raws.size(), invalid_entries);
    } else {
        LOG_INFO(Render_Vulkan, "Found a transferable disk cache with {} entries", raws.size());
    }
    return {std::move(raws)};
}

ShaderDecompiledMap ShaderDiskCache::LoadPrecompiled() {
    if (!IsUsable())
        return {};

    if (precompiled_file.GetSize() == 0) {
        LOG_INFO(Render_Vulkan, "No precompiled shader cache found for game with title id={}",
                 GetTitleID());
        return {};
    }

    const auto result = LoadPrecompiledFile(precompiled_file);
    if (!result) {
        LOG_INFO(Render_Vulkan,
                 "Failed to load precompiled cache for game with title id={} - removing",
                 GetTitleID());
        InvalidatePrecompiled();
        return {};
    }
    LOG_INFO(Render_Vulkan, "Loaded {} precompiled shaders for game with title id={}",
             result->size(), GetTitleID());
    return *result;
}

std::optional<ShaderDecompiledMap> ShaderDiskCache::LoadPrecompiledFile(FileUtil::IOFile& file) {
    // Read version
    u32 version{};
    if (file.ReadBytes(&version, sizeof(version)) != sizeof(version)) {
        return std::nullopt;
    }

    if (version < NativeVersion) {
        LOG_INFO(Render_Vulkan, "Precompiled cache is old - removing");
        return std::nullopt;
    }
    if (version > NativeVersion) {
        LOG_WARNING(
            Render_Vulkan,
            "Precompiled cache was generated with a newer version of the emulator - skipping");
        return std::nullopt;
    }

    // Read shader cache version hash
    ShaderCacheVersionHash hash{};
    if (file.ReadArray(hash.data(), hash.size()) != hash.size()) {
        return std::nullopt;
    } // Version is valid, load the shaders
    ShaderDecompiledMap decompiled;
    u32 entry_index = 0;

    while (file.Tell() < file.GetSize()) {
        PrecompiledEntryKind kind{};
        if (file.ReadBytes(&kind, sizeof(u32)) != sizeof(u32)) {
            LOG_ERROR(Render_Vulkan, "Failed to read precompiled entry kind at index {} - aborting",
                      entry_index);
            return std::nullopt;
        }

        switch (kind) {
        case PrecompiledEntryKind::Decompiled: {
            u64 unique_identifier{};
            if (file.ReadBytes(&unique_identifier, sizeof(u64)) != sizeof(u64)) {
                LOG_ERROR(Render_Vulkan,
                          "Failed to read precompiled shader ID at index {} - aborting",
                          entry_index);
                return std::nullopt;
            }

            // Skip entries with invalid IDs
            if (unique_identifier == 0) {
                LOG_WARNING(Render_Vulkan, "Invalid shader ID (0) at index {} - skipping",
                            entry_index);

                // Try to read past this entry by skipping the rest of its data
                bool dummy_bool;
                if (file.ReadBytes(&dummy_bool, sizeof(bool)) != sizeof(bool)) {
                    return std::nullopt;
                }

                u64 code_size{};
                if (file.ReadBytes(&code_size, sizeof(u64)) != sizeof(u64)) {
                    return std::nullopt;
                }

                // Sanity check on code size
                if (code_size > 1024 * 1024) { // Arbitrary 1MB limit
                    LOG_ERROR(Render_Vulkan, "Suspiciously large shader code size {} - aborting",
                              code_size);
                    return std::nullopt;
                }

                // Skip the code data
                file.Seek(code_size, SEEK_CUR);
                entry_index++;
                continue;
            }

            bool sanitize_mul;
            if (file.ReadBytes(&sanitize_mul, sizeof(bool)) != sizeof(bool)) {
                LOG_ERROR(Render_Vulkan, "Failed to read sanitize_mul flag at index {} - aborting",
                          entry_index);
                return std::nullopt;
            }

            u64 code_size{};
            if (file.ReadBytes(&code_size, sizeof(u64)) != sizeof(u64)) {
                LOG_ERROR(Render_Vulkan, "Failed to read code size at index {} - aborting",
                          entry_index);
                return std::nullopt;
            }

            // Sanity check on code size
            if (code_size == 0 || code_size > 1024 * 1024) { // Arbitrary 1MB limit
                LOG_ERROR(Render_Vulkan, "Invalid shader code size {} at index {} - skipping",
                          code_size, entry_index);
                // Skip the code data
                file.Seek(code_size, SEEK_CUR);
                entry_index++;
                continue;
            }

            std::string code(code_size, '\0');
            if (file.ReadArray(code.data(), code_size) != code_size) {
                LOG_ERROR(Render_Vulkan, "Failed to read shader code at index {} - aborting",
                          entry_index);
                return std::nullopt;
            }

            // Validate that the code appears to be valid GLSL
            if (code.empty() || code.size() < 10) { // Basic sanity check
                LOG_WARNING(Render_Vulkan, "Skipping invalid shader with empty or very small code");
                entry_index++;
                continue; // Skip this entry but continue processing the file
            }

            // Check for common GLSL shader header
            if (code.find("#version") == std::string::npos) {
                LOG_WARNING(Render_Vulkan,
                            "Shader at index {} doesn't look like valid GLSL - skipping",
                            entry_index);
                entry_index++;
                continue;
            }

            decompiled.insert({unique_identifier, {std::move(code), sanitize_mul}});
            break;
        }
        default:
            return std::nullopt;
        }
    }

    return decompiled;
}

std::optional<ShaderDiskCacheDecompiled> ShaderDiskCache::LoadDecompiledEntry() {
    // Read in type
    PrecompiledEntryKind kind;
    if (!LoadObjectFromPrecompiled(kind)) {
        return std::nullopt;
    }

    if (kind != PrecompiledEntryKind::Decompiled) {
        return std::nullopt;
    }

    u64 unique_identifier;
    if (!LoadObjectFromPrecompiled(unique_identifier)) {
        return std::nullopt;
    }

    bool sanitize_mul;
    if (!LoadObjectFromPrecompiled(sanitize_mul)) {
        return std::nullopt;
    }

    u64 code_size;
    if (!LoadObjectFromPrecompiled(code_size)) {
        return std::nullopt;
    }

    std::string code(code_size, '\0');
    if (!LoadArrayFromPrecompiled(code.data(), code_size)) {
        return std::nullopt;
    }

    return {{code, sanitize_mul}};
}

void ShaderDiskCache::SaveDecompiledToFile(FileUtil::IOFile& file, u64 unique_identifier,
                                           const std::string& code, bool sanitize_mul) {
    if (file.WriteObject(static_cast<u32>(PrecompiledEntryKind::Decompiled)) != 1 ||
        file.WriteObject(unique_identifier) != 1 || file.WriteObject(sanitize_mul) != 1 ||
        file.WriteObject(static_cast<u64>(code.size())) != 1 ||
        file.WriteArray(code.data(), code.size()) != code.size()) {
        LOG_ERROR(Render_Vulkan, "Failed to save decompiled cache entry - removing");
        InvalidatePrecompiled();
    }
}

bool ShaderDiskCache::SaveObjectToPrecompiled(bool object) {
    const auto value = static_cast<u8>(object);
    return SaveArrayToPrecompiled(&value, 1);
}

void ShaderDiskCache::InvalidateAll() {
    transferable.clear();
    InvalidatePrecompiled();

    transferable_file.Close();
    if (!FileUtil::Delete(GetTransferablePath())) {
        LOG_ERROR(Render_Vulkan, "Failed to invalidate transferable file={}",
                  GetTransferablePath());
    }
    transferable_file = AppendTransferableFile();
}

void ShaderDiskCache::InvalidatePrecompiled() {
    // Clear virtual precompiled cache file
    decompressed_precompiled_cache.resize(0);

    precompiled_file.Close();
    if (!FileUtil::Delete(GetPrecompiledPath())) {
        LOG_ERROR(Render_Vulkan, "Failed to invalidate precompiled file={}", GetPrecompiledPath());
    }
    precompiled_file = AppendPrecompiledFile();
}

void ShaderDiskCache::SaveRaw(const ShaderDiskCacheRaw& entry) {
    if (!IsUsable())
        return;

    const u64 id = entry.GetUniqueIdentifier();
    if (transferable.find(id) != transferable.end()) {
        // The shader already exists
        return;
    }

    if (transferable_file.WriteObject(TransferableEntryKind::Raw) != 1 ||
        !entry.Save(transferable_file)) {
        LOG_ERROR(Render_Vulkan, "Failed to save raw transferable cache entry - removing");
        InvalidateAll();
        return;
    }
    transferable.insert({id, entry});
    transferable_file.Flush();
    LOG_DEBUG(Render_Vulkan, "Saved raw shader entry with id={:016X}", id);
}

void ShaderDiskCache::SaveDecompiled(u64 unique_identifier, const std::string& code,
                                     bool sanitize_mul) {
    if (!IsUsable() || code.empty())
        return;

    // Don't save very small code - this is likely an error
    if (code.size() < 10) {
        LOG_WARNING(Render_Vulkan, "Not saving suspiciously small shader code of size {}",
                    code.size());
        return;
    }

    SaveDecompiledToFile(precompiled_file, unique_identifier, code, sanitize_mul);
    precompiled_file.Flush();
}

bool ShaderDiskCache::IsUsable() const {
    return tried_to_load && Settings::values.use_disk_shader_cache;
}

FileUtil::IOFile ShaderDiskCache::AppendTransferableFile() {
    if (!EnsureDirectories()) {
        return {};
    }

    const auto path = GetTransferablePath();
    // Create new transferable if it doesn't exist
    if (!FileUtil::Exists(path)) {
        FileUtil::IOFile file(path, "wb");
        if (!file.IsOpen()) {
            LOG_ERROR(Render_Vulkan, "Failed to create transferable cache in path={}", path);
            return {};
        }
        if (file.WriteObject(NativeVersion) != 1) {
            LOG_ERROR(Render_Vulkan, "Failed to write transferable cache version in path={}", path);
            return {};
        }
        return file;
    }

    // Otherwise, just append to the existing file
    FileUtil::IOFile file(path, "r+b");
    if (!file.IsOpen()) {
        LOG_ERROR(Render_Vulkan, "Failed to open transferable cache in path={}", path);
        return {};
    }
    return file;
}

FileUtil::IOFile ShaderDiskCache::AppendPrecompiledFile() {
    if (!EnsureDirectories()) {
        return {};
    }

    const auto path = GetPrecompiledPath();
    // If the file doesn't exist, create it with the version
    if (!FileUtil::Exists(path)) {
        FileUtil::IOFile file(path, "wb");
        if (!file.IsOpen()) {
            LOG_ERROR(Render_Vulkan, "Failed to create precompiled cache in path={}", path);
            return {};
        }
        if (file.WriteObject(NativeVersion) != 1 ||
            file.WriteArray(GetShaderCacheVersionHash().data(), HASH_LENGTH) != HASH_LENGTH) {
            LOG_ERROR(Render_Vulkan, "Failed to write header to precompiled cache in path={}",
                      path);
            return {};
        }
        return file;
    }

    // Otherwise, just append to the existing file
    FileUtil::IOFile file(path, "r+b");
    if (!file.IsOpen()) {
        LOG_ERROR(Render_Vulkan, "Failed to open precompiled cache in path={}", path);
        return {};
    }
    return file;
}

bool ShaderDiskCache::EnsureDirectories() const {
    const auto CreateDir = [](const std::string& dir) {
        if (!FileUtil::CreateDir(dir)) {
            LOG_ERROR(Render_Vulkan, "Failed to create directory={}", dir);
            return false;
        }
        return true;
    };

    return CreateDir(FileUtil::GetUserPath(FileUtil::UserPath::ShaderDir)) &&
           CreateDir(GetBaseDir()) && CreateDir(GetTransferableDir()) &&
           CreateDir(GetPrecompiledDir());
}

std::string ShaderDiskCache::GetTransferablePath() {
    return FileUtil::SanitizePath(GetTransferableDir() + DIR_SEP_CHR + GetTitleID() + ".bin");
}

std::string ShaderDiskCache::GetPrecompiledPath() {
    return FileUtil::SanitizePath(GetPrecompiledDir() + DIR_SEP_CHR + GetTitleID() + ".bin");
}

std::string ShaderDiskCache::GetTransferableDir() const {
    return GetBaseDir() + DIR_SEP "transferable";
}

std::string ShaderDiskCache::GetPrecompiledDir() const {
    return GetBaseDir() + DIR_SEP "precompiled";
}

std::string ShaderDiskCache::GetBaseDir() const {
    return FileUtil::GetUserPath(FileUtil::UserPath::ShaderDir) + DIR_SEP "vulkan";
}

u64 ShaderDiskCache::GetProgramID() const {
    return program_id;
}

std::string ShaderDiskCache::GetTitleID() {
    if (!title_id.empty()) {
        return title_id;
    }
    title_id = fmt::format("{:016X}", GetProgramID());
    return title_id;
}

} // namespace Vulkan
