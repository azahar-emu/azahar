// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <boost/container/static_vector.hpp>

#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "common/scope_exit.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/loader/loader.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/renderer_vulkan/pica_to_vk.h"
#include "video_core/renderer_vulkan/vk_descriptor_update_queue.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_disk_cache.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h"
#include "video_core/shader/generator/glsl_shader_gen.h"
#include "video_core/shader/generator/spv_fs_shader_gen.h"

using namespace Pica::Shader::Generator;
using Pica::Shader::FSConfig;

MICROPROFILE_DEFINE(Vulkan_Bind, "Vulkan", "Pipeline Bind", MP_RGB(192, 32, 32));

namespace Vulkan {

u32 AttribBytes(Pica::PipelineRegs::VertexAttributeFormat format, u32 size) {
    switch (format) {
    case Pica::PipelineRegs::VertexAttributeFormat::FLOAT:
        return sizeof(float) * size;
    case Pica::PipelineRegs::VertexAttributeFormat::SHORT:
        return sizeof(u16) * size;
    case Pica::PipelineRegs::VertexAttributeFormat::BYTE:
    case Pica::PipelineRegs::VertexAttributeFormat::UBYTE:
        return sizeof(u8) * size;
    }
    return 0;
}

AttribLoadFlags MakeAttribLoadFlag(Pica::PipelineRegs::VertexAttributeFormat format) {
    switch (format) {
    case Pica::PipelineRegs::VertexAttributeFormat::BYTE:
    case Pica::PipelineRegs::VertexAttributeFormat::SHORT:
        return AttribLoadFlags::Sint;
    case Pica::PipelineRegs::VertexAttributeFormat::UBYTE:
        return AttribLoadFlags::Uint;
    default:
        return AttribLoadFlags::Float;
    }
}

constexpr std::array<vk::DescriptorSetLayoutBinding, 6> BUFFER_BINDINGS = {{
    {0, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eVertex},
    {1, vk::DescriptorType::eUniformBufferDynamic, 1,
     vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eGeometry},
    {2, vk::DescriptorType::eUniformBufferDynamic, 1, vk::ShaderStageFlagBits::eFragment},
    {3, vk::DescriptorType::eUniformTexelBuffer, 1, vk::ShaderStageFlagBits::eFragment},
    {4, vk::DescriptorType::eUniformTexelBuffer, 1, vk::ShaderStageFlagBits::eFragment},
    {5, vk::DescriptorType::eUniformTexelBuffer, 1, vk::ShaderStageFlagBits::eFragment},
}};

template <u32 NumTex0>
constexpr std::array<vk::DescriptorSetLayoutBinding, 3> TEXTURE_BINDINGS = {{
    {0, vk::DescriptorType::eCombinedImageSampler, NumTex0,
     vk::ShaderStageFlagBits::eFragment},                                                  // tex0
    {1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment}, // tex1
    {2, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment}, // tex2
}};

constexpr std::array<vk::DescriptorSetLayoutBinding, 2> UTILITY_BINDINGS = {{
    {0, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eFragment}, // shadow_buffer
    {1, vk::DescriptorType::eCombinedImageSampler, 1,
     vk::ShaderStageFlagBits::eFragment}, // tex_normal
}};

PipelineCache::PipelineCache(const Instance& instance_, Scheduler& scheduler_,
                             RenderManager& renderpass_cache_, DescriptorUpdateQueue& update_queue_)
    : instance{instance_}, scheduler{scheduler_}, renderpass_cache{renderpass_cache_},
      update_queue{update_queue_},
      num_worker_threads{std::max(std::thread::hardware_concurrency(), 2U) >> 1},
      workers{num_worker_threads, "Pipeline workers"}, shader_cache{nullptr},
      descriptor_heaps{
          DescriptorHeap{instance, scheduler.GetMasterSemaphore(), BUFFER_BINDINGS, 32},
          DescriptorHeap{instance, scheduler.GetMasterSemaphore(), TEXTURE_BINDINGS<1>},
          DescriptorHeap{instance, scheduler.GetMasterSemaphore(), UTILITY_BINDINGS, 32}},
      trivial_vertex_shader{
          instance, vk::ShaderStageFlagBits::eVertex,
          GLSL::GenerateTrivialVertexShader(instance.IsShaderClipDistanceSupported(), true)},
      current_program_id{0} {
    scheduler.RegisterOnDispatch([this] { update_queue.Flush(); });
    profile = Pica::Shader::Profile{
        .has_separable_shaders = true,
        .has_clip_planes = instance.IsShaderClipDistanceSupported(),
        .has_geometry_shader = instance.UseGeometryShaders(),
        .has_custom_border_color = instance.IsCustomBorderColorSupported(),
        .has_fragment_shader_interlock = instance.IsFragmentShaderInterlockSupported(),
        .has_fragment_shader_barycentric = instance.IsFragmentShaderBarycentricSupported(),
        .has_blend_minmax_factor = false,
        .has_minus_one_to_one_range = false,
        .has_logic_op = !instance.NeedsLogicOpEmulation(),
        .is_vulkan = true,
    };
    BuildLayout();
}

void PipelineCache::BuildLayout() {
    std::array<vk::DescriptorSetLayout, NumRasterizerSets> descriptor_set_layouts;
    descriptor_set_layouts[0] = descriptor_heaps[0].Layout();
    descriptor_set_layouts[1] = descriptor_heaps[1].Layout();
    descriptor_set_layouts[2] = descriptor_heaps[2].Layout();

    const vk::PipelineLayoutCreateInfo layout_info = {
        .setLayoutCount = NumRasterizerSets,
        .pSetLayouts = descriptor_set_layouts.data(),
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr,
    };
    pipeline_layout = instance.GetDevice().createPipelineLayoutUnique(layout_info);
}

PipelineCache::~PipelineCache() {
    SaveDiskCache();
}

void PipelineCache::LoadDiskCache(const std::atomic_bool& stop_loading,
                                  const VideoCore::DiskResourceLoadCallback& callback) {
    // Initialize the Vulkan pipeline cache
    if (!Settings::values.use_disk_shader_cache || !EnsureDirectories()) {
        // Create an empty pipeline cache if disk cache is disabled
        const vk::Device device = instance.GetDevice();
        vk::PipelineCacheCreateInfo cache_info{};
        pipeline_cache = device.createPipelineCacheUnique(cache_info);
        return;
    } // No need to initialize program ID here - it will be set when a process acquires GPU right
    const u64 program_id = 0;

    // Initialize the shader disk cache
    shader_cache = std::make_unique<ShaderDiskCache>(program_id); // Load shaders from disk
    auto transferable = shader_cache->LoadTransferable();
    if (transferable) {
        LOG_INFO(Render_Vulkan, "Loaded {} transferable shaders", transferable->size());

        if (callback) {
            callback(VideoCore::LoadCallbackStage::Decompile, 0, transferable->size());
        }

        // Load precompiled shaders once
        ShaderDecompiledMap precompiled;
        if (!transferable->empty()) {
            precompiled = shader_cache->LoadPrecompiled();
        }

        // Process the loaded shader entries and compile them
        std::size_t shader_index = 0;
        for (const auto& raw : *transferable) {
            if (stop_loading) {
                return;
            }

            const u64 unique_identifier = raw.GetUniqueIdentifier();
            const auto config = raw.GetRawShaderConfig();

            // Find precompiled shader if available
            auto precompiled_it = precompiled.find(unique_identifier);
            bool has_precompiled = precompiled_it != precompiled.end();

            // Handle each shader type
            switch (raw.GetProgramType()) {
            case ProgramType::VS: {
                // For vertex shaders, we need to reconstruct the shader setup
                const auto& code = raw.GetProgramCode();
                if (code.empty()) {
                    continue;
                }

                if (has_precompiled) {
                    const auto& shader_info = precompiled_it->second;

                    // Create a shader entry using the precompiled code
                    std::string decompiled_code = shader_info.code;
                    if (!decompiled_code.empty()) {
                        auto [iter, new_program] =
                            programmable_vertex_cache.try_emplace(decompiled_code, instance);
                        auto& shader = iter->second;

                        // Compile the shader if it's new
                        if (new_program) {
                            shader.program = decompiled_code; // Make a copy for the worker
                            const vk::Device device = instance.GetDevice();
                            workers.QueueWork([device, program = shader.program, &shader,
                                               unique_identifier] {
                                try {
                                    shader.module =
                                        Compile(program, vk::ShaderStageFlagBits::eVertex, device);
                                    shader.MarkDone();
                                } catch (const std::exception& e) {
                                    LOG_ERROR(Render_Vulkan,
                                              "Failed to compile vertex shader {}: {}",
                                              unique_identifier, e.what());
                                    // Mark as done even if it failed to avoid hangs
                                    shader.MarkDone();
                                }
                            });
                        }
                    }
                }
                break;
            }
            case ProgramType::GS: {
                if (has_precompiled) {
                    const auto& shader_info = precompiled_it->second;

                    // Create a PicaFixedGSConfig that would hash to the same value
                    PicaFixedGSConfig gs_config{config, instance.IsShaderClipDistanceSupported()};

                    // Create a shader entry using the precompiled code
                    auto [iter, new_shader] =
                        fixed_geometry_shaders.try_emplace(gs_config, instance);
                    if (new_shader) {
                        auto& shader = iter->second;
                        const vk::Device device = instance.GetDevice();
                        std::string decompiled_code = shader_info.code;

                        if (!decompiled_code.empty()) {
                            if (!decompiled_code.empty()) {
                                workers.QueueWork([device, program = decompiled_code, &shader,
                                                   unique_identifier] {
                                    try {
                                        shader.module = Compile(
                                            program, vk::ShaderStageFlagBits::eGeometry, device);
                                        shader.MarkDone();
                                    } catch (const std::exception& e) {
                                        LOG_ERROR(Render_Vulkan,
                                                  "Failed to compile geometry shader {}: {}",
                                                  unique_identifier, e.what());
                                        // Mark as done even if it failed to avoid hangs
                                        shader.MarkDone();
                                    }
                                });
                            }
                        }
                    }
                    break;
                }
            case ProgramType::FS: {
                if (has_precompiled) {
                    const auto& shader_info = precompiled_it->second;

                    // This is an approximation since we don't have the full UserConfig
                    Pica::Shader::UserConfig user{};
                    const FSConfig fs_config{config, user, profile};

                    // Create a shader entry using the precompiled code
                    auto [iter, new_shader] = fragment_shaders.try_emplace(fs_config, instance);
                    if (new_shader) {
                        auto& shader = iter->second;
                        const vk::Device device = instance.GetDevice();
                        std::string decompiled_code = shader_info.code;
                        if (!decompiled_code.empty()) {
                            workers.QueueWork(
                                [device, program = decompiled_code, &shader, unique_identifier] {
                                    try {
                                        shader.module = Compile(
                                            program, vk::ShaderStageFlagBits::eFragment, device);
                                        shader.MarkDone();
                                    } catch (const std::exception& e) {
                                        LOG_ERROR(Render_Vulkan,
                                                  "Failed to compile fragment shader {}: {}",
                                                  unique_identifier, e.what());
                                        // Mark as done even if it failed to avoid hangs
                                        shader.MarkDone();
                                    }
                                });
                        }
                    }
                }
                break;
            }
            default:
                LOG_ERROR(Render_Vulkan, "Unknown shader program type {}", raw.GetProgramType());
                break;
            }
                // Update progress
                if (callback) {
                    callback(VideoCore::LoadCallbackStage::Decompile, ++shader_index,
                             transferable->size());
                }
            } // Wait for all shader compilations to complete
            if (callback) {
                callback(VideoCore::LoadCallbackStage::Build, 0, transferable->size());
            }

            // This will ensure all worker threads finish their current tasks
            workers.WaitForRequests();

            // Final callback
            if (callback) {
                callback(VideoCore::LoadCallbackStage::Build, transferable->size(),
                         transferable->size());
                LOG_INFO(Render_Vulkan, "Finished loading and building {} shaders",
                         transferable->size());
            }
        }
    }

    // Also load the Vulkan pipeline cache
    const auto cache_dir = GetPipelineCacheDir();
    const u32 vendor_id = instance.GetVendorID();
    const u32 device_id = instance.GetDeviceID();
    const auto cache_file_path = fmt::format("{}{:x}{:x}.bin", cache_dir, vendor_id, device_id);

    vk::PipelineCacheCreateInfo cache_info{};
    std::vector<u8> cache_data;

    SCOPE_EXIT({
        const vk::Device device = instance.GetDevice();
        pipeline_cache = device.createPipelineCacheUnique(cache_info);
    });

    FileUtil::IOFile cache_file{cache_file_path, "rb"};
    if (!cache_file.IsOpen()) {
        LOG_INFO(Render_Vulkan, "No pipeline cache found for device");
        return;
    }

    const u64 cache_file_size = cache_file.GetSize();
    cache_data.resize(cache_file_size);
    if (cache_file.ReadBytes(cache_data.data(), cache_file_size) != cache_file_size) {
        LOG_ERROR(Render_Vulkan, "Error during pipeline cache read");
        return;
    }

    if (!IsCacheValid(cache_data)) {
        LOG_WARNING(Render_Vulkan, "Pipeline cache provided invalid, removing");
        cache_file.Close();
        FileUtil::Delete(cache_file_path);
        return;
    }

    LOG_INFO(Render_Vulkan, "Loading pipeline cache with size {} KB", cache_file_size / 1024);
    cache_info.initialDataSize = cache_file_size;
    cache_info.pInitialData = cache_data.data();
}

void PipelineCache::SaveDiskCache() {
    // Save Vulkan pipeline cache
    if (!Settings::values.use_disk_shader_cache || !EnsureDirectories() || !pipeline_cache) {
        return;
    }

    const auto cache_dir = GetPipelineCacheDir();
    const u32 vendor_id = instance.GetVendorID();
    const u32 device_id = instance.GetDeviceID();
    const u64 program_id = current_program_id; // Use our stored program ID instead
    const auto cache_file_path =
        fmt::format("{}{:x}{:x}_{:016x}.bin", cache_dir, vendor_id, device_id, program_id);

    FileUtil::IOFile cache_file{cache_file_path, "wb"};
    if (!cache_file.IsOpen()) {
        LOG_ERROR(Render_Vulkan, "Unable to open pipeline cache {} for writing", cache_file_path);
        return;
    }

    const vk::Device device = instance.GetDevice();
    const auto cache_data = device.getPipelineCacheData(*pipeline_cache);
    if (cache_file.WriteBytes(cache_data.data(), cache_data.size()) != cache_data.size()) {
        LOG_ERROR(Render_Vulkan, "Error during pipeline cache write to {}", cache_file_path);
        return;
    }
}

bool PipelineCache::BindPipeline(const PipelineInfo& info, bool wait_built) {
    MICROPROFILE_SCOPE(Vulkan_Bind);

    u64 shader_hash = 0;
    for (u32 i = 0; i < MAX_SHADER_STAGES; i++) {
        shader_hash = Common::HashCombine(shader_hash, shader_hashes[i]);
    }

    const u64 info_hash = info.Hash(instance);
    const u64 pipeline_hash = Common::HashCombine(shader_hash, info_hash);

    auto [it, new_pipeline] = graphics_pipelines.try_emplace(pipeline_hash);
    if (new_pipeline) {
        it.value() =
            std::make_unique<GraphicsPipeline>(instance, renderpass_cache, info, *pipeline_cache,
                                               *pipeline_layout, current_shaders, &workers);
    }

    GraphicsPipeline* const pipeline{it->second.get()};
    if (!pipeline->IsDone() && !pipeline->TryBuild(wait_built)) {
        return false;
    }

    const bool is_dirty = scheduler.IsStateDirty(StateFlags::Pipeline);
    const bool pipeline_dirty = (current_pipeline != pipeline) || is_dirty;
    scheduler.Record([this, is_dirty, pipeline_dirty, pipeline,
                      current_dynamic = current_info.dynamic, dynamic = info.dynamic,
                      descriptor_sets = bound_descriptor_sets, offsets = offsets,
                      current_rasterization = current_info.rasterization,
                      current_depth_stencil = current_info.depth_stencil,
                      rasterization = info.rasterization,
                      depth_stencil = info.depth_stencil](vk::CommandBuffer cmdbuf) {
        if (dynamic.viewport != current_dynamic.viewport || is_dirty) {
            const vk::Viewport vk_viewport = {
                .x = static_cast<f32>(dynamic.viewport.left),
                .y = static_cast<f32>(dynamic.viewport.top),
                .width = static_cast<f32>(dynamic.viewport.GetWidth()),
                .height = static_cast<f32>(dynamic.viewport.GetHeight()),
                .minDepth = 0.f,
                .maxDepth = 1.f,
            };
            cmdbuf.setViewport(0, vk_viewport);
        }

        if (dynamic.scissor != current_dynamic.scissor || is_dirty) {
            const vk::Rect2D scissor = {
                .offset{
                    .x = static_cast<s32>(dynamic.scissor.left),
                    .y = static_cast<s32>(dynamic.scissor.bottom),
                },
                .extent{
                    .width = dynamic.scissor.GetWidth(),
                    .height = dynamic.scissor.GetHeight(),
                },
            };
            cmdbuf.setScissor(0, scissor);
        }

        if (dynamic.stencil_compare_mask != current_dynamic.stencil_compare_mask || is_dirty) {
            cmdbuf.setStencilCompareMask(vk::StencilFaceFlagBits::eFrontAndBack,
                                         dynamic.stencil_compare_mask);
        }

        if (dynamic.stencil_write_mask != current_dynamic.stencil_write_mask || is_dirty) {
            cmdbuf.setStencilWriteMask(vk::StencilFaceFlagBits::eFrontAndBack,
                                       dynamic.stencil_write_mask);
        }

        if (dynamic.stencil_reference != current_dynamic.stencil_reference || is_dirty) {
            cmdbuf.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack,
                                       dynamic.stencil_reference);
        }

        if (dynamic.blend_color != current_dynamic.blend_color || is_dirty) {
            const Common::Vec4f color = PicaToVK::ColorRGBA8(dynamic.blend_color);
            cmdbuf.setBlendConstants(color.AsArray());
        }

        if (instance.IsExtendedDynamicStateSupported()) {
            const bool needs_flip =
                rasterization.flip_viewport != current_rasterization.flip_viewport;
            if (rasterization.cull_mode != current_rasterization.cull_mode || needs_flip ||
                is_dirty) {
                cmdbuf.setCullModeEXT(
                    PicaToVK::CullMode(rasterization.cull_mode, rasterization.flip_viewport));
                cmdbuf.setFrontFaceEXT(PicaToVK::FrontFace(rasterization.cull_mode));
            }

            if (depth_stencil.depth_compare_op != current_depth_stencil.depth_compare_op ||
                is_dirty) {
                cmdbuf.setDepthCompareOpEXT(PicaToVK::CompareFunc(depth_stencil.depth_compare_op));
            }

            if (depth_stencil.depth_test_enable != current_depth_stencil.depth_test_enable ||
                is_dirty) {
                cmdbuf.setDepthTestEnableEXT(depth_stencil.depth_test_enable);
            }

            if (depth_stencil.depth_write_enable != current_depth_stencil.depth_write_enable ||
                is_dirty) {
                cmdbuf.setDepthWriteEnableEXT(depth_stencil.depth_write_enable);
            }

            if (rasterization.topology != current_rasterization.topology || is_dirty) {
                cmdbuf.setPrimitiveTopologyEXT(PicaToVK::PrimitiveTopology(rasterization.topology));
            }

            if (depth_stencil.stencil_test_enable != current_depth_stencil.stencil_test_enable ||
                is_dirty) {
                cmdbuf.setStencilTestEnableEXT(depth_stencil.stencil_test_enable);
            }

            if (depth_stencil.stencil_fail_op != current_depth_stencil.stencil_fail_op ||
                depth_stencil.stencil_pass_op != current_depth_stencil.stencil_pass_op ||
                depth_stencil.stencil_depth_fail_op !=
                    current_depth_stencil.stencil_depth_fail_op ||
                depth_stencil.stencil_compare_op != current_depth_stencil.stencil_compare_op ||
                is_dirty) {
                cmdbuf.setStencilOpEXT(vk::StencilFaceFlagBits::eFrontAndBack,
                                       PicaToVK::StencilOp(depth_stencil.stencil_fail_op),
                                       PicaToVK::StencilOp(depth_stencil.stencil_pass_op),
                                       PicaToVK::StencilOp(depth_stencil.stencil_depth_fail_op),
                                       PicaToVK::CompareFunc(depth_stencil.stencil_compare_op));
            }
        }

        if (pipeline_dirty) {
            if (!pipeline->IsDone()) {
                pipeline->WaitDone();
            }
            cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->Handle());
        }

        cmdbuf.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline_layout, 0,
                                  descriptor_sets, offsets);
    });

    current_info = info;
    current_pipeline = pipeline;
    scheduler.MarkStateNonDirty(StateFlags::Pipeline | StateFlags::DescriptorSets);

    return true;
}

