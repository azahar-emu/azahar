// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/hash.h"
#include "common/thread_worker.h"
#include "video_core/pica/regs_pipeline.h"
#include "video_core/pica/regs_rasterizer.h"
#include "video_core/rasterizer_cache/pixel_format.h"
#include "video_core/renderer_vulkan/vk_common.h"

#define LAYOUT_HASH static_cast<u64>(sizeof(T)), static_cast<u64>(alignof(T))
#define FIELD_HASH(x) static_cast<u64>(offsetof(T, x)), static_cast<u64>(sizeof(x))

namespace Common {

struct AsyncHandle {
public:
    AsyncHandle(bool is_done_ = false) : is_done{is_done_} {}

    [[nodiscard]] bool IsDone() noexcept {
        return is_done.load(std::memory_order::relaxed);
    }

    void WaitDone() noexcept {
        std::unique_lock lock{mutex};
        condvar.wait(lock, [this] { return is_done.load(std::memory_order::relaxed); });
    }

    void MarkDone(bool done = true) noexcept {
        std::scoped_lock lock{mutex};
        is_done = done;
        condvar.notify_all();
    }

private:
    std::condition_variable condvar;
    std::mutex mutex;
    std::atomic_bool is_done{false};
};

} // namespace Common

namespace Vulkan {

class Instance;
class RenderManager;

constexpr u32 MAX_SHADER_STAGES = 3;
constexpr u32 MAX_VERTEX_ATTRIBUTES = 16;
constexpr u32 MAX_VERTEX_BINDINGS = 13;

/**
 * The pipeline state is tightly packed with bitfields to reduce
 * the overhead of hashing as much as possible
 */
union RasterizationState {
    u8 value;
    BitField<0, 2, Pica::PipelineRegs::TriangleTopology> topology;
    BitField<4, 2, Pica::RasterizerRegs::CullMode> cull_mode;
    BitField<6, 1, u8> flip_viewport;

    static consteval u64 StructHash() {
        constexpr u64 STRUCT_VERSION = 0;

        using T = RasterizationState;
        return Common::HashCombine(STRUCT_VERSION,

                                   // layout
                                   LAYOUT_HASH,

                                   // fields
                                   FIELD_HASH(topology), FIELD_HASH(cull_mode),
                                   FIELD_HASH(flip_viewport));
    }
};
static_assert(std::is_trivial_v<RasterizationState>);

union DepthStencilState {
    u32 value;
    BitField<0, 1, u32> depth_test_enable;
    BitField<1, 1, u32> depth_write_enable;
    BitField<2, 1, u32> stencil_test_enable;
    BitField<3, 3, Pica::FramebufferRegs::CompareFunc> depth_compare_op;
    BitField<6, 3, Pica::FramebufferRegs::StencilAction> stencil_fail_op;
    BitField<9, 3, Pica::FramebufferRegs::StencilAction> stencil_pass_op;
    BitField<12, 3, Pica::FramebufferRegs::StencilAction> stencil_depth_fail_op;
    BitField<15, 3, Pica::FramebufferRegs::CompareFunc> stencil_compare_op;

    static consteval u64 StructHash() {
        constexpr u64 STRUCT_VERSION = 0;

        using T = DepthStencilState;
        return Common::HashCombine(STRUCT_VERSION,

                                   // layout
                                   LAYOUT_HASH,

                                   // fields
                                   FIELD_HASH(depth_test_enable), FIELD_HASH(depth_write_enable),
                                   FIELD_HASH(stencil_test_enable), FIELD_HASH(depth_compare_op),
                                   FIELD_HASH(stencil_fail_op), FIELD_HASH(stencil_pass_op),
                                   FIELD_HASH(stencil_depth_fail_op),
                                   FIELD_HASH(stencil_compare_op));
    }
};
static_assert(std::is_trivial_v<DepthStencilState>);

struct BlendingState {
    u16 blend_enable;
    u16 color_write_mask;
    Pica::FramebufferRegs::LogicOp logic_op;
    union {
        u32 value;
        BitField<0, 4, Pica::FramebufferRegs::BlendFactor> src_color_blend_factor;
        BitField<4, 4, Pica::FramebufferRegs::BlendFactor> dst_color_blend_factor;
        BitField<8, 3, Pica::FramebufferRegs::BlendEquation> color_blend_eq;
        BitField<11, 4, Pica::FramebufferRegs::BlendFactor> src_alpha_blend_factor;
        BitField<15, 4, Pica::FramebufferRegs::BlendFactor> dst_alpha_blend_factor;
        BitField<19, 3, Pica::FramebufferRegs::BlendEquation> alpha_blend_eq;
    };

