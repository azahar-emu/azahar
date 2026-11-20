// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "common/common_funcs.h"
#include "common/file_util.h"
#include "common/hash.h"
#include "common/thread_worker.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_metal/mtl_common.h"
#include "video_core/shader/generator/pica_fs_config.h"

namespace Pica {
struct RegsInternal;
struct ShaderSetup;
} // namespace Pica

namespace Metal {

class Instance;

/// Fixed function state baked into a MTLRenderPipelineState.
struct PipelineKey {
    u64 fs_hash;
    u64 vs_hash; ///< 0 for the software vertex path, see PipelineCache::UseVertexShader
    u32 color_format;   ///< MTLPixelFormat
    u32 depth_format;   ///< MTLPixelFormat
    u32 stencil_format; ///< MTLPixelFormat
    u8 blend_enable;
    u8 rgb_op;     ///< MTLBlendOperation
    u8 alpha_op;   ///< MTLBlendOperation
    u8 src_rgb;    ///< MTLBlendFactor
    u8 dst_rgb;    ///< MTLBlendFactor
    u8 src_alpha;  ///< MTLBlendFactor
    u8 dst_alpha;  ///< MTLBlendFactor
    u8 write_mask; ///< MTLColorWriteMask
    /// 1: no fragment function, the draw only writes depth and stencil (fs_hash is 0).
    u8 no_fragment;
    u8 pad[3];

    u64 Hash() const noexcept {
        return Common::ComputeHash64(this, sizeof(PipelineKey));
    }
};
static_assert(std::has_unique_object_representations_v<PipelineKey>);

/// Depth/stencil state baked into a MTLDepthStencilState.
struct DepthStencilKey {
    u8 depth_compare; ///< MTLCompareFunction
    u8 depth_write;
    u8 stencil_enable;
    u8 stencil_compare; ///< MTLCompareFunction
    u8 stencil_fail;    ///< MTLStencilOperation
    u8 depth_fail;      ///< MTLStencilOperation
    u8 depth_pass;      ///< MTLStencilOperation
    u8 read_mask;
    u8 write_mask;
    u8 pad[3];

    u64 Hash() const noexcept {
        return Common::ComputeHash64(this, sizeof(DepthStencilKey));
    }
};
static_assert(std::has_unique_object_representations_v<DepthStencilKey>);

/**
 * Generates, compiles and caches the MSL programs and the pipeline/depth-stencil state objects
 * used by the rasterizer.
 *
 * Programs and pipelines can be built on worker threads (async shader compilation), in which case
 * GetPipeline returns nil until the pipeline is ready and the draw is skipped. Every program and
 * pipeline that is built is also recorded in a per title disk cache, which is built in advance
 * the next time the title boots, and the GPU binaries of the pipelines are kept in a
 * MTLBinaryArchive so building them again is cheap.
 */
class PipelineCache {
public:
    struct Shader;
    using ShaderHandle = std::shared_ptr<Shader>;

    explicit PipelineCache(const Instance& instance, MTLVertexDescriptor* vertex_descriptor);
    ~PipelineCache();

    /// Returns the shader generator profile.
    const Pica::Shader::Profile& GetProfile() const;

    /// Whether the depth of the current draw is mapped in the vertex shader (see
    /// MSL::VSDepthData), which leaves the fragment shader without a [[depth]] output.
    bool UsesVertexDepth(const Pica::RegsInternal& regs, bool has_depth_attachment) const;

    /// Returns the fragment program for the current PICA state, generating it if it was never
    /// seen before. Compilation happens when a pipeline using it is built.
    ShaderHandle UseFragmentShader(const Pica::RegsInternal& regs,
                                   const Pica::Shader::UserConfig& user, bool has_color_attachment,
                                   bool has_depth_attachment);

    /**
     * Returns the hardware vertex program translated from the current PICA vertex shader. Returns
     * nullptr if the program cannot be translated or failed to compile, in which case the draw has
     * to use the software vertex path.
     */
    ShaderHandle UseVertexShader(const Pica::RegsInternal& regs, Pica::ShaderSetup& setup);

    /// The vertex program of the software vertex path.
    const ShaderHandle& TrivialVertexShader() const {
        return trivial_vertex_shader;
    }

    /// Hash identifying a program, the value to store in PipelineKey::vs_hash/fs_hash.
    static u64 HashOf(const ShaderHandle& shader);

    /**
     * Returns the pipeline state for the key and programs. With `wait` false and async shader
     * compilation enabled, a pipeline that is not built yet is queued to the workers and nil is
     * returned. Also returns nil if building the pipeline failed.
     */
    id<MTLRenderPipelineState> GetPipeline(const PipelineKey& key, const ShaderHandle& vertex,
                                           const ShaderHandle& fragment, bool wait);

