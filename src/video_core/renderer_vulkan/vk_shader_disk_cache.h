// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <mutex>
#include <optional>
#include <unordered_set>
#include "common/common_types.h"
#include "common/file_util.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/shader/generator/shader_gen.h"

namespace Vulkan {

class PipelineCache;

class ShaderDiskCache {
public:
    ShaderDiskCache(PipelineCache& _parent, bool _accurate_mul)
        : parent(_parent), accurate_mul(_accurate_mul) {}

    void Init(u64 title_id, const std::atomic_bool& stop_loading,
              const VideoCore::DiskResourceLoadCallback& callback);

    std::optional<std::pair<size_t, Shader* const>> UseProgrammableVertexShader(
        const Pica::RegsInternal& regs, Pica::ShaderSetup& setup, const VertexLayout& layout);

private:
    static constexpr std::size_t SOURCE_FILE_HASH_LENGTH = 64;
    using SourceFileCacheVersionHash = std::array<u8, SOURCE_FILE_HASH_LENGTH>;

    static SourceFileCacheVersionHash GetSourceFileCacheVersionHash();

    enum class CacheFileType : u32 {
        VS_CACHE = 0,
        FS_CACHE = 1,

        MAX,
    };

    enum class CacheEntryType : u16 {
        FILE_INFO = 0,
        VS_CONFIG = 1,
        VS_PROGRAM = 2,
        VS_SPIRV = 3,

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

        struct VSProgramDriverUserSettings {
            u8 accurate_mul;
            u8 disable_spirv_optimize;
            u8 clip_distance_supported;
            u8 use_geometry_shaders;
            u8 fragment_barycentric_supported;
            std::array<FormatTraits, 16> traits{};

            auto operator<=>(const VSProgramDriverUserSettings&) const = default;
        };
        static_assert(sizeof(VSProgramDriverUserSettings) == 328);

        union {
            u8 reserved[0x400];
            VSProgramDriverUserSettings vs_settings;
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

    class CacheFile;
    class CacheEntry {
    public:
        static constexpr u32 MAX_ENTRY_SIZE = 1 * 1024 * 1024;

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
        CacheEntry ReadLast();
        CacheEntry ReadNext(const CacheEntry& previous);
        CacheEntry ReadPrevious(const CacheEntry& next);

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

    std::string GetFSDir() const;
    std::string GetVSDir() const;
    std::string GetVSFile(u64 title_id, bool is_temp) const;

    bool RecreateVSCache(CacheFile& file);
    bool InitVSCache(u64 title_id, const std::atomic_bool& stop_loading,
                     const VideoCore::DiskResourceLoadCallback& callback);

    bool AppendVSConfigProgram(CacheFile& file, const Pica::Shader::Generator::PicaVSConfig& config,
                               const Pica::ShaderSetup& setup, u64 config_id, u64 program_id);

    bool AppendVSProgram(CacheFile& file, const VSProgramEntry& entry, u64 program_id);
    bool AppendVSConfig(CacheFile& file, const VSConfigEntry& entry, u64 config_id);

    bool AppendVSSPIRV(CacheFile& file, std::span<const u32> program, u64 program_id);

    CacheFile vs_cache;

    PipelineCache& parent;
    bool accurate_mul;

    std::unordered_map<size_t, Shader> programmable_vertex_cache;
    std::unordered_map<size_t, Shader*> programmable_vertex_map;
    std::unordered_set<size_t> known_vertex_programs;
};

} // namespace Vulkan