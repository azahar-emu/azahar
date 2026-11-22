// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <algorithm>
#include <cstdlib>
#include <array>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <thread>
#include <pthread/qos.h>
#include <fmt/format.h>
#include "common/frame_cost.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "common/zstd_compression.h"
#include "video_core/pica/regs_internal.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/renderer_metal/mtl_instance.h"
#include "video_core/renderer_metal/mtl_pipeline_cache.h"
#include "video_core/renderer_metal/renderer_metal.h"
#include "video_core/shader/generator/msl_shader_decompiler.h"
#include "video_core/shader/generator/msl_shader_gen.h"
#include "video_core/shader/generator/shader_gen.h"

MICROPROFILE_DEFINE(Metal_ShaderCompile, "Metal", "Shader Compile", MP_RGB(255, 128, 0));
MICROPROFILE_DEFINE(Metal_PipelineBuild, "Metal", "Pipeline Build", MP_RGB(255, 64, 0));

namespace Metal {

using namespace Pica::Shader::Generator;

namespace {

/// Disk cache file layout: a CacheHeader followed by records (RecordHeader + payload).
constexpr u32 CACHE_MAGIC = 0x434C544D; // "MTLC"
constexpr u32 CACHE_VERSION = 3; // 2: vertex shaders take VSDepth, 3: hardware scissor

struct CacheHeader {
    u32 magic;
    u32 version;
    u64 build_hash;
    u64 profile_hash;
};

enum RecordType : u32 {
    RECORD_SHADER = 1,   ///< u64 hash, u8 is_vertex, zstd compressed MSL source
    RECORD_PIPELINE = 2, ///< PipelineKey
};

struct RecordHeader {
    u32 type;
    u32 size;
};

constexpr std::size_t SHADER_RECORD_PREFIX = sizeof(u64) + sizeof(u8);

/// Cached programs are MSL produced by this exact build for this exact profile, anything else is
/// regenerated.
u64 BuildHash() {
    return Common::ComputeHash64(Common::g_scm_rev, std::strlen(Common::g_scm_rev));
}

u64 ProfileHash(const Pica::Shader::Profile& profile) {
    // The leading u8 flags are the ones the MSL generators look at. The rest of the struct has
    // padding that is not guaranteed to be zero.
    return Common::ComputeHash64(&profile, offsetof(Pica::Shader::Profile, vk_disable_spirv_optimizer));
}

/// The header a cache file written now starts with. Files with another header are discarded.
CacheHeader ExpectedCacheHeader(const Pica::Shader::Profile& profile,
                                bool fragment_quaternion_fix) {
    const auto& quality = Metal::GetQualityOptions();
    return CacheHeader{
        .magic = CACHE_MAGIC,
        .version = CACHE_VERSION,
        .build_hash = BuildHash(),
        .profile_hash = Common::HashCombine(
            ProfileHash(profile), static_cast<u64>(quality.fast_math) |
                                      static_cast<u64>(quality.fast_functions) << 1 |
                                      static_cast<u64>(quality.vertex_fast_functions) << 2 |
                                      static_cast<u64>(quality.vertex_fast_math) << 3 |
                                      static_cast<u64>(fragment_quaternion_fix) << 5),
    };
}

} // Anonymous namespace

struct PipelineCache::Shader {
    u64 hash{};
    bool is_vertex{};
    std::string source;
    std::once_flag once;
    id<MTLFunction> function = nil;
    std::atomic<bool> compiled{};
    std::atomic<bool> failed{};
};

struct PipelineCache::Pipeline {
    PipelineKey key{};
    ShaderHandle vertex;
    ShaderHandle fragment;
    bool queued{}; ///< GPU thread only
    std::once_flag once;
    id<MTLRenderPipelineState> state = nil;
    std::atomic<bool> done{};
};

PipelineCache::PipelineCache(const Instance& instance_, MTLVertexDescriptor* vertex_descriptor_)
    : instance{instance_}, vertex_descriptor{vertex_descriptor_} {
    const auto make_options = [](bool fast_math, bool fast_functions) {
        MTLCompileOptions* options = [MTLCompileOptions new];
        if (@available(iOS 16.0, macOS 13.0, *)) {
            options.languageVersion = MTLLanguageVersion2_4;
        }
        // Keep the position output bit-exact between pipelines sharing geometry (multi-pass
        // effects).
        if (@available(iOS 14.0, macOS 11.0, *)) {
            options.preserveInvariance = YES;
        }
        if (@available(iOS 18.0, macOS 15.0, *)) {
            options.mathMode = fast_math ? MTLMathModeFast : MTLMathModeSafe;
        } else {
            options.fastMathEnabled = fast_math;
        }
        // Faster versions of the math functions (rsqrt, normalize, pow, ...), which do not need
        // the assumptions about NaN and infinity that fast math makes. Set whatever the math
        // mode is, so the two options work together.
        if (fast_functions) {
            if (@available(iOS 18.0, macOS 15.0, *)) {
                options.mathFloatingPointFunctions = MTLMathFloatingPointFunctionsFast;
            }
        }
        return options;
    };
    // Vertex shaders may use the fast versions of the math functions, which keep the NaN and
    // infinity rules, or full fast math, which does not (and therefore needs the accurate
    // multiplication off, see AZEmulator).
    compile_options = make_options(GetQualityOptions().vertex_fast_math,
                                   GetQualityOptions().vertex_fast_functions ||
                                       GetQualityOptions().vertex_fast_math);
    if (GetQualityOptions().vertex_fast_math) {
        LOG_INFO(Render, "Vertex shaders are compiled with fast math");
    } else if (GetQualityOptions().vertex_fast_functions) {
        LOG_INFO(Render, "Vertex shaders use the fast math functions");
    }
    fragment_compile_options =
        make_options(GetQualityOptions().fast_math, GetQualityOptions().fast_functions);
    if (GetQualityOptions().fast_functions) {
        LOG_INFO(Render, "Fragment shaders use the fast math functions{}",
                 GetQualityOptions().fast_math ? " (together with fast math)" : "");
    }
    if (GetQualityOptions().half_interpolants) {
        LOG_INFO(Render, "Vertex colors are passed to the fragment shaders in half precision");
    }
    if (GetQualityOptions().fast_math) {
        LOG_INFO(Render, "Fragment shaders are compiled with fast math");
    }

    // Lit fragment shaders that fix the lighting quaternions themselves, see
    // ProbeFragmentQuaternionFix. Only useful when the draws share vertices.
    if (GetQualityOptions().vertex_reuse) {
        fragment_quaternion_fix = ProbeFragmentQuaternionFix();
    }

    trivial_vertex_shader =
        MakeShader(0, true, MSL::GenerateTrivialVertexShader(GetProfile().has_clip_planes,
                                         GetQualityOptions().half_interpolants));
    CompileShader(*trivial_vertex_shader);
    ASSERT_MSG(!trivial_vertex_shader->failed, "Failed to compile the Metal vertex shader");

    // Leave a core for the emulated CPU and one for the GPU thread.
    const u32 cores = std::max(std::thread::hardware_concurrency(), 2u);
    const std::size_t num_workers = std::clamp<u32>(cores - 2, 1, 4);
    workers = std::make_unique<Common::ThreadWorker>(num_workers, "MetalShaderWorker");
}

PipelineCache::~PipelineCache() {
    // Finish the build in progress, then keep what was built for the next run.
    workers.reset();
    SaveBinaryArchive();
    CloseDiskCache();
}

bool PipelineCache::ProbeFragmentQuaternionFix() {
    id<MTLDevice> device = instance.GetDevice();
    if (![device supportsShaderBarycentricCoordinates]) {
        LOG_INFO(Render, "Lit draws are shaded corner by corner (the GPU has no barycentric "
                         "coordinates)");
        return false;
    }
    // Draw one triangle over a 4x4 target with the real FixQuaternion and corners whose
    // quaternions need a flip, and compare every pixel with the result worked out here. The
    // corners have different w, so the perspective correction is checked too.
    struct Corner {
        float x, y, w;
        std::array<float, 4> quat;
    };
    static constexpr std::array<Corner, 3> CORNERS{{
        {-1.2f, -1.2f, 1.0f, {1.0f, 0.0f, 0.0f, 0.0f}},
        {3.5f, -1.2f, 2.0f, {-0.8f, 0.6f, 0.0f, 0.0f}},
        {-1.2f, 3.5f, 1.5f, {0.6f, 0.0f, 0.8f, 0.0f}},
    }};
    std::string source = R"(
#include <metal_stdlib>
using namespace metal;
struct ProbeOutput {
    float4 position [[position, invariant]];
    float4 normquat [[user(normquat)]];
};
struct ProbeInput {
    float4 position [[position]];
    float4 normquat [[user(normquat)]];
    float3 barycentric [[barycentric_coord, center_perspective]];
};
)";
    source += Pica::Shader::Generator::MSL::QUATERNION_FIX_MSL;
    source += "vertex ProbeOutput probe_vs(uint vid [[vertex_id]]) {\n"
              "    float4 corners[3] = {";
    for (std::size_t i = 0; i < CORNERS.size(); i++) {
        const Corner& c = CORNERS[i];
        source += fmt::format("{}float4({:.3f}f, {:.3f}f, 0.5f, {:.3f}f)", i == 0 ? "" : ", ",
                              c.x * c.w, c.y * c.w, c.w);
    }
    source += "};\n    float4 quats[3] = {";
    for (std::size_t i = 0; i < CORNERS.size(); i++) {
        const auto& q = CORNERS[i].quat;
        source += fmt::format("{}float4({:.3f}f, {:.3f}f, {:.3f}f, {:.3f}f)", i == 0 ? "" : ", ",
                              q[0], q[1], q[2], q[3]);
    }
    source += R"(};
    ProbeOutput out;
    out.position = corners[vid];
    out.normquat = quats[vid];
    return out;
}
fragment float4 probe_fs(ProbeInput in [[stage_in]]) {
    return FixQuaternion(in.normquat, in.barycentric);
}
)";
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:ToNSString(source)
                                                  options:fragment_compile_options
                                                    error:&error];
    id<MTLRenderPipelineState> state = nil;
    if (library) {
        MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
        desc.vertexFunction = [library newFunctionWithName:@"probe_vs"];
        desc.fragmentFunction = [library newFunctionWithName:@"probe_fs"];
        desc.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Float;
        state = [device newRenderPipelineStateWithDescriptor:desc error:&error];
    }
    if (!state) {
        LOG_WARNING(Render, "Lit draws are shaded corner by corner, the lighting fix test failed "
                            "to build: {}",
                    error ? [error.localizedDescription UTF8String] : "unknown error");
        return false;
    }

    constexpr u32 SIZE = 4;
    MTLTextureDescriptor* tex_desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
                                                           width:SIZE
                                                          height:SIZE
                                                       mipmapped:NO];
    tex_desc.usage = MTLTextureUsageRenderTarget;
    tex_desc.storageMode = MTLStorageModeShared;
    id<MTLTexture> target = [device newTextureWithDescriptor:tex_desc];
    if (target == nil) {
        return false;
    }
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = target;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(9.0, 9.0, 9.0, 9.0);
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLCommandBuffer> buffer = [queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [buffer renderCommandEncoderWithDescriptor:pass];
    [encoder setRenderPipelineState:state];
    [encoder setViewport:MTLViewport{0.0, 0.0, double(SIZE), double(SIZE), 0.0, 1.0}];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [encoder endEncoding];
    [buffer commit];
    [buffer waitUntilCompleted];
    if (buffer.status != MTLCommandBufferStatusCompleted) {
        LOG_WARNING(Render, "Lit draws are shaded corner by corner, the lighting fix test did "
                            "not run");
        return false;
    }
    std::array<float, SIZE * SIZE * 4> pixels{};
    [target getBytes:pixels.data()
         bytesPerRow:sizeof(float) * 4 * SIZE
          fromRegion:MTLRegionMake2D(0, 0, SIZE, SIZE)
         mipmapLevel:0];

    // The expected value at each pixel center: perspective correct weights of the corners, the
    // other corners flipped to the side of the first one. Taking another corner as the first
    // gives the same or the opposite quaternion, the same rotation: either is accepted.
    float worst = 0.0f;
    for (u32 py = 0; py < SIZE; py++) {
        for (u32 px = 0; px < SIZE; px++) {
            const float x = (px + 0.5f) / SIZE * 2.0f - 1.0f;
            const float y = 1.0f - (py + 0.5f) / SIZE * 2.0f;
            const Corner& a = CORNERS[0];
            const Corner& b = CORNERS[1];
            const Corner& c = CORNERS[2];
            const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
            std::array<float, 3> l{
                ((b.x - x) * (c.y - y) - (c.x - x) * (b.y - y)) / area,
                ((c.x - x) * (a.y - y) - (a.x - x) * (c.y - y)) / area,
                0.0f,
            };
            l[2] = 1.0f - l[0] - l[1];
            float sum = 0.0f;
            for (std::size_t i = 0; i < 3; i++) {
                l[i] /= CORNERS[i].w;
                sum += l[i];
            }
            float error_same = 0.0f;
            float error_opposite = 0.0f;
            for (std::size_t k = 0; k < 4; k++) {
                float expected = 0.0f;
                for (std::size_t i = 0; i < 3; i++) {
                    float dot = 0.0f;
                    for (std::size_t j = 0; j < 4; j++) {
                        dot += CORNERS[0].quat[j] * CORNERS[i].quat[j];
                    }
                    expected += (dot < 0.0f ? -1.0f : 1.0f) * CORNERS[i].quat[k] * l[i] / sum;
                }
                const float value = pixels[(py * SIZE + px) * 4 + k];
                error_same = std::max(error_same, std::abs(value - expected));
                error_opposite = std::max(error_opposite, std::abs(value + expected));
            }
            worst = std::max(worst, std::min(error_same, error_opposite));
        }
    }
    if (!(worst < 0.002f)) {
        LOG_WARNING(Render,
                    "Lit draws are shaded corner by corner, the lighting fix test is off by {:.4f} "
                    "(first pixels {:.3f} {:.3f} {:.3f} {:.3f}, {:.3f} {:.3f} {:.3f} {:.3f})",
                    worst, pixels[0], pixels[1], pixels[2], pixels[3], pixels[4], pixels[5],
                    pixels[6], pixels[7]);
        return false;
    }
    LOG_INFO(Render, "Lit draws share vertices (the lighting fix runs in the fragment shader, "
                     "test error {:.5f})",
             worst);
    return true;
}

