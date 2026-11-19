// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <algorithm>
#include <bit>
#include <cstddef>
#include "common/alignment.h"
#include "common/literals.h"
#include "common/logging/log.h"
#include "common/math_util.h"
#include "common/microprofile.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/loader/loader.h"
#include "core/memory.h"
#include "video_core/pica/regs_shader.h"
#include "video_core/pica/pica_core.h"
#include "video_core/renderer_metal/mtl_instance.h"
#include "video_core/renderer_metal/mtl_rasterizer.h"
#include "video_core/renderer_metal/mtl_scheduler.h"
#include "video_core/renderer_metal/renderer_metal.h"
#include "video_core/renderer_metal/pica_to_mtl.h"
#include "video_core/shader/generator/msl_shader_gen.h"
#include "video_core/shader/generator/shader_gen.h"
#include "video_core/texture/texture_decode.h"

MICROPROFILE_DEFINE(Metal_Drawing, "Metal", "Drawing", MP_RGB(128, 128, 192));

namespace Metal {

namespace {

using namespace Common::Literals;
using namespace Pica::Shader::Generator;
using VideoCore::SurfaceType;
using TextureType = Pica::TexturingRegs::TextureConfig::TextureType;

constexpr u64 VERTEX_BUFFER_SIZE = 32_MiB;
constexpr u64 TEXTURE_BUFFER_SIZE = 2_MiB;

/// Converts the PICA RGBA color write mask (bit 0 = red) to a MTLColorWriteMask.
MTLColorWriteMask ColorWriteMask(u32 pica_mask) {
    MTLColorWriteMask mask = MTLColorWriteMaskNone;
    if (pica_mask & 1) {
        mask |= MTLColorWriteMaskRed;
    }
    if (pica_mask & 2) {
        mask |= MTLColorWriteMaskGreen;
    }
    if (pica_mask & 4) {
        mask |= MTLColorWriteMaskBlue;
    }
    if (pica_mask & 8) {
        mask |= MTLColorWriteMaskAlpha;
    }
    return mask;
}

} // Anonymous namespace

RasterizerMetal::RasterizerMetal(Memory::MemorySystem& memory, Pica::PicaCore& pica,
                                 VideoCore::CustomTexManager& custom_tex_manager,
                                 VideoCore::RendererBase& renderer, const Instance& instance_,
                                 Scheduler& scheduler_)
    : RasterizerAccelerated{memory, pica}, scheduler{scheduler_}, runtime{instance_, scheduler_},
      res_cache{memory, custom_tex_manager, runtime, regs, renderer},
      pipeline_cache{instance_, MakeVertexDescriptor()},
      vertex_buffer{instance_, scheduler_, VERTEX_BUFFER_SIZE, "Vertex buffer"},
      texture_buffer{instance_, scheduler_, TEXTURE_BUFFER_SIZE, "Proctex LUT buffer"},
      texture_lf_buffer{instance_, scheduler_, TEXTURE_BUFFER_SIZE, "Lighting/fog LUT buffer"} {}

RasterizerMetal::~RasterizerMetal() = default;

MTLVertexDescriptor* RasterizerMetal::MakeVertexDescriptor() {
    MTLVertexDescriptor* desc = [MTLVertexDescriptor vertexDescriptor];
    const auto set_attribute = [&](u32 index, MTLVertexFormat format, std::size_t offset) {
        desc.attributes[index].format = format;
        desc.attributes[index].offset = offset;
        desc.attributes[index].bufferIndex = MSL::VS_BUFFER_VERTEX_BASE;
    };
    set_attribute(ATTRIBUTE_POSITION, MTLVertexFormatFloat4, offsetof(HardwareVertex, position));
    set_attribute(ATTRIBUTE_COLOR, MTLVertexFormatFloat4, offsetof(HardwareVertex, color));
    set_attribute(ATTRIBUTE_TEXCOORD0, MTLVertexFormatFloat2, offsetof(HardwareVertex, tex_coord0));
    set_attribute(ATTRIBUTE_TEXCOORD1, MTLVertexFormatFloat2, offsetof(HardwareVertex, tex_coord1));
    set_attribute(ATTRIBUTE_TEXCOORD2, MTLVertexFormatFloat2, offsetof(HardwareVertex, tex_coord2));
    set_attribute(ATTRIBUTE_TEXCOORD0_W, MTLVertexFormatFloat,
                  offsetof(HardwareVertex, tex_coord0_w));
    set_attribute(ATTRIBUTE_NORMQUAT, MTLVertexFormatFloat4, offsetof(HardwareVertex, normquat));
    set_attribute(ATTRIBUTE_VIEW, MTLVertexFormatFloat3, offsetof(HardwareVertex, view));
    desc.layouts[MSL::VS_BUFFER_VERTEX_BASE].stride = sizeof(HardwareVertex);
    desc.layouts[MSL::VS_BUFFER_VERTEX_BASE].stepFunction = MTLVertexStepFunctionPerVertex;
    return desc;
}

void RasterizerMetal::TickFrame() {
    res_cache.TickFrame();
    runtime.TickFrame();
    pipeline_cache.TickFrame();
}

void RasterizerMetal::LoadDefaultDiskResources(
    const std::atomic_bool& stop_loading, const VideoCore::DiskResourceLoadCallback& callback) {
    if (callback) {
        callback(VideoCore::LoadCallbackStage::Prepare, 0, 0, "");
    }
    u64 program_id{};
    if (Core::System::GetInstance().GetAppLoader().ReadProgramId(program_id) !=
        Loader::ResultStatus::Success) {
        program_id = 0;
    }
    pipeline_cache.LoadDiskCache(program_id, stop_loading, callback);
    if (callback) {
        callback(VideoCore::LoadCallbackStage::Complete, 0, 0, "");
    }
}

void RasterizerMetal::SyncFixedState(const Framebuffer& framebuffer, u64 fs_hash, PipelineKey& key,
                                     DepthStencilKey& ds_key) const {
    const auto& output_merger = regs.framebuffer.output_merger;
    const auto& fb_regs = regs.framebuffer.framebuffer;

    key = {};
    key.fs_hash = fs_hash;

    const bool shadow_rendering = regs.framebuffer.IsShadowRendering();
    id<MTLTexture> color = framebuffer.Attachment(SurfaceType::Color);
    id<MTLTexture> depth = framebuffer.Attachment(SurfaceType::Depth);
    key.color_format = static_cast<u32>(
        (color != nil && !shadow_rendering) ? color.pixelFormat : MTLPixelFormatInvalid);
    key.depth_format = static_cast<u32>(
        (depth != nil && !shadow_rendering) ? depth.pixelFormat : MTLPixelFormatInvalid);
    key.stencil_format = static_cast<u32>((depth != nil && !shadow_rendering &&
                                           depth.pixelFormat == MTLPixelFormatDepth32Float_Stencil8)
                                              ? depth.pixelFormat
                                              : MTLPixelFormatInvalid);

    // Blending
    bool rgb_emulation = false;
    bool alpha_emulation = false;
    pipeline_cache.QueryBlendEmulation(regs, rgb_emulation, alpha_emulation);

    const auto& blending = output_merger.alpha_blending;
    key.blend_enable = output_merger.alphablend_enable;
    key.rgb_op = static_cast<u8>(PicaToMTL::BlendEquation(blending.blend_equation_rgb));
    key.alpha_op = static_cast<u8>(PicaToMTL::BlendEquation(blending.blend_equation_a));
    key.src_rgb = static_cast<u8>(PicaToMTL::BlendFunc(blending.factor_source_rgb));
    key.dst_rgb = static_cast<u8>(PicaToMTL::BlendFunc(blending.factor_dest_rgb));
    key.src_alpha = static_cast<u8>(PicaToMTL::BlendFunc(blending.factor_source_a));
    key.dst_alpha = static_cast<u8>(PicaToMTL::BlendFunc(blending.factor_dest_a));
    if (rgb_emulation) {
        // The fragment shader computes the blended color through framebuffer fetch.
        key.rgb_op = static_cast<u8>(MTLBlendOperationAdd);
        key.src_rgb = static_cast<u8>(MTLBlendFactorOne);
        key.dst_rgb = static_cast<u8>(MTLBlendFactorZero);
    }
    if (alpha_emulation) {
        key.alpha_op = static_cast<u8>(MTLBlendOperationAdd);
        key.src_alpha = static_cast<u8>(MTLBlendFactorOne);
        key.dst_alpha = static_cast<u8>(MTLBlendFactorZero);
    }

    const u32 color_mask =
        fb_regs.allow_color_write != 0 ? (output_merger.depth_color_mask >> 8) & 0xF : 0;
    key.write_mask =
        static_cast<u8>(shadow_rendering ? MTLColorWriteMaskNone : ColorWriteMask(color_mask));

    // Depth test
    const bool depth_test = output_merger.depth_test_enable == 1;
    const bool depth_write =
        fb_regs.allow_depth_stencil_write != 0 && output_merger.depth_write_enable == 1;
    ds_key = {};
    ds_key.depth_compare =
        static_cast<u8>(depth_test ? PicaToMTL::CompareFunc(output_merger.depth_test_func.Value())
                                   : MTLCompareFunctionAlways);
    ds_key.depth_write = depth_write && key.depth_format != MTLPixelFormatInvalid;

    // Stencil test
    const auto& stencil_test = output_merger.stencil_test;
    const bool stencil_enable = stencil_test.enable && regs.framebuffer.HasStencil() &&
                                key.stencil_format != MTLPixelFormatInvalid;
    ds_key.stencil_enable = stencil_enable;
    if (stencil_enable) {
        ds_key.stencil_compare = static_cast<u8>(PicaToMTL::CompareFunc(stencil_test.func));
        ds_key.stencil_fail =
            static_cast<u8>(PicaToMTL::StencilOp(stencil_test.action_stencil_fail));
        ds_key.depth_fail = static_cast<u8>(PicaToMTL::StencilOp(stencil_test.action_depth_fail));
        ds_key.depth_pass = static_cast<u8>(PicaToMTL::StencilOp(stencil_test.action_depth_pass));
        ds_key.read_mask = static_cast<u8>(stencil_test.input_mask);
        ds_key.write_mask =
            fb_regs.allow_depth_stencil_write != 0 ? static_cast<u8>(stencil_test.write_mask) : 0;
    }
}

void RasterizerMetal::DrawTriangles() {
    if (vertex_batch.empty()) {
        return;
    }
    Draw();
    vertex_batch.clear();
}

bool RasterizerMetal::AccelerateDrawBatch(bool is_indexed) {
    using Topology = Pica::PipelineRegs::TriangleTopology;

    // Geometry shaders keep running on the CPU, Metal has no geometry stage.
    if (regs.pipeline.use_gs != Pica::PipelineRegs::UseGS::No) {
        return false;
    }

    HardwareDraw draw;
    draw.is_indexed = is_indexed;
    draw.vertex = pipeline_cache.UseVertexShader(regs, pica.vs_setup);
    if (!draw.vertex) {
        return false;
    }

    const u32 num_vertices = regs.pipeline.num_vertices;
    const auto topology = regs.pipeline.triangle_topology.Value();
    u32 triangles = 0;
    switch (topology) {
    case Topology::List:
    case Topology::Shader:
        triangles = num_vertices / 3;
        break;
    case Topology::Strip:
    case Topology::Fan:
        triangles = num_vertices >= 3 ? num_vertices - 2 : 0;
        break;
    default:
        return false;
    }

    draw.vertex_info = AnalyzeVertexArray(is_indexed);
    if (draw.vertex_info.Invalid() || triangles == 0) {
        // Nothing to draw.
        return true;
    }
    draw.vertex_count = triangles * 3;

    // Native draws let the GPU reuse the shaded vertices. The per-triangle quaternion correction
    // of lit draws needs the first corner of every triangle: unless the fragment shader does it
    // (iOS 26), those keep the corner by corner draw. So do fans (Metal has none). A strip
    // index of 0xFFFF would restart the strip.
    const bool index_u16 = regs.pipeline.index_array.format != 0;
    draw.direct = GetQualityOptions().vertex_reuse &&
                  (regs.lighting.disable.Value() != 0 ||
                   pipeline_cache.FragmentQuaternionFix()) &&
                  topology != Topology::Fan &&
                  !(is_indexed && topology == Topology::Strip &&
                    draw.vertex_info.vs_input_index_max >= 0xFFFFu);
    draw.strip = topology == Topology::Strip;
    // Metal has no 8 bit indices, those are widened.
    const u32 index_size =
        is_indexed ? num_vertices * (index_u16 || draw.direct ? 2 : 1) : 0;
    draw.upload_size = draw.vertex_info.vs_input_size + Common::AlignUp(index_size, 4);
    if (draw.upload_size > VERTEX_BUFFER_SIZE / 4) {
        // Implausibly large, let the software path deal with it.
        return false;
    }

    return Draw(&draw);
}

void RasterizerMetal::UploadHardwareVertices(HardwareDraw& draw) {
    using namespace Pica::Shader::Generator::MSL;
    const auto& vertex_attributes = regs.pipeline.vertex_attributes;
    const PAddr base_address = vertex_attributes.GetPhysicalBaseAddress();
    const u32 vertex_min = draw.vertex_info.vs_input_index_min;
    const u32 vertex_num = draw.vertex_info.vs_input_index_max - vertex_min + 1;

    auto [buffer, offset, _] = vertex_buffer.Map(draw.upload_size, 16);
    draw.data_offset = offset;

    VSVertexLayout& layout = draw.layout;
    layout = {};
    for (u32 reg = 0; reg < 16; reg++) {
        layout.attributes[reg] = {0, 0, VS_ATTRIBUTE_DEFAULT, 4};
        layout.defaults[reg] = Common::MakeVec(0.0f, 0.0f, 0.0f, 1.0f);
    }
    std::array<bool, 16> loaded{};

    // The 12 attribute loaders each read an interleaved array of attributes, see
    // RasterizerVulkan::SetupVertexArray.
    u32 used = 0;
    for (const auto& loader : vertex_attributes.attribute_loaders) {
        if (loader.component_count == 0 || loader.byte_count == 0) {
            continue;
        }

        u32 attribute_offset = 0;
        for (u32 comp = 0; comp < loader.component_count && comp < 12; comp++) {
            const u32 attribute_index = loader.GetComponent(comp);
            if (attribute_index >= 12) {
                // Attribute ids 12 to 15 are 4, 8, 12 and 16 byte paddings.
                attribute_offset = Common::AlignUp(attribute_offset, 4);
                attribute_offset += (attribute_index - 11) * 4;
                continue;
            }
            const u32 size = vertex_attributes.GetNumElements(attribute_index);
            if (size == 0) {
                continue;
            }
            attribute_offset = Common::AlignUp(
                attribute_offset, vertex_attributes.GetElementSizeInBytes(attribute_index));

            const u32 reg = regs.vs.GetRegisterForAttribute(attribute_index);
            layout.attributes[reg] = {
                .offset = used + attribute_offset,
                .stride = static_cast<u32>(loader.byte_count),
                .format = static_cast<u32>(vertex_attributes.GetFormat(attribute_index)),
                .size = size,
            };
            loaded[reg] = true;
            attribute_offset += vertex_attributes.GetStride(attribute_index);
        }

        const PAddr data_addr =
            base_address + loader.data_offset + vertex_min * static_cast<u32>(loader.byte_count);
        const u32 data_size = static_cast<u32>(loader.byte_count) * vertex_num;
        {
            const ReadbackReason reason{"vertex data"};
            res_cache.FlushRegion(data_addr, data_size);
        }

        const MemoryRef source = memory.GetPhysicalRef(data_addr);
        const u32 available = static_cast<u32>(std::min<std::size_t>(source.GetSize(), data_size));
        if (available < data_size) {
            LOG_ERROR(Render, "Vertex buffer size {} exceeds available space {} at address {:#010x}",
                      data_size, available, data_addr);
            std::memset(buffer + used + available, 0, data_size - available);
        }
        if (available > 0) {
            std::memcpy(buffer + used, source.GetPtr(), available);
        }
        used += Common::AlignUp(data_size, 4);
    }

    // Attributes without a loader use their fixed value.
    for (u32 i = 0; i < 16; i++) {
        if (!vertex_attributes.IsDefaultAttribute(i)) {
            continue;
        }
        const u32 reg = regs.vs.GetRegisterForAttribute(i);
        if (loaded[reg]) {
            continue;
        }
        const auto& value = pica.input_default_attributes[i];
        layout.defaults[reg] = Common::MakeVec(value.x.ToFloat32(), value.y.ToFloat32(),
                                               value.z.ToFloat32(), value.w.ToFloat32());
        loaded[reg] = true;
    }

    // Indices, already flushed by AnalyzeVertexArray.
    layout.index_format = VS_INDEX_NONE;
    if (draw.is_indexed) {
        const bool index_u16 = regs.pipeline.index_array.format != 0;
        const u32 source_size = regs.pipeline.num_vertices * (index_u16 ? 2 : 1);
        const u32 index_size = regs.pipeline.num_vertices * (index_u16 || draw.direct ? 2 : 1);
        const u8* indices =
            memory.GetPhysicalPointer(base_address + regs.pipeline.index_array.offset);
        if (indices == nullptr) {
            std::memset(buffer + used, 0, index_size);
        } else if (draw.direct && !index_u16) {
            // Widened to 16 bits.
            u16* wide = reinterpret_cast<u16*>(buffer + used);
            for (u32 i = 0; i < regs.pipeline.num_vertices; i++) {
                wide[i] = indices[i];
            }
        } else {
            std::memcpy(buffer + used, indices, source_size);
        }
        layout.index_offset = used;
        layout.index_format = index_u16 ? VS_INDEX_U16 : VS_INDEX_U8;
        used += Common::AlignUp(index_size, 4);
    }
    layout.direct = draw.direct ? 1 : 0;

    layout.vertex_min = vertex_min;
    layout.vertex_base = regs.pipeline.vertex_offset;
    switch (regs.pipeline.triangle_topology.Value()) {
    case Pica::PipelineRegs::TriangleTopology::Strip:
        layout.topology = VS_TOPOLOGY_STRIP;
        break;
    case Pica::PipelineRegs::TriangleTopology::Fan:
        layout.topology = VS_TOPOLOGY_FAN;
        break;
    default:
        layout.topology = VS_TOPOLOGY_LIST;
        break;
    }

    vertex_buffer.Commit(used);
}

bool RasterizerMetal::Draw(HardwareDraw* hw_draw) {
    MICROPROFILE_SCOPE(Metal_Drawing);
    SyncDrawUniforms();

    const auto& output_merger = regs.framebuffer.output_merger;
    const auto& fb_regs = regs.framebuffer.framebuffer;
    const bool shadow_rendering = regs.framebuffer.IsShadowRendering();
    const bool has_stencil = regs.framebuffer.HasStencil();

    const u32 color_mask =
        fb_regs.allow_color_write != 0 ? (output_merger.depth_color_mask >> 8) & 0xF : 0;
    const bool write_color_fb = shadow_rendering || color_mask != 0;
    const bool write_depth_fb =
        fb_regs.allow_depth_stencil_write != 0 && output_merger.depth_write_enable;
    const bool stencil_test = output_merger.stencil_test.enable && has_stencil;
    const bool using_color_fb = fb_regs.GetColorBufferPhysicalAddress() != 0 && write_color_fb;
    const bool using_depth_fb =
        !shadow_rendering && fb_regs.GetDepthBufferPhysicalAddress() != 0 &&
        (write_depth_fb || output_merger.depth_test_enable != 0 || stencil_test);

    const auto fb_helper = res_cache.GetFramebufferSurfaces(using_color_fb, using_depth_fb);
    const Framebuffer* framebuffer = fb_helper.Framebuffer();
    if (!framebuffer->IsValid()) {
        return true;
    }

    const auto draw_rect = fb_helper.DrawRect();
    if (draw_rect.GetArea() == 0) {
        return true;
    }

    // Update scissor uniforms
    const auto [scissor_x1, scissor_y2, scissor_x2, scissor_y1] = fb_helper.Scissor();
    fs_data.scissor_x1 = scissor_x1;
    fs_data.scissor_x2 = scissor_x2;
    fs_data.scissor_y1 = scissor_y1;
    fs_data.scissor_y2 = scissor_y2;

    // Sync the texture units. This may record blits so it must happen before rendering begins.
    SyncTextureUnits(*framebuffer);

    // Sync and compile the fragment shader
    const bool has_color = framebuffer->Attachment(SurfaceType::Color) != nil && !shadow_rendering;
    const bool has_depth = framebuffer->Attachment(SurfaceType::Depth) != nil && !shadow_rendering;
    const auto fragment =
        pipeline_cache.UseFragmentShader(regs, user_config, has_color, has_depth);
    const bool vertex_depth = pipeline_cache.UsesVertexDepth(regs, has_depth);
    const auto& vertex = hw_draw ? hw_draw->vertex : pipeline_cache.TrivialVertexShader();

    // Sync the LUTs within the texture buffers
    SyncAndUploadLUTs();
    SyncAndUploadLUTsLF();

    // Draws that only touch depth and stencil (stencil shadow volumes, depth pre-passes) get a
    // framebuffer without the color buffer. Between color draws on the same depth buffer that
    // would end the render pass and start a new one each time (Luigi's Mansion does it about 36
    // times a frame), and every pass loads and stores its attachments. Keep drawing in the
    // active pass instead, with color writes off. Not for textured draws: a texture could be
    // the color buffer of the pass, which the feedback handling above did not see.
    RenderTarget target = framebuffer->Target();
    bool in_color_pass = false;
    const auto& tex_config = regs.texturing.main_config;
    const bool textured = tex_config.texture0_enable || tex_config.texture1_enable ||
                          tex_config.texture2_enable;
    if (const RenderTarget* active = scheduler.ActiveTarget();
        active != nullptr && !shadow_rendering && !textured && target.color == nil &&
        target.depth != nil &&
        active->color != nil && active->depth == target.depth &&
        active->depth_level == target.depth_level && active->has_stencil == target.has_stencil &&
        active->width == target.width && active->height == target.height) {
        target = *active;
        in_color_pass = true;
    }

    PipelineKey key;
    DepthStencilKey ds_key;
    SyncFixedState(*framebuffer, PipelineCache::HashOf(fragment), key, ds_key);
    if (in_color_pass) {
        key.color_format = static_cast<u32>(target.color.pixelFormat);
        key.write_mask = static_cast<u8>(MTLColorWriteMaskNone);
        key.blend_enable = 0;
    }
    key.vs_hash = PipelineCache::HashOf(vertex);

    // A draw that writes no color (stencil shadow volumes, depth pre-passes) needs its fragment
    // function only to discard pixels or to output the depth. Otherwise it is drawn without one:
    // the PICA keeps the lighting and texturing state of the draws before, and the GPU ran all of
    // it for every covered pixel only to throw the color away.
    const auto& output_merger = regs.framebuffer.output_merger;
    PixelTraits traits{};
    traits.depth_stencil_only =
        !shadow_rendering &&
        (key.write_mask == static_cast<u8>(MTLColorWriteMaskNone) ||
         key.color_format == static_cast<u32>(MTLPixelFormatInvalid));
    traits.writes_depth = has_depth && !shadow_rendering && !vertex_depth;
    traits.alpha_test =
        output_merger.alpha_test.enable.Value() != 0 &&
        output_merger.alpha_test.func.Value() != Pica::FramebufferRegs::CompareFunc::Always;
    traits.scissor_test = regs.rasterizer.scissor_test.mode.Value() !=
                          Pica::RasterizerRegs::ScissorMode::Disabled;
    traits.lit = regs.lighting.disable.Value() == 0;
    traits.blended = key.blend_enable != 0;
    traits.no_fragment = traits.depth_stencil_only && !traits.writes_depth &&
                         !traits.alpha_test && !traits.scissor_test;
    NotePixelTraits(traits);
    PipelineCache::ShaderHandle fragment_used = fragment;
    if (traits.no_fragment) {
        key.no_fragment = 1;
        key.fs_hash = 0;
        key.blend_enable = 0;
        fragment_used = nullptr;
    }
    // With async shader compilation a draw whose pipeline is still being built is skipped, but
    // small draws (fullscreen passes, UI quads) wait: skipping them is much more visible.
    const u32 draw_vertices =
        hw_draw ? regs.pipeline.num_vertices : static_cast<u32>(vertex_batch.size());
    id<MTLRenderPipelineState> pipeline =
        pipeline_cache.GetPipeline(key, vertex, fragment_used, draw_vertices <= 6);
    if (pipeline == nil) {
        Stats().skipped.fetch_add(1, std::memory_order_relaxed);
        // Only a hole in a target the size of a screen makes a frame worth holding back; shadow
        // maps and small off-screen textures are not seen as a flicker.
        const u32 native_width = framebuffer->Width() / std::max(framebuffer->Scale(), 1u);
        const u32 native_height = framebuffer->Height() / std::max(framebuffer->Scale(), 1u);
        if (has_color && native_width >= 240 && native_height >= 320) {
            Stats().frame_skipped.fetch_add(1, std::memory_order_relaxed);
        }
        return true;
    }
    id<MTLDepthStencilState> depth_stencil = pipeline_cache.GetDepthStencilState(ds_key);

    // Upload the vertices before beginning rendering, as waiting on the stream buffer may
    // submit the current command buffer.
    u32 vertex_count = 0;
    u32 vertex_offset = 0;
    if (hw_draw) {
        UploadHardwareVertices(*hw_draw);
        vertex_count = hw_draw->vertex_count;
        if (pica.vs_setup.uniforms_dirty) {
            vs_pica_data.SetFromRegs(pica.vs_setup);
            pica.vs_setup.uniforms_dirty = false;
        }
    } else {
        vertex_count = static_cast<u32>(vertex_batch.size());
        const u32 vertex_size = vertex_count * static_cast<u32>(sizeof(HardwareVertex));
        const auto [vertex_data, offset, _] = vertex_buffer.Map(vertex_size, 16);
        std::memcpy(vertex_data, vertex_batch.data(), vertex_size);
        vertex_buffer.Commit(vertex_size);
        vertex_offset = offset;
    }

    // Clamp the scissor to the render area.
    const u32 scissor_x = std::min(draw_rect.left, target.width);
    const u32 scissor_y = std::min(draw_rect.bottom, target.height);
    const u32 scissor_w = std::min(draw_rect.GetWidth(), target.width - scissor_x);
    const u32 scissor_h = std::min(draw_rect.GetHeight(), target.height - scissor_y);
    if (scissor_w == 0 || scissor_h == 0) {
        return true;
    }

    id<MTLRenderCommandEncoder> encoder = scheduler.BeginRendering(target);
    if (scheduler.EncoderGeneration() != bound_generation) {
        bound_generation = scheduler.EncoderGeneration();
        encoder_cache = {};
        [encoder setFragmentBuffer:texture_lf_buffer.Handle()
                            offset:0
                           atIndex:MSL::FS_BUFFER_LUT_LF];
        [encoder setFragmentBuffer:texture_buffer.Handle() offset:0 atIndex:MSL::FS_BUFFER_LUT_RG];
        [encoder setFragmentBuffer:texture_buffer.Handle()
                            offset:0
                           atIndex:MSL::FS_BUFFER_LUT_RGBA];
        [encoder setVertexBuffer:vertex_buffer.Handle()
                          offset:0
                         atIndex:MSL::VS_BUFFER_VERTEX_BASE];
    }

    // Only send what changed since the previous draw on this encoder.
    EncoderCache& cache = encoder_cache;
    if (pipeline != cache.pipeline) {
        [encoder setRenderPipelineState:pipeline];
        cache.pipeline = pipeline;
    }
    if (depth_stencil != cache.depth_stencil) {
        [encoder setDepthStencilState:depth_stencil];
        cache.depth_stencil = depth_stencil;
    }
    const MTLWinding winding = PicaToMTL::FrontFace(regs.rasterizer.cull_mode);
    const MTLCullMode cull = PicaToMTL::CullMode(regs.rasterizer.cull_mode, fb_regs.IsFlipped());
    const u32 stencil_reference = output_merger.stencil_test.reference_value;
    const auto blend_color = PicaToMTL::ColorRGBA8(output_merger.blend_const.raw);
    const std::array<f32, 4> blend_color_array{blend_color.x, blend_color.y, blend_color.z,
                                               blend_color.w};
    const auto viewport = fb_helper.Viewport();
    const MTLViewport mtl_viewport{
        .originX = static_cast<double>(viewport.x),
        .originY = static_cast<double>(viewport.y),
        .width = static_cast<double>(viewport.width),
        .height = static_cast<double>(viewport.height),
        .znear = 0.0,
        .zfar = 1.0,
    };
    const MTLScissorRect mtl_scissor{scissor_x, scissor_y, scissor_w, scissor_h};
    if (!cache.state_valid || winding != cache.winding) {
        [encoder setFrontFacingWinding:winding];
        cache.winding = winding;
    }
    if (!cache.state_valid || cull != cache.cull) {
        [encoder setCullMode:cull];
        cache.cull = cull;
    }
    if (!cache.state_valid || stencil_reference != cache.stencil_reference) {
        [encoder setStencilReferenceValue:stencil_reference];
        cache.stencil_reference = stencil_reference;
    }
    if (!cache.state_valid || blend_color_array != cache.blend_color) {
        [encoder setBlendColorRed:blend_color.x
                            green:blend_color.y
                             blue:blend_color.z
                            alpha:blend_color.w];
        cache.blend_color = blend_color_array;
    }
    if (!cache.state_valid || std::memcmp(&mtl_viewport, &cache.viewport, sizeof(MTLViewport))) {
        [encoder setViewport:mtl_viewport];
        cache.viewport = mtl_viewport;
    }
    if (!cache.state_valid || std::memcmp(&mtl_scissor, &cache.scissor, sizeof(MTLScissorRect))) {
        [encoder setScissorRect:mtl_scissor];
        cache.scissor = mtl_scissor;
    }
    cache.state_valid = true;
    fs_data.blend_color = blend_color;

    // Depth mapped in the vertex shader: the depth is clamped instead of clipped, the clip
    // distances of the vertex shader keep the PICA clip volume.
    const MSL::VSDepthData depth_data{
        .scale = vertex_depth ? fs_data.depth_scale : 0.0f,
        .offset = vertex_depth ? fs_data.depth_offset : 0.0f,
        .enabled = vertex_depth ? 1u : 0u,
        .pad = 0,
    };
    if (!cache.depth_valid || cache.depth.enabled != depth_data.enabled) {
        [encoder setDepthClipMode:vertex_depth ? MTLDepthClipModeClamp : MTLDepthClipModeClip];
    }
    if (!cache.depth_valid ||
        std::memcmp(&depth_data, &cache.depth, sizeof(MSL::VSDepthData)) != 0) {
        [encoder setVertexBytes:&depth_data
                         length:sizeof(depth_data)
                        atIndex:MSL::VS_BUFFER_DEPTH];
        cache.depth = depth_data;
        cache.depth_valid = true;
    }

    // Uniforms are small enough to be pushed inline, but only when they changed.
    if (!cache.vs_uniforms_valid ||
        std::memcmp(&vs_data, cache.vs_uniforms.data(), sizeof(vs_data)) != 0) {
        [encoder setVertexBytes:&vs_data length:sizeof(vs_data) atIndex:MSL::VS_BUFFER_UNIFORMS];
        std::memcpy(cache.vs_uniforms.data(), &vs_data, sizeof(vs_data));
        cache.vs_uniforms_valid = true;
    }
    if (!cache.fs_uniforms_valid ||
        std::memcmp(&fs_data, cache.fs_uniforms.data(), sizeof(fs_data)) != 0) {
        [encoder setFragmentBytes:&fs_data length:sizeof(fs_data) atIndex:MSL::FS_BUFFER_UNIFORMS];
        std::memcpy(cache.fs_uniforms.data(), &fs_data, sizeof(fs_data));
        cache.fs_uniforms_valid = true;
    }
    vs_data_dirty = false;
    fs_data_dirty = false;

    for (u32 slot = 0; slot < texture_bindings.size(); slot++) {
        id<MTLTexture> texture = texture_bindings[slot].texture;
        if (texture != nil && texture != cache.textures[slot]) {
            [encoder setFragmentTexture:texture atIndex:slot];
            cache.textures[slot] = texture;
        }
    }
    const auto bind_sampler = [&](u32 texture_slot, u32 sampler_slot) {
        id<MTLSamplerState> sampler = texture_bindings[texture_slot].sampler;
        if (sampler != nil && sampler != cache.samplers[sampler_slot]) {
            [encoder setFragmentSamplerState:sampler atIndex:sampler_slot];
            cache.samplers[sampler_slot] = sampler;
        }
    };
    bind_sampler(MSL::FS_TEXTURE_0, MSL::FS_SAMPLER_0);
    bind_sampler(MSL::FS_TEXTURE_1, MSL::FS_SAMPLER_1);
    bind_sampler(MSL::FS_TEXTURE_2, MSL::FS_SAMPLER_2);
    if (shadow_rendering) {
        id<MTLTexture> shadow_buffer = framebuffer->ShadowBuffer();
        if (shadow_buffer != cache.shadow_buffer) {
            [encoder setFragmentTexture:shadow_buffer atIndex:MSL::FS_TEXTURE_SHADOW_BUFFER];
            cache.shadow_buffer = shadow_buffer;
        }
    }

    if (hw_draw) {
        [encoder setVertexBuffer:vertex_buffer.Handle()
                          offset:hw_draw->data_offset
                         atIndex:MSL::VS_BUFFER_VERTEX_DATA];
        [encoder setVertexBytes:&hw_draw->layout
                         length:sizeof(hw_draw->layout)
                        atIndex:MSL::VS_BUFFER_VERTEX_LAYOUT];
        if (!cache.pica_uniforms_valid ||
            std::memcmp(&vs_pica_data, cache.pica_uniforms.data(), sizeof(vs_pica_data)) != 0) {
            [encoder setVertexBytes:&vs_pica_data
                             length:sizeof(vs_pica_data)
                            atIndex:MSL::VS_BUFFER_PICA_UNIFORMS];
            std::memcpy(cache.pica_uniforms.data(), &vs_pica_data, sizeof(vs_pica_data));
            cache.pica_uniforms_valid = true;
        }
    } else {
        [encoder setVertexBufferOffset:vertex_offset atIndex:MSL::VS_BUFFER_VERTEX_BASE];
    }
    if (hw_draw) {
        NoteHardwareDraw(hw_draw->direct ? 0 : (regs.lighting.disable.Value() == 0 ? 1 : 2),
                         hw_draw->direct ? regs.pipeline.num_vertices : hw_draw->vertex_count);
    }
    if (hw_draw && hw_draw->direct) {
        const MTLPrimitiveType primitive =
            hw_draw->strip ? MTLPrimitiveTypeTriangleStrip : MTLPrimitiveTypeTriangle;
        if (hw_draw->is_indexed) {
            [encoder drawIndexedPrimitives:primitive
                                indexCount:regs.pipeline.num_vertices
                                 indexType:MTLIndexTypeUInt16
                               indexBuffer:vertex_buffer.Handle()
                         indexBufferOffset:hw_draw->data_offset + hw_draw->layout.index_offset];
        } else {
            [encoder drawPrimitives:primitive
                        vertexStart:regs.pipeline.vertex_offset
                        vertexCount:regs.pipeline.num_vertices];
        }
    } else {
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:vertex_count];
    }
    Stats().draws.fetch_add(1, std::memory_order_relaxed);
    scheduler.NoteDraw();
    return true;
}