    static consteval u64 StructHash() {
        constexpr u64 STRUCT_VERSION = 0;

        using T = BlendingState;
        return Common::HashCombine(STRUCT_VERSION,

                                   // layout
                                   LAYOUT_HASH,

                                   // fields
                                   FIELD_HASH(blend_enable), FIELD_HASH(color_write_mask),
                                   FIELD_HASH(logic_op), FIELD_HASH(src_color_blend_factor),
                                   FIELD_HASH(dst_color_blend_factor), FIELD_HASH(color_blend_eq),
                                   FIELD_HASH(src_alpha_blend_factor),
                                   FIELD_HASH(dst_alpha_blend_factor), FIELD_HASH(alpha_blend_eq));
    }
};
static_assert(std::is_trivial_v<BlendingState>);

union VertexBinding {
    u16 value;
    BitField<0, 4, u16> binding;
    BitField<4, 1, u16> fixed;
    BitField<5, 11, u16> byte_count;

    static consteval u64 StructHash() {
        constexpr u64 STRUCT_VERSION = 0;

        using T = VertexBinding;
        return Common::HashCombine(STRUCT_VERSION,

                                   // layout
                                   LAYOUT_HASH,

                                   // fields
                                   FIELD_HASH(binding), FIELD_HASH(fixed), FIELD_HASH(byte_count));
    }
};
static_assert(std::is_trivial_v<VertexBinding>);

union VertexAttribute {
    u32 value;
    BitField<0, 4, u32> binding;
    BitField<4, 4, u32> location;
    BitField<8, 3, Pica::PipelineRegs::VertexAttributeFormat> type;
    BitField<11, 3, u32> size;
    BitField<14, 11, u32> offset;

    static consteval u64 StructHash() {
        constexpr u64 STRUCT_VERSION = 0;

        using T = VertexAttribute;
        return Common::HashCombine(STRUCT_VERSION,

                                   // layout
                                   LAYOUT_HASH,

                                   // fields
                                   FIELD_HASH(binding), FIELD_HASH(location), FIELD_HASH(type),
                                   FIELD_HASH(size), FIELD_HASH(offset));
    }
};
static_assert(std::is_trivial_v<VertexAttribute>);

struct VertexLayout {
    u8 binding_count;
    u8 attribute_count;
    std::array<VertexBinding, MAX_VERTEX_BINDINGS> bindings;
    std::array<VertexAttribute, MAX_VERTEX_ATTRIBUTES> attributes;

    static consteval u64 StructHash() {
        constexpr u64 STRUCT_VERSION = 0;

        using T = VertexLayout;
        return Common::HashCombine(STRUCT_VERSION,

                                   // layout
                                   LAYOUT_HASH,

                                   // fields
                                   FIELD_HASH(binding_count), FIELD_HASH(attribute_count),
                                   FIELD_HASH(bindings), FIELD_HASH(attributes),

                                   // nested layout
                                   VertexBinding::StructHash(), VertexAttribute::StructHash());
    }
};
static_assert(std::is_trivial_v<VertexLayout>);

struct AttachmentInfo {
    VideoCore::PixelFormat color;
    VideoCore::PixelFormat depth;