bool PipelineCache::UseProgrammableVertexShader(const Pica::RegsInternal& regs,
                                                Pica::ShaderSetup& setup,
                                                const VertexLayout& layout, bool accurate_mul) {
    // Enable the geometry-shader only if we are actually doing per-fragment lighting
    // and care about proper quaternions. Otherwise just use standard vertex+fragment shaders.
    // We also don't need the geometry shader if we have the barycentric extension.
    const bool use_geometry_shader = instance.UseGeometryShaders() && !regs.lighting.disable &&
                                     !instance.IsFragmentShaderBarycentricSupported();
    PicaVSConfig config{regs, setup, instance.IsShaderClipDistanceSupported(), use_geometry_shader,
                        accurate_mul};

    for (u32 i = 0; i < layout.attribute_count; i++) {
        const VertexAttribute& attr = layout.attributes[i];
        const FormatTraits& traits = instance.GetTraits(attr.type, attr.size);
        const u32 location = attr.location.Value();
        AttribLoadFlags& flags = config.state.load_flags[location];

        if (traits.needs_conversion) {
            flags = MakeAttribLoadFlag(attr.type);
        }
        if (traits.needs_emulation) {
            flags |= AttribLoadFlags::ZeroW;
        }
    }

    // Generate the unique identifier for this shader
    const u64 unique_identifier = setup.GetProgramCodeHash();

    const auto [it, new_config] = programmable_vertex_map.try_emplace(config);
    if (new_config) {
        auto program = GLSL::GenerateVertexShader(setup, config, true);
        if (program.empty()) {
            LOG_ERROR(Render_Vulkan, "Failed to retrieve programmable vertex shader");
            programmable_vertex_map[config] = nullptr;
            return false;
        }

        // Save to the disk cache if it's initialized
        if (shader_cache) {
            // Create a raw shader entry
            ProgramCode program_code(setup.program_code.begin(), setup.program_code.end());
            program_code.insert(program_code.end(), setup.swizzle_data.begin(),
                                setup.swizzle_data.end());

            const auto config_raw = regs;
            ShaderDiskCacheRaw raw{unique_identifier, ProgramType::VS, config_raw, program_code};

            // Save raw shader to disk
            shader_cache->SaveRaw(raw);

            // Also save the decompiled shader
            shader_cache->SaveDecompiled(unique_identifier, program, accurate_mul);
        }

        auto [iter, new_program] = programmable_vertex_cache.try_emplace(program, instance);
        auto& shader = iter->second;

        if (new_program) {
            shader.program = std::move(program);
            const vk::Device device = instance.GetDevice();
            workers.QueueWork([device, &shader] {
                try {
                    shader.module =
                        Compile(shader.program, vk::ShaderStageFlagBits::eVertex, device);
                    shader.MarkDone();
                } catch (const std::exception& e) {
                    LOG_ERROR(Render_Vulkan, "Failed to compile vertex shader: {}", e.what());
                    // Mark as done even if it failed to avoid hangs
                    shader.MarkDone();
                }
            });
        }

        it->second = &shader;
    }

    Shader* const shader{it->second};
    if (!shader) {
        LOG_ERROR(Render_Vulkan, "Failed to retrieve programmable vertex shader");
        return false;
    }

    current_shaders[ProgramType::VS] = shader;
    shader_hashes[ProgramType::VS] = config.Hash();

    return true;
}