void RasterizerMetal::SyncTextureUnits(const Framebuffer& framebuffer) {
    const auto pica_textures = regs.texturing.GetTextures();
    const auto texture0_type = regs.texturing.texture0.type.Value();

    // Take the fallback resources by value. Looking up a texture surface or sampler below may
    // create a new cache entry and grow the slot storage, which moves every entry and leaves
    // references to cache entries dangling.
    id<MTLTexture> null_texture = res_cache.GetSurface(VideoCore::NULL_SURFACE_ID).Texture();
    id<MTLTexture> null_storage = res_cache.GetSurface(VideoCore::NULL_SURFACE_ID).StorageTexture();
    id<MTLTexture> null_cube_texture =
        res_cache.GetSurface(VideoCore::NULL_SURFACE_CUBE_ID).Texture();
    id<MTLSamplerState> null_sampler = res_cache.GetSampler(VideoCore::NULL_SAMPLER_ID).Handle();
    id<MTLTexture> color_attachment = framebuffer.Attachment(SurfaceType::Color);

    texture_bindings = {};

    const auto bind_2d = [&](u32 slot, const Pica::TexturingRegs::FullTextureConfig& texture) {
        if (!texture.enabled) {
            texture_bindings[slot] = {null_texture, null_sampler};
            return;
        }
        Surface& surface = res_cache.GetTextureSurface(texture);
        Sampler& sampler = res_cache.GetSampler(texture.config);
        id<MTLTexture> view = surface.Texture();
        if (view == nil) {
            view = null_texture;
        } else if (view == color_attachment) {
            // Sampling the render target being drawn to is undefined, sample a copy instead.
            view = surface.CopyTexture();
        }
        texture_bindings[slot] = {view, sampler.Handle()};
    };

    switch (texture0_type) {
    case TextureType::Texture2D:
    case TextureType::Projection2D:
        bind_2d(MSL::FS_TEXTURE_0, pica_textures[0]);
        break;
    case TextureType::TextureCube:
        if (pica_textures[0].enabled) {
            BindTextureCube(pica_textures[0]);
        } else {
            texture_bindings[MSL::FS_TEXTURE_0] = {null_cube_texture, null_sampler};
        }
        break;
    case TextureType::Shadow2D:
        if (pica_textures[0].enabled) {
            Surface& surface = res_cache.GetTextureSurface(pica_textures[0]);
            surface.flags |= VideoCore::SurfaceFlagBits::ShadowSource;
            id<MTLTexture> storage = surface.StorageTexture();
            texture_bindings[MSL::FS_TEXTURE_0].texture = storage != nil ? storage : null_storage;
        } else {
            texture_bindings[MSL::FS_TEXTURE_0].texture = null_storage;
        }
        break;
    case TextureType::ShadowCube:
        if (pica_textures[0].enabled) {
            BindShadowCube(pica_textures[0], null_storage);
        } else {
            for (u32 face = 0; face < 6; face++) {
                texture_bindings[MSL::FS_TEXTURE_0 + face].texture = null_storage;
            }
        }
        break;
    default:
        break;
    }

    bind_2d(MSL::FS_TEXTURE_1, pica_textures[1]);
    bind_2d(MSL::FS_TEXTURE_2, pica_textures[2]);
}

