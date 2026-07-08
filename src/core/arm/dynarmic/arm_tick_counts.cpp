// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <bit>
#include <functional>
#include "common/common_types.h"
#include "common/string_literal.h"
#include "core/arm/dynarmic/arm_tick_counts.h"

namespace {

template <Common::StringLiteral haystack, Common::StringLiteral needle>
constexpr u32 GetMatchingBitsFromStringLiteral() {
    u32 result = 0;
    for (std::size_t i = 0; i < haystack.strlen; i++) {
        for (std::size_t a = 0; a < needle.strlen; a++) {
            if (haystack.value[i] == needle.value[a]) {
                result |= 1 << (haystack.strlen - 1 - i);
            }
        }
    }
    return result;
}

template <u32 mask_>
constexpr u32 ExtractBits(u32 val) {
    u32 mask = mask_;
    u32 res = 0;
    for (u32 bb = 1; mask; bb += bb) {
        const u32 lowest_bit = mask & (0 - mask);
        if (val & lowest_bit)
            res |= bb;
        mask &= mask - 1;
    }
    return res;
}

template <Common::StringLiteral haystack>
struct MatcherArg {
    template <Common::StringLiteral needle>
    u32 Get() {
        return ExtractBits<GetMatchingBitsFromStringLiteral<haystack, needle>()>(instruction);
    }

