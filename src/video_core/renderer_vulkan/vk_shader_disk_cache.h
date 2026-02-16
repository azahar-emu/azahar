// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <mutex>
#include <optional>
#include <unordered_set>
#include <tsl/robin_map.h>

#include "common/common_types.h"
#include "common/file_util.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/shader/generator/pica_fs_config.h"
#include "video_core/shader/generator/profile.h"
#include "video_core/shader/generator/shader_gen.h"

namespace Vulkan {

class PipelineCache;

class ShaderDiskCache {
public:
    ShaderDiskCache(PipelineCache& _parent, u64 _title_id) : parent(_parent), title_id(_title_id) {}

    void Init(const std::atomic_bool& stop_loading,
              const VideoCore::DiskResourceLoadCallback& callback);

    std::optional<std::pair<u64, Shader* const>> UseProgrammableVertexShader(
        const Pica::RegsInternal& regs, Pica::ShaderSetup& setup, const VertexLayout& layout);
    std::optional<std::pair<u64, Shader* const>> UseFragmentShader(
        const Pica::RegsInternal& regs, const Pica::Shader::UserConfig& user);
    std::optional<std::pair<u64, Shader* const>> UseFixedGeometryShader(
        const Pica::RegsInternal& regs);

    GraphicsPipeline* GetPipeline(const PipelineInfo& info);

    u64 GetProgramID() const {
        return title_id;
    }

private:
    static constexpr std::size_t SOURCE_FILE_HASH_LENGTH = 64;
    using SourceFileCacheVersionHash = std::array<u8, SOURCE_FILE_HASH_LENGTH>;

    static SourceFileCacheVersionHash GetSourceFileCacheVersionHash();

    enum class CacheFileType : u32 {
        VS_CACHE = 0,
        FS_CACHE = 1,
        GS_CACHE = 2,
        PL_CACHE = 3,

        MAX,
    };

    enum class CacheEntryType : u16 {
        // Common
        FILE_INFO = 0,

        // VS_CACHE
        VS_CONFIG = 1,
        VS_PROGRAM = 2,
        VS_SPIRV = 3,

        // FS_CACHE
        FS_CONFIG = 4,
        FS_SPIRV = 5,

        // GS_CACHE
        GS_CONFIG = 6,
        GS_SPIRV = 7,

        // PL_CACHE
        PL_CONFIG = 8,

        MAX,
    };

    struct FileInfoEntry {
        static constexpr u32 CACHE_FILE_MAGIC = 0x48434B56;
        static constexpr u32 CACHE_FILE_VERSION = 0;

        u32_le cache_magic;
        u32 file_version;
        u64 config_struct_hash;
        CacheFileType file_type;
        SourceFileCacheVersionHash source_hash;
        std::array<char, 0x20> build_name;

        union {
            u8 reserved[0x400];
            Pica::Shader::Profile profile;
        };
    };
    static_assert(sizeof(FileInfoEntry) == 1144);

    struct VSConfigEntry {
        static constexpr u8 EXPECTED_VERSION = 0;

        u8 version; // Surprise tool that can help us later
        u64 program_entry_id;
        u64 spirv_entry_id;
        Pica::Shader::Generator::PicaVSConfig vs_config;
    };
    static_assert(sizeof(VSConfigEntry) == 216);

    struct VSProgramEntry {
        static constexpr u8 EXPECTED_VERSION = 0;

        u8 version; // Surprise tool that can help us later
        u32 program_len;
        u32 swizzle_len;
        Pica::ProgramCode program_code;
        Pica::SwizzleData swizzle_code;
    };
    static_assert(sizeof(VSProgramEntry) == 32780);

    struct FSConfigEntry {
        static constexpr u8 EXPECTED_VERSION = 0;

        u8 version; // Surprise tool that can help us later
        Pica::Shader::FSConfig fs_config;
    };
    static_assert(sizeof(FSConfigEntry) == 276);

    struct GSConfigEntry {
        static constexpr u8 EXPECTED_VERSION = 0;

        u8 version; // Surprise tool that can help us later
        Pica::Shader::Generator::PicaFixedGSConfig gs_config;
    };
    static_assert(sizeof(GSConfigEntry) == 44);

    struct PLConfigEntry {
        static constexpr u8 EXPECTED_VERSION = 0;

        u8 version; // Surprise tool that can help us later
        StaticPipelineInfo pl_info;
    };
    static_assert(sizeof(PLConfigEntry) == 152);

    class CacheFile;
    class CacheEntry {
    public:
        static constexpr u32 MAX_ENTRY_SIZE = 4 * 1024 * 1024;

        struct CacheEntryFooter {
            static constexpr u8 ENTRY_VERSION = 0x24;
            union {
                u32 first_word{};

                BitField<0, 8, u32> version;
                BitField<8, 24, u32> entry_id;
            };
            u32 entry_size{};
            u64 reserved{};
        };
        static_assert(sizeof(CacheEntryFooter) == 0x10);

        struct CacheEntryHeader {
            static constexpr u8 ENTRY_VERSION = 0x42;
            u8 entry_version{};
            union {
                u8 flags{};

                BitField<0, 1, u8> zstd_compressed;
                BitField<1, 7, u8> reserved;
            };
            CacheEntryType type{};
            u32 entry_size{};
            u64 id{};

            CacheEntryType Type() const {
                return type;
            }

            u64 Id() const {
                return id;
            }