    /// Returns the depth/stencil state for the provided key.
    id<MTLDepthStencilState> GetDepthStencilState(const DepthStencilKey& key);

    /// Queries whether min/max blending has to be emulated in the fragment shader.
    void QueryBlendEmulation(const Pica::RegsInternal& regs, bool& rgb_emulation,
                             bool& alpha_emulation) const;

    /**
     * Opens the disk cache of a title and builds every program and pipeline it contains, reporting
     * the progress through `callback`. Stops early when `stop_loading` becomes true.
     */
    void LoadDiskCache(u64 title_id, const std::atomic_bool& stop_loading,
                       const VideoCore::DiskResourceLoadCallback& callback);

    /// Writes the pipeline binaries built since the last call to the binary archive.
    void SaveBinaryArchive();

    /// Called once per emulated frame, saves the binary archive from time to time.
    void TickFrame();

    /// Counts a draw with these programs, for the shader dump (QualityOptions::dump_shaders):
    /// the most used vertex programs (by vertices) and fragment programs (by draws) are written
    /// to the shader_dump folder every 10 seconds or so.
    void NoteShaderUse(const ShaderHandle& vertex, const ShaderHandle& fragment, u32 vertices);

    /// Number of fragment programs generated so far.
    std::size_t NumPrograms() const {
        return fragment_shaders.size();
    }

    /// True when lit fragment shaders correct the normal quaternions per triangle themselves
    /// (MetalFSConfig::fragment_quaternion_fix), so lit draws may share vertices too.
    bool FragmentQuaternionFix() const {
        return fragment_quaternion_fix;
    }

private:
    struct Pipeline;

    ShaderHandle MakeShader(u64 hash, bool is_vertex, std::string source);
    void CompileShader(Shader& shader);
    void BuildPipeline(Pipeline& pipeline);
    id<MTLLibrary> Compile(const std::string& source, std::string_view name, bool fragment);
    bool ProbeFragmentQuaternionFix();

    void OpenDiskCache(u64 title_id);
    void CloseDiskCache();
    void AppendRecord(u32 type, std::span<const u8> payload);
    void AppendShaderRecord(const Shader& shader);
    std::string CachePath(std::string_view extension) const;

private:
    const Instance& instance;
    MTLVertexDescriptor* vertex_descriptor;
    MTLCompileOptions* compile_options;
    MTLCompileOptions* fragment_compile_options; ///< Like compile_options, optionally fast math
    bool fragment_quaternion_fix{};
    ShaderHandle trivial_vertex_shader;

    // Only accessed from the GPU thread.
    std::unordered_map<u64, ShaderHandle> fragment_shaders;
    std::unordered_map<u64, ShaderHandle> vertex_shaders;
    std::unordered_map<u64, std::shared_ptr<Pipeline>> pipelines;
    std::unordered_map<u64, id<MTLDepthStencilState>> depth_stencil_states;

    // Disk cache of the current title.
    u64 title_id{};
    bool disk_cache_open{};
    FileUtil::IOFile cache_file;

    // GPU binaries of the pipelines. Shared with the workers, guarded by the mutex.
    std::mutex archive_mutex;
    id<MTLBinaryArchive> binary_archive = nil;
    std::atomic<bool> archive_dirty{};
    /// Every pipeline made this session that can be archived, in order: each save writes a new
    /// archive with all of them (see SaveBinaryArchive).
    std::vector<MTLRenderPipelineDescriptor*> archived_descs;
    std::vector<PipelineKey> archived_keys;
    /// Pipelines a save failed on, never added again.
    std::unordered_set<u64> archive_excluded;
    /// Saving the archive works (it is turned off after a save that cannot be made to work).
    bool archive_enabled{};
    /// One save at a time (SaveBinaryArchive builds the new archive without archive_mutex).
    std::mutex archive_save_mutex;
    u32 frames_since_save{};
    /// Microseconds this frame spent waiting for pipelines to build (see GetPipeline).
    u32 frame_wait_us{};

    // Shader dump (GPU thread only).
    struct ShaderUse {
        ShaderHandle shader;
        u64 draws{};
        u64 vertices{};
    };
    std::unordered_map<u64, ShaderUse> vertex_use;
    std::unordered_map<u64, ShaderUse> fragment_use;
    u32 frames_since_dump{};
    void DumpShaders();

    // Declared last so the workers stop before the rest is destroyed.
    std::unique_ptr<Common::ThreadWorker> workers;
};

} // namespace Metal