const Pica::Shader::Profile& PipelineCache::GetProfile() const {
    return instance.GetProfile();
}

u64 PipelineCache::HashOf(const ShaderHandle& shader) {
    return shader ? shader->hash : 0;
}

id<MTLLibrary> PipelineCache::Compile(const std::string& source, std::string_view name,
                                      bool fragment) {
    MICROPROFILE_SCOPE(Metal_ShaderCompile);
    Common::FrameCost::Scope frame_cost{Common::FrameCost::Kind::ShaderBuild};
    NSError* error = nil;
    id<MTLLibrary> library = [instance.GetDevice()
        newLibraryWithSource:ToNSString(source)
                     options:fragment ? fragment_compile_options : compile_options
                       error:&error];
    if (!library) {
        LOG_CRITICAL(Render, "Failed to compile {}: {}\n{}", name,
                     error ? [error.localizedDescription UTF8String] : "unknown error", source);
    } else if (error && Settings::values.renderer_debug) {
        LOG_WARNING(Render, "Warnings compiling {}: {}", name,
                    [error.localizedDescription UTF8String]);
    }
    return library;
}

PipelineCache::ShaderHandle PipelineCache::MakeShader(u64 hash, bool is_vertex,
                                                      std::string source) {
    auto shader = std::make_shared<Shader>();
    shader->hash = hash;
    shader->is_vertex = is_vertex;
    shader->source = std::move(source);
    return shader;
}