void RasterizerMetal::BindShadowCube(const Pica::TexturingRegs::FullTextureConfig& texture,
                                     id<MTLTexture> null_storage) {
    using CubeFace = Pica::TexturingRegs::CubeFace;
    auto info = Pica::Texture::TextureInfo::FromPicaRegister(texture.config, texture.format);
    constexpr std::array faces = {
        CubeFace::PositiveX, CubeFace::NegativeX, CubeFace::PositiveY,
        CubeFace::NegativeY, CubeFace::PositiveZ, CubeFace::NegativeZ,
    };

    for (CubeFace face : faces) {
        const u32 binding = static_cast<u32>(face);
        info.physical_address = regs.texturing.GetCubePhysicalAddress(face);

        const VideoCore::SurfaceId surface_id = res_cache.GetTextureSurface(info);
        Surface& surface = res_cache.GetSurface(surface_id);
        surface.flags |= VideoCore::SurfaceFlagBits::ShadowSource;
        id<MTLTexture> storage = surface.StorageTexture();
        texture_bindings[MSL::FS_TEXTURE_0 + binding].texture =
            storage != nil ? storage : null_storage;
    }
}

void RasterizerMetal::BindTextureCube(const Pica::TexturingRegs::FullTextureConfig& texture) {
    using CubeFace = Pica::TexturingRegs::CubeFace;
    const VideoCore::TextureCubeConfig config = {
        .px = regs.texturing.GetCubePhysicalAddress(CubeFace::PositiveX),
        .nx = regs.texturing.GetCubePhysicalAddress(CubeFace::NegativeX),
        .py = regs.texturing.GetCubePhysicalAddress(CubeFace::PositiveY),
        .ny = regs.texturing.GetCubePhysicalAddress(CubeFace::NegativeY),
        .pz = regs.texturing.GetCubePhysicalAddress(CubeFace::PositiveZ),
        .nz = regs.texturing.GetCubePhysicalAddress(CubeFace::NegativeZ),
        .width = texture.config.width,
        .levels = texture.config.lod.max_level + 1,
        .format = texture.format,
    };

    Surface& surface = res_cache.GetTextureCube(config);
    Sampler& sampler = res_cache.GetSampler(texture.config);
    texture_bindings[MSL::FS_TEXTURE_0] = {surface.Texture(), sampler.Handle()};
}