    u32 instruction;
};

struct Matcher {
    u32 mask;
    u32 expect;
    std::function<u64(u32)> fn;
};

// pipeline flush cost for a PC write that program flow prediction cannot handle
// (ARM DDI 0211: refilling the 8-stage pipeline costs 5 to 7 cycles)
constexpr u64 pipeline_flush_cycles = 7;
// Indirect branches through a register other than lr hit the BTAC only when the
// target is stable; charge the average of a fold and a flush
constexpr u64 indirect_branch_cycles = 4;

u64 DataProcessing_imm(auto i) {
    if (i.template Get<"d">() == 15) {
        return pipeline_flush_cycles;
    }
    return 1;
}
u64 DataProcessing_reg(auto i) {
    if (i.template Get<"d">() == 15) {
        return pipeline_flush_cycles;
    }
    return 1;
}
u64 DataProcessing_rsr(auto i) {
    if (i.template Get<"d">() == 15) {
        return pipeline_flush_cycles + 1;
    }
    return 2;
}
// MOV (reg) has its own handler: a plain "MOV pc, lr" is a procedure return,
// predicted by the return stack
u64 MovReg(auto i) {
    if (i.template Get<"d">() == 15) {
        const bool is_return =
            i.template Get<"m">() == 14 && i.template Get<"v">() == 0 && i.template Get<"r">() == 0;
        return is_return ? 1 : pipeline_flush_cycles;
    }
    return 1;
}
// Loads: 1 issue cycle, loads into PC are branches: "LDR pc, [sp...]" is a
// return-stack-predicted procedure return; anything else (jump tables) flushes
u64 Load_imm(auto i) {
    if (i.template Get<"t">() == 15) {
        return i.template Get<"n">() == 13 ? 1 : pipeline_flush_cycles - 1;
    }
    return 1;
}
u64 Load_lit(auto i) {
    if (i.template Get<"t">() == 15) {
        return pipeline_flush_cycles - 1;
    }
    return 1;
}
u64 Load_reg(auto i) {
    if (i.template Get<"t">() == 15) {
        return pipeline_flush_cycles - 1;
    }
    // Scaled register offset requires an extra cycle for the offset shift
    return (i.template Get<"v">() == 0 && i.template Get<"r">() == 0) ? 1 : 2;
}
u64 Load_reg_noshift(auto i) {
    if (i.template Get<"t">() == 15) {
        return pipeline_flush_cycles - 1;
    }
    return 1;
}
u64 Store_imm(auto) {
    return 1;
}
u64 Store_reg(auto i) {
    return (i.template Get<"v">() == 0 && i.template Get<"r">() == 0) ? 1 : 2;
}
u64 Store_reg_noshift(auto) {
    return 1;
}
// Two registers per cycle over the 64-bit data path, a pop including PC is a
// return-stack-predicted procedure return, so no branch penalty is added
u64 LoadStoreMultiple(auto i) {
    return 1 + std::popcount(i.template Get<"x">()) / 2;
}
// VLDR/VSTR (p=1, w=0): single transfer, 1 cycle. VLDM/VSTM/VPUSH/VPOP: the imm8
// field counts words; two words transfer per cycle
u64 VfpLoadStore(auto i) {
    if (i.template Get<"p">() == 0 && i.template Get<"u">() == 0) {
        // MCRR/MRRC to the VFP (VMOV to/from a core register pair) share this
        // encoding space (P=U=0 is not a valid VFP load/store addressing mode)
        return 2;
    }
    if (i.template Get<"p">() == 1 && i.template Get<"w">() == 0) {
        return 1;
    }
    return 1 + i.template Get<"v">() / 2;
}

#define INST(NAME, BS, CYCLES)                                                                     \
    Matcher{GetMatchingBitsFromStringLiteral<BS, "01">(),                                          \
            GetMatchingBitsFromStringLiteral<BS, "1">(),                                           \
            std::function<u64(u32)>{[](u32 instruction) -> u64 {                                   \
                [[maybe_unused]] MatcherArg<BS> i{instruction};                                    \
                return (CYCLES);                                                                   \
            }}},

const std::array arm_matchers{
    // clang-format off

    // branch instructions
    // direct branches and calls are BTAC/statically predicted (folded when
    // predicted; we charge the issue slot), BX lr is a return-stack-predicted
    // return; indirect branches through other registers depend on target
    // stability
    INST("BLX (imm)",           "1111101hvvvvvvvvvvvvvvvvvvvvvvvv",  1) // v5
    INST("BLX (reg)",           "cccc000100101111111111110011mmmm",  2) // v5
    INST("B",                   "cccc1010vvvvvvvvvvvvvvvvvvvvvvvv",  1) // v1
    INST("BL",                  "cccc1011vvvvvvvvvvvvvvvvvvvvvvvv",  1) // v1
    INST("BX",                  "cccc000100101111111111110001mmmm",  (i.template Get<"m">() == 14 ? 1 : indirect_branch_cycles)) // v4T
    INST("BXJ",                 "cccc000100101111111111110010mmmm",  1) // v5J

    // floating-point (VFP11) instructions
    // must precede the generic coprocessor entries below, Bit 8 selects double
    // precision. Divide/sqrt block the non-pipelined DS unit for their full
    // throughput; other data-processing operations are pipelined
    INST("VDIV",                "cccc11101-00--------101z-0-0----",  (i.template Get<"z">() ? 29 : 15)) // VFPv2
    INST("VSQRT",               "cccc11101-110001----101z11-0----",  (i.template Get<"z">() ? 29 : 15)) // VFPv2
    INST("VFP DP op",           "cccc1110------------101z---0----",  (i.template Get<"z">() ? 2 : 1)) // VFPv2
    INST("VFP load/store",      "cccc110pu-w---------101-vvvvvvvv",  VfpLoadStore(i)) // VFPv2

    // Coprocessor instructions
    INST("CDP",                 "cccc1110ooooNNNNDDDDppppooo0MMMM",  1) // v2  (CDP2:  v5)
    INST("LDC",                 "cccc110pudw1nnnnDDDDppppvvvvvvvv",  1) // v2  (LDC2:  v5)
    INST("MCR",                 "cccc1110ooo0NNNNttttppppooo1MMMM",  2) // v2  (MCR2:  v5)
    INST("MCRR",                "cccc11000100uuuuttttppppooooMMMM",  2) // v5E (MCRR2: v6)
    INST("MRC",                 "cccc1110ooo1NNNNttttppppooo1MMMM",  2) // v2  (MRC2:  v5)
    INST("MRRC",                "cccc11000101uuuuttttppppooooMMMM",  2) // v5E (MRRC2: v6)
    INST("STC",                 "cccc110pudw0nnnnDDDDppppvvvvvvvv",  1) // v2  (STC2:  v5)

    // Data Processing instructions
    INST("ADC (imm)",           "cccc0010101Snnnnddddrrrrvvvvvvvv",  DataProcessing_imm(i)) // v1
    INST("ADC (reg)",           "cccc0000101Snnnnddddvvvvvrr0mmmm",  DataProcessing_reg(i)) // v1
    INST("ADC (rsr)",           "cccc0000101Snnnnddddssss0rr1mmmm",  DataProcessing_rsr(i)) // v1
    INST("ADD (imm)",           "cccc0010100Snnnnddddrrrrvvvvvvvv",  DataProcessing_imm(i)) // v1
    INST("ADD (reg)",           "cccc0000100Snnnnddddvvvvvrr0mmmm",  DataProcessing_reg(i)) // v1
    INST("ADD (rsr)",           "cccc0000100Snnnnddddssss0rr1mmmm",  DataProcessing_rsr(i)) // v1
    INST("AND (imm)",           "cccc0010000Snnnnddddrrrrvvvvvvvv",  DataProcessing_imm(i)) // v1
    INST("AND (reg)",           "cccc0000000Snnnnddddvvvvvrr0mmmm",  DataProcessing_reg(i)) // v1
    INST("AND (rsr)",           "cccc0000000Snnnnddddssss0rr1mmmm",  DataProcessing_rsr(i)) // v1
    INST("BIC (imm)",           "cccc0011110Snnnnddddrrrrvvvvvvvv",  DataProcessing_imm(i)) // v1
    INST("BIC (reg)",           "cccc0001110Snnnnddddvvvvvrr0mmmm",  DataProcessing_reg(i)) // v1
    INST("BIC (rsr)",           "cccc0001110Snnnnddddssss0rr1mmmm",  DataProcessing_rsr(i)) // v1
    INST("CMN (imm)",           "cccc00110111nnnn0000rrrrvvvvvvvv",  DataProcessing_imm(i)) // v1
    INST("CMN (reg)",           "cccc00010111nnnn0000vvvvvrr0mmmm",  DataProcessing_reg(i)) // v1
    INST("CMN (rsr)",           "cccc00010111nnnn0000ssss0rr1mmmm",  DataProcessing_rsr(i)) // v1
    INST("CMP (imm)",           "cccc00110101nnnn0000rrrrvvvvvvvv",  DataProcessing_imm(i)) // v1
    INST("CMP (reg)",           "cccc00010101nnnn0000vvvvvrr0mmmm",  DataProcessing_reg(i)) // v1
    INST("CMP (rsr)",           "cccc00010101nnnn0000ssss0rr1mmmm",  DataProcessing_rsr(i)) // v1
    INST("EOR (imm)",           "cccc0010001Snnnnddddrrrrvvvvvvvv",  DataProcessing_imm(i)) // v1
    INST("EOR (reg)",           "cccc0000001Snnnnddddvvvvvrr0mmmm",  DataProcessing_reg(i)) // v1
    INST("EOR (rsr)",           "cccc0000001Snnnnddddssss0rr1mmmm",  DataProcessing_rsr(i)) // v1
    INST("MOV (imm)",           "cccc0011101S0000ddddrrrrvvvvvvvv",  DataProcessing_imm(i)) // v1
    INST("MOV (reg)",           "cccc0001101S0000ddddvvvvvrr0mmmm",  MovReg(i)) // v1
    INST("MOV (rsr)",           "cccc0001101S0000ddddssss0rr1mmmm",  DataProcessing_rsr(i)) // v1
    INST("MVN (imm)",           "cccc0011111S0000ddddrrrrvvvvvvvv",  DataProcessing_imm(i)) // v1
    INST("MVN (reg)",           "cccc0001111S0000ddddvvvvvrr0mmmm",  DataProcessing_reg(i)) // v1
    INST("MVN (rsr)",           "cccc0001111S0000ddddssss0rr1mmmm",  DataProcessing_rsr(i)) // v1
    INST("ORR (imm)",           "cccc0011100Snnnnddddrrrrvvvvvvvv",  DataProcessing_imm(i)) // v1
    INST("ORR (reg)",           "cccc0001100Snnnnddddvvvvvrr0mmmm",  DataProcessing_reg(i)) // v1
    INST("ORR (rsr)",           "cccc0001100Snnnnddddssss0rr1mmmm",  DataProcessing_rsr(i)) // v1
    INST("RSB (imm)",           "cccc0010011Snnnnddddrrrrvvvvvvvv",  DataProcessing_imm(i)) // v1
    INST("RSB (reg)",           "cccc0000011Snnnnddddvvvvvrr0mmmm",  DataProcessing_reg(i)) // v1
    INST("RSB (rsr)",           "cccc0000011Snnnnddddssss0rr1mmmm",  DataProcessing_rsr(i)) // v1
    INST("RSC (imm)",           "cccc0010111Snnnnddddrrrrvvvvvvvv",  DataProcessing_imm(i)) // v1
    INST("RSC (reg)",           "cccc0000111Snnnnddddvvvvvrr0mmmm",  DataProcessing_reg(i)) // v1
    INST("RSC (rsr)",           "cccc0000111Snnnnddddssss0rr1mmmm",  DataProcessing_rsr(i)) // v1
    INST("SBC (imm)",           "cccc0010110Snnnnddddrrrrvvvvvvvv",  DataProcessing_imm(i)) // v1
    INST("SBC (reg)",           "cccc0000110Snnnnddddvvvvvrr0mmmm",  DataProcessing_reg(i)) // v1
    INST("SBC (rsr)",           "cccc0000110Snnnnddddssss0rr1mmmm",  DataProcessing_rsr(i)) // v1
    INST("SUB (imm)",           "cccc0010010Snnnnddddrrrrvvvvvvvv",  DataProcessing_imm(i)) // v1
    INST("SUB (reg)",           "cccc0000010Snnnnddddvvvvvrr0mmmm",  DataProcessing_reg(i)) // v1
    INST("SUB (rsr)",           "cccc0000010Snnnnddddssss0rr1mmmm",  DataProcessing_rsr(i)) // v1
    INST("TEQ (imm)",           "cccc00110011nnnn0000rrrrvvvvvvvv",  DataProcessing_imm(i)) // v1
    INST("TEQ (reg)",           "cccc00010011nnnn0000vvvvvrr0mmmm",  DataProcessing_reg(i)) // v1
    INST("TEQ (rsr)",           "cccc00010011nnnn0000ssss0rr1mmmm",  DataProcessing_rsr(i)) // v1
    INST("TST (imm)",           "cccc00110001nnnn0000rrrrvvvvvvvv",  DataProcessing_imm(i)) // v1
    INST("TST (reg)",           "cccc00010001nnnn0000vvvvvrr0mmmm",  DataProcessing_reg(i)) // v1
    INST("TST (rsr)",           "cccc00010001nnnn0000ssss0rr1mmmm",  DataProcessing_rsr(i)) // v1

    // Exception Generating instructions
    INST("BKPT",                "cccc00010010vvvvvvvvvvvv0111vvvv",  8) // v5
    INST("SVC",                 "cccc1111vvvvvvvvvvvvvvvvvvvvvvvv",  8) // v1
    INST("UDF",                 "111001111111------------1111----",  8)

    // Extension instructions
    INST("SXTB",                "cccc011010101111ddddrr000111mmmm",  1) // v6
    INST("SXTB16",              "cccc011010001111ddddrr000111mmmm",  1) // v6
    INST("SXTH",                "cccc011010111111ddddrr000111mmmm",  1) // v6
    INST("SXTAB",               "cccc01101010nnnnddddrr000111mmmm",  1) // v6
    INST("SXTAB16",             "cccc01101000nnnnddddrr000111mmmm",  1) // v6
    INST("SXTAH",               "cccc01101011nnnnddddrr000111mmmm",  1) // v6
    INST("UXTB",                "cccc011011101111ddddrr000111mmmm",  1) // v6
    INST("UXTB16",              "cccc011011001111ddddrr000111mmmm",  1) // v6
    INST("UXTH",                "cccc011011111111ddddrr000111mmmm",  1) // v6
    INST("UXTAB",               "cccc01101110nnnnddddrr000111mmmm",  1) // v6
    INST("UXTAB16",             "cccc01101100nnnnddddrr000111mmmm",  1) // v6
    INST("UXTAH",               "cccc01101111nnnnddddrr000111mmmm",  1) // v6

    // Hint instructions
    INST("PLD (imm)",           "11110101uz01nnnn1111iiiiiiiiiiii",  1) // v5E for PLD; v7 for PLDW
    INST("PLD (reg)",           "11110111uz01nnnn1111iiiiitt0mmmm",  1) // v5E for PLD; v7 for PLDW
    INST("SEV",                 "----0011001000001111000000000100",  1) // v6K
    INST("WFE",                 "----0011001000001111000000000010",  1) // v6K
    INST("WFI",                 "----0011001000001111000000000011",  1) // v6K
    INST("YIELD",               "----0011001000001111000000000001",  1) // v6K

    // Synchronization Primitive instructions
    INST("CLREX",               "11110101011111111111000000011111",  1) // v6K
    INST("SWP",                 "cccc00010000nnnntttt00001001uuuu",  4) // v2S (v6: Deprecated)
    INST("SWPB",                "cccc00010100nnnntttt00001001uuuu",  4) // v2S (v6: Deprecated)
    INST("STREX",               "cccc00011000nnnndddd11111001mmmm",  2) // v6
    INST("LDREX",               "cccc00011001nnnndddd111110011111",  2) // v6
    INST("STREXD",              "cccc00011010nnnndddd11111001mmmm",  2) // v6K
    INST("LDREXD",              "cccc00011011nnnndddd111110011111",  2) // v6K
    INST("STREXB",              "cccc00011100nnnndddd11111001mmmm",  2) // v6K
    INST("LDREXB",              "cccc00011101nnnndddd111110011111",  2) // v6K
    INST("STREXH",              "cccc00011110nnnndddd11111001mmmm",  2) // v6K
    INST("LDREXH",              "cccc00011111nnnndddd111110011111",  2) // v6K

    // Load/Store instructions
    INST("LDRBT (A1)",          "----0100-111--------------------",  1) // v1
    INST("LDRBT (A2)",          "----0110-111---------------0----",  1) // v1
    INST("LDRT (A1)",           "----0100-011--------------------",  1) // v1
    INST("LDRT (A2)",           "----0110-011---------------0----",  1) // v1
    INST("STRBT (A1)",          "----0100-110--------------------",  1) // v1
    INST("STRBT (A2)",          "----0110-110---------------0----",  1) // v1
    INST("STRT (A1)",           "----0100-010--------------------",  1) // v1
    INST("STRT (A2)",           "----0110-010---------------0----",  1) // v1
    INST("LDR (lit)",           "cccc0101u0011111ttttvvvvvvvvvvvv",  Load_lit(i)) // v1
    INST("LDR (imm)",           "cccc010pu0w1nnnnttttvvvvvvvvvvvv",  Load_imm(i)) // v1
    INST("LDR (reg)",           "cccc011pu0w1nnnnttttvvvvvrr0mmmm",  Load_reg(i)) // v1
    INST("LDRB (lit)",          "cccc0101u1011111ttttvvvvvvvvvvvv",  Load_lit(i)) // v1
    INST("LDRB (imm)",          "cccc010pu1w1nnnnttttvvvvvvvvvvvv",  Load_imm(i)) // v1
    INST("LDRB (reg)",          "cccc011pu1w1nnnnttttvvvvvrr0mmmm",  Load_reg(i)) // v1
    INST("LDRD (lit)",          "cccc0001u1001111ttttvvvv1101vvvv",  1) // v5E
    INST("LDRD (imm)",          "cccc000pu1w0nnnnttttvvvv1101vvvv",  1) // v5E
    INST("LDRD (reg)",          "cccc000pu0w0nnnntttt00001101mmmm",  1) // v5E
    INST("LDRH (lit)",          "cccc000pu1w11111ttttvvvv1011vvvv",  Load_reg_noshift(i)) // v4
    INST("LDRH (imm)",          "cccc000pu1w1nnnnttttvvvv1011vvvv",  Load_reg_noshift(i)) // v4
    INST("LDRH (reg)",          "cccc000pu0w1nnnntttt00001011mmmm",  Load_reg_noshift(i)) // v4
    INST("LDRSB (lit)",         "cccc0001u1011111ttttvvvv1101vvvv",  Load_reg_noshift(i)) // v4
    INST("LDRSB (imm)",         "cccc000pu1w1nnnnttttvvvv1101vvvv",  Load_reg_noshift(i)) // v4
    INST("LDRSB (reg)",         "cccc000pu0w1nnnntttt00001101mmmm",  Load_reg_noshift(i)) // v4
    INST("LDRSH (lit)",         "cccc0001u1011111ttttvvvv1111vvvv",  Load_reg_noshift(i)) // v4
    INST("LDRSH (imm)",         "cccc000pu1w1nnnnttttvvvv1111vvvv",  Load_reg_noshift(i)) // v4
    INST("LDRSH (reg)",         "cccc000pu0w1nnnntttt00001111mmmm",  Load_reg_noshift(i)) // v4
    INST("STR (imm)",           "cccc010pu0w0nnnnttttvvvvvvvvvvvv",  Store_imm(i)) // v1
    INST("STR (reg)",           "cccc011pu0w0nnnnttttvvvvvrr0mmmm",  Store_reg(i)) // v1
    INST("STRB (imm)",          "cccc010pu1w0nnnnttttvvvvvvvvvvvv",  Store_imm(i)) // v1
    INST("STRB (reg)",          "cccc011pu1w0nnnnttttvvvvvrr0mmmm",  Store_reg(i)) // v1
    INST("STRD (imm)",          "cccc000pu1w0nnnnttttvvvv1111vvvv",  1) // v5E
    INST("STRD (reg)",          "cccc000pu0w0nnnntttt00001111mmmm",  1) // v5E
    INST("STRH (imm)",          "cccc000pu1w0nnnnttttvvvv1011vvvv",  Store_reg_noshift(i)) // v4
    INST("STRH (reg)",          "cccc000pu0w0nnnntttt00001011mmmm",  Store_reg_noshift(i)) // v4

    // Load/Store Multiple instructions
    INST("LDM",                 "cccc100010w1nnnnxxxxxxxxxxxxxxxx",  LoadStoreMultiple(i)) // v1
    INST("LDMDA",               "cccc100000w1nnnnxxxxxxxxxxxxxxxx",  LoadStoreMultiple(i)) // v1
    INST("LDMDB",               "cccc100100w1nnnnxxxxxxxxxxxxxxxx",  LoadStoreMultiple(i)) // v1
    INST("LDMIB",               "cccc100110w1nnnnxxxxxxxxxxxxxxxx",  LoadStoreMultiple(i)) // v1
    INST("LDM (usr reg)",       "----100--101--------------------",  1) // v1
    INST("LDM (exce ret)",      "----100--1-1----1---------------",  1) // v1
    INST("STM",                 "cccc100010w0nnnnxxxxxxxxxxxxxxxx",  LoadStoreMultiple(i)) // v1
    INST("STMDA",               "cccc100000w0nnnnxxxxxxxxxxxxxxxx",  LoadStoreMultiple(i)) // v1
    INST("STMDB",               "cccc100100w0nnnnxxxxxxxxxxxxxxxx",  LoadStoreMultiple(i)) // v1
    INST("STMIB",               "cccc100110w0nnnnxxxxxxxxxxxxxxxx",  LoadStoreMultiple(i)) // v1
    INST("STM (usr reg)",       "----100--100--------------------",  1) // v1

    // Miscellaneous instructions
    INST("CLZ",                 "cccc000101101111dddd11110001mmmm",  1) // v5
    INST("NOP",                 "----0011001000001111000000000000",  1) // v6K
    INST("SEL",                 "cccc01101000nnnndddd11111011mmmm",  1) // v6

    // Unsigned Sum of Absolute Differences instructions
    INST("USAD8",               "cccc01111000dddd1111mmmm0001nnnn",  1) // v6
    INST("USADA8",              "cccc01111000ddddaaaammmm0001nnnn",  1) // v6

    // Packing instructions
    INST("PKHBT",               "cccc01101000nnnnddddvvvvv001mmmm",  1) // v6K
    INST("PKHTB",               "cccc01101000nnnnddddvvvvv101mmmm",  1) // v6K

    // Reversal instructions
    INST("REV",                 "cccc011010111111dddd11110011mmmm",  1) // v6
    INST("REV16",               "cccc011010111111dddd11111011mmmm",  1) // v6
    INST("REVSH",               "cccc011011111111dddd11111011mmmm",  1) // v6

    // Saturation instructions
    INST("SSAT",                "cccc0110101vvvvvddddvvvvvr01nnnn",  1) // v6
    INST("SSAT16",              "cccc01101010vvvvdddd11110011nnnn",  1) // v6
    INST("USAT",                "cccc0110111vvvvvddddvvvvvr01nnnn",  1) // v6
    INST("USAT16",              "cccc01101110vvvvdddd11110011nnnn",  1) // v6

    // Multiply (Normal) instructions
    INST("MLA",                 "cccc0000001Sddddaaaammmm1001nnnn",  (i.template Get<"S">() ? 5 : 2)) // v2
    INST("MUL",                 "cccc0000000Sdddd0000mmmm1001nnnn",  (i.template Get<"S">() ? 5 : 2)) // v2

    // Multiply (Long) instructions
    INST("SMLAL",               "cccc0000111Sddddaaaammmm1001nnnn",  (i.template Get<"S">() ? 6 : 3)) // v3M
    INST("SMULL",               "cccc0000110Sddddaaaammmm1001nnnn",  (i.template Get<"S">() ? 6 : 3)) // v3M
    INST("UMAAL",               "cccc00000100ddddaaaammmm1001nnnn",  3) // v6
    INST("UMLAL",               "cccc0000101Sddddaaaammmm1001nnnn",  (i.template Get<"S">() ? 6 : 3)) // v3M
    INST("UMULL",               "cccc0000100Sddddaaaammmm1001nnnn",  (i.template Get<"S">() ? 6 : 3)) // v3M

    // Multiply (Halfword) instructions
    INST("SMLALXY",             "cccc00010100ddddaaaammmm1xy0nnnn",  2) // v5xP
    INST("SMLAXY",              "cccc00010000ddddaaaammmm1xy0nnnn",  1) // v5xP
    INST("SMULXY",              "cccc00010110dddd0000mmmm1xy0nnnn",  1) // v5xP

    // Multiply (Word by Halfword) instructions
    INST("SMLAWY",              "cccc00010010ddddaaaammmm1y00nnnn",  1) // v5xP
    INST("SMULWY",              "cccc00010010dddd0000mmmm1y10nnnn",  1) // v5xP

    // Multiply (Most Significant Word) instructions
    INST("SMMUL",               "cccc01110101dddd1111mmmm00R1nnnn",  2) // v6
    INST("SMMLA",               "cccc01110101ddddaaaammmm00R1nnnn",  2) // v6
    INST("SMMLS",               "cccc01110101ddddaaaammmm11R1nnnn",  2) // v6

    // Multiply (Dual) instructions
    INST("SMLAD",               "cccc01110000ddddaaaammmm00M1nnnn",  2) // v6
    INST("SMLALD",              "cccc01110100ddddaaaammmm00M1nnnn",  2) // v6
    INST("SMLSD",               "cccc01110000ddddaaaammmm01M1nnnn",  2) // v6
    INST("SMLSLD",              "cccc01110100ddddaaaammmm01M1nnnn",  2) // v6
    INST("SMUAD",               "cccc01110000dddd1111mmmm00M1nnnn",  2) // v6
    INST("SMUSD",               "cccc01110000dddd1111mmmm01M1nnnn",  2) // v6

    // Parallel Add/Subtract (Modulo) instructions
    INST("SADD8",               "cccc01100001nnnndddd11111001mmmm",  1) // v6
    INST("SADD16",              "cccc01100001nnnndddd11110001mmmm",  1) // v6
    INST("SASX",                "cccc01100001nnnndddd11110011mmmm",  1) // v6
    INST("SSAX",                "cccc01100001nnnndddd11110101mmmm",  1) // v6
    INST("SSUB8",               "cccc01100001nnnndddd11111111mmmm",  1) // v6
    INST("SSUB16",              "cccc01100001nnnndddd11110111mmmm",  1) // v6
    INST("UADD8",               "cccc01100101nnnndddd11111001mmmm",  1) // v6
    INST("UADD16",              "cccc01100101nnnndddd11110001mmmm",  1) // v6
    INST("UASX",                "cccc01100101nnnndddd11110011mmmm",  1) // v6
    INST("USAX",                "cccc01100101nnnndddd11110101mmmm",  1) // v6
    INST("USUB8",               "cccc01100101nnnndddd11111111mmmm",  1) // v6
    INST("USUB16",              "cccc01100101nnnndddd11110111mmmm",  1) // v6

    // Parallel Add/Subtract (Saturating) instructions
    INST("QADD8",               "cccc01100010nnnndddd11111001mmmm",  1) // v6
    INST("QADD16",              "cccc01100010nnnndddd11110001mmmm",  1) // v6
    INST("QASX",                "cccc01100010nnnndddd11110011mmmm",  1) // v6
    INST("QSAX",                "cccc01100010nnnndddd11110101mmmm",  1) // v6
    INST("QSUB8",               "cccc01100010nnnndddd11111111mmmm",  1) // v6
    INST("QSUB16",              "cccc01100010nnnndddd11110111mmmm",  1) // v6
    INST("UQADD8",              "cccc01100110nnnndddd11111001mmmm",  1) // v6
    INST("UQADD16",             "cccc01100110nnnndddd11110001mmmm",  1) // v6
    INST("UQASX",               "cccc01100110nnnndddd11110011mmmm",  1) // v6
    INST("UQSAX",               "cccc01100110nnnndddd11110101mmmm",  1) // v6
    INST("UQSUB8",              "cccc01100110nnnndddd11111111mmmm",  1) // v6
    INST("UQSUB16",             "cccc01100110nnnndddd11110111mmmm",  1) // v6

    // Parallel Add/Subtract (Halving) instructions
    INST("SHADD8",              "cccc01100011nnnndddd11111001mmmm",  1) // v6
    INST("SHADD16",             "cccc01100011nnnndddd11110001mmmm",  1) // v6
    INST("SHASX",               "cccc01100011nnnndddd11110011mmmm",  1) // v6
    INST("SHSAX",               "cccc01100011nnnndddd11110101mmmm",  1) // v6
    INST("SHSUB8",              "cccc01100011nnnndddd11111111mmmm",  1) // v6
    INST("SHSUB16",             "cccc01100011nnnndddd11110111mmmm",  1) // v6
    INST("UHADD8",              "cccc01100111nnnndddd11111001mmmm",  1) // v6
    INST("UHADD16",             "cccc01100111nnnndddd11110001mmmm",  1) // v6
    INST("UHASX",               "cccc01100111nnnndddd11110011mmmm",  1) // v6
    INST("UHSAX",               "cccc01100111nnnndddd11110101mmmm",  1) // v6
    INST("UHSUB8",              "cccc01100111nnnndddd11111111mmmm",  1) // v6
    INST("UHSUB16",             "cccc01100111nnnndddd11110111mmmm",  1) // v6

    // Saturated Add/Subtract instructions
    INST("QADD",                "cccc00010000nnnndddd00000101mmmm",  1) // v5xP
    INST("QSUB",                "cccc00010010nnnndddd00000101mmmm",  1) // v5xP
    INST("QDADD",               "cccc00010100nnnndddd00000101mmmm",  1) // v5xP
    INST("QDSUB",               "cccc00010110nnnndddd00000101mmmm",  1) // v5xP

    // Status Register Access instructions
    INST("CPS",                 "111100010000---00000000---0-----",  1) // v6
    INST("SETEND",              "1111000100000001000000e000000000",  1) // v6
    INST("MRS",                 "cccc000100001111dddd000000000000",  1) // v3
    INST("MSR (imm)",           "cccc00110010mmmm1111rrrrvvvvvvvv",  (i.template Get<"m">() == 0b1000 ? 1 : 4)) // v3
    INST("MSR (reg)",           "cccc00010010mmmm111100000000nnnn",  (i.template Get<"m">() == 0b1000 ? 1 : 4)) // v3
    INST("RFE",                 "1111100--0-1----0000101000000000",  9) // v6
    INST("SRS",                 "1111100--1-0110100000101000-----",  1) // v6

    // clang-format on
};

const std::array thumb16_matchers{
    // clang-format off

    // Shift (immediate) add, subtract, move and compare instructions
    INST("LSL (imm)",                "00000vvvvvmmmddd",    1)
    INST("LSR (imm)",                "00001vvvvvmmmddd",    1)
    INST("ASR (imm)",                "00010vvvvvmmmddd",    1)
    INST("ADD (reg, T1)",            "0001100mmmnnnddd",    1)
    INST("SUB (reg)",                "0001101mmmnnnddd",    1)
    INST("ADD (imm, T1)",            "0001110vvvnnnddd",    1)
    INST("SUB (imm, T1)",            "0001111vvvnnnddd",    1)
    INST("MOV (imm)",                "00100dddvvvvvvvv",    1)
    INST("CMP (imm)",                "00101nnnvvvvvvvv",    1)
    INST("ADD (imm, T2)",            "00110dddvvvvvvvv",    1)
    INST("SUB (imm, T2)",            "00111dddvvvvvvvv",    1)

     // Data-processing instructions
    INST("AND (reg)",                "0100000000mmmddd",    1)
    INST("EOR (reg)",                "0100000001mmmddd",    1)
    INST("LSL (reg)",                "0100000010mmmddd",    1)
    INST("LSR (reg)",                "0100000011mmmddd",    1)
    INST("ASR (reg)",                "0100000100mmmddd",    1)
    INST("ADC (reg)",                "0100000101mmmddd",    1)
    INST("SBC (reg)",                "0100000110mmmddd",    1)
    INST("ROR (reg)",                "0100000111sssddd",    1)
    INST("TST (reg)",                "0100001000mmmnnn",    1)
    INST("RSB (imm)",                "0100001001nnnddd",    1)
    INST("CMP (reg, T1)",            "0100001010mmmnnn",    1)
    INST("CMN (reg)",                "0100001011mmmnnn",    1)
    INST("ORR (reg)",                "0100001100mmmddd",    1)
    INST("MUL (reg)",                "0100001101nnnddd",    2)
    INST("BIC (reg)",                "0100001110mmmddd",    1)
    INST("MVN (reg)",                "0100001111mmmddd",    1)

    // Special data instructions
    INST("ADD (reg, T2)",            "01000100Dmmmmddd",    1) // v4T, Low regs: v6T2
    INST("CMP (reg, T2)",            "01000101Nmmmmnnn",    1) // v4T
    INST("MOV (reg)",                "01000110Dmmmmddd",    1) // v4T, Low regs: v6

    // Store/Load single data item instructions
    INST("LDR (literal)",            "01001tttvvvvvvvv",    1)
    INST("STR (reg)",                "0101000mmmnnnttt",    1)
    INST("STRH (reg)",               "0101001mmmnnnttt",    1)
    INST("STRB (reg)",               "0101010mmmnnnttt",    1)
    INST("LDRSB (reg)",              "0101011mmmnnnttt",    1)
    INST("LDR (reg)",                "0101100mmmnnnttt",    1)
    INST("LDRH (reg)",               "0101101mmmnnnttt",    1)
    INST("LDRB (reg)",               "0101110mmmnnnttt",    1)
    INST("LDRSH (reg)",              "0101111mmmnnnttt",    1)
    INST("STR (imm, T1)",            "01100vvvvvnnnttt",    1)
    INST("LDR (imm, T1)",            "01101vvvvvnnnttt",    1)
    INST("STRB (imm)",               "01110vvvvvnnnttt",    1)
    INST("LDRB (imm)",               "01111vvvvvnnnttt",    1)
    INST("STRH (imm)",               "10000vvvvvnnnttt",    1)
    INST("LDRH (imm)",               "10001vvvvvnnnttt",    1)
    INST("STR (imm, T2)",            "10010tttvvvvvvvv",    1)
    INST("LDR (imm, T2)",            "10011tttvvvvvvvv",    1)

    // Generate relative address instructions
    INST("ADR",                      "10100dddvvvvvvvv",    1)
    INST("ADD (SP plus imm, T1)",    "10101dddvvvvvvvv",    1)
    INST("ADD (SP plus imm, T2)",    "101100000vvvvvvv",    1) // v4T
    INST("SUB (SP minus imm)",       "101100001vvvvvvv",    1) // v4T

    // Hint instructions
    INST("NOP",                      "10111111--------",    (1)) // IT on v7

    // Miscellaneous 16-bit instructions
    INST("SXTH",                     "1011001000mmmddd",    1) // v6
    INST("SXTB",                     "1011001001mmmddd",    1) // v6
    INST("UXTH",                     "1011001010mmmddd",    1) // v6
    INST("UXTB",                     "1011001011mmmddd",    1) // v6
    INST("PUSH",                     "1011010xxxxxxxxx",    LoadStoreMultiple(i)) // v4T
    INST("POP",                      "1011110xxxxxxxxx",    LoadStoreMultiple(i)) // v4T
    INST("SETEND",                   "101101100101x000",    1) // v6
    INST("CPS",                      "10110110011m0aif",    1) // v6
    INST("REV",                      "1011101000mmmddd",    1) // v6
    INST("REV16",                    "1011101001mmmddd",    1) // v6
    INST("REVSH",                    "1011101011mmmddd",    1) // v6
    INST("BKPT",                     "10111110xxxxxxxx",    8) // v5

    // Store/Load multiple registers
    INST("STMIA",                    "11000nnnxxxxxxxx",    LoadStoreMultiple(i))
    INST("LDMIA",                    "11001nnnxxxxxxxx",    LoadStoreMultiple(i))

    // Branch instructions
    INST("BX",                       "010001110mmmm000",    (i.template Get<"m">() == 14 ? 1 : indirect_branch_cycles)) // v4T
    INST("BLX (reg)",                "010001111mmmm000",    2) // v5T
    INST("UDF",                      "11011110--------",    8)
    INST("SVC",                      "11011111xxxxxxxx",    8)
    INST("B (T1)",                   "1101ccccvvvvvvvv",    1)
    INST("B (T2)",                   "11100vvvvvvvvvvv",    1)

    // clang-format on
};

const std::array thumb32_matchers{
    // clang-format off

    // Branch instructions (the only 32-bit encodings on ARMv6: BL/BLX prefix pairs)
    INST("BL (imm)",                 "11110Svvvvvvvvvv11j1jvvvvvvvvvvv",    1) // v4T
    INST("BLX (imm)",                "11110Svvvvvvvvvv11j0jvvvvvvvvvvv",    1) // v5T

    // clang-format on
};

} // namespace

namespace Core {

u64 TicksForInstruction(bool is_thumb, u32 instruction) {
    const auto matches_instruction = [instruction](const auto& matcher) {
        return (instruction & matcher.mask) == matcher.expect;
    };

    if (is_thumb) {
        // dynarmic passes 16-bit Thumb instructions zero-extended and 32-bit ones
        // (BL/BLX prefix+suffix pairs on ARMv6) with the first halfword in the
        // upper 16 bits
        if ((instruction >> 16) != 0) {
            auto iter =
                std::find_if(thumb32_matchers.begin(), thumb32_matchers.end(), matches_instruction);
            if (iter != thumb32_matchers.end()) {
                return iter->fn(instruction);
            }
            return 1;
        }
        auto iter =
            std::find_if(thumb16_matchers.begin(), thumb16_matchers.end(), matches_instruction);
        if (iter != thumb16_matchers.end()) {
            return iter->fn(instruction);
        }
        return 1;
    }

    auto iter = std::find_if(arm_matchers.begin(), arm_matchers.end(), matches_instruction);
    if (iter != arm_matchers.end()) {
        return iter->fn(instruction);
    }
    return 1;
}

} // namespace Core
