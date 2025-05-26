// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "video_core/pica/shader_setup.h"
#include "video_core/shader/generator/shader_uniforms.h"

namespace Pica::Shader::Generator {

void VSPicaUniformData::SetFromRegs(const Pica::ShaderSetup& setup) {
    b = 0;
    for (u32 j = 0; j < setup.uniforms.b.size(); j++) {
        b |= setup.uniforms.b[j] << j;
    }
    for (u32 j = 0; j < setup.uniforms.i.size(); j++) {
        const auto& value = setup.uniforms.i[j];
        i[j] = Common::MakeVec<u32>(value.x, value.y, value.z, value.w);
    }
    for (u32 j = 0; j < setup.uniforms.f.size(); j++) {
        const auto& value = setup.uniforms.f[j];
        f[j] = Common::MakeVec<f32>(value.x.ToFloat32(), value.y.ToFloat32(), value.z.ToFloat32(),
                                    value.w.ToFloat32());
    }
}

} // namespace Pica::Shader::Generator