void RasterizerMetal::SyncAndUploadLUTsLF() {
    constexpr std::size_t max_size =
        sizeof(Common::Vec2f) * 256 * Pica::LightingRegs::NumLightingSampler +
        sizeof(Common::Vec2f) * 128; // fog

    if (!pica.lighting.lut_dirty && !pica.fog.lut_dirty) {
        return;
    }

    std::size_t bytes_used = 0;
    auto [buffer, offset, invalidate] = texture_lf_buffer.Map(max_size, sizeof(Common::Vec4f));

    if (invalidate) {
        pica.lighting.lut_dirty = pica.lighting.LutAllDirty;
        pica.fog.lut_dirty = true;
    }

    // With fast lighting the entries are half2, half the bytes for every lookup (the 3DS tables
    // are 12-bit values, half precision holds them almost exactly). See MetalFSConfig::half_luts.
    struct HalfPair {
        _Float16 value;
        _Float16 diff;
    };
    const bool half = GetQualityOptions().fast_lighting;
    const std::size_t entry_size = half ? sizeof(HalfPair) : sizeof(Common::Vec2f);
    const auto write = [&](std::size_t index, float value, float diff) {
        if (half) {
            reinterpret_cast<HalfPair*>(buffer + bytes_used)[index] = {
                static_cast<_Float16>(value), static_cast<_Float16>(diff)};
        } else {
            reinterpret_cast<Common::Vec2f*>(buffer + bytes_used)[index] = {value, diff};
        }
    };

    // Sync the lighting luts
    while (pica.lighting.lut_dirty) {
        u32 index = std::countr_zero(pica.lighting.lut_dirty);
        pica.lighting.lut_dirty &= ~(1 << index);

        const auto& source_lut = pica.lighting.luts[index];
        for (u32 i = 0; i < source_lut.size(); i++) {
            write(i, source_lut[i].ToFloat(), source_lut[i].DiffToFloat());
        }
        fs_data.lighting_lut_offset[index / 4][index % 4] =
            static_cast<int>((offset + bytes_used) / entry_size);
        bytes_used += source_lut.size() * entry_size;
    }

    // Sync the fog lut
    if (pica.fog.lut_dirty) {
        for (u32 i = 0; i < pica.fog.lut.size(); i++) {
            write(i, pica.fog.lut[i].ToFloat(), pica.fog.lut[i].DiffToFloat());
        }
        fs_data.fog_lut_offset = static_cast<int>((offset + bytes_used) / entry_size);
        bytes_used += pica.fog.lut.size() * entry_size;
        pica.fog.lut_dirty = false;
    }

    texture_lf_buffer.Commit(static_cast<u32>(bytes_used));
}

