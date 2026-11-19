// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

// Generates a corpus of MSL shaders covering the different code paths of the Metal shader
// generators. CI compiles the output with Apple's offline Metal compiler (xcrun metal) to catch
// generator regressions without needing a device.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "video_core/pica/regs_internal.h"
#include "video_core/pica/shader_setup.h"
#include "video_core/renderer_metal/mtl_utility_shaders.h"
#include "video_core/shader/generator/msl_shader_gen.h"

using namespace Pica::Shader;
using namespace Pica::Shader::Generator;

namespace {

Profile MakeMetalProfile() {
    Profile profile{};
    profile.has_separable_shaders = true;
    profile.has_clip_planes = true;
    profile.has_geometry_shader = false;
    profile.has_custom_border_color = false;
    profile.has_fragment_shader_interlock = true;
    profile.has_fragment_shader_barycentric = false;
    profile.has_blend_minmax_factor = false;
    profile.has_minus_one_to_one_range = false;
    profile.has_logic_op = false;
    profile.is_metal = true;
    return profile;
}

void Write(const std::filesystem::path& dir, const std::string& name, const std::string& source) {
    std::ofstream file{dir / name};
    file << source;
}

/**
 * Builds random but well formed PICA vertex shader programs: arithmetic of every supported kind,
 * address register use, comparisons, IF/ELSE, LOOP, CALL(C/U) into a subroutine placed after the
 * END and conditional jumps, which together cover the code paths of the MSL decompiler.
 */
class ProgramBuilder {
public:
    explicit ProgramBuilder(std::mt19937& rng_, u32 num_outputs_)
        : rng{rng_}, num_outputs{num_outputs_} {}

    u32 Size() const {
        return static_cast<u32>(code.size());
    }

    void Arithmetic(u32 count) {
        static constexpr std::array<u32, 18> ops = {
            0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x08, 0x09, 0x0A,
            0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x13, 0x18, 0x1A, 0x1B,
        };
        for (u32 i = 0; i < count; i++) {
            if (rng() % 6 == 0) {
                Mad();
                continue;
            }
            const u32 op = ops[rng() % ops.size()];
            const bool inverted = op >= 0x18;
            const u32 src1 = inverted ? SmallSource() : Source();
            const u32 src2 = inverted ? Source() : SmallSource();
            const u32 src1_bits = inverted ? (src1 << 14) : (src1 << 12);
            const u32 src2_bits = src2 << 7;
            code.push_back((op << 26) | (Dest() << 21) | ((rng() % 4) << 19) | src1_bits |
                           src2_bits | (rng() % 128));
        }
    }

    void Mad() {
        const bool inverted = rng() % 2 == 0;
        const u32 top = inverted ? 0b110u : 0b111u;
        const u32 src2 = inverted ? SmallSource() : Source();
        const u32 src3 = inverted ? Source() : SmallSource();
        const u32 operands = inverted ? ((src2 << 12) | (src3 << 5)) : ((src2 << 10) | (src3 << 5));
        code.push_back((top << 29) | (Dest() << 24) | ((rng() % 4) << 22) |
                       (SmallSource() << 17) | operands | (rng() % 32));
    }

    void Mova() {
        code.push_back((0x12u << 26) | (Source() << 12) | (rng() % 128));
    }

    void Cmp() {
        code.push_back((0x2Eu << 26) | ((rng() % 6) << 24) | ((rng() % 6) << 21) |
                       (Source() << 12) | (SmallSource() << 7) | (rng() % 128));
    }

    u32 Condition() {
        return ((rng() % 2) << 25) | ((rng() % 2) << 24) | ((rng() % 4) << 22);
    }

    void If(bool uniform) {
        const u32 at = Size();
        code.push_back(0);
        Arithmetic(1 + rng() % 4);
        const u32 else_offset = Size();
        const u32 else_count = rng() % 3;
        Arithmetic(else_count);
        const u32 head = uniform ? ((0x27u << 26) | ((rng() % 16) << 22))
                                 : ((0x28u << 26) | Condition());
        code[at] = head | (else_offset << 10) | else_count;
    }

