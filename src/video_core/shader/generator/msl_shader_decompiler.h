// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <functional>
#include <string>
#include "video_core/pica/shader_setup.h"

namespace Pica::Shader::Generator::MSL {

using RegGetter = std::function<std::string(u32)>;

/**
 * Translates a PICA shader program to MSL. The result declares the registers and defines
 * `bool exec_shader()` plus the subroutines it calls, all meant to be members of a struct that
 * also has a `constant PicaUniforms* uniforms` member and the members named by the register
 * getters. Returns an empty string if the program cannot be translated.
 */
std::string DecompileProgram(const Pica::ProgramCode& program_code,
                             const Pica::SwizzleData& swizzle_data, u32 main_offset,
                             const RegGetter& inputreg_getter, const RegGetter& outputreg_getter,
                             bool sanitize_mul, u32 fixed_bools_mask = 0, u32 fixed_bools = 0);

/**
 * The bool uniforms the program's flow control tests (jmpu, callu, ifu), as a bit mask. The
 * program can be made for given values of them (DecompileProgram's fixed_bools), which are
 * constant for a draw: the compiler then drops the paths not taken.
 */
u32 BoolUniformsTested(const Pica::ProgramCode& program_code);

} // namespace Pica::Shader::Generator::MSL