void RasterizerMetal::SyncAndUploadLUTs() {
    const auto& proctex = pica.proctex;
    constexpr std::size_t max_size =
        sizeof(Common::Vec2f) * 128 * 3 + // proctex: noise + color + alpha
        sizeof(Common::Vec4f) * 256 +     // proctex
        sizeof(Common::Vec4f) * 256;      // proctex diff

    if (!pica.proctex.table_dirty) {
        return;
    }

    std::size_t bytes_used = 0;
    auto [buffer, offset, invalidate] = texture_buffer.Map(max_size, sizeof(Common::Vec4f));

    if (invalidate) {
        pica.proctex.table_dirty = pica.proctex.TableAllDirty;
    }

    // helper function for SyncProcTexNoiseLUT/ColorMap/AlphaMap
    const auto sync_proctex_value_lut =
        [&](const std::array<Pica::PicaCore::ProcTex::ValueEntry, 128>& lut, int& lut_offset) {
            Common::Vec2f* new_data = reinterpret_cast<Common::Vec2f*>(buffer + bytes_used);
            for (u32 i = 0; i < lut.size(); i++) {
                new_data[i] = {lut[i].ToFloat(), lut[i].DiffToFloat()};
            }
            lut_offset = static_cast<int>((offset + bytes_used) / sizeof(Common::Vec2f));
            bytes_used += lut.size() * sizeof(Common::Vec2f);
        };

    if (pica.proctex.noise_lut_dirty) {
        sync_proctex_value_lut(proctex.noise_table, fs_data.proctex_noise_lut_offset);
    }
    if (pica.proctex.color_map_dirty) {
        sync_proctex_value_lut(proctex.color_map_table, fs_data.proctex_color_map_offset);
    }
    if (pica.proctex.alpha_map_dirty) {
        sync_proctex_value_lut(proctex.alpha_map_table, fs_data.proctex_alpha_map_offset);
    }

    // The float4 tables must be aligned to their element size since they share the buffer.
    bytes_used = Common::AlignUp(bytes_used, sizeof(Common::Vec4f));

    if (pica.proctex.color_lut_dirty) {
        Common::Vec4f* new_data = reinterpret_cast<Common::Vec4f*>(buffer + bytes_used);
        for (u32 i = 0; i < proctex.color_table.size(); i++) {
            new_data[i] = proctex.color_table[i].ToVector() / 255.0f;
        }
        fs_data.proctex_lut_offset =
            static_cast<int>((offset + bytes_used) / sizeof(Common::Vec4f));
        bytes_used += proctex.color_table.size() * sizeof(Common::Vec4f);
    }

    if (pica.proctex.color_diff_lut_dirty) {
        Common::Vec4f* new_data = reinterpret_cast<Common::Vec4f*>(buffer + bytes_used);
        for (u32 i = 0; i < proctex.color_diff_table.size(); i++) {
            new_data[i] = proctex.color_diff_table[i].ToVector() / 255.0f;
        }
        fs_data.proctex_diff_lut_offset =
            static_cast<int>((offset + bytes_used) / sizeof(Common::Vec4f));
        bytes_used += proctex.color_diff_table.size() * sizeof(Common::Vec4f);
    }

    pica.proctex.table_dirty = 0;
    texture_buffer.Commit(static_cast<u32>(bytes_used));
}