void PipelineCache::UseTrivialVertexShader() {
    current_shaders[ProgramType::VS] = &trivial_vertex_shader;
    shader_hashes[ProgramType::VS] = 0;
}

bool PipelineCache::UseFixedGeometryShader(const Pica::RegsInternal& regs) {
    if (!instance.UseGeometryShaders()) {
        UseTrivialGeometryShader();
        return true;
    }

    const PicaFixedGSConfig gs_config{regs, instance.IsShaderClipDistanceSupported()};
    auto [it, new_shader] = fixed_geometry_shaders.try_emplace(gs_config, instance);
    auto& shader = it->second;

    if (new_shader) {
        // Generate shader code
        const auto code = GLSL::GenerateFixedGeometryShader(gs_config, true);

        // Save to disk cache if available
        if (shader_cache) {
            const u64 unique_identifier = gs_config.Hash();

            // Create a raw shader entry with empty program code (geometry shaders are generated
            // from config)
            ProgramCode program_code;
            ShaderDiskCacheRaw raw{unique_identifier, ProgramType::GS, regs, program_code};

            // Save raw shader to disk
            shader_cache->SaveRaw(raw);

            // Also save the decompiled shader
            shader_cache->SaveDecompiled(unique_identifier, code, false);
        }
        workers.QueueWork([device = instance.GetDevice(), &shader, code = code,
                           unique_identifier = gs_config.Hash()]() {
            try {
                shader.module = Compile(code, vk::ShaderStageFlagBits::eGeometry, device);
                shader.MarkDone();
            } catch (const std::exception& e) {
                LOG_ERROR(Render_Vulkan, "Failed to compile geometry shader {}: {}",
                          unique_identifier, e.what());
                // Mark as done even if it failed to avoid hangs
                shader.MarkDone();
            }
        });
    }

    current_shaders[ProgramType::GS] = &shader;
    shader_hashes[ProgramType::GS] = gs_config.Hash();

    return true;
}