    void Loop() {
        const u32 at = Size();
        code.push_back(0);
        Arithmetic(1 + rng() % 4);
        code[at] = (0x29u << 26) | ((rng() % 4) << 22) | ((Size() - 1) << 10);
    }

    /// A call to the subroutine; the target is patched once it is placed after the END.
    void Call() {
        const u32 kind = rng() % 3;
        const u32 head = kind == 0   ? (0x24u << 26)
                         : kind == 1 ? ((0x25u << 26) | Condition())
                                     : ((0x26u << 26) | ((rng() % 16) << 22));
        calls.push_back(Size());
        code.push_back(head);
    }

    /// A conditional jump a few instructions ahead.
    void Jump() {
        const u32 at = Size();
        code.push_back(0);
        Arithmetic(1 + rng() % 3);
        const u32 head = rng() % 2 ? ((0x2Cu << 26) | Condition())
                                   : ((0x2Du << 26) | ((rng() % 16) << 22) | (rng() % 2));
        code[at] = head | (Size() << 10);
    }

    void WriteOutputs() {
        for (u32 reg = 0; reg < num_outputs; reg++) {
            // mov o{reg}, r/v with a random swizzle.
            code.push_back((0x13u << 26) | (reg << 21) | (SmallSource() << 12) | (rng() % 128));
        }
    }

    void End() {
        code.push_back(0x22u << 26);
    }

    void Subroutine() {
        const u32 begin = Size();
        Arithmetic(1 + rng() % 4);
        const u32 count = Size() - begin;
        for (const u32 call : calls) {
            code[call] |= (begin << 10) | count;
        }
    }

    void Store(Pica::ShaderSetup& setup) const {
        for (u32 i = 0; i < code.size(); i++) {
            setup.UpdateProgramCode(i, code[i]);
        }
    }

private:
    u32 Dest() {
        // Outputs and temporaries.
        return rng() % 2 ? (rng() % num_outputs) : (0x10 + rng() % 16);
    }
    u32 SmallSource() {
        // Inputs and temporaries.
        return rng() % 32;
    }
    u32 Source() {
        // Inputs, temporaries and float uniforms.
        return rng() % 0x80;
    }

