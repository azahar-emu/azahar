// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "common/static_lru_cache.h"
#include "common/zstd_compression.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_shader_disk_cache.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/shader/generator/glsl_shader_gen.h"
#include "video_core/shader/generator/shader_gen.h"

#define MALFORMED_DISK_CACHE                                                                       \
    do {                                                                                           \
        LOG_ERROR(Render_Vulkan, "Malformed disk shader cache");                                   \
        cleanup_on_error();                                                                        \
        return false;                                                                              \
    } while (0)

namespace Vulkan {

using namespace Pica::Shader::Generator;

void ShaderDiskCache::Init(u64 title_id, const std::atomic_bool& stop_loading,
                           const VideoCore::DiskResourceLoadCallback& callback) {
    if (!InitVSCache(title_id, stop_loading, callback)) {
        RecreateVSCache(vs_cache);
    }
}

std::optional<std::pair<size_t, Shader* const>> ShaderDiskCache::UseProgrammableVertexShader(
    const Pica::RegsInternal& regs, Pica::ShaderSetup& setup, const VertexLayout& layout) {

    PicaVSConfig config{regs, setup};

    // Transfer vertex attributes to the VS config
    config.state.used_input_vertex_attributes = layout.attribute_count;
    for (u32 i = 0; i < layout.attribute_count; i++) {
        auto& dst = config.state.input_vertex_attributes[i];
        const auto& src = layout.attributes[i];

        dst.location = src.location;
        dst.type = static_cast<u8>(src.type.Value());
        dst.size = src.size;
    }

    const auto config_hash = config.Hash();

    const auto [iter_config, new_config] = programmable_vertex_map.try_emplace(config_hash);
    if (new_config) {

        ExtraVSConfig extra_config = parent.CalcExtraConfig(config);

        auto program =
            Common::HashableString(GLSL::GenerateVertexShader(setup, config, extra_config));

        if (program.empty()) {
            LOG_ERROR(Render_Vulkan, "Failed to retrieve programmable vertex shader");
            programmable_vertex_map.erase(config_hash);
            return {};
        }

        const size_t spirv_id = program.Hash();

        auto [iter_prog, new_program] =
            programmable_vertex_cache.try_emplace(spirv_id, parent.instance);
        auto& shader = iter_prog->second;

        if (new_program) {
            shader.program = std::move(program);
            const vk::Device device = parent.instance.GetDevice();
            parent.workers.QueueWork([device, &shader, this, spirv_id] {
                auto spirv = CompileGLSL(shader.program, vk::ShaderStageFlagBits::eVertex);
                AppendVSSPIRV(vs_cache, spirv, spirv_id);
                shader.program.clear();
                shader.module = CompileSPV(spirv, device);
                shader.MarkDone();
            });
        }

        AppendVSConfigProgram(vs_cache, config, setup, config_hash, spirv_id);

        iter_config->second = &shader;
    }

    Shader* const shader{iter_config->second};
    if (!shader) {
        LOG_ERROR(Render_Vulkan, "Failed to retrieve programmable vertex shader");
        return {};
    }

    return std::make_pair(config_hash, shader);
}

ShaderDiskCache::SourceFileCacheVersionHash ShaderDiskCache::GetSourceFileCacheVersionHash() {
    SourceFileCacheVersionHash hash{};
    const std::size_t length = std::min(std::strlen(Common::g_shader_cache_version), hash.size());
    std::memcpy(hash.data(), Common::g_shader_cache_version, length);
    return hash;
}

ShaderDiskCache::CacheEntry ShaderDiskCache::CacheFile::ReadFirst() {
    return ReadAt(0);
}

ShaderDiskCache::CacheEntry ShaderDiskCache::CacheFile::ReadLast() {
    const size_t file_size = file.GetSize();
    CacheEntry::CacheEntryFooter footer{};

    if (file.ReadAtArray(&footer, 1, file_size - sizeof(footer)) == sizeof(footer) &&
        footer.version == CacheEntry::CacheEntryFooter::ENTRY_VERSION) {
        return ReadAt(file_size - footer.entry_size);
    }

    return CacheEntry();
}

ShaderDiskCache::CacheEntry ShaderDiskCache::CacheFile::ReadNext(const CacheEntry& previous) {
    if (!previous.valid)
        return CacheEntry();

    return ReadAt(previous.position + previous.header.entry_size);
}

ShaderDiskCache::CacheEntry ShaderDiskCache::CacheFile::ReadPrevious(const CacheEntry& next) {
    CacheEntry::CacheEntryFooter footer{};

    if (!next.valid || next.position < sizeof(footer))
        return CacheEntry();

    if (file.ReadAtArray(&footer, 1, next.position - sizeof(footer)) == sizeof(footer) &&
        footer.version == CacheEntry::CacheEntryFooter::ENTRY_VERSION) {
        return ReadAt(next.position - footer.entry_size);
    }
}

std::pair<size_t, ShaderDiskCache::CacheEntry::CacheEntryHeader>
ShaderDiskCache::CacheFile::ReadNextHeader(
    const ShaderDiskCache::CacheEntry::CacheEntryHeader& previous, size_t previous_position) {

    size_t new_pos = previous_position + previous.entry_size;

    return {new_pos, ReadAtHeader(new_pos)};
}

ShaderDiskCache::CacheEntry::CacheEntryHeader ShaderDiskCache::CacheFile::ReadAtHeader(
    size_t position) {

    CacheEntry::CacheEntryHeader header;

    if (file.ReadAtArray(&header, 1, position) == sizeof(CacheEntry::CacheEntryHeader)) {
        return header;
    }

    return CacheEntry::CacheEntryHeader();
}

ShaderDiskCache::CacheEntry ShaderDiskCache::CacheFile::ReadAt(size_t position) {
    CacheEntry res{};
    res.position = position;

    constexpr u32 headers_size =
        sizeof(CacheEntry::CacheEntryHeader) + sizeof(CacheEntry::CacheEntryFooter);

    res.header = ReadAtHeader(position);

    if (res.header.Valid()) {

        // We have everything validated, read the data.
        u32 payload_size = res.header.entry_size - headers_size;
        std::vector<u8> payload(payload_size);

        if (file.ReadAtBytes(payload.data(), payload_size,
                             position + sizeof(CacheEntry::CacheEntryHeader)) == payload_size) {
            // Decompress data if needed
            if (res.header.zstd_compressed) {
                if (Common::Compression::GetDecompressedSize(payload) <
                    CacheEntry::MAX_ENTRY_SIZE) {
                    res.data = Common::Compression::DecompressDataZSTD(payload);
                    res.valid = true;
                }
            } else {
                res.data = std::move(payload);
                res.valid = true;
            }
        }
    }
    return res;
}

size_t ShaderDiskCache::CacheFile::GetTotalEntries() {
    if (biggest_entry_id != SIZE_MAX) {
        return biggest_entry_id + 1;
    }

    const size_t file_size = file.GetSize();
    CacheEntry::CacheEntryFooter footer{};

    if (file.ReadAtArray(&footer, 1, file_size - sizeof(footer)) == sizeof(footer) &&
        footer.version == CacheEntry::CacheEntryFooter::ENTRY_VERSION) {
        biggest_entry_id = footer.entry_id;
    }

    return biggest_entry_id + 1;
}

bool ShaderDiskCache::CacheFile::Append(CacheEntryType type, u64 id, std::span<const u8> data,
                                        bool compress) {
    std::scoped_lock lock(mutex);

    std::span<const u8> data_final;
    std::vector<u8> data_compress;

    CacheEntry::CacheEntryHeader header{};
    CacheEntry::CacheEntryFooter footer{};

    constexpr u32 headers_size =
        sizeof(CacheEntry::CacheEntryHeader) + sizeof(CacheEntry::CacheEntryFooter);

    if (compress) {
        data_compress = Common::Compression::CompressDataZSTDDefault(data);
        data_final = data_compress;
        header.zstd_compressed.Assign(true);
    } else {
        data_final = data;
    }
    header.entry_version = CacheEntry::CacheEntryHeader::ENTRY_VERSION;
    footer.version.Assign(CacheEntry::CacheEntryFooter::ENTRY_VERSION);
    header.entry_size = footer.entry_size = data_final.size() + headers_size;
    footer.entry_id.Assign(biggest_entry_id++);

    header.type = type;
    header.id = id;

    std::vector<u8> out_data(data_final.size() + headers_size);
    memcpy(out_data.data(), &header, sizeof(header));
    memcpy(out_data.data() + sizeof(header), data_final.data(), data_final.size());
    memcpy(out_data.data() + sizeof(header) + data_final.size(), &footer, sizeof(footer));

    return file.WriteBytes(out_data.data(), out_data.size()) == out_data.size();
}

bool ShaderDiskCache::CacheFile::SwitchMode(CacheOpMode mode) {
    switch (mode) {
    case CacheOpMode::READ: {
        file = FileUtil::IOFile(filepath, "rb");
        bool is_open = file.IsOpen();
        if (is_open) {
            GetTotalEntries();
        }
        return is_open;
    }
    case CacheOpMode::APPEND: {
        GetTotalEntries();
        file.Close();
        if (biggest_entry_id == SIZE_MAX) {
            // Cannot append if getting total items fails
            return false;
        }

        file = FileUtil::IOFile(filepath, "ab");
        return file.IsOpen();
    }
    case CacheOpMode::DELETE: {
        biggest_entry_id = 0;
        file.Close();
        FileUtil::Delete(filepath);
        return true;
    }
    case CacheOpMode::RECREATE: {
        SwitchMode(CacheOpMode::DELETE);
        return SwitchMode(CacheOpMode::APPEND);
    }
    default:
        UNREACHABLE();
    }
    return false;
}

std::string ShaderDiskCache::GetVSDir() const {
    return parent.GetVulkanDir() + DIR_SEP + "vertex";
}

std::string ShaderDiskCache::GetFSDir() const {
    return parent.GetVulkanDir() + DIR_SEP + "fragment";
}

std::string ShaderDiskCache::GetVSFile(u64 title_id, bool is_temp) const {
    return GetVSDir() + DIR_SEP + fmt::format("{:016X}", title_id) + (is_temp ? "_temp" : "") +
           ".vkch";
}

bool ShaderDiskCache::RecreateVSCache(CacheFile& file) {
    file.SwitchMode(CacheFile::CacheOpMode::RECREATE);

    std::array<char, 0x20> build_name{};
    size_t name_len = std::strlen(Common::g_build_fullname);
    memcpy(build_name.data(), Common::g_build_fullname, std::min(name_len, build_name.size()));

    FileInfoEntry entry{
        .cache_magic = FileInfoEntry::CACHE_FILE_MAGIC,
        .file_version = FileInfoEntry::CACHE_FILE_VERSION,
        .config_struct_hash = PicaVSConfigState::StructHash(),
        .file_type = CacheFileType::VS_CACHE,
        .source_hash = GetSourceFileCacheVersionHash(),
        .build_name = build_name,
        .vs_settings = {.accurate_mul = accurate_mul,
                        .disable_spirv_optimize =
                            Settings::values.disable_spirv_optimizer.GetValue(),
                        .clip_distance_supported = parent.instance.IsShaderClipDistanceSupported(),
                        .use_geometry_shaders = parent.instance.UseGeometryShaders(),
                        .fragment_barycentric_supported =
                            parent.instance.IsFragmentShaderBarycentricSupported(),
                        .traits = parent.instance.GetAllTraits()},
    };

    file.Append(CacheEntryType::FILE_INFO, 0, entry, false);
    return true;
}

bool ShaderDiskCache::InitVSCache(u64 title_id, const std::atomic_bool& stop_loading,
                                  const VideoCore::DiskResourceLoadCallback& callback) {
    std::vector<size_t> pending_configs;
    std::unordered_map<size_t, size_t> pending_programs;
    std::unique_ptr<Pica::ShaderSetup> shader_setup;
    std::unique_ptr<CacheFile> regenerate_file;

    auto cleanup_on_error = [&]() {
        programmable_vertex_cache.clear();
        programmable_vertex_map.clear();
        known_vertex_programs.clear();
        if (regenerate_file) {
            regenerate_file->SwitchMode(CacheFile::CacheOpMode::DELETE);
        }
    };

    LOG_INFO(Render_Vulkan, "Loading VS disk shader cache for title {:016X}", title_id);

    vs_cache.SetFilePath(GetVSFile(title_id, false));

    if (!vs_cache.SwitchMode(CacheFile::CacheOpMode::READ)) {
        LOG_INFO(Render_Vulkan, "Missing shader disk cache for title {:016X}", title_id);
        cleanup_on_error();
        return false;
    }

    u32 tot_entries = vs_cache.GetTotalEntries();
    auto curr = vs_cache.ReadFirst();
    if (!curr.Valid() || curr.Type() != CacheEntryType::FILE_INFO) {
        MALFORMED_DISK_CACHE;
    }

    const FileInfoEntry* file_info = curr.Payload<FileInfoEntry>();
    if (!file_info || file_info->cache_magic != FileInfoEntry::CACHE_FILE_MAGIC ||
        file_info->file_version != FileInfoEntry::CACHE_FILE_VERSION ||
        file_info->file_type != CacheFileType::VS_CACHE) {
        MALFORMED_DISK_CACHE;
    }

    if (file_info->config_struct_hash != PicaVSConfigState::StructHash()) {
        LOG_ERROR(Render_Vulkan,
                  "Cache was created for a different PicaVSConfigState, resetting...");
        cleanup_on_error();
        return false;
    }

    if (file_info->source_hash != GetSourceFileCacheVersionHash()) {
        LOG_INFO(Render_Vulkan, "Cache contains old vertex program, cache needs regeneration.");
        regenerate_file = std::make_unique<CacheFile>(GetVSFile(title_id, true));
    }

    {
        const FileInfoEntry::VSProgramDriverUserSettings settings{
            .accurate_mul = accurate_mul,
            .disable_spirv_optimize = Settings::values.disable_spirv_optimizer.GetValue(),
            .clip_distance_supported = parent.instance.IsShaderClipDistanceSupported(),
            .use_geometry_shaders = parent.instance.UseGeometryShaders(),
            .fragment_barycentric_supported =
                parent.instance.IsFragmentShaderBarycentricSupported(),
            .traits = parent.instance.GetAllTraits()};

        if (file_info->vs_settings != settings && !regenerate_file) {
            LOG_INFO(Render_Vulkan,
                     "Cache has driver and user settings mismatch, cache needs regeneration.");
            regenerate_file = std::make_unique<CacheFile>(GetVSFile(title_id, true));
        }
    }

    if (regenerate_file) {
        RecreateVSCache(*regenerate_file);
    }

    CacheEntry::CacheEntryHeader curr_header = curr.Header();
    size_t curr_offset = curr.Position();

    size_t current_callback_index = 0;
    size_t tot_callback_index = tot_entries - 1;

    // Scan the entire file first, while keeping track of configs and programs.
    // SPIRV can be compiled directly and will be linked to the proper config entries
    // later.
    for (int i = 1; i < tot_entries; i++) {

        std::tie(curr_offset, curr_header) = vs_cache.ReadNextHeader(curr_header, curr_offset);

        if (!curr_header.Valid()) {
            MALFORMED_DISK_CACHE;
        }

        LOG_DEBUG(Render_Vulkan, "Processing ID: {:016X} (type {})", curr_header.Id(),
                  curr_header.Type());

        if (curr_header.Type() == CacheEntryType::VS_CONFIG) {
            pending_configs.push_back(curr_offset);
        } else if (curr_header.Type() == CacheEntryType::VS_PROGRAM) {
            pending_programs.try_emplace(curr_header.Id(), curr_offset);

            // We won't use this entry sequentially again, so report progress.
            if (callback) {
                callback(VideoCore::LoadCallbackStage::Build, current_callback_index++,
                         tot_callback_index, "Vertex Shader");
            }
        } else if (curr_header.Type() == CacheEntryType::VS_SPIRV) {

            // Only use SPIRV entries if we are not regenerating the cache, as the driver or
            // user settings do not match, which could lead to different SPIRV.
            // These will be re-created from the cached config and programs later.
            if (!regenerate_file) {
                LOG_DEBUG(Render_Vulkan, "    processing SPIRV.");

                curr = vs_cache.ReadAt(curr_offset);
                if (!curr.Valid() || curr.Type() != CacheEntryType::VS_SPIRV) {
                    MALFORMED_DISK_CACHE;
                }

                const u8* spirv_data = curr.Data().data();
                const size_t spirv_size = curr.Data().size();

                auto [iter_prog, new_program] =
                    programmable_vertex_cache.try_emplace(curr.Id(), parent.instance);
                if (new_program) {
                    LOG_DEBUG(Render_Vulkan, "    compiling SPIRV.");

                    const auto spirv = std::span<const u32>(
                        reinterpret_cast<const u32*>(spirv_data), spirv_size / sizeof(u32));

                    iter_prog->second.module = CompileSPV(spirv, parent.instance.GetDevice());
                    iter_prog->second.MarkDone();

                    if (!iter_prog->second.module) {
                        // Compilation failed for some reason, remove from cache to let it
                        // be re-generated at runtime or during config and program processing.
                        LOG_ERROR(Render_Vulkan, "Unexpected program compilation failure");
                        programmable_vertex_cache.erase(iter_prog);
                    }
                }
            }

            if (callback) {
                callback(VideoCore::LoadCallbackStage::Build, current_callback_index++,
                         tot_callback_index, "Vertex Shader");
            }
        } else {
            MALFORMED_DISK_CACHE;
        }
    }

    // Once we have all the shader instances created from SPIRV, we can link them to the VS configs.
    LOG_DEBUG(Render_Vulkan, "Linking with config entries.");

    // Mmultiple config entries may point to the same program entry. We could load all program
    // entries to memory to prevent having to read them from disk on every config entry, but program
    // entries are pretty big (around 50KB each). A LRU cache is a middle point between disk access
    // and memory usage.
    std::unique_ptr<Common::StaticLRUCache<size_t, VSProgramEntry, 10>> program_lru =
        std::make_unique<Common::StaticLRUCache<size_t, VSProgramEntry, 10>>();

    for (auto& offset : pending_configs) {
        if (callback) {
            callback(VideoCore::LoadCallbackStage::Build, current_callback_index++,
                     tot_callback_index, "Vertex Shader");
        }

        curr = vs_cache.ReadAt(offset);
        const VSConfigEntry* entry;

        if (!curr.Valid() || curr.Type() != CacheEntryType::VS_CONFIG ||
            !(entry = curr.Payload<VSConfigEntry>()) ||
            entry->version != VSConfigEntry::EXPECTED_VERSION) {
            MALFORMED_DISK_CACHE;
        }

        if (curr.Id() != entry->vs_config.Hash()) {
            LOG_ERROR(Render_Vulkan, "Unexpected PicaVSConfig hash mismatch");
            continue;
        }

        LOG_DEBUG(Render_Vulkan, "Linking {:016X}.", curr.Id());

        auto [iter_config, new_config] = programmable_vertex_map.try_emplace(curr.Id());
        if (new_config) {
            // New config entry, usually always taken unless there is duplicate entries on the cache
            // for some reason.

            auto shader_it = programmable_vertex_cache.find(entry->spirv_entry_id);
            if (shader_it != programmable_vertex_cache.end()) {
                // The config entry uses a SPIRV entry that was already compiled (this is the usual
                // path when the cache doesn't need to be re-generated).

                LOG_DEBUG(Render_Vulkan, "    linked with existing SPIRV {:016X}.",
                          entry->spirv_entry_id);

                iter_config->second = &shader_it->second;

                if (regenerate_file) {
                    // In case we are re-generating the cache, we could only have gotten here if the
                    // SPIRV was already compiled and cached, so only cache the config.
                    AppendVSConfig(*regenerate_file, *entry, curr.Id());
                }

                bool new_program = known_vertex_programs.emplace(entry->program_entry_id).second;
                if (new_program && regenerate_file) {
                    // If the vertex program is not known at this point we need to save it as well.
                    // This can happen to config entries that compile to the same SPIRV but use
                    // different program code (maybe because garbage data was in the program
                    // buffer).
                    auto program_it = pending_programs.find(entry->program_entry_id);
                    if (program_it == pending_programs.end()) {
                        // Program code not in disk cache, should never happen.
                        LOG_ERROR(Render_Vulkan, "Missing program code for config entry");
                        programmable_vertex_map.erase(iter_config);
                        continue;
                    }

                    // This is very rare so no need to use the LRU.
                    auto program_cache_entry = vs_cache.ReadAt(program_it->second);
                    const VSProgramEntry* program_entry;

                    if (!program_cache_entry.Valid() ||
                        program_cache_entry.Type() != CacheEntryType::VS_PROGRAM ||
                        !(program_entry = program_cache_entry.Payload<VSProgramEntry>()) ||
                        program_entry->version != VSProgramEntry::EXPECTED_VERSION) {
                        MALFORMED_DISK_CACHE;
                    }

                    AppendVSProgram(*regenerate_file, *program_entry, entry->program_entry_id);
                }
            } else {
                // Cached SPIRV not found, need to recompile.

                // Search program entry in a LRU first, to prevent having to read from the cache
                // file on each separate config entry.
                auto [found, program_lru_entry] = program_lru->request(entry->program_entry_id);
                if (!found) {
                    LOG_DEBUG(Render_Vulkan, "    reading program {:016X}.",
                              entry->program_entry_id);

                    // Program not on the LRU, need to read it from cache file
                    auto program_it = pending_programs.find(entry->program_entry_id);
                    if (program_it == pending_programs.end()) {
                        // Program code not in disk cache, should never happen.
                        LOG_ERROR(Render_Vulkan, "Missing program code for config entry");
                        programmable_vertex_map.erase(iter_config);
                        continue;
                    }

                    auto program_cache_entry = vs_cache.ReadAt(program_it->second);
                    const VSProgramEntry* program_entry;

                    if (!program_cache_entry.Valid() ||
                        program_cache_entry.Type() != CacheEntryType::VS_PROGRAM ||
                        !(program_entry = program_cache_entry.Payload<VSProgramEntry>()) ||
                        program_entry->version != VSProgramEntry::EXPECTED_VERSION) {
                        MALFORMED_DISK_CACHE;
                    }

                    program_lru_entry = *program_entry;

                    bool new_program =
                        known_vertex_programs.emplace(entry->program_entry_id).second;

                    if (new_program && regenerate_file) {
                        // When regenerating, only append if it's a new program entry not seen
                        // before.
                        AppendVSProgram(*regenerate_file, program_lru_entry,
                                        entry->program_entry_id);
                    }
                }

                // Recompile SPIRV from config and program now.
                LOG_DEBUG(Render_Vulkan, "    using program {:016X}.", entry->program_entry_id);

                shader_setup = std::make_unique<Pica::ShaderSetup>();
                shader_setup->UpdateProgramCode(program_lru_entry.program_code,
                                                program_lru_entry.program_len);
                shader_setup->UpdateSwizzleData(program_lru_entry.swizzle_code,
                                                program_lru_entry.swizzle_len);
                shader_setup->DoProgramCodeFixup();

                if (entry->vs_config.state.program_hash != shader_setup->GetProgramCodeHash() ||
                    entry->vs_config.state.swizzle_hash != shader_setup->GetSwizzleDataHash()) {
                    LOG_ERROR(Render_Vulkan, "Unexpected ShaderSetup hash mismatch");
                    programmable_vertex_map.erase(iter_config);
                    continue;
                }

                ExtraVSConfig extra_config = parent.CalcExtraConfig(entry->vs_config);

                auto program_glsl = Common::HashableString(
                    GLSL::GenerateVertexShader(*shader_setup, entry->vs_config, extra_config));
                if (program_glsl.empty()) {
                    LOG_ERROR(Render_Vulkan, "Failed to retrieve programmable vertex shader");
                    programmable_vertex_map.erase(iter_config);
                    continue;
                }

                const u64 spirv_id = program_glsl.Hash();

                auto [iter_prog, new_spirv] =
                    programmable_vertex_cache.try_emplace(spirv_id, parent.instance);

                LOG_DEBUG(Render_Vulkan, "    processing SPIRV.");

                if (new_spirv) {
                    LOG_DEBUG(Render_Vulkan, "    compiling SPIRV.");

                    auto spirv = CompileGLSL(program_glsl, vk::ShaderStageFlagBits::eVertex);

                    iter_prog->second.module = CompileSPV(spirv, parent.instance.GetDevice());
                    iter_prog->second.MarkDone();

                    if (regenerate_file) {
                        // If we are regenerating, save the new spirv to disk.
                        AppendVSSPIRV(*regenerate_file, spirv, spirv_id);
                    }
                }

                if (regenerate_file) {
                    // If we are regenerating, save the config entry to the cache. We need to make a
                    // copy first because it's possible the SPIRV id has changed and we need to
                    // adjust it.
                    std::unique_ptr<VSConfigEntry> entry_copy =
                        std::make_unique<VSConfigEntry>(*entry);
                    entry_copy->spirv_entry_id = spirv_id;
                    AppendVSConfig(*regenerate_file, *entry_copy, curr.Id());
                }

                // Asign the SPIRV shader to the config
                iter_config->second = &iter_prog->second;

                LOG_DEBUG(Render_Vulkan, "    linked with new SPIRV {:016X}.",
                          entry->spirv_entry_id);
            }
        }
    }

    if (regenerate_file) {
        // If we are regenerating, replace the old file with the new one.
        vs_cache.SwitchMode(CacheFile::CacheOpMode::DELETE);
        regenerate_file.reset();
        FileUtil::Rename(GetVSFile(title_id, true), GetVSFile(title_id, false));
    }

    // Switch to append mode to receiving new entries.
    vs_cache.SwitchMode(CacheFile::CacheOpMode::APPEND);
    return true;
}

bool ShaderDiskCache::AppendVSConfigProgram(CacheFile& file,
                                            const Pica::Shader::Generator::PicaVSConfig& config,
                                            const Pica::ShaderSetup& setup, u64 config_id,
                                            u64 spirv_id) {

    VSConfigEntry entry;
    entry.version = VSConfigEntry::EXPECTED_VERSION;
    entry.vs_config = config;
    entry.spirv_entry_id = spirv_id;
    entry.program_entry_id =
        Common::HashCombine(config.state.program_hash, config.state.swizzle_hash);

    bool new_entry = known_vertex_programs.emplace(entry.program_entry_id).second;
    bool prog_res = true;
    if (new_entry) {
        VSProgramEntry prog_entry;
        prog_entry.version = VSProgramEntry::EXPECTED_VERSION;
        prog_entry.program_len = setup.GetBiggestProgramSize();
        prog_entry.program_code = setup.GetProgramCode();
        prog_entry.swizzle_len = setup.GetBiggestSwizzleSize();
        prog_entry.swizzle_code = setup.GetSwizzleData();

        prog_res = AppendVSProgram(file, prog_entry, entry.program_entry_id);
    }

    return AppendVSConfig(file, entry, config_id) && prog_res;
}

bool ShaderDiskCache::AppendVSProgram(CacheFile& file, const VSProgramEntry& entry,
                                      u64 program_id) {
    return file.Append(CacheEntryType::VS_PROGRAM, program_id, entry, true);
}

bool ShaderDiskCache::AppendVSConfig(CacheFile& file, const VSConfigEntry& entry, u64 config_id) {
    return file.Append(CacheEntryType::VS_CONFIG, config_id, entry, true);
}

bool ShaderDiskCache::AppendVSSPIRV(CacheFile& file, std::span<const u32> program, u64 program_id) {
    return file.Append(CacheEntryType::VS_SPIRV, program_id,
                       {reinterpret_cast<const u8*>(program.data()), program.size() * sizeof(u32)},
                       true);
}

} // namespace Vulkan