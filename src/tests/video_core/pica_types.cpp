// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <catch2/catch_test_macros.hpp>
#include "video_core/pica_types.h"

TEST_CASE("float24", "[video_core]") {
    REQUIRE(Pica::f24::FromRaw(0x3F0000).ToFloat32() == +1.0f);
    REQUIRE(Pica::f24::FromRaw(0xBF0000).ToFloat32() == -1.0f);
    REQUIRE(Pica::f24::FromRaw(0x3E0000).ToFloat32() == +0.5f);
    REQUIRE(Pica::f24::FromRaw(0xBE0000).ToFloat32() == -0.5f);
    REQUIRE(Pica::f24::FromRaw(0x3D0000).ToFloat32() == +0.25f);
    REQUIRE(Pica::f24::FromRaw(0xBD0000).ToFloat32() == -0.25f);
    REQUIRE(Pica::f24::FromRaw(0x408000).ToFloat32() == +3.0f);
    REQUIRE(Pica::f24::FromFloat32(+1.0f).ToRaw() == 0x3F0000);
    REQUIRE(Pica::f24::FromFloat32(-1.0f).ToRaw() == 0xBF0000);
    REQUIRE(Pica::f24::FromFloat32(+0.5f).ToRaw() == 0x3E0000);
    REQUIRE(Pica::f24::FromFloat32(-0.5f).ToRaw() == 0xBE0000);
    REQUIRE(Pica::f24::FromFloat32(+0.25f).ToRaw() == 0x3D0000);
    REQUIRE(Pica::f24::FromFloat32(-0.25f).ToRaw() == 0xBD0000);
    REQUIRE(Pica::f24::FromFloat32(+3.0f).ToRaw() == 0x408000);

    // Min/Max norm
    REQUIRE(Pica::f24::FromRaw(0x010000).ToFloat32() == std::bit_cast<float>(0x20800000));
    REQUIRE(Pica::f24::FromRaw(0x7EFFFF).ToFloat32() == std::bit_cast<float>(0x5F7FFF80));
    REQUIRE(Pica::f24::FromFloat32(std::bit_cast<float>(0x20800000)).ToRaw() == 0x010000);
    REQUIRE(Pica::f24::FromFloat32(std::bit_cast<float>(0x5F7FFF80)).ToRaw() == 0x7EFFFF);

    // Zero
    REQUIRE(Pica::f24::FromRaw(0x000000).ToFloat32() == +0.0f);
    REQUIRE(Pica::f24::FromRaw(0x800000).ToFloat32() == -0.0f);
    REQUIRE(Pica::f24::FromFloat32(+0.0f).ToRaw() == 0x000000);
    REQUIRE(Pica::f24::FromFloat32(-0.0f).ToRaw() == 0x800000);

    // Infinity
    REQUIRE(Pica::f24::FromRaw(0x7F0000).ToFloat32() == std::bit_cast<float>(0x7F800000));
    REQUIRE(Pica::f24::FromRaw(0xFF0000).ToFloat32() == std::bit_cast<float>(0xFF800000));
    REQUIRE(Pica::f24::FromFloat32(std::bit_cast<float>(0x7F800000)).ToRaw() == 0x7F0000);
    REQUIRE(Pica::f24::FromFloat32(std::bit_cast<float>(0xFF800000)).ToRaw() == 0xFF0000);

    // Denormal/Subnormal
    REQUIRE(Pica::f24::FromRaw(0x000001).ToFloat32() == std::bit_cast<float>(0x18800000));
    REQUIRE(Pica::f24::FromRaw(0x00FFFF).ToFloat32() == std::bit_cast<float>(0x207FFF00));
    REQUIRE(Pica::f24::FromFloat32(std::bit_cast<float>(0x18800000)).ToRaw() == 0x000001);
    REQUIRE(Pica::f24::FromFloat32(std::bit_cast<float>(0x207FFF00)).ToRaw() == 0x00FFFF);

    // NaN
    REQUIRE(std::bit_cast<u32>(Pica::f24::FromRaw(0x7F8000).ToFloat32()) == 0x7FC00000);
    REQUIRE(std::bit_cast<u32>(Pica::f24::FromRaw(0xFF8000).ToFloat32()) == 0xFFC00000);
    REQUIRE(Pica::f24::FromFloat32(std::bit_cast<float>(0x7FC00000)).ToRaw() == 0x7F8000);
    REQUIRE(Pica::f24::FromFloat32(std::bit_cast<float>(0xFFC00000)).ToRaw() == 0xFF8000);
}