void PipelineCache::UseTrivialGeometryShader() {
    current_shaders[ProgramType::GS] = nullptr;
    shader_hashes[ProgramType::GS] = 0;
}

void PipelineCache::UseFragmentShader(const Pica::RegsInternal& regs,
                                      const Pica::Shader::UserConfig& user) {
    const FSConfig fs_config{regs, user, profile};
    const auto [it, new_shader] = fragment_shaders.try_emplace(fs_config, instance);
    auto& shader = it->second;

    if (new_shader) {
        // Get unique identifier
        const u64 unique_identifier = fs_config.Hash();

        // Save to disk cache if available
        if (shader_cache) {
            // Create a raw shader entry with empty program code (fragment shaders are generated
            // from config)
            ProgramCode program_code;
            ShaderDiskCacheRaw raw{unique_identifier, ProgramType::FS, regs, program_code};

            // Save raw shader to disk
            shader_cache->SaveRaw(raw);
        }
        workers.QueueWork([fs_config, this, &shader, unique_identifier]() {
            try {
                const bool use_spirv = Settings::values.spirv_shader_gen.GetValue();
                std::string glsl_code;

                if (use_spirv && !fs_config.UsesSpirvIncompatibleConfig()) {
                    const std::vector code = SPIRV::GenerateFragmentShader(fs_config, profile);
                    shader.module = CompileSPV(code, instance.GetDevice());
                } else {
                    glsl_code = GLSL::GenerateFragmentShader(fs_config, profile);
                    shader.module = Compile(glsl_code, vk::ShaderStageFlagBits::eFragment,
                                            instance.GetDevice());
                }

                // If we generated GLSL code, save it to disk cache
                if (shader_cache && !glsl_code.empty()) {
                    shader_cache->SaveDecompiled(unique_identifier, glsl_code, false);
                }

                shader.MarkDone();
            } catch (const std::exception& e) {
                LOG_ERROR(Render_Vulkan, "Failed to compile fragment shader {}: {}",
                          unique_identifier, e.what());
                // Mark as done even if it failed to avoid hangs
                shader.MarkDone();
            }
        });
    }

    current_shaders[ProgramType::FS] = &shader;
    shader_hashes[ProgramType::FS] = fs_config.Hash();
}