void PipelineCache::CompileShader(Shader& shader) {
    std::call_once(shader.once, [&] {
        id<MTLFunction> function = nil;
        if (!shader.source.empty()) {
            @autoreleasepool {
                id<MTLLibrary> library =
                    Compile(shader.source, shader.is_vertex ? "vertex shader" : "fragment shader",
                            !shader.is_vertex);
                if (library) {
                    function = [library
                        newFunctionWithName:ToNSString(shader.is_vertex ? MSL::VS_ENTRY_POINT
                                                                        : MSL::FS_ENTRY_POINT)];
                }
            }
        }
        shader.function = function;
        // Kept for the shader dump.
        if (!GetQualityOptions().dump_shaders) {
            shader.source = {};
        }
        shader.failed.store(function == nil, std::memory_order_relaxed);
        shader.compiled.store(true, std::memory_order_release);
    });
}

bool PipelineCache::UsesVertexDepth(const Pica::RegsInternal& regs,
                                    bool has_depth_attachment) const {
    return GetQualityOptions().vertex_depth && has_depth_attachment &&
           GetProfile().has_clip_planes && !regs.framebuffer.IsShadowRendering() &&
           regs.rasterizer.depthmap_enable != Pica::RasterizerRegs::DepthBuffering::WBuffering;
}

