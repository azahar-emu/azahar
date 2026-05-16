// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/assert.h"
#include "common/bit_set.h"
#include "video_core/pica/regs_shader.h"
#include "video_core/pica/shader_unit.h"

namespace Pica {

ShaderUnit::ShaderUnit(GeometryEmitter* emitter) : emitter_ptr{emitter} {}

ShaderUnit::~ShaderUnit() = default;

void ShaderUnit::LoadInput(const ShaderRegs& config, const AttributeBuffer& buffer) {
    const u32 max_attribute = config.max_input_attribute_index;
    for (u32 attr = 0; attr <= max_attribute; ++attr) {
        const u32 reg = config.GetRegisterForAttribute(attr);
        input[reg] = buffer[attr];
    }
}

void ShaderUnit::WriteOutput(const ShaderRegs& config, AttributeBuffer& buffer) {
    u32 output_index{};
    for (u32 reg : Common::BitSet<u32>(config.output_mask)) {
        buffer[output_index++] = output[reg];
    }
}

void GeometryEmitter::Emit(std::span<Common::Vec4<f24>, 16> output_regs) {
    ASSERT(emit_state.vertex_id < 3);

    u32 output_index{};
    for (u32 reg : Common::BitSet<u32>(output_mask)) {
        buffer[emit_state.vertex_id][output_index++] = output_regs[reg];
    }

    if (emit_state.prim_emit) {
        if (emit_state.winding) {
            handlers->winding_setter();
        }
        for (std::size_t i = 0; i < buffer.size(); ++i) {
            handlers->vertex_handler(buffer[i]);
        }
    }
}

GeometryShaderUnit::GeometryShaderUnit() : ShaderUnit{&emitter} {}

GeometryShaderUnit::~GeometryShaderUnit() = default;

void GeometryShaderUnit::SetVertexHandlers(VertexHandler vertex_handler,
                                           WindingSetter winding_setter) {
    emitter.handlers = new Handlers;
    emitter.handlers->vertex_handler = vertex_handler;
    emitter.handlers->winding_setter = winding_setter;
}

void GeometryShaderUnit::ConfigOutput(const ShaderRegs& config) {
    emitter.output_mask = config.output_mask;
}

} // namespace Pica
