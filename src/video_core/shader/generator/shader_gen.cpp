// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/bit_set.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "video_core/pica/regs_internal.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/shader/generator/shader_gen.h"

namespace Pica::Shader::Generator {

void PicaGSConfigState::Init(const Pica::RegsInternal& regs) {
    vs_output_attributes_count = Common::BitSet<u32>(regs.vs.output_mask).Count();
    gs_output_attributes_count = vs_output_attributes_count;
    vs_output_total = regs.rasterizer.vs_output_total;

    memcpy(vs_output_attributes.data(), regs.rasterizer.vs_output_attributes,
           vs_output_total * sizeof(Pica::RasterizerRegs::VSOutputAttributes));
}

std::array<PicaGSConfigState::SemanticMap, 24> PicaGSConfigState::GetSemanticMaps() const {
    std::array<SemanticMap, 24> semantic_maps{};

    semantic_maps.fill({16, 0});
    for (u32 attrib = 0; attrib < vs_output_total; ++attrib) {
        const std::array semantics{
            vs_output_attributes[attrib].map_x.Value(),
            vs_output_attributes[attrib].map_y.Value(),
            vs_output_attributes[attrib].map_z.Value(),
            vs_output_attributes[attrib].map_w.Value(),
        };
        for (u32 comp = 0; comp < 4; ++comp) {
            const auto semantic = semantics[comp];
            if (static_cast<std::size_t>(semantic) < 24) {
                semantic_maps[static_cast<std::size_t>(semantic)] = {attrib, comp};
            } else if (semantic != Pica::RasterizerRegs::VSOutputAttributes::INVALID) {
                LOG_ERROR(Render, "Invalid/unknown semantic id: {}", semantic);
            }
        }
    }

    return semantic_maps;
}

void PicaVSConfigState::Init(const Pica::RegsInternal& regs, Pica::ShaderSetup& setup) {
    setup.DoProgramCodeFixup();
    program_hash = setup.GetProgramCodeHash();
    swizzle_hash = setup.GetSwizzleDataHash();
    main_offset = regs.vs.main_offset;

    lighting_disable = regs.lighting.disable;

    num_outputs = 0;
    output_map.fill(16);

    for (u32 reg : Common::BitSet<u32>(regs.vs.output_mask)) {
        output_map[reg] = num_outputs++;
    }

    gs_state.Init(regs);
}

PicaVSConfig::PicaVSConfig(const Pica::RegsInternal& regs, Pica::ShaderSetup& setup) {
    state.Init(regs, setup);
}

PicaFixedGSConfig::PicaFixedGSConfig(const Pica::RegsInternal& regs) {
    state.Init(regs);
}

} // namespace Pica::Shader::Generator
