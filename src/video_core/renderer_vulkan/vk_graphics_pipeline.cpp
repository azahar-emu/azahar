// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <boost/container/static_vector.hpp>

#include "common/alignment.h"
#include "common/hash.h"
#include "common/microprofile.h"
#include "video_core/renderer_vulkan/pica_to_vk.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_render_manager.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"

namespace Vulkan {

MICROPROFILE_DEFINE(Vulkan_Pipeline, "Vulkan", "Pipeline Building", MP_RGB(0, 192, 32));

vk::ShaderStageFlagBits MakeShaderStage(std::size_t index) {
    switch (index) {
    case 0:
        return vk::ShaderStageFlagBits::eVertex;
    case 1:
        return vk::ShaderStageFlagBits::eFragment;
    case 2:
        return vk::ShaderStageFlagBits::eGeometry;
    default:
        LOG_CRITICAL(Render_Vulkan, "Invalid shader stage index!");
        UNREACHABLE();
    }
    return vk::ShaderStageFlagBits::eVertex;
}

u64 StaticPipelineInfo::OptimizedHash(const Instance& instance) const {
    u64 info_hash = Common::HashCombine(
        shader_ids[0], shader_ids[1], shader_ids[2], Common::ComputeStructHash64(vertex_layout),
        Common::ComputeStructHash64(attachments), Common::ComputeStructHash64(blending));

    if (!instance.IsExtendedDynamicStateSupported()) {
        Common::HashCombine(info_hash, Common::ComputeStructHash64(rasterization),
                            Common::ComputeStructHash64(depth_stencil));
    }

    return info_hash;
}

Shader::Shader(const Instance& instance) : device{instance.GetDevice()} {}

Shader::Shader(const Instance& instance, vk::ShaderStageFlagBits stage, std::string code)
    : Shader{instance} {
    module = Compile(code, stage, instance.GetDevice());
    MarkDone();
}

Shader::~Shader() {
    if (device && module) {
        device.destroyShaderModule(module);
    }
}

GraphicsPipeline::GraphicsPipeline(const Instance& instance_, RenderManager& renderpass_cache_,
                                   const PipelineInfo& info_, vk::PipelineCache pipeline_cache_,
                                   vk::PipelineLayout layout_, std::array<Shader*, 3> stages_,
                                   Common::ThreadWorker* worker_)
    : instance{instance_}, renderpass_cache{renderpass_cache_}, worker{worker_},
      pipeline_layout{layout_}, pipeline_cache{pipeline_cache_}, info{info_}, stages{stages_} {}

GraphicsPipeline::~GraphicsPipeline() = default;

bool GraphicsPipeline::TryBuild(bool wait_built) {
    // The pipeline is currently being compiled. We can either wait for it
    // or skip the draw.
    if (is_pending) {
        return wait_built;
    }

    // If the shaders haven't been compiled yet, we cannot proceed.
    const bool shaders_pending = std::any_of(
        stages.begin(), stages.end(), [](Shader* shader) { return shader && !shader->IsDone(); });
    if (!wait_built && shaders_pending) {
        return false;
    }

    // Ask the driver if it can give us the pipeline quickly.
    if (!shaders_pending && instance.IsPipelineCreationCacheControlSupported() && Build(true)) {
        return true;
    }

    // Fallback to (a)synchronous compilation
    worker->QueueWork([this] { Build(); });
    is_pending = true;
    return wait_built;
}

bool GraphicsPipeline::Build(bool fail_on_compile_required) {
    MICROPROFILE_SCOPE(Vulkan_Pipeline);

    const u32 stride_alignment = instance.GetMinVertexStrideAlignment();
    std::array<vk::VertexInputBindingDescription, MAX_VERTEX_BINDINGS> bindings;
    for (u32 i = 0; i < info.state.vertex_layout.binding_count; i++) {
        const auto& binding = info.state.vertex_layout.bindings[i];
        bindings[i] = vk::VertexInputBindingDescription{
            .binding = binding.binding,
            .stride = Common::AlignUp(binding.byte_count.Value(), stride_alignment),
            .inputRate = binding.fixed.Value() ? vk::VertexInputRate::eInstance
                                               : vk::VertexInputRate::eVertex,
        };
    }

    std::array<vk::VertexInputAttributeDescription, MAX_VERTEX_ATTRIBUTES> attributes;
    for (u32 i = 0; i < info.state.vertex_layout.attribute_count; i++) {
        const auto& attr = info.state.vertex_layout.attributes[i];
        const FormatTraits& traits = instance.GetTraits(attr.type, attr.size);
        attributes[i] = vk::VertexInputAttributeDescription{
            .location = attr.location,
            .binding = attr.binding,
            .format = traits.native,
            .offset = attr.offset,
        };

        // At the end there's always the fixed binding which takes up
        // at least 16 bytes so we should always be able to alias.
        if (traits.needs_emulation) {
            const FormatTraits& comp_four_traits = instance.GetTraits(attr.type, 4);
            attributes[i].format = comp_four_traits.native;
        }
    }

    const vk::PipelineVertexInputStateCreateInfo vertex_input_info = {
        .vertexBindingDescriptionCount = info.state.vertex_layout.binding_count,
        .pVertexBindingDescriptions = bindings.data(),
        .vertexAttributeDescriptionCount = info.state.vertex_layout.attribute_count,
        .pVertexAttributeDescriptions = attributes.data(),
    };

    const vk::PipelineInputAssemblyStateCreateInfo input_assembly = {
        .topology = PicaToVK::PrimitiveTopology(info.state.rasterization.topology),
        .primitiveRestartEnable = false,
    };

    const vk::PipelineRasterizationStateCreateInfo raster_state = {
        .depthClampEnable = false,
        .rasterizerDiscardEnable = false,
        .cullMode = PicaToVK::CullMode(info.state.rasterization.cull_mode,
                                       info.state.rasterization.flip_viewport),
        .frontFace = PicaToVK::FrontFace(info.state.rasterization.cull_mode),
        .depthBiasEnable = false,
        .lineWidth = 1.0f,
    };

    const vk::PipelineMultisampleStateCreateInfo multisampling = {
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = false,
    };

    auto color_write_mask = info.state.blending.color_write_mask;
    const bool is_logic_op_emulated =
        instance.NeedsLogicOpEmulation() && !info.state.blending.blend_enable;
    const bool is_logic_op_noop =
        info.state.blending.logic_op == Pica::FramebufferRegs::LogicOp::NoOp;
    if (is_logic_op_emulated && is_logic_op_noop) {
        // Color output is disabled by logic operation. We use color write mask to skip
        // color but allow depth write.
        color_write_mask = 0;
    }

    const vk::PipelineColorBlendAttachmentState colorblend_attachment = {
        .blendEnable = info.state.blending.blend_enable,
        .srcColorBlendFactor = PicaToVK::BlendFunc(info.state.blending.src_color_blend_factor),
        .dstColorBlendFactor = PicaToVK::BlendFunc(info.state.blending.dst_color_blend_factor),
        .colorBlendOp = PicaToVK::BlendEquation(info.state.blending.color_blend_eq),
        .srcAlphaBlendFactor = PicaToVK::BlendFunc(info.state.blending.src_alpha_blend_factor),
        .dstAlphaBlendFactor = PicaToVK::BlendFunc(info.state.blending.dst_alpha_blend_factor),
        .alphaBlendOp = PicaToVK::BlendEquation(info.state.blending.alpha_blend_eq),
        .colorWriteMask = static_cast<vk::ColorComponentFlags>(color_write_mask),
    };

    const vk::PipelineColorBlendStateCreateInfo color_blending = {
        .logicOpEnable = !info.state.blending.blend_enable && !instance.NeedsLogicOpEmulation(),
        .logicOp = PicaToVK::LogicOp(info.state.blending.logic_op),
        .attachmentCount = 1,
        .pAttachments = &colorblend_attachment,
        .blendConstants = std::array{1.0f, 1.0f, 1.0f, 1.0f},
    };

    const vk::Viewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = 1.0f,
        .height = 1.0f,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    const vk::Rect2D scissor = {
        .offset = {0, 0},
        .extent = {1, 1},
    };

    const vk::PipelineViewportStateCreateInfo viewport_info = {
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    boost::container::static_vector<vk::DynamicState, 14> dynamic_states = {
        vk::DynamicState::eViewport,           vk::DynamicState::eScissor,
        vk::DynamicState::eStencilCompareMask, vk::DynamicState::eStencilWriteMask,
        vk::DynamicState::eStencilReference,   vk::DynamicState::eBlendConstants,
    };

    if (instance.IsExtendedDynamicStateSupported()) {
        constexpr std::array extended = {
            vk::DynamicState::eCullModeEXT,        vk::DynamicState::eDepthCompareOpEXT,
            vk::DynamicState::eDepthTestEnableEXT, vk::DynamicState::eDepthWriteEnableEXT,
            vk::DynamicState::eFrontFaceEXT,       vk::DynamicState::ePrimitiveTopologyEXT,
            vk::DynamicState::eStencilOpEXT,       vk::DynamicState::eStencilTestEnableEXT,
        };
        dynamic_states.insert(dynamic_states.end(), extended.begin(), extended.end());
    }

    const vk::PipelineDynamicStateCreateInfo dynamic_info = {
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };

    const vk::StencilOpState stencil_op_state = {
        .failOp = PicaToVK::StencilOp(info.state.depth_stencil.stencil_fail_op),
        .passOp = PicaToVK::StencilOp(info.state.depth_stencil.stencil_pass_op),
        .depthFailOp = PicaToVK::StencilOp(info.state.depth_stencil.stencil_depth_fail_op),
        .compareOp = PicaToVK::CompareFunc(info.state.depth_stencil.stencil_compare_op),
    };

    const vk::PipelineDepthStencilStateCreateInfo depth_info = {
        .depthTestEnable = static_cast<u32>(info.state.depth_stencil.depth_test_enable.Value()),
        .depthWriteEnable = static_cast<u32>(info.state.depth_stencil.depth_write_enable.Value()),
        .depthCompareOp = PicaToVK::CompareFunc(info.state.depth_stencil.depth_compare_op),
        .depthBoundsTestEnable = false,
        .stencilTestEnable = static_cast<u32>(info.state.depth_stencil.stencil_test_enable.Value()),
        .front = stencil_op_state,
        .back = stencil_op_state,
    };

    u32 shader_count = 0;
    std::array<vk::PipelineShaderStageCreateInfo, MAX_SHADER_STAGES> shader_stages;
    for (std::size_t i = 0; i < stages.size(); i++) {
        Shader* shader = stages[i];
        if (!shader) {
            continue;
        }

        shader->WaitDone();
        shader_stages[shader_count++] = vk::PipelineShaderStageCreateInfo{
            .stage = MakeShaderStage(i),
            .module = shader->Handle(),
            .pName = "main",
        };
    }

    vk::GraphicsPipelineCreateInfo pipeline_info = {
        .stageCount = shader_count,
        .pStages = shader_stages.data(),
        .pVertexInputState = &vertex_input_info,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_info,
        .pRasterizationState = &raster_state,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth_info,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_info,
        .layout = pipeline_layout,
        .renderPass = renderpass_cache.GetRenderpass(info.state.attachments.color,
                                                     info.state.attachments.depth, false),
    };

    if (fail_on_compile_required) {
        pipeline_info.flags |= vk::PipelineCreateFlagBits::eFailOnPipelineCompileRequiredEXT;
    }

    auto result = instance.GetDevice().createGraphicsPipelineUnique(pipeline_cache, pipeline_info);
    if (result.result == vk::Result::eSuccess) {
        pipeline = std::move(result.value);
    } else if (result.result == vk::Result::eErrorPipelineCompileRequiredEXT) {
        return false;
    } else {
        UNREACHABLE_MSG("Graphics pipeline creation failed!");
    }

    MarkDone();
    return true;
}

} // namespace Vulkan