    std::mt19937& rng;
    u32 num_outputs;
    std::vector<u32> code;
    std::vector<u32> calls;
};

std::string RandomVertexShader(std::mt19937& rng, int index) {
    const u32 num_outputs = 1 + rng() % 7;
    ProgramBuilder builder{rng, num_outputs};
    builder.Arithmetic(2 + rng() % 6);
    builder.Mova();
    builder.Cmp();
    builder.If(false);
    builder.If(true);
    if (index % 2 == 0) {
        builder.Loop();
    }
    builder.Call();
    if (index % 3 == 0) {
        builder.Jump();
    }
    builder.Arithmetic(rng() % 4);
    builder.WriteOutputs();
    builder.End();
    builder.Subroutine();

    auto setup = std::make_unique<Pica::ShaderSetup>();
    builder.Store(*setup);
    for (u32 i = 0; i < 128; i++) {
        setup->UpdateSwizzleData(i, rng());
    }

    PicaVSConfig config{};
    auto& state = config.state;
    state.lighting_disable = index % 2;
    state.main_offset = 0;
    state.num_outputs = num_outputs;
    state.output_map.fill(16);
    for (u32 i = 0; i < num_outputs; i++) {
        state.output_map[i] = i;
    }
    state.gs_state.vs_output_attributes_count = num_outputs;
    state.gs_state.gs_output_attributes_count = num_outputs;
    state.gs_state.vs_output_total = std::min<u32>(num_outputs, 7);
    for (auto& attribute : state.gs_state.vs_output_attributes) {
        // Semantics 0x00-0x17 are valid, 0x1F is unused.
        const auto pick = [&] { return rng() % 5 == 0 ? 0x1Fu : rng() % 0x18; };
        attribute.map_x.Assign(static_cast<Pica::RasterizerRegs::VSOutputAttributes::Semantic>(pick()));
        attribute.map_y.Assign(static_cast<Pica::RasterizerRegs::VSOutputAttributes::Semantic>(pick()));
        attribute.map_z.Assign(static_cast<Pica::RasterizerRegs::VSOutputAttributes::Semantic>(pick()));
        attribute.map_w.Assign(static_cast<Pica::RasterizerRegs::VSOutputAttributes::Semantic>(pick()));
    }

    return MSL::GenerateVertexShader(*setup, config, index % 3 != 0, index % 4 != 0, index % 5 < 2,
                                     index % 7 < 3);
}

} // Anonymous namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <output dir> [random count]\n", argv[0]);
        return 1;
    }
    const std::filesystem::path out_dir{argv[1]};
    const int random_count = argc > 2 ? std::atoi(argv[2]) : 256;
    std::filesystem::create_directories(out_dir);

    const Profile profile = MakeMetalProfile();

    Write(out_dir, "utility.metal", std::string{Metal::Shaders::UTILITY});
    Write(out_dir, "utility_stencil.metal", std::string{Metal::Shaders::STENCIL});
    Write(out_dir, "present.metal", std::string{Metal::Shaders::PRESENT});
    Write(out_dir, "present_post_example.metal",
          std::string{Metal::Shaders::PRESENT} + std::string{Metal::Shaders::POST_EXAMPLE} +
              std::string{Metal::Shaders::PRESENT_POST_WRAPPER});
    Write(out_dir, "filters.metal", std::string{Metal::Shaders::FILTERS});

    Write(out_dir, "vs_trivial.metal", MSL::GenerateTrivialVertexShader(false));
    Write(out_dir, "vs_trivial_clip.metal", MSL::GenerateTrivialVertexShader(true));
    Write(out_dir, "vs_trivial_half.metal", MSL::GenerateTrivialVertexShader(false, true));
    Write(out_dir, "vs_trivial_clip_half.metal", MSL::GenerateTrivialVertexShader(true, true));

    // Random register states exercise most of the generator branches.
    std::mt19937 rng{1234};
    int count = 0;
    for (int i = 0; i < random_count; i++) {
        Pica::RegsInternal regs{};
        auto* words = reinterpret_cast<u32*>(&regs);
        for (std::size_t w = 0; w < sizeof(regs) / sizeof(u32); w++) {
            words[w] = rng();
        }
        // Keep the lighting enabled half of the time and cap the number of lights.
        regs.lighting.disable.Assign(i % 2);
        regs.lighting.max_light_index.Assign(rng() % 8);

        const FSConfig config{regs};
        UserConfig user{};
        user.use_custom_normal.Assign(i % 7 == 0);

        MSL::MetalFSConfig metal_config{};
        metal_config.has_color_attachment = (i % 5) != 0;
        metal_config.has_depth_attachment = (i % 3) != 0;
        // Lighting is on for even i, use fast lighting for half of those.
        metal_config.fast_lighting = (i / 2) % 2 != 0;
        metal_config.fast_color = (i % 4) >= 2;
        metal_config.half_luts = metal_config.fast_lighting;
        metal_config.half_interpolants = (i % 5) < 2;
        metal_config.vertex_depth = metal_config.has_depth_attachment && (i % 6) < 3;
        // The per-triangle quaternion correction in the fragment shader.
        metal_config.fragment_quaternion_fix = (i % 8) < 4;
        metal_config.hardware_scissor = (i % 3) != 1;

        Write(out_dir, "fs_random_" + std::to_string(i) + ".metal",
              MSL::GenerateFragmentShader(config, user, profile, metal_config));
        count++;
    }

    // Default (all zero) register state.
    {
        Pica::RegsInternal regs{};
        const FSConfig config{regs};
        Write(out_dir, "fs_default.metal",
              MSL::GenerateFragmentShader(config, {}, profile, MSL::MetalFSConfig{}));
    }

    // Hardware vertex shaders translated from random PICA programs.
    int vs_count = 0;
    int vs_failed = 0;
    for (int i = 0; i < random_count / 4; i++) {
        const std::string source = RandomVertexShader(rng, i);
        if (source.empty()) {
            vs_failed++;
            continue;
        }
        Write(out_dir, "vs_random_" + std::to_string(i) + ".metal", source);
        vs_count++;
    }

    std::printf("Generated %d random fragment shaders and %d vertex shaders (%d programs not "
                "translatable) into %s\n",
                count, vs_count, vs_failed, out_dir.c_str());
    return 0;
}