void RasterizerMetal::FlushAll() {
    const ReadbackReason reason{"flush all"};
    res_cache.FlushAll();
}

void RasterizerMetal::FlushRegion(PAddr addr, u32 size) {
    const ReadbackReason reason{size <= 8 ? "CPU access to GPU memory" : "memory transfer (GSP)"};
    res_cache.FlushRegion(addr, size);
}

void RasterizerMetal::InvalidateRegion(PAddr addr, u32 size) {
    const ReadbackReason reason{"CPU write to GPU memory"};
    res_cache.InvalidateRegion(addr, size);
}

void RasterizerMetal::FlushAndInvalidateRegion(PAddr addr, u32 size) {
    const ReadbackReason reason{"flush and invalidate"};
    res_cache.FlushRegion(addr, size);
    res_cache.InvalidateRegion(addr, size);
}

void RasterizerMetal::ClearAll(bool flush) {
    res_cache.ClearAll(flush);
}

bool RasterizerMetal::AccelerateDisplayTransfer(const Pica::DisplayTransferConfig& config) {
    const bool accelerated = res_cache.AccelerateDisplayTransfer(config);
    // The first transfers show whether the game's display transfers run on the GPU at all.
    static u32 logged_transfers = 0;
    if (logged_transfers < 24) {
        logged_transfers++;
        LOG_INFO(Render, "Display transfer {:#010x} -> {:#010x} {}x{} -> {}x{}: {}",
                 config.GetPhysicalInputAddress(), config.GetPhysicalOutputAddress(),
                 config.input_width.Value(), config.input_height.Value(),
                 config.output_width.Value(), config.output_height.Value(),
                 accelerated ? "accelerated" : "NOT accelerated (CPU path)");
    }
    return accelerated;
}