    static consteval u64 StructHash() {
        constexpr u64 STRUCT_VERSION = 0;

        using T = AttachmentInfo;
        return Common::HashCombine(STRUCT_VERSION,

                                   // layout
                                   LAYOUT_HASH,

                                   // fields
                                   FIELD_HASH(color), FIELD_HASH(depth));
    }
};
static_assert(std::is_trivial_v<AttachmentInfo>);

struct StaticPipelineInfo {
    std::array<u64, MAX_SHADER_STAGES> shader_ids;

    BlendingState blending;
    AttachmentInfo attachments;
    VertexLayout vertex_layout;

    RasterizationState rasterization;
    DepthStencilState depth_stencil;

    [[nodiscard]] u64 OptimizedHash(const Instance& instance) const;

    static consteval u64 StructHash() {
        constexpr u64 STRUCT_VERSION = 0;

        using T = StaticPipelineInfo;
        return Common::HashCombine(
            STRUCT_VERSION,

            // layout
            LAYOUT_HASH,

            // fields
            FIELD_HASH(shader_ids), FIELD_HASH(blending), FIELD_HASH(attachments),
            FIELD_HASH(vertex_layout), FIELD_HASH(rasterization), FIELD_HASH(depth_stencil),

            // nested layout
            BlendingState::StructHash(), AttachmentInfo::StructHash(), VertexLayout::StructHash(),
            RasterizationState::StructHash(), DepthStencilState::StructHash());
    }
};
static_assert(std::is_trivial_v<StaticPipelineInfo>);

struct DynamicPipelineInfo {
    u32 blend_color = 0;
    u8 stencil_reference;
    u8 stencil_compare_mask;
    u8 stencil_write_mask;

    Common::Rectangle<u32> scissor;
    Common::Rectangle<s32> viewport;

    bool operator==(const DynamicPipelineInfo& other) const noexcept {
        return std::memcmp(this, &other, sizeof(DynamicPipelineInfo)) == 0;
    }
};

/**
 * Information about a graphics pipeline
 */
struct PipelineInfo : Common::HashableStruct<StaticPipelineInfo> {
    DynamicPipelineInfo dynamic_info;

    [[nodiscard]] bool IsDepthWriteEnabled() const noexcept {
        const bool has_stencil = state.attachments.depth == VideoCore::PixelFormat::D24S8;
        const bool depth_write =
            state.depth_stencil.depth_test_enable && state.depth_stencil.depth_write_enable;
        const bool stencil_write = has_stencil && state.depth_stencil.stencil_test_enable &&
                                   dynamic_info.stencil_write_mask != 0;

        return depth_write || stencil_write;
    }

    [[nodiscard]] u16 GetFinalColorWriteMask(const Instance& instance);
};

struct Shader : public Common::AsyncHandle {
    explicit Shader(const Instance& instance);
    explicit Shader(const Instance& instance, vk::ShaderStageFlagBits stage, std::string code);
    ~Shader();

    [[nodiscard]] vk::ShaderModule Handle() const noexcept {
        return module;
    }

    vk::ShaderModule module;
    vk::Device device;
    std::string program;
};

class GraphicsPipeline : public Common::AsyncHandle {
public:
    explicit GraphicsPipeline(const Instance& instance, RenderManager& renderpass_cache,
                              const PipelineInfo& info, vk::PipelineCache pipeline_cache,
                              vk::PipelineLayout layout, std::array<Shader*, 3> stages,
                              Common::ThreadWorker* worker);
    ~GraphicsPipeline();

    bool TryBuild(bool wait_built);

    bool Build(bool fail_on_compile_required = false);

    [[nodiscard]] vk::Pipeline Handle() const noexcept {
        return *pipeline;
    }

private:
    const Instance& instance;
    RenderManager& renderpass_cache;
    Common::ThreadWorker* worker;

    vk::UniquePipeline pipeline;
    vk::PipelineLayout pipeline_layout;
    vk::PipelineCache pipeline_cache;

    PipelineInfo info;
    std::array<Shader*, 3> stages;
    bool is_pending{};
};

} // namespace Vulkan

#undef LAYOUT_HASH
#undef FIELD_HASH