bool PipelineCache::IsCacheValid(std::span<const u8> data) const {
    if (data.size() < sizeof(vk::PipelineCacheHeaderVersionOne)) {
        LOG_ERROR(Render_Vulkan, "Pipeline cache failed validation: Invalid header");
        return false;
    }

    vk::PipelineCacheHeaderVersionOne header;
    std::memcpy(&header, data.data(), sizeof(header));
    if (header.headerSize < sizeof(header)) {
        LOG_ERROR(Render_Vulkan, "Pipeline cache failed validation: Invalid header length");
        return false;
    }

    if (header.headerVersion != vk::PipelineCacheHeaderVersion::eOne) {
        LOG_ERROR(Render_Vulkan, "Pipeline cache failed validation: Invalid header version");
        return false;
    }

    if (u32 vendor_id = instance.GetVendorID(); header.vendorID != vendor_id) {
        LOG_ERROR(
            Render_Vulkan,
            "Pipeline cache failed validation: Incorrect vendor ID (file: {:#X}, device: {:#X})",
            header.vendorID, vendor_id);
        return false;
    }

    if (u32 device_id = instance.GetDeviceID(); header.deviceID != device_id) {
        LOG_ERROR(
            Render_Vulkan,
            "Pipeline cache failed validation: Incorrect device ID (file: {:#X}, device: {:#X})",
            header.deviceID, device_id);
        return false;
    }

    if (header.pipelineCacheUUID != instance.GetPipelineCacheUUID()) {
        LOG_ERROR(Render_Vulkan, "Pipeline cache failed validation: Incorrect UUID");
        return false;
    }

    return true;
}