TEST_CASE("float20", "[video_core]") {
    REQUIRE(Pica::f20::FromRaw(0x3f000).ToFloat32() == +1.0f);
    REQUIRE(Pica::f20::FromRaw(0xBf000).ToFloat32() == -1.0f);
    REQUIRE(Pica::f20::FromRaw(0x3E000).ToFloat32() == +0.5f);
    REQUIRE(Pica::f20::FromRaw(0xBE000).ToFloat32() == -0.5f);
    REQUIRE(Pica::f20::FromRaw(0x3D000).ToFloat32() == +0.25f);
    REQUIRE(Pica::f20::FromRaw(0xBD000).ToFloat32() == -0.25f);
    REQUIRE(Pica::f20::FromRaw(0x40800).ToFloat32() == +3.0f);
    REQUIRE(Pica::f20::FromFloat32(+1.0f).ToRaw() == 0x3f000);
    REQUIRE(Pica::f20::FromFloat32(-1.0f).ToRaw() == 0xBf000);
    REQUIRE(Pica::f20::FromFloat32(+0.5f).ToRaw() == 0x3E000);
    REQUIRE(Pica::f20::FromFloat32(-0.5f).ToRaw() == 0xBE000);
    REQUIRE(Pica::f20::FromFloat32(+0.25f).ToRaw() == 0x3D000);
    REQUIRE(Pica::f20::FromFloat32(-0.25f).ToRaw() == 0xBD000);
    REQUIRE(Pica::f20::FromFloat32(+3.0f).ToRaw() == 0x40800);

    // Min/Max norm
    REQUIRE(Pica::f20::FromRaw(0x01000).ToFloat32() == std::bit_cast<float>(0x20800000));
    REQUIRE(Pica::f20::FromRaw(0x7EFFF).ToFloat32() == std::bit_cast<float>(0x5F7FF800));
    REQUIRE(Pica::f20::FromFloat32(std::bit_cast<float>(0x20800000)).ToRaw() == 0x01000);
    REQUIRE(Pica::f20::FromFloat32(std::bit_cast<float>(0x5F7FF800)).ToRaw() == 0x7EFFF);

    // Zero
    REQUIRE(Pica::f20::FromRaw(0x00000).ToFloat32() == +0.0f);
    REQUIRE(Pica::f20::FromRaw(0x80000).ToFloat32() == -0.0f);
    REQUIRE(Pica::f20::FromFloat32(+0.0f).ToRaw() == 0x00000);
    REQUIRE(Pica::f20::FromFloat32(-0.0f).ToRaw() == 0x80000);

    // Infinity
    REQUIRE(Pica::f20::FromRaw(0x7F000).ToFloat32() == std::bit_cast<float>(0x7F800000));
    REQUIRE(Pica::f20::FromRaw(0xFF000).ToFloat32() == std::bit_cast<float>(0xFF800000));
    REQUIRE(Pica::f20::FromFloat32(std::bit_cast<float>(0x7F800000)).ToRaw() == 0x7F000);
    REQUIRE(Pica::f20::FromFloat32(std::bit_cast<float>(0xFF800000)).ToRaw() == 0xFF000);

    // Denormal/Subnormal
    REQUIRE(Pica::f20::FromRaw(0x00001).ToFloat32() == std::bit_cast<float>(0x1A800000));
    REQUIRE(Pica::f20::FromRaw(0x00FFF).ToFloat32() == std::bit_cast<float>(0x207FF000));
    REQUIRE(Pica::f20::FromFloat32(std::bit_cast<float>(0x1A800000)).ToRaw() == 0x00001);
    REQUIRE(Pica::f20::FromFloat32(std::bit_cast<float>(0x207FF000)).ToRaw() == 0x00FFF);

    // NaN
    REQUIRE(std::bit_cast<u32>(Pica::f20::FromRaw(0x7F800)) == 0x7FC00000);
    REQUIRE(std::bit_cast<u32>(Pica::f20::FromRaw(0xFFFFF)) == 0xFFFFF800);
    REQUIRE(Pica::f20::FromFloat32(std::bit_cast<float>(0x7FC00000)).ToRaw() == 0x7F800);
    REQUIRE(Pica::f20::FromFloat32(std::bit_cast<float>(0xFFFFF800)).ToRaw() == 0xFFFFF);
}