PipelineCache::ShaderHandle PipelineCache::UseFragmentShader(const Pica::RegsInternal& regs,
                                                             const Pica::Shader::UserConfig& user,
                                                             bool has_color_attachment,
                                                             bool has_depth_attachment) {
    const Pica::Shader::FSConfig fs_config{regs};
    MSL::MetalFSConfig metal_config{};
    metal_config.has_color_attachment = has_color_attachment;
    metal_config.has_depth_attachment = has_depth_attachment;
    metal_config.fast_lighting = GetQualityOptions().fast_lighting && fs_config.lighting.enable;

    u64 hash = Common::HashCombine(fs_config.Hash(), static_cast<u64>(user.raw),
                                   static_cast<u64>(has_color_attachment),
                                   static_cast<u64>(has_depth_attachment));
    metal_config.vertex_depth = UsesVertexDepth(regs, has_depth_attachment);
    metal_config.fast_color = GetQualityOptions().fast_color;
    metal_config.half_luts = GetQualityOptions().fast_lighting;
    metal_config.half_interpolants = GetQualityOptions().half_interpolants;
    metal_config.fragment_quaternion_fix =
        fragment_quaternion_fix && fs_config.lighting.enable.Value() != 0;
    metal_config.lut_texture =
        GetQualityOptions().lut_textures && fs_config.lighting.enable.Value() != 0;
    if (metal_config.lut_texture) {
        hash = Common::HashCombine(hash, 0x1D7E'7E77'0000'0009ULL);
    }
    // The rasterizer sets the scissor rectangle for the test keeping the inside of the box.
    metal_config.hardware_scissor = GetQualityOptions().draw_optimizations;
    if (metal_config.hardware_scissor) {
        hash = Common::HashCombine(hash, 0x5C15'5012'0000'0008ULL);
    }
    if (metal_config.fragment_quaternion_fix) {
        hash = Common::HashCombine(hash, 0x9A7E'0F1C'0000'0007ULL);
    }
    if (metal_config.fast_lighting) {
        hash = Common::HashCombine(hash, 0xFA57'1164'7000'0001ULL);
    }
    if (metal_config.vertex_depth) {
        hash = Common::HashCombine(hash, 0x0DE9'7400'0000'0002ULL);
    }
    if (metal_config.fast_color) {
        hash = Common::HashCombine(hash, 0xFA57'C010'0000'0003ULL);
    }
    if (metal_config.half_luts) {
        hash = Common::HashCombine(hash, 0x4A1F'0000'0000'0004ULL);
    }
    if (metal_config.half_interpolants) {
        hash = Common::HashCombine(hash, 0x4A1F'1177'0000'0005ULL);
    }
    hash = hash == 0 ? 1 : hash;
    if (const auto it = fragment_shaders.find(hash); it != fragment_shaders.end()) {
        return it->second;
    }

    Common::FrameCost::Scope frame_cost{Common::FrameCost::Kind::ShaderBuild};
    auto shader = MakeShader(
        hash, false, MSL::GenerateFragmentShader(fs_config, user, GetProfile(), metal_config));
    AppendShaderRecord(*shader);
    fragment_shaders.emplace(hash, shader);
    return shader;
}

PipelineCache::ShaderHandle PipelineCache::UseVertexShader(const Pica::RegsInternal& regs,
                                                           Pica::ShaderSetup& setup) {
    const PicaVSConfig config{regs, setup};
    const bool use_clip_planes = GetProfile().has_clip_planes;
    const bool sanitize_mul = GetProfile().enable_accurate_mul;
    const bool half_interpolants = GetQualityOptions().half_interpolants;
    u64 hash = Common::HashCombine(config.Hash(), static_cast<u64>(use_clip_planes),
                                   static_cast<u64>(sanitize_mul),
                                   static_cast<u64>(half_interpolants),
                                   static_cast<u64>(fragment_quaternion_fix));
    // The program is made for the values of the bool uniforms its flow control tests, which do
    // not change within a draw (with Draw Optimizations): its branches are then constants for
    // the compiler. Big programs that handle many cases with them (Luigi's Mansion's run every
    // path through a jump table) become a straight line, without the loads they do not need.
    u32 bools_mask = 0;
    u32 bools = 0;
    if (GetQualityOptions().draw_optimizations) {
        auto [it, inserted] = program_bool_masks.try_emplace(config.state.program_hash, 0u);
        if (inserted) {
            it->second = MSL::BoolUniformsTested(setup.GetProgramCode());
        }
        bools_mask = it->second;
        for (u32 i = 0; i < 16; i++) {
            if (setup.uniforms.b[i]) {
                bools |= 1u << i;
            }
        }
        bools &= bools_mask;
        hash = Common::HashCombine(hash, 0xB001'5BEC'0000'0000ULL | bools_mask,
                                   static_cast<u64>(bools));
    }
    hash = hash == 0 ? 1 : hash;
    if (const auto it = vertex_shaders.find(hash); it != vertex_shaders.end()) {
        const ShaderHandle& shader = it->second;
        // A program whose compilation failed (possibly in the background) uses the CPU path.
        return shader->failed.load(std::memory_order_relaxed) ? nullptr : shader;
    }

    Common::FrameCost::Scope frame_cost{Common::FrameCost::Kind::ShaderBuild};
    std::string source =
        MSL::GenerateVertexShader(setup, config, use_clip_planes, sanitize_mul, half_interpolants,
                                  fragment_quaternion_fix, bools_mask, bools);
    auto shader = MakeShader(hash, true, std::move(source));
    if (shader->source.empty()) {
        LOG_INFO(Render, "Vertex shader {:016x} cannot be translated, using the CPU for it",
                 config.state.program_hash);
        // Remembered so the program is not translated again on every draw.
        shader->failed = true;
        shader->compiled = true;
        vertex_shaders.emplace(hash, shader);
        return nullptr;
    }
    AppendShaderRecord(*shader);
    vertex_shaders.emplace(hash, shader);
    return shader;
}

void PipelineCache::BuildPipeline(Pipeline& pipeline) {
    std::call_once(pipeline.once, [&] {
        CompileShader(*pipeline.vertex);
        if (pipeline.fragment) {
            CompileShader(*pipeline.fragment);
        }
        if (pipeline.vertex->failed || (pipeline.fragment && pipeline.fragment->failed)) {
            pipeline.done.store(true, std::memory_order_release);
            return;
        }

        MICROPROFILE_SCOPE(Metal_PipelineBuild);
        const PipelineKey& key = pipeline.key;
        @autoreleasepool {
            MTLRenderPipelineDescriptor* desc = [MTLRenderPipelineDescriptor new];
            // Depth and stencil only draws run no fragment function (PipelineKey::no_fragment).
            desc.fragmentFunction = pipeline.fragment ? pipeline.fragment->function : nil;
            desc.vertexFunction = pipeline.vertex->function;
            if (key.vs_hash == 0) {
                desc.vertexDescriptor = vertex_descriptor;
            }
            // Hardware vertex shaders fetch their inputs themselves.
            desc.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
            desc.rasterSampleCount = std::max<NSUInteger>(key.samples, 1);

            MTLRenderPipelineColorAttachmentDescriptor* color = desc.colorAttachments[0];
            color.pixelFormat = static_cast<MTLPixelFormat>(key.color_format);
            color.writeMask = static_cast<MTLColorWriteMask>(key.write_mask);
            color.blendingEnabled = key.blend_enable && key.color_format != MTLPixelFormatInvalid;
            color.rgbBlendOperation = static_cast<MTLBlendOperation>(key.rgb_op);
            color.alphaBlendOperation = static_cast<MTLBlendOperation>(key.alpha_op);
            color.sourceRGBBlendFactor = static_cast<MTLBlendFactor>(key.src_rgb);
            color.destinationRGBBlendFactor = static_cast<MTLBlendFactor>(key.dst_rgb);
            color.sourceAlphaBlendFactor = static_cast<MTLBlendFactor>(key.src_alpha);
            color.destinationAlphaBlendFactor = static_cast<MTLBlendFactor>(key.dst_alpha);
            desc.depthAttachmentPixelFormat = static_cast<MTLPixelFormat>(key.depth_format);
            desc.stencilAttachmentPixelFormat = static_cast<MTLPixelFormat>(key.stencil_format);

            Common::FrameCost::Scope frame_cost{Common::FrameCost::Kind::ShaderBuild};
            id<MTLDevice> device = instance.GetDevice();
            id<MTLRenderPipelineState> state = nil;
            NSError* error = nil;

            // Look the GPU binary up in the archive first. The archive is only touched with the
            // mutex held, building a pipeline that is not in it happens without.
            // Pipelines that write no color are kept out of the archive: one without a fragment
            // function, or whose fragment stage Metal drops because nothing it outputs is written
            // (color writes off, shadow passes), makes serializing fail ("expecting 'fragment'
            // stage in pipeline no. N") and takes every other pipeline down with it. Seen in every
            // game, the archive was never saved. They are few and build quickly.
            const bool archivable = !key.no_fragment &&
                                    key.color_format != static_cast<u32>(MTLPixelFormatInvalid) &&
                                    key.write_mask != static_cast<u8>(MTLColorWriteMaskNone);
            const u64 key_hash = key.Hash();
            {
                std::scoped_lock lock{archive_mutex};
                if (binary_archive != nil && archivable) {
                    desc.binaryArchives = @[ binary_archive ];
                    state = [device newRenderPipelineStateWithDescriptor:desc
                                                                 options:
                                                                     MTLPipelineOptionFailOnBinaryArchiveMiss
                                                              reflection:nil
                                                                   error:nil];
                    desc.binaryArchives = nil;
                }
            }

            const bool from_archive = state != nil;
            if (state == nil) {
                state = [device newRenderPipelineStateWithDescriptor:desc error:&error];
                if (state == nil) {
                    LOG_CRITICAL(Render, "Failed to create Metal pipeline: {}",
                                 error ? [error.localizedDescription UTF8String]
                                       : "unknown error");
                }
            }
            // Every pipeline of this session goes into the next archive that is saved (the ones
            // found in the loaded archive too, it is replaced). Nothing is added to an archive
            // that was loaded or saved: a pipeline added after that could not be packed ("expecting
            // 'fragment' stage in pipeline no. N", always the first one added since).
            if (state != nil && archivable && !archive_excluded.contains(key_hash)) {
                std::scoped_lock lock{archive_mutex};
                archived_descs.push_back(desc);
                archived_keys.push_back(key);
                if (!from_archive) {
                    archive_dirty.store(true, std::memory_order_relaxed);
                }
            }
            pipeline.state = state;
        }
        pipeline.done.store(true, std::memory_order_release);
    });
}

id<MTLRenderPipelineState> PipelineCache::GetPipeline(const PipelineKey& key,
                                                      const ShaderHandle& vertex,
                                                      const ShaderHandle& fragment, bool wait) {
    const u64 hash = key.Hash();
    auto& entry = pipelines[hash];
    if (!entry) {
        entry = std::make_shared<Pipeline>();
        entry->key = key;
        entry->vertex = vertex;
        entry->fragment = fragment;
        AppendRecord(RECORD_PIPELINE,
                     {reinterpret_cast<const u8*>(&key), sizeof(PipelineKey)});
    }
    if (entry->done.load(std::memory_order_acquire)) {
        return entry->state;
    }

    if (wait || !Settings::values.async_shader_compilation.GetValue()) {
        // Also waits for a worker that is already building it.
        BuildPipeline(*entry);
        return entry->state;
    }
    if (!entry->queued) {
        entry->queued = true;
        workers->QueueWork([this, pipeline = entry] {
            pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
            BuildPipeline(*pipeline);
        });
    }

    // Give the build a moment before the draw is skipped: with the binary archive most builds
    // finish within a couple of milliseconds. The waiting per frame is limited.
    const u32 wait_us = GetQualityOptions().shader_wait_us;
    if (wait_us > 0) {
        const u32 budget = wait_us * 4;
        const u32 allowed = std::min(wait_us, budget > frame_wait_us ? budget - frame_wait_us : 0u);
        if (allowed > 0) {
            const auto start = std::chrono::steady_clock::now();
            const auto deadline = start + std::chrono::microseconds(allowed);
            while (!entry->done.load(std::memory_order_acquire) &&
                   std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::microseconds(40));
            }
            frame_wait_us += static_cast<u32>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start)
                    .count());
            if (entry->done.load(std::memory_order_acquire)) {
                return entry->state;
            }
        }
    }
    return nil;
}

id<MTLDepthStencilState> PipelineCache::GetDepthStencilState(const DepthStencilKey& key) {
    const u64 hash = key.Hash();
    if (const auto it = depth_stencil_states.find(hash); it != depth_stencil_states.end()) {
        return it->second;
    }

    MTLDepthStencilDescriptor* desc = [MTLDepthStencilDescriptor new];
    desc.depthCompareFunction = static_cast<MTLCompareFunction>(key.depth_compare);
    desc.depthWriteEnabled = key.depth_write != 0;
    if (key.stencil_enable) {
        MTLStencilDescriptor* stencil = [MTLStencilDescriptor new];
        stencil.stencilCompareFunction = static_cast<MTLCompareFunction>(key.stencil_compare);
        stencil.stencilFailureOperation = static_cast<MTLStencilOperation>(key.stencil_fail);
        stencil.depthFailureOperation = static_cast<MTLStencilOperation>(key.depth_fail);
        stencil.depthStencilPassOperation = static_cast<MTLStencilOperation>(key.depth_pass);
        stencil.readMask = key.read_mask;
        stencil.writeMask = key.write_mask;
        desc.frontFaceStencil = stencil;
        desc.backFaceStencil = stencil;
    }

    id<MTLDepthStencilState> state = [instance.GetDevice() newDepthStencilStateWithDescriptor:desc];
    depth_stencil_states.emplace(hash, state);
    return state;
}

void PipelineCache::QueryBlendEmulation(const Pica::RegsInternal& regs, bool& rgb_emulation,
                                        bool& alpha_emulation) const {
    Pica::Shader::FramebufferConfig framebuffer_config{regs};
    framebuffer_config.ApplyProfile(instance.GetProfile());
    rgb_emulation = framebuffer_config.requested_rgb_blend.RequiresMinMaxEmulation();
    alpha_emulation = framebuffer_config.requested_alpha_blend.RequiresMinMaxEmulation();
}

void PipelineCache::NoteShaderUse(const ShaderHandle& vertex, const ShaderHandle& fragment,
                                  u32 vertices) {
    if (vertex) {
        ShaderUse& use = vertex_use[vertex->hash];
        use.shader = vertex;
        use.draws++;
        use.vertices += vertices;
    }
    if (fragment) {
        ShaderUse& use = fragment_use[fragment->hash];
        use.shader = fragment;
        use.draws++;
        use.vertices += vertices;
    }
}

void PipelineCache::DumpShaders() {
    if (title_id == 0 || (vertex_use.empty() && fragment_use.empty())) {
        return;
    }
    const std::string dir = fmt::format("{}shader_dump/{:016X}/",
                                        FileUtil::GetUserPath(FileUtil::UserPath::UserDir),
                                        title_id);
    // Only the current ones: the ranking changes as the game goes on.
    FileUtil::DeleteDirRecursively(dir);
    FileUtil::CreateFullPath(dir);
    std::string index = fmt::format(
        "Most used shaders of {:016X} since the game started (vertex programs by vertices "
        "drawn, fragment programs by draws). Written again every 10 seconds or so.\n\n",
        title_id);
    const auto dump = [&](std::unordered_map<u64, ShaderUse>& uses, std::string_view kind,
                          bool by_vertices) {
        std::vector<const ShaderUse*> sorted;
        for (const auto& [hash, use] : uses) {
            sorted.push_back(&use);
        }
        std::sort(sorted.begin(), sorted.end(), [&](const ShaderUse* a, const ShaderUse* b) {
            return by_vertices ? a->vertices > b->vertices : a->draws > b->draws;
        });
        constexpr std::size_t COUNT = 8;
        for (std::size_t i = 0; i < sorted.size() && i < COUNT; i++) {
            const ShaderUse& use = *sorted[i];
            const std::string name = fmt::format("{}_{:02}_{:016x}.metal", kind, i + 1,
                                                 use.shader->hash);
            index += fmt::format("{}: {} draws, {} vertices{}\n", name, use.draws, use.vertices,
                                 use.shader->source.empty() ? " (source not kept)" : "");
            if (!use.shader->source.empty()) {
                FileUtil::WriteStringToFile(
                    true, dir + name,
                    fmt::format("// {} draws, {} vertices since the game started\n{}",
                                use.draws, use.vertices, use.shader->source));
            }
        }
        index += "\n";
    };
    dump(vertex_use, "vs", true);
    dump(fragment_use, "fs", false);
    FileUtil::WriteStringToFile(true, dir + "index.txt", index);
}

void PipelineCache::TickFrame() {
    frame_wait_us = 0;
    if (GetQualityOptions().dump_shaders && ++frames_since_dump >= 600) {
        frames_since_dump = 0;
        DumpShaders();
    }
    // Save new pipeline binaries every minute or so, in the background, so they are not lost if
    // iOS terminates the app.
    constexpr u32 SAVE_INTERVAL_FRAMES = 3600;
    if (++frames_since_save < SAVE_INTERVAL_FRAMES) {
        return;
    }
    frames_since_save = 0;
    if (archive_dirty.load(std::memory_order_relaxed)) {
        workers->QueueWork([this] { SaveBinaryArchive(); });
    }
}

// Disk cache

std::string PipelineCache::CachePath(std::string_view extension) const {
    return fmt::format("{}metal/{:016X}.{}", FileUtil::GetUserPath(FileUtil::UserPath::ShaderDir),
                       title_id, extension);
}

void PipelineCache::AppendRecord(u32 type, std::span<const u8> payload) {
    if (!disk_cache_open || !cache_file.IsOpen()) {
        return;
    }
    const RecordHeader header{type, static_cast<u32>(payload.size())};
    cache_file.WriteBytes(&header, sizeof(header));
    cache_file.WriteBytes(payload.data(), payload.size());
    cache_file.Flush();
}

void PipelineCache::AppendShaderRecord(const Shader& shader) {
    if (!disk_cache_open) {
        return;
    }
    const auto compressed = Common::Compression::CompressDataZSTDDefault(
        {reinterpret_cast<const u8*>(shader.source.data()), shader.source.size()});
    std::vector<u8> payload(SHADER_RECORD_PREFIX + compressed.size());
    std::memcpy(payload.data(), &shader.hash, sizeof(u64));
    payload[sizeof(u64)] = shader.is_vertex ? 1 : 0;
    std::memcpy(payload.data() + SHADER_RECORD_PREFIX, compressed.data(), compressed.size());
    AppendRecord(RECORD_SHADER, payload);
}

void PipelineCache::CloseDiskCache() {
    cache_file.Close();
    disk_cache_open = false;
}

void PipelineCache::OpenDiskCache(u64 title_id_) {
    title_id = title_id_;
    FileUtil::CreateFullPath(CachePath("cache"));

    const CacheHeader expected = ExpectedCacheHeader(GetProfile(), fragment_quaternion_fix);

    // Reuse the file when it was written by this build with the same profile, otherwise start over.
    bool valid = false;
    {
        FileUtil::IOFile file{CachePath("cache"), "rb"};
        CacheHeader header{};
        valid = file.IsOpen() && file.ReadBytes(&header, sizeof(header)) == sizeof(header) &&
                std::memcmp(&header, &expected, sizeof(header)) == 0;
    }
    if (!valid) {
        FileUtil::Delete(CachePath("cache"));
        FileUtil::IOFile file{CachePath("cache"), "wb"};
        file.WriteBytes(&expected, sizeof(expected));
    }
    cache_file = FileUtil::IOFile{CachePath("cache"), "ab"};
    disk_cache_open = cache_file.IsOpen();
    if (!disk_cache_open) {
        LOG_ERROR(Render, "Unable to open the Metal shader cache {}", CachePath("cache"));
    }

    // The binary archive keeps the GPU binaries of the pipelines.
    if (@available(iOS 14.0, macOS 11.0, *)) {
        const std::string archive_path = CachePath("binarchive");
        if (!valid) {
            FileUtil::Delete(archive_path);
        }
        id<MTLDevice> device = instance.GetDevice();
        MTLBinaryArchiveDescriptor* desc = [MTLBinaryArchiveDescriptor new];
        if (FileUtil::Exists(archive_path)) {
            desc.url = [NSURL fileURLWithPath:ToNSString(archive_path)];
        }
        NSError* error = nil;
        id<MTLBinaryArchive> archive = [device newBinaryArchiveWithDescriptor:desc error:&error];
        if (archive == nil && desc.url != nil) {
            // Written by another OS or GPU driver version.
            LOG_INFO(Render, "Discarding the Metal binary archive: {}",
                     error ? [error.localizedDescription UTF8String] : "unknown error");
            FileUtil::Delete(archive_path);
            desc.url = nil;
            archive = [device newBinaryArchiveWithDescriptor:desc error:&error];
        }
        std::scoped_lock lock{archive_mutex};
        binary_archive = archive;
        archive_enabled = archive != nil;
        archived_descs.clear();
        archived_keys.clear();
        archive_dirty.store(false, std::memory_order_relaxed);
    }
}

void PipelineCache::SaveBinaryArchive() {
    // One save at a time; the pipeline builds only wait for the short copy of the list.
    std::scoped_lock save_lock{archive_save_mutex};
    std::vector<MTLRenderPipelineDescriptor*> descs;
    std::vector<PipelineKey> keys;
    {
        std::scoped_lock lock{archive_mutex};
        if (!archive_enabled || !archive_dirty.load(std::memory_order_relaxed)) {
            return;
        }
        archive_dirty.store(false, std::memory_order_relaxed);
        descs = archived_descs;
        keys = archived_keys;
    }
    // A new archive with every pipeline of this session, written next to the file and then
    // replacing it. The loaded one is only used to find binaries, never added to.
    const std::string path = CachePath("binarchive");
    const std::string temp_path = path + ".tmp";
    id<MTLDevice> device = instance.GetDevice();
    const auto build_and_save = [&](NSError** error) {
        MTLBinaryArchiveDescriptor* desc = [MTLBinaryArchiveDescriptor new];
        id<MTLBinaryArchive> fresh = [device newBinaryArchiveWithDescriptor:desc error:error];
        if (fresh == nil) {
            return false;
        }
        for (MTLRenderPipelineDescriptor* pipeline_desc : descs) {
            [fresh addRenderPipelineFunctionsWithDescriptor:pipeline_desc error:nil];
        }
        return static_cast<bool>([fresh serializeToURL:[NSURL fileURLWithPath:ToNSString(temp_path)]
                                                 error:error]);
    };
    NSError* error = nil;
    bool saved = false;
    @autoreleasepool {
        saved = build_and_save(&error);
        // A pipeline the archive cannot pack: left out (for good) and saved again, a few times.
        for (int attempt = 0; !saved && attempt < 4; attempt++) {
            const std::string message = error ? [error.localizedDescription UTF8String] : "";
            const auto at = message.find("pipeline no. '");
            if (at == std::string::npos) {
                break;
            }
            const std::size_t index = std::strtoul(message.c_str() + at + 14, nullptr, 10);
            if (index >= keys.size()) {
                break;
            }
            const PipelineKey bad = keys[index];
            LOG_WARNING(Render,
                        "Metal binary archive: cannot pack pipeline {} of {} (color format {}, "
                        "write mask {:x}, blend {}, depth format {}, stencil format {}, vertex "
                        "program {:016x}, fragment program {:016x}); leaving it out",
                        index, keys.size(), bad.color_format, bad.write_mask, bad.blend_enable,
                        bad.depth_format, bad.stencil_format, bad.vs_hash, bad.fs_hash);
            keys.erase(keys.begin() + index);
            descs.erase(descs.begin() + index);
            {
                std::scoped_lock lock{archive_mutex};
                archive_excluded.insert(bad.Hash());
                const auto it = std::find_if(archived_keys.begin(), archived_keys.end(),
                                             [&](const PipelineKey& k) {
                                                 return std::memcmp(&k, &bad, sizeof(k)) == 0;
                                             });
                if (it != archived_keys.end()) {
                    archived_descs.erase(archived_descs.begin() + (it - archived_keys.begin()));
                    archived_keys.erase(it);
                }
            }
            error = nil;
            saved = build_and_save(&error);
        }
    }
    if (saved) {
        FileUtil::Delete(path);
        FileUtil::Rename(temp_path, path);
        LOG_INFO(Render, "Saved the Metal binary archive ({} pipelines)", descs.size());
        return;
    }
    // Still failing: stop saving instead of retrying forever, and drop the file so the next
    // session starts clean. The loaded archive stays for finding binaries.
    LOG_WARNING(Render, "Unable to save the Metal binary archive, disabling it: {}",
                error ? [error.localizedDescription UTF8String] : "unknown error");
    FileUtil::Delete(temp_path);
    FileUtil::Delete(path);
    std::scoped_lock lock{archive_mutex};
    archive_enabled = false;
    archived_descs.clear();
    archived_keys.clear();
}

void PipelineCache::LoadDiskCache(u64 title_id_, const std::atomic_bool& stop_loading,
                                  const VideoCore::DiskResourceLoadCallback& callback) {
    using VideoCore::LoadCallbackStage;
    if (!Settings::values.use_disk_shader_cache.GetValue() || title_id_ == 0) {
        return;
    }
    title_id = title_id_;

    // A loading flag left behind means building the cache crashed last time, so it is not trusted.
    const std::string flag_path = CachePath("loading");
    const bool crashed = FileUtil::Exists(flag_path);
    if (crashed) {
        LOG_WARNING(Render, "Building the shader cache of {:016X} did not finish last time, "
                            "discarding it",
                    title_id);
        FileUtil::Delete(CachePath("cache"));
        FileUtil::Delete(CachePath("binarchive"));
        FileUtil::Delete(flag_path);
    }

    // Read the records before opening the file for appending.
    std::vector<u8> contents;
    {
        FileUtil::IOFile file{CachePath("cache"), "rb"};
        if (file.IsOpen()) {
            contents.resize(file.GetSize());
            contents.resize(file.ReadBytes(contents.data(), contents.size()));
        }
    }
    OpenDiskCache(title_id);
    if (!disk_cache_open) {
        return;
    }
    // The same header OpenDiskCache keeps. (They used to differ, which threw every cache away.)
    const CacheHeader expected = ExpectedCacheHeader(GetProfile(), fragment_quaternion_fix);
    if (contents.size() < sizeof(CacheHeader) ||
        std::memcmp(contents.data(), &expected, sizeof(CacheHeader)) != 0) {
        LOG_INFO(Render, "Shader cache of {:016X}: {} ({} bytes), starting a new one", title_id,
                 contents.empty() ? "no file"
                                  : "made by another build or with other settings",
                 contents.size());
        return;
    }

    // Parse the records. A truncated record at the end (the app was killed while writing) ends
    // the list.
    std::vector<PipelineKey> keys;
    std::size_t offset = sizeof(CacheHeader);
    while (offset + sizeof(RecordHeader) <= contents.size()) {
        RecordHeader header;
        std::memcpy(&header, contents.data() + offset, sizeof(header));
        offset += sizeof(header);
        if (offset + header.size > contents.size()) {
            LOG_INFO(Render, "Shader cache of {:016X}: the record at byte {} of {} is cut off "
                             "(the app stopped while writing it), reading stops there",
                     title_id, offset - sizeof(header), contents.size());
            break;
        }
        const std::span<const u8> payload{contents.data() + offset, header.size};
        offset += header.size;

        if (header.type == RECORD_SHADER && payload.size() > SHADER_RECORD_PREFIX) {
            u64 hash;
            std::memcpy(&hash, payload.data(), sizeof(hash));
            const bool is_vertex = payload[sizeof(u64)] != 0;
            const auto source =
                Common::Compression::DecompressDataZSTD(payload.subspan(SHADER_RECORD_PREFIX));
            if (source.empty()) {
                continue;
            }
            auto& map = is_vertex ? vertex_shaders : fragment_shaders;
            map.try_emplace(hash, MakeShader(hash, is_vertex,
                                             std::string{source.begin(), source.end()}));
        } else if (header.type == RECORD_PIPELINE && payload.size() == sizeof(PipelineKey)) {
            PipelineKey key;
            std::memcpy(&key, payload.data(), sizeof(key));
            keys.push_back(key);
        }
    }

    // Build every pipeline on the workers.
    std::vector<std::shared_ptr<Pipeline>> to_build;
    for (const PipelineKey& key : keys) {
        const u64 hash = key.Hash();
        if (pipelines.contains(hash)) {
            continue;
        }
        ShaderHandle vertex = trivial_vertex_shader;
        if (key.vs_hash != 0) {
            const auto it = vertex_shaders.find(key.vs_hash);
            if (it == vertex_shaders.end()) {
                continue;
            }
            vertex = it->second;
        }
        ShaderHandle fragment;
        if (!key.no_fragment) {
            const auto it = fragment_shaders.find(key.fs_hash);
            if (it == fragment_shaders.end()) {
                continue;
            }
            fragment = it->second;
        }
        auto pipeline = std::make_shared<Pipeline>();
        pipeline->key = key;
        pipeline->vertex = vertex;
        pipeline->fragment = fragment;
        pipeline->queued = true;
        pipelines.emplace(hash, pipeline);
        to_build.push_back(std::move(pipeline));
    }

    const std::size_t total = to_build.size();
    LOG_INFO(Render,
             "Building {} cached pipelines of {:016X} ({} fragment and {} vertex programs, {} "
             "pipeline records in a {} byte file)",
             total, title_id, fragment_shaders.size(), vertex_shaders.size(), keys.size(),
             contents.size());
    if (total == 0) {
        return;
    }

    FileUtil::CreateEmptyFile(flag_path);
    if (callback) {
        callback(LoadCallbackStage::Build, 0, total, "");
    }
    const auto start = std::chrono::steady_clock::now();
    auto built = std::make_shared<std::atomic<std::size_t>>(0);
    for (auto& pipeline : to_build) {
        workers->QueueWork([this, pipeline, built] {
            pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
            BuildPipeline(*pipeline);
            built->fetch_add(1, std::memory_order_relaxed);
        });
    }
    // Wait while reporting the progress. On stop the rest is built in the background.
    std::size_t reported = 0;
    while (!stop_loading) {
        const std::size_t done = built->load(std::memory_order_relaxed);
        if (callback && done != reported) {
            callback(LoadCallbackStage::Build, done, total, "");
            reported = done;
        }
        if (done >= total) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // Only a crash while building leaves the flag behind; an early stop is not one.
    FileUtil::Delete(flag_path);
    if (built->load() >= total) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        LOG_INFO(Render, "Built {} cached pipelines in {} ms", total, elapsed.count());
        SaveBinaryArchive();
    }
    if (callback) {
        callback(LoadCallbackStage::Complete, 0, 0, "");
    }
}

} // namespace Metal