bool PipelineCache::EnsureDirectories() const {
    const auto create_dir = [](const std::string& dir) {
        if (!FileUtil::CreateDir(dir)) {
            LOG_ERROR(Render_Vulkan, "Failed to create directory={}", dir);
            return false;
        }

        return true;
    };

    return create_dir(FileUtil::GetUserPath(FileUtil::UserPath::ShaderDir)) &&
           create_dir(GetPipelineCacheDir());
}

std::string PipelineCache::GetPipelineCacheDir() const {
    return FileUtil::GetUserPath(FileUtil::UserPath::ShaderDir) + "vulkan" + DIR_SEP;
}

void PipelineCache::SwitchProgramID(u64 program_id) {
    SaveDiskCache();

    // Store the program ID locally
    current_program_id = program_id;

    // Create a new shader cache for the new program
    shader_cache = std::make_unique<ShaderDiskCache>(program_id);
    auto transferable = shader_cache->LoadTransferable();
    if (transferable) {
        LOG_INFO(Render_Vulkan, "Loaded {} transferable shaders for program {}",
                 transferable->size(), program_id);
    }

    // Create a new Vulkan pipeline cache for the new program
    const vk::Device device = instance.GetDevice();
    vk::PipelineCacheCreateInfo cache_info{};

    if (Settings::values.use_disk_shader_cache && EnsureDirectories()) {
        const auto cache_dir = GetPipelineCacheDir();
        const u32 vendor_id = instance.GetVendorID();
        const u32 device_id = instance.GetDeviceID();
        const auto cache_file_path =
            fmt::format("{}{:x}{:x}_{:016x}.bin", cache_dir, vendor_id, device_id, program_id);

        // Try to load existing cache file
        FileUtil::IOFile cache_file{cache_file_path, "rb"};
        if (cache_file.IsOpen()) {
            const u64 cache_file_size = cache_file.GetSize();
            std::vector<u8> cache_data(cache_file_size);

            if (cache_file.ReadBytes(cache_data.data(), cache_file_size) == cache_file_size &&
                IsCacheValid(cache_data)) {
                LOG_INFO(Render_Vulkan, "Loading pipeline cache for program {} with size {} KB",
                         program_id, cache_file_size / 1024);
                cache_info.initialDataSize = cache_file_size;
                cache_info.pInitialData = cache_data.data();
            }
        }
    }

    pipeline_cache = device.createPipelineCacheUnique(cache_info);

    // Clear existing pipeline entries
    graphics_pipelines.clear();
    fixed_geometry_shaders.clear();
    fragment_shaders.clear();
    programmable_vertex_cache.clear();
}

} // namespace Vulkan