            bool Valid() {
                constexpr u32 headers_size =
                    sizeof(CacheEntry::CacheEntryHeader) + sizeof(CacheEntry::CacheEntryFooter);

                return entry_version == ENTRY_VERSION && type < CacheEntryType::MAX &&
                       entry_size < CacheEntry::MAX_ENTRY_SIZE && entry_size >= headers_size;
            }
        };
        static_assert(sizeof(CacheEntryHeader) == 0x10);

        bool Valid() const {
            return valid;
        }

        CacheEntryType Type() const {
            return header.Type();
        }

        u64 Id() const {
            return header.Id();
        }

        const std::span<const u8> Data() const {
            return data;
        }

        template <typename T>
        const T* Payload() const {
            if (data.size() != sizeof(T)) {
                return nullptr;
            }

            return reinterpret_cast<const T*>(data.data());
        }

        size_t Position() const {
            return position;
        }

        const CacheEntryHeader& Header() const {
            return header;
        }

    private:
        friend CacheFile;

        CacheEntry() = default;

        CacheEntryHeader header{};

        size_t position = SIZE_MAX;
        bool valid = false;
        std::vector<u8> data{};
    };

    class CacheFile {
    public:
        enum class CacheOpMode {
            READ,
            APPEND,
            DELETE,
            RECREATE,
        };

        CacheFile() = default;
        CacheFile(const std::string& _filepath) : filepath(_filepath) {}

        void SetFilePath(const std::string& path) {
            filepath = path;
        }

        CacheEntry ReadFirst();
        CacheEntry ReadNext(const CacheEntry& previous);

        CacheEntry ReadAt(size_t position);

        std::pair<size_t, CacheEntry::CacheEntryHeader> ReadNextHeader(
            const ShaderDiskCache::CacheEntry::CacheEntryHeader& previous,
            size_t previous_position);

        CacheEntry::CacheEntryHeader ReadAtHeader(size_t position);

        size_t GetTotalEntries();

        template <typename T>
        bool Append(CacheEntryType type, u64 id, const T& object, bool compress) {
            static_assert(std::is_trivially_copyable_v<T>);

            auto bytes = std::as_bytes(std::span{&object, 1});
            auto u8_span =
                std::span<const u8>(reinterpret_cast<const u8*>(bytes.data()), bytes.size());
            return Append(type, id, u8_span, compress);
        }

        bool Append(CacheEntryType type, u64 id, std::span<const u8> data, bool compress);

        bool SwitchMode(CacheOpMode mode);

    private:
        std::string filepath;
        std::mutex mutex;
        FileUtil::IOFile file{};
        size_t biggest_entry_id = SIZE_MAX;
    };

    std::string GetVSFile(u64 title_id, bool is_temp) const;
    std::string GetFSFile(u64 title_id, bool is_temp) const;
    std::string GetGSFile(u64 title_id, bool is_temp) const;
    std::string GetPLFile(u64 title_id, bool is_temp) const;

    bool RecreateCache(CacheFile& file, CacheFileType type);

    bool InitVSCache(const std::atomic_bool& stop_loading,
                     const VideoCore::DiskResourceLoadCallback& callback);

    bool InitFSCache(const std::atomic_bool& stop_loading,
                     const VideoCore::DiskResourceLoadCallback& callback);

    bool InitGSCache(const std::atomic_bool& stop_loading,
                     const VideoCore::DiskResourceLoadCallback& callback);

    bool InitPLCache(const std::atomic_bool& stop_loading,
                     const VideoCore::DiskResourceLoadCallback& callback);

    bool AppendVSConfigProgram(CacheFile& file, const Pica::Shader::Generator::PicaVSConfig& config,
                               const Pica::ShaderSetup& setup, u64 config_id, u64 program_id);
    bool AppendVSProgram(CacheFile& file, const VSProgramEntry& entry, u64 program_id);
    bool AppendVSConfig(CacheFile& file, const VSConfigEntry& entry, u64 config_id);
    bool AppendVSSPIRV(CacheFile& file, std::span<const u32> program, u64 program_id);

    bool AppendFSConfig(CacheFile& file, const FSConfigEntry& entry, u64 config_id);
    bool AppendFSSPIRV(CacheFile& file, std::span<const u32> program, u64 program_id);

    bool AppendGSConfig(CacheFile& file, const GSConfigEntry& entry, u64 config_id);
    bool AppendGSSPIRV(CacheFile& file, std::span<const u32> program, u64 program_id);

    bool AppendPLConfig(CacheFile& file, const PLConfigEntry& entry, u64 config_id);

    CacheFile vs_cache;
    CacheFile fs_cache;
    CacheFile gs_cache;
    CacheFile pl_cache;

    PipelineCache& parent;
    u64 title_id;

    std::unordered_map<u64, Shader> programmable_vertex_cache;
    std::unordered_map<u64, Shader*> programmable_vertex_map;
    std::unordered_set<u64> known_vertex_programs;

    std::unordered_map<u64, Shader> fragment_shaders;

    std::unordered_map<size_t, Shader> fixed_geometry_shaders;
    std::unordered_set<u64> known_geometry_shaders;

    tsl::robin_map<u64, std::unique_ptr<GraphicsPipeline>, Common::IdentityHash<u64>>
        graphics_pipelines;
    std::unordered_set<u64> known_graphic_pipelines;
};

} // namespace Vulkan