TEST_CASE("float16", "[video_core]") {
    REQUIRE(Pica::f16::FromRaw(0x3C00).ToFloat32() == +1.0f);
    REQUIRE(Pica::f16::FromRaw(0xBC00).ToFloat32() == -1.0f);
    REQUIRE(Pica::f16::FromRaw(0x3800).ToFloat32() == +0.5f);
    REQUIRE(Pica::f16::FromRaw(0xB800).ToFloat32() == -0.5f);
    REQUIRE(Pica::f16::FromRaw(0x3400).ToFloat32() == +0.25f);
    REQUIRE(Pica::f16::FromRaw(0xB400).ToFloat32() == -0.25f);
    REQUIRE(Pica::f16::FromRaw(0x4200).ToFloat32() == +3.0f);

    REQUIRE(Pica::f16::FromFloat32(+1.0f).ToRaw() == 0x3C00);
    REQUIRE(Pica::f16::FromFloat32(-1.0f).ToRaw() == 0xBC00);
    REQUIRE(Pica::f16::FromFloat32(+0.5f).ToRaw() == 0x3800);
    REQUIRE(Pica::f16::FromFloat32(-0.5f).ToRaw() == 0xB800);
    REQUIRE(Pica::f16::FromFloat32(+0.25f).ToRaw() == 0x3400);
    REQUIRE(Pica::f16::FromFloat32(-0.25f).ToRaw() == 0xB400);
    REQUIRE(Pica::f16::FromFloat32(+3.0f).ToRaw() == 0x4200);

    // Min/Max norm
    REQUIRE(Pica::f16::FromRaw(0x0400).ToFloat32() == std::bit_cast<float>(0x38800000));
    REQUIRE(Pica::f16::FromRaw(0x7BFF).ToFloat32() == std::bit_cast<float>(0x477FE000));
    REQUIRE(Pica::f16::FromFloat32(std::bit_cast<float>(0x38800000)).ToRaw() == 0x0400);
    REQUIRE(Pica::f16::FromFloat32(std::bit_cast<float>(0x477FE000)).ToRaw() == 0x7BFF);

    // Zero
    REQUIRE(Pica::f16::FromRaw(0x0000).ToFloat32() == +0.0f);
    REQUIRE(Pica::f16::FromRaw(0x8000).ToFloat32() == -0.0f);
    REQUIRE(Pica::f16::FromFloat32(+0.0f).ToRaw() == 0x0000);
    REQUIRE(Pica::f16::FromFloat32(-0.0f).ToRaw() == 0x8000);

    // Infinity
    REQUIRE(Pica::f16::FromRaw(0x7C00).ToFloat32() == std::bit_cast<float>(0x7F800000));
    REQUIRE(Pica::f16::FromRaw(0xFC00).ToFloat32() == std::bit_cast<float>(0xFF800000));
    REQUIRE(Pica::f16::FromFloat32(std::bit_cast<float>(0x7F800000)).ToRaw() == 0x7C00);
    REQUIRE(Pica::f16::FromFloat32(std::bit_cast<float>(0xFF800000)).ToRaw() == 0xFC00);

    // Denormal/Subnormal
    REQUIRE(Pica::f16::FromRaw(0x0001).ToFloat32() == std::bit_cast<float>(0x33800000));
    REQUIRE(Pica::f16::FromRaw(0x03FF).ToFloat32() == std::bit_cast<float>(0x387FC000));
    REQUIRE(Pica::f16::FromFloat32(std::bit_cast<float>(0x33800000)).ToRaw() == 0x0001);
    REQUIRE(Pica::f16::FromFloat32(std::bit_cast<float>(0x387FC000)).ToRaw() == 0x03FF);

    // NaN
    REQUIRE(std::bit_cast<u32>(Pica::f16::FromRaw(0x7E00)) == 0x7FC00000);
    REQUIRE(std::bit_cast<u32>(Pica::f16::FromRaw(0xFFFF)) == 0xFFFFE000);
    REQUIRE(Pica::f16::FromFloat32(std::bit_cast<float>(0x7FC00000)).ToRaw() == 0x7E00);
    REQUIRE(Pica::f16::FromFloat32(std::bit_cast<float>(0xFFFFE000)).ToRaw() == 0xFFFF);
}