bool RasterizerMetal::AccelerateTextureCopy(const Pica::DisplayTransferConfig& config) {
    return res_cache.AccelerateTextureCopy(config);
}

bool RasterizerMetal::AccelerateFill(const Pica::MemoryFillConfig& config) {
    return res_cache.AccelerateFill(config);
}

bool RasterizerMetal::AccelerateDisplay(const Pica::FramebufferConfig& config,
                                        PAddr framebuffer_addr, u32 pixel_stride,
                                        ScreenInfo& screen_info) {
    if (framebuffer_addr == 0) [[unlikely]] {
        return false;
    }

    VideoCore::SurfaceParams src_params;
    src_params.addr = framebuffer_addr;
    src_params.width = std::min(config.width.Value(), pixel_stride);
    src_params.height = config.height;
    src_params.stride = pixel_stride;
    src_params.is_tiled = false;
    src_params.pixel_format = VideoCore::PixelFormatFromGPUPixelFormat(config.color_format);
    src_params.UpdateParams();

    const auto [src_surface_id, src_rect] =
        res_cache.GetSurfaceSubRect(src_params, VideoCore::ScaleMatch::Ignore, true);

    if (!src_surface_id) {
        return false;
    }

    Surface& src_surface = res_cache.GetSurface(src_surface_id);
    const u32 scaled_width = src_surface.GetScaledWidth();
    const u32 scaled_height = src_surface.GetScaledHeight();

    screen_info.texcoords = Common::Rectangle<f32>(
        (float)src_rect.bottom / (float)scaled_height, (float)src_rect.left / (float)scaled_width,
        (float)src_rect.top / (float)scaled_height, (float)src_rect.right / (float)scaled_width);
    screen_info.texture = src_surface.Texture();
    screen_info.width = config.width;
    screen_info.height = config.height;

    // Log what is displayed whenever it changes: a screen shown from a surface that is not at the
    // internal resolution looks blurry however it is scaled afterwards.
    struct Logged {
        const ScreenInfo* screen;
        u64 signature;
    };
    static std::array<Logged, 3> logged{};
    const u64 signature = (static_cast<u64>(screen_info.texture.width) << 40) |
                          (static_cast<u64>(screen_info.texture.height) << 16) |
                          static_cast<u64>(src_surface.res_scale);
    for (auto& entry : logged) {
        if (entry.screen == &screen_info || entry.screen == nullptr) {
            if (entry.signature != signature) {
                entry.screen = &screen_info;
                entry.signature = signature;
                LOG_INFO(Render,
                         "Display {:#010x} {}x{} fmt {}: surface res scale {} (setting {}x), "
                         "texture {}x{}, covers {}x{}",
                         framebuffer_addr, config.width.Value(), config.height.Value(),
                         static_cast<u32>(config.color_format.Value()), src_surface.res_scale,
                         Settings::values.resolution_factor.GetValue(),
                         static_cast<u32>(screen_info.texture.width),
                         static_cast<u32>(screen_info.texture.height), src_rect.GetWidth(),
                         src_rect.GetHeight());
            }
            break;
        }
    }
    return true;
}

} // namespace Metal
