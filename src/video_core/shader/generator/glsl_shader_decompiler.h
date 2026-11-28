// Copyright 2017-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <functional>
#include <string>
#include "video_core/pica/shader_setup.h"

namespace Pica::Shader::Generator::GLSL {

using RegGetter = std::function<std::string(u32)>;

std::string DecompileProgram(const Pica::ProgramCode& program_code,
                             const Pica::SwizzleData& swizzle_data, u32 main_offset,
                             u16 bool_uniforms, const RegGetter& inputreg_getter,
                             const RegGetter& outputreg_getter, bool sanitize_mul);

} // namespace Pica::Shader::Generator::GLSL
