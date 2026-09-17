// Copyright 2022-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstring>
#include <span>
#include "common/alignment.h"
#include "common/color.h"
#include "video_core/rasterizer_cache/pixel_format.h"
#include "video_core/utils.h"

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define TEXCODEC_NEON 1
#elif defined(CITRA_HAS_SSE42)
#include <tmmintrin.h>
#define TEXCODEC_SSE42 1
#endif

#if defined(TEXCODEC_NEON) || defined(TEXCODEC_SSE42)
#define TEXCODEC_SIMD 1
#endif

namespace VideoCore {

template <typename T>
inline T MakeInt(const u8* bytes) {
    T integer{};
    std::memcpy(&integer, bytes, sizeof(T));

    return integer;
}

// Scalar fallback and reference
template <PixelFormat format, bool converted>
constexpr void DecodePixel(const u8* source, u8* dest) {
    using namespace Common::Color;
    constexpr u32 bytes_per_pixel = GetFormatBpp(format) / 8;

    if constexpr (format == PixelFormat::RGBA8 && converted) {
        const auto abgr = DecodeRGBA8(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::RGB8 && converted) {
        const auto abgr = DecodeRGB8(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::RGB565 && converted) {
        const auto abgr = DecodeRGB565(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::RGB5A1 && converted) {
        const auto abgr = DecodeRGB5A1(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::RGBA4 && converted) {
        const auto abgr = DecodeRGBA4(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::IA8) {
        const auto abgr = DecodeIA8(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::RG8) {
        const auto abgr = DecodeRG8(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::I8) {
        const auto abgr = DecodeI8(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::A8) {
        const auto abgr = DecodeA8(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::IA4) {
        const auto abgr = DecodeIA4(source);
        std::memcpy(dest, abgr.AsArray(), 4);
    } else if constexpr (format == PixelFormat::D24 && converted) {
        const auto d32 = DecodeD24(source) / 16777215.f;
        std::memcpy(dest, &d32, sizeof(d32));
    } else if constexpr (format == PixelFormat::D24S8) {
        const u32 d24s8 = std::rotl(MakeInt<u32>(source), 8);
        std::memcpy(dest, &d24s8, sizeof(u32));
    } else {
        std::memcpy(dest, source, bytes_per_pixel);
    }
}

template <PixelFormat format, bool converted>
constexpr void EncodePixel(const u8* source, u8* dest) {
    using namespace Common::Color;
    constexpr u32 bytes_per_pixel = GetFormatBpp(format) / 8;

    if constexpr (format == PixelFormat::D24 && converted) {
        float d32;
        std::memcpy(&d32, source, sizeof(d32));
        EncodeD24(static_cast<u32>(d32 * 0xFFFFFF), dest);
    } else if constexpr (format == PixelFormat::D24S8) {
        const u32 s8d24 = std::rotr(MakeInt<u32>(source), 8);
        std::memcpy(dest, &s8d24, sizeof(u32));
    } else if constexpr ((converted &&
                          (format == PixelFormat::RGBA8 || format == PixelFormat::RGB8 ||
                           format == PixelFormat::RGB565 || format == PixelFormat::RGB5A1 ||
                           format == PixelFormat::RGBA4)) ||
                         format == PixelFormat::IA8 || format == PixelFormat::RG8 ||
                         format == PixelFormat::I8 || format == PixelFormat::A8 ||
                         format == PixelFormat::IA4) {
        Common::Vec4<u8> rgba;
        std::memcpy(rgba.AsArray(), source, 4);
        if constexpr (format == PixelFormat::RGBA8) {
            EncodeRGBA8(rgba, dest);
        } else if constexpr (format == PixelFormat::RGB8) {
            EncodeRGB8(rgba, dest);
        } else if constexpr (format == PixelFormat::RGB565) {
            EncodeRGB565(rgba, dest);
        } else if constexpr (format == PixelFormat::RGB5A1) {
            EncodeRGB5A1(rgba, dest);
        } else if constexpr (format == PixelFormat::RGBA4) {
            EncodeRGBA4(rgba, dest);
        } else if constexpr (format == PixelFormat::IA8) {
            EncodeIA8(rgba, dest);
        } else if constexpr (format == PixelFormat::RG8) {
            EncodeRG8(rgba, dest);
        } else if constexpr (format == PixelFormat::I8) {
            EncodeI8(rgba, dest);
        } else if constexpr (format == PixelFormat::A8) {
            EncodeA8(rgba, dest);
        } else {
            EncodeIA4(rgba, dest);
        }
    } else {
        std::memcpy(dest, source, bytes_per_pixel);
    }
}

namespace TextureCodec {

template <PixelFormat format, bool converted>
constexpr bool IsTrivialCopy() {
    constexpr bool always_converted = format == PixelFormat::IA8 || format == PixelFormat::RG8 ||
                                      format == PixelFormat::I8 || format == PixelFormat::A8 ||
                                      format == PixelFormat::IA4 || format == PixelFormat::D24S8;
    constexpr bool converted_when_asked =
        format == PixelFormat::RGBA8 || format == PixelFormat::RGB8 ||
        format == PixelFormat::RGB565 || format == PixelFormat::RGB5A1 ||
        format == PixelFormat::RGBA4 || format == PixelFormat::D24;
    return !always_converted && !(converted && converted_when_asked);
}

// 128 bit SIMD helpers.
// Lane counts refer to a 16-byte vector, so 16 u8, 8 u16 or 4 u32 lanes.
//
//   Load:     load 16 bytes from p (unaligned)
//   Load64:   load 8 bytes from p into the low half, zero the high half
//   Store:    write all 16 bytes of v to p (unaligned)
//   Store64:  write only the low 8 bytes of v to p, later bytes untouched
//   Shuffle:  permute the bytes of v by the per lane byte indices in idx
//   Or:       bitwise OR
//   And:      bitwise AND
//   Sub16:    per lane a - b over eight u16 lanes
//   Shr16<n>: logical right shift of each u16 lane by n
//   Shl16<n>: logical left shift of each u16 lane by n
//   Splat8:   broadcast a byte into all 16 byte lanes
//   Splat16:  broadcast a 16 bit value into all eight u16 lanes
//   Splat32:  broadcast a 32 bit value into all four u32 lanes
//   Zero:     all lanes to zero
//   ZipLo8:   interleave the low 8 bytes of a and b -> a0, b0, a1, b1, ...
//   ZipHi8:   interleave the high 8 bytes of a and b -> a8, b8, a9, b9, ...
//   ZipLo16:  interleave the low four u16 lanes -> a0, b0, a1, b1, ...
//   ZipHi16:  interleave the high four u16 lanes -> a4, b4, a5, b5, ...
//   ZipLo64:  interleave the low 2 u64 lanes -> a0, b0
//   AddSat8:  per byte unsigned add, saturating at 255 instead of wrapping
//   SubSat8:  per byte unsigned subtract, saturating at 0 instead of wrapping
#if defined(TEXCODEC_NEON)
using V128 = uint8x16_t;
inline V128 Load(const u8* p) {
    return vld1q_u8(p);
}
inline V128 Load64(const u8* p) {
    return vcombine_u8(vld1_u8(p), vdup_n_u8(0));
}
inline void Store(u8* p, V128 v) {
    vst1q_u8(p, v);
}
inline void Store64(u8* p, V128 v) {
    vst1_u8(p, vget_low_u8(v));
}
inline V128 Shuffle(V128 v, V128 idx) {
    return vqtbl1q_u8(v, idx);
}
inline V128 Or(V128 a, V128 b) {
    return vorrq_u8(a, b);
}
inline V128 And(V128 a, V128 b) {
    return vandq_u8(a, b);
}
inline V128 Sub16(V128 a, V128 b) {
    return vreinterpretq_u8_u16(vsubq_u16(vreinterpretq_u16_u8(a), vreinterpretq_u16_u8(b)));
}
template <int n>
inline V128 Shr16(V128 v) {
    return vreinterpretq_u8_u16(vshrq_n_u16(vreinterpretq_u16_u8(v), n));
}
template <int n>
inline V128 Shl16(V128 v) {
    return vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(v), n));
}
inline V128 Splat8(u8 x) {
    return vdupq_n_u8(x);
}
inline V128 Splat16(u16 x) {
    return vreinterpretq_u8_u16(vdupq_n_u16(x));
}
inline V128 Splat32(u32 x) {
    return vreinterpretq_u8_u32(vdupq_n_u32(x));
}
inline V128 Zero() {
    return vdupq_n_u8(0);
}
inline V128 ZipLo8(V128 a, V128 b) {
    return vzip1q_u8(a, b);
}
inline V128 ZipHi8(V128 a, V128 b) {
    return vzip2q_u8(a, b);
}
inline V128 ZipLo16(V128 a, V128 b) {
    return vreinterpretq_u8_u16(vzip1q_u16(vreinterpretq_u16_u8(a), vreinterpretq_u16_u8(b)));
}
inline V128 ZipHi16(V128 a, V128 b) {
    return vreinterpretq_u8_u16(vzip2q_u16(vreinterpretq_u16_u8(a), vreinterpretq_u16_u8(b)));
}
inline V128 ZipLo64(V128 a, V128 b) {
    return vreinterpretq_u8_u64(vzip1q_u64(vreinterpretq_u64_u8(a), vreinterpretq_u64_u8(b)));
}
inline V128 AddSat8(V128 a, V128 b) {
    return vqaddq_u8(a, b);
}
inline V128 SubSat8(V128 a, V128 b) {
    return vqsubq_u8(a, b);
}
#elif defined(TEXCODEC_SSE42)
using V128 = __m128i;
inline V128 Load(const u8* p) {
    return _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
}
inline V128 Load64(const u8* p) {
    return _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p));
}
inline void Store(u8* p, V128 v) {
    _mm_storeu_si128(reinterpret_cast<__m128i*>(p), v);
}
inline void Store64(u8* p, V128 v) {
    _mm_storel_epi64(reinterpret_cast<__m128i*>(p), v);
}
inline V128 Shuffle(V128 v, V128 idx) {
    return _mm_shuffle_epi8(v, idx);
}
inline V128 Or(V128 a, V128 b) {
    return _mm_or_si128(a, b);
}
inline V128 And(V128 a, V128 b) {
    return _mm_and_si128(a, b);
}
inline V128 Sub16(V128 a, V128 b) {
    return _mm_sub_epi16(a, b);
}
template <int n>
inline V128 Shr16(V128 v) {
    return _mm_srli_epi16(v, n);
}
template <int n>
inline V128 Shl16(V128 v) {
    return _mm_slli_epi16(v, n);
}
inline V128 Splat8(u8 x) {
    return _mm_set1_epi8(static_cast<char>(x));
}
inline V128 Splat16(u16 x) {
    return _mm_set1_epi16(static_cast<short>(x));
}
inline V128 Splat32(u32 x) {
    return _mm_set1_epi32(static_cast<int>(x));
}
inline V128 Zero() {
    return _mm_setzero_si128();
}
inline V128 ZipLo8(V128 a, V128 b) {
    return _mm_unpacklo_epi8(a, b);
}
inline V128 ZipHi8(V128 a, V128 b) {
    return _mm_unpackhi_epi8(a, b);
}
inline V128 ZipLo16(V128 a, V128 b) {
    return _mm_unpacklo_epi16(a, b);
}
inline V128 ZipHi16(V128 a, V128 b) {
    return _mm_unpackhi_epi16(a, b);
}
inline V128 ZipLo64(V128 a, V128 b) {
    return _mm_unpacklo_epi64(a, b);
}
inline V128 AddSat8(V128 a, V128 b) {
    return _mm_adds_epu8(a, b);
}
inline V128 SubSat8(V128 a, V128 b) {
    return _mm_subs_epu8(a, b);
}
#endif

#ifdef TEXCODEC_SIMD

// Builds a shuffle mask from a constexpr byte table.
inline V128 Mask(const std::array<u8, 16>& m) {
    return Load(m.data());
}
// Shuffle tables, used along the Shuffle instruction (0x80 = write zero).
inline constexpr std::array<u8, 16> BSWAP32 = {3,  2,  1, 0, 7,  6,  5,  4,
                                               11, 10, 9, 8, 15, 14, 13, 12};
inline constexpr std::array<u8, 16> ROTL32_8 = {3,  0, 1, 2,  7,  4,  5,  6,
                                                11, 8, 9, 10, 15, 12, 13, 14};
// BGR -> RGB (pixels 0-3 of a load, and pixels 4-7 of a load taken 8 bytes later)
inline constexpr std::array<u8, 16> BGR_LO = {2, 1, 0, 0x80, 5,  4,  3, 0x80,
                                              8, 7, 6, 0x80, 11, 10, 9, 0x80};
inline constexpr std::array<u8, 16> BGR_HI = {6,  5,  4,  0x80, 9,  8,  7,  0x80,
                                              12, 11, 10, 0x80, 15, 14, 13, 0x80};
// RGBA -> BGR (encode), 32 bytes in -> 24 bytes out
inline constexpr std::array<u8, 16> RGBA_TO_BGR_A = {2, 1,  0,  6,  5,    4,    10,   9,
                                                     8, 14, 13, 12, 0x80, 0x80, 0x80, 0x80};
inline constexpr std::array<u8, 16> RGBA_TO_BGR_B = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
                                                     0x80, 0x80, 0x80, 0x80, 2,    1,    0,    6};
inline constexpr std::array<u8, 16> RGBA_TO_BGR_C = {
    5, 4, 10, 9, 8, 14, 13, 12, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};

// IA8 [a,i] -> [i,i,i,a]
inline constexpr std::array<u8, 16> IA8_LO = {1, 1, 1, 0, 3, 3, 3, 2, 5, 5, 5, 4, 7, 7, 7, 6};
inline constexpr std::array<u8, 16> IA8_HI = {9,  9,  9,  8,  11, 11, 11, 10,
                                              13, 13, 13, 12, 15, 15, 15, 14};

// RG8 [g,r] -> [r,g,0,_]
inline constexpr std::array<u8, 16> RG8_LO = {1, 0, 0x80, 0x80, 3, 2, 0x80, 0x80,
                                              5, 4, 0x80, 0x80, 7, 6, 0x80, 0x80};
inline constexpr std::array<u8, 16> RG8_HI = {9,  8,  0x80, 0x80, 11, 10, 0x80, 0x80,
                                              13, 12, 0x80, 0x80, 15, 14, 0x80, 0x80};

// I8 [i] -> [i,i,i,_]
inline constexpr std::array<u8, 16> I8_LO = {0, 0, 0, 0x80, 1, 1, 1, 0x80,
                                             2, 2, 2, 0x80, 3, 3, 3, 0x80};
inline constexpr std::array<u8, 16> I8_HI = {4, 4, 4, 0x80, 5, 5, 5, 0x80,
                                             6, 6, 6, 0x80, 7, 7, 7, 0x80};

// A8 [a] -> [0,0,0,a]
inline constexpr std::array<u8, 16> A8_LO = {0x80, 0x80, 0x80, 0, 0x80, 0x80, 0x80, 1,
                                             0x80, 0x80, 0x80, 2, 0x80, 0x80, 0x80, 3};
inline constexpr std::array<u8, 16> A8_HI = {0x80, 0x80, 0x80, 4, 0x80, 0x80, 0x80, 5,
                                             0x80, 0x80, 0x80, 6, 0x80, 0x80, 0x80, 7};

// [I0..I7, A0..A7] -> [i,i,i,a]
inline constexpr std::array<u8, 16> IA_LO = {0, 0, 0, 8, 1, 1, 1, 9, 2, 2, 2, 10, 3, 3, 3, 11};
inline constexpr std::array<u8, 16> IA_HI = {4, 4, 4, 12, 5, 5, 5, 13, 6, 6, 6, 14, 7, 7, 7, 15};

// Expands each 4-bit value (0..15) held in a byte to 8 bits (x * 17).
inline V128 Expand4To8(V128 nibbles) {
    return Or(nibbles, Shl16<4>(nibbles));
}

// Converts 8 pixels of PICA data at src to host RGBA8 at dst (32 bytes).
template <PixelFormat format>
inline void DecodeSIMD8(const u8* src, u8* dst) {
    constexpr u32 OPAQUE = 0xFF000000;
    if constexpr (format == PixelFormat::RGBA8) {
        Store(dst, Shuffle(Load(src), Mask(BSWAP32)));
        Store(dst + 16, Shuffle(Load(src + 16), Mask(BSWAP32)));
    } else if constexpr (format == PixelFormat::D24S8) {
        Store(dst, Shuffle(Load(src), Mask(ROTL32_8)));
        Store(dst + 16, Shuffle(Load(src + 16), Mask(ROTL32_8)));
    } else if constexpr (format == PixelFormat::RGB8) {
        Store(dst, Or(Shuffle(Load(src), Mask(BGR_LO)), Splat32(OPAQUE)));
        Store(dst + 16, Or(Shuffle(Load(src + 8), Mask(BGR_HI)), Splat32(OPAQUE)));
    } else if constexpr (format == PixelFormat::RGB565 || format == PixelFormat::RGB5A1) {
        const V128 p = Load(src);
        const V128 r = Or(And(Shr16<8>(p), Splat16(0xF8)), Shr16<13>(p));
        V128 g, b, a;
        if constexpr (format == PixelFormat::RGB565) {
            g = Or(And(Shr16<3>(p), Splat16(0xFC)), And(Shr16<9>(p), Splat16(0x03)));
            b = Or(And(Shl16<3>(p), Splat16(0xF8)), And(Shr16<2>(p), Splat16(0x07)));
            a = Splat16(0xFF00);
        } else {
            g = Or(And(Shr16<3>(p), Splat16(0xF8)), And(Shr16<8>(p), Splat16(0x07)));
            b = Or(And(Shl16<2>(p), Splat16(0xF8)), And(Shr16<3>(p), Splat16(0x07)));
            a = And(Sub16(Zero(), And(p, Splat16(1))), Splat16(0xFF00));
        }
        const V128 rg = Or(r, Shl16<8>(g));
        const V128 ba = Or(b, a);
        Store(dst, ZipLo16(rg, ba));
        Store(dst + 16, ZipHi16(rg, ba));
    } else if constexpr (format == PixelFormat::RGBA4) {
        const V128 p =
            Shuffle(Load(src),
                    Mask(std::array<u8, 16>{1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14}));
        const V128 hi = Expand4To8(And(Shr16<4>(p), Splat8(0x0F)));
        const V128 lo = Expand4To8(And(p, Splat8(0x0F)));
        Store(dst, ZipLo8(hi, lo));
        Store(dst + 16, ZipHi8(hi, lo));
    } else if constexpr (format == PixelFormat::IA8) {
        const V128 p = Load(src);
        Store(dst, Shuffle(p, Mask(IA8_LO)));
        Store(dst + 16, Shuffle(p, Mask(IA8_HI)));
    } else if constexpr (format == PixelFormat::RG8) {
        const V128 p = Load(src);
        Store(dst, Or(Shuffle(p, Mask(RG8_LO)), Splat32(OPAQUE)));
        Store(dst + 16, Or(Shuffle(p, Mask(RG8_HI)), Splat32(OPAQUE)));
    } else if constexpr (format == PixelFormat::I8) {
        const V128 p = Load64(src);
        Store(dst, Or(Shuffle(p, Mask(I8_LO)), Splat32(OPAQUE)));
        Store(dst + 16, Or(Shuffle(p, Mask(I8_HI)), Splat32(OPAQUE)));
    } else if constexpr (format == PixelFormat::A8) {
        const V128 p = Load64(src);
        Store(dst, Shuffle(p, Mask(A8_LO)));
        Store(dst + 16, Shuffle(p, Mask(A8_HI)));
    } else if constexpr (format == PixelFormat::IA4) {
        const V128 p = Load64(src);
        const V128 i = Expand4To8(And(Shr16<4>(p), Splat8(0x0F)));
        const V128 a = Expand4To8(And(p, Splat8(0x0F)));
        const V128 ia = ZipLo64(i, a);
        Store(dst, Shuffle(ia, Mask(IA_LO)));
        Store(dst + 16, Shuffle(ia, Mask(IA_HI)));
    }
}

template <PixelFormat format, bool converted>
constexpr bool HasDecodeSIMD() {
    if constexpr (IsTrivialCopy<format, converted>()) {
        return false;
    } else {
        return format != PixelFormat::D24;
    }
}

// Converts 8 host RGBA8 pixels at src (32 bytes) into PICA data at dst.
template <PixelFormat format>
inline void EncodeSIMD8(const u8* src, u8* dst) {
    if constexpr (format == PixelFormat::RGBA8) {
        Store(dst, Shuffle(Load(src), Mask(BSWAP32)));
        Store(dst + 16, Shuffle(Load(src + 16), Mask(BSWAP32)));
    } else if constexpr (format == PixelFormat::D24S8) {
        constexpr std::array<u8, 16> ROTR32_8 = {1, 2,  3,  0, 5,  6,  7,  4,
                                                 9, 10, 11, 8, 13, 14, 15, 12};
        Store(dst, Shuffle(Load(src), Mask(ROTR32_8)));
        Store(dst + 16, Shuffle(Load(src + 16), Mask(ROTR32_8)));
    } else if constexpr (format == PixelFormat::RGB8) {
        const V128 p0 = Load(src);
        const V128 p1 = Load(src + 16);
        Store(dst, Or(Shuffle(p0, Mask(RGBA_TO_BGR_A)), Shuffle(p1, Mask(RGBA_TO_BGR_B))));
        Store64(dst + 16, Shuffle(p1, Mask(RGBA_TO_BGR_C)));
    }
}

template <PixelFormat format, bool converted>
constexpr bool HasEncodeSIMD() {
    return (format == PixelFormat::RGBA8 && converted) ||
           (format == PixelFormat::RGB8 && converted) || format == PixelFormat::D24S8;
}
#else
template <PixelFormat format, bool converted>
constexpr bool HasDecodeSIMD() {
    return false;
}
template <PixelFormat format, bool converted>
constexpr bool HasEncodeSIMD() {
    return false;
}
#endif // TEXCODEC_SIMD

// Decodes count consecutive pixels. Used for both tile rows and linear copies.
template <PixelFormat format, bool converted>
inline void Decode(const u8* src, u8* dst, std::size_t count) {
    constexpr u32 src_bpp = GetFormatBpp(format) / 8;
    constexpr u32 dst_bpp = converted ? 4 : GetFormatBytesPerPixel(format);
    std::size_t i = 0;
    if constexpr (IsTrivialCopy<format, converted>() && src_bpp == dst_bpp) {
        std::memcpy(dst, src, count * src_bpp);
        return;
    }
#ifdef TEXCODEC_SIMD
    if constexpr (HasDecodeSIMD<format, converted>()) {
        for (; i + 8 <= count; i += 8) {
            DecodeSIMD8<format>(src + i * src_bpp, dst + i * dst_bpp);
        }
    }
#endif
    for (; i < count; i++) {
        DecodePixel<format, converted>(src + i * src_bpp, dst + i * dst_bpp);
    }
}

template <PixelFormat format, bool converted>
inline void Encode(const u8* src, u8* dst, std::size_t count) {
    constexpr u32 src_bpp = converted ? 4 : GetFormatBytesPerPixel(format);
    constexpr u32 dst_bpp = GetFormatBpp(format) / 8;
    std::size_t i = 0;
    if constexpr (IsTrivialCopy<format, converted>() && src_bpp == dst_bpp) {
        std::memcpy(dst, src, count * dst_bpp);
        return;
    }
#ifdef TEXCODEC_SIMD
    if constexpr (HasEncodeSIMD<format, converted>()) {
        for (; i + 8 <= count; i += 8) {
            EncodeSIMD8<format>(src + i * src_bpp, dst + i * dst_bpp);
        }
    }
#endif
    for (; i < count; i++) {
        EncodePixel<format, converted>(src + i * src_bpp, dst + i * dst_bpp);
    }
}

// Morton helpers.
// In an 8x8 Morton tile, pixels (2k, y) and (2k+1, y) are always adjacent in memory, so every
// tile row is 4 contiguous pixel pairs located at MORTON_ROW[y] + MORTON_PAIR[k].
inline constexpr std::array<u32, 8> MORTON_ROW = {0x00, 0x02, 0x08, 0x0A, 0x20, 0x22, 0x28, 0x2A};
inline constexpr std::array<u32, 4> MORTON_PAIR = {0x0, 0x4, 0x10, 0x14};
inline constexpr std::array<u32, 8> MORTON_X = {0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15};

template <u32 bpp>
inline void GatherTileRow(const u8* tile, u32 y, u8* out) {
    const u8* row = tile + MORTON_ROW[y] * bpp;
    for (u32 k = 0; k < 4; k++) {
        std::memcpy(out + 2 * k * bpp, row + MORTON_PAIR[k] * bpp, 2 * bpp);
    }
}

template <u32 bpp>
inline void ScatterTileRow(const u8* in, u32 y, u8* tile) {
    u8* row = tile + MORTON_ROW[y] * bpp;
    for (u32 k = 0; k < 4; k++) {
        std::memcpy(row + MORTON_PAIR[k] * bpp, in + 2 * k * bpp, 2 * bpp);
    }
}

// Generic (non 4-bit, non compressed) tile decode. Row y of the tile lands on linear row 7 - y.
template <PixelFormat format, bool converted>
inline void DecodeTile(const u8* tile, u8* linear, u32 stride) {
    constexpr u32 bpp = GetFormatBpp(format) / 8;
    constexpr u32 linear_bpp = converted ? 4 : GetFormatBytesPerPixel(format);
    const std::size_t row_pitch = static_cast<std::size_t>(stride) * linear_bpp;

    if constexpr (IsTrivialCopy<format, converted>() && bpp == linear_bpp) {
        for (u32 y = 0; y < 8; y++) {
            GatherTileRow<bpp>(tile, y, linear + (7 - y) * row_pitch);
        }
    } else if constexpr (HasDecodeSIMD<format, converted>()) {
        alignas(16) std::array<u8, 64 * bpp + 16> rows;
        for (u32 y = 0; y < 8; y++) {
            GatherTileRow<bpp>(tile, y, rows.data() + y * 8 * bpp);
        }
        for (u32 y = 0; y < 8; y++) {
            Decode<format, converted>(rows.data() + y * 8 * bpp, linear + (7 - y) * row_pitch, 8);
        }
    } else {
        for (u32 y = 0; y < 8; y++) {
            u8* dst = linear + (7 - y) * row_pitch;
            for (u32 x = 0; x < 8; x++) {
                DecodePixel<format, converted>(tile + (MORTON_ROW[y] + MORTON_X[x]) * bpp,
                                               dst + x * linear_bpp);
            }
        }
    }
}

template <PixelFormat format, bool converted>
inline void EncodeTile(const u8* linear, u8* tile, u32 stride) {
    constexpr u32 bpp = GetFormatBpp(format) / 8;
    constexpr u32 linear_bpp = converted ? 4 : GetFormatBytesPerPixel(format);
    const std::size_t row_pitch = static_cast<std::size_t>(stride) * linear_bpp;

    if constexpr (IsTrivialCopy<format, converted>() && bpp == linear_bpp) {
        for (u32 y = 0; y < 8; y++) {
            ScatterTileRow<bpp>(linear + (7 - y) * row_pitch, y, tile);
        }
    } else if constexpr (HasEncodeSIMD<format, converted>()) {
        alignas(16) std::array<u8, 64 * bpp + 16> rows;
        for (u32 y = 0; y < 8; y++) {
            Encode<format, converted>(linear + (7 - y) * row_pitch, rows.data() + y * 8 * bpp, 8);
        }
        for (u32 y = 0; y < 8; y++) {
            ScatterTileRow<bpp>(rows.data() + y * 8 * bpp, y, tile);
        }
    } else {
        for (u32 y = 0; y < 8; y++) {
            const u8* src = linear + (7 - y) * row_pitch;
            for (u32 x = 0; x < 8; x++) {
                EncodePixel<format, converted>(src + x * linear_bpp,
                                               tile + (MORTON_ROW[y] + MORTON_X[x]) * bpp);
            }
        }
    }
}

template <PixelFormat format>
inline void DecodeTile4(const u8* tile, u8* linear, u32 stride) {
    constexpr PixelFormat expanded = format == PixelFormat::I4 ? PixelFormat::I8 : PixelFormat::A8;
    const std::size_t row_pitch = static_cast<std::size_t>(stride) * 4;

    alignas(16) std::array<u8, 64> px;
#ifdef TEXCODEC_SIMD
    // Rows 0-3 live in bytes 0-15 and rows 4-7 in bytes 16-31.
    constexpr std::array<u8, 16> ROWS = {0, 2, 8, 10, 1, 3, 9, 11, 4, 6, 12, 14, 5, 7, 13, 15};
    for (u32 half = 0; half < 2; half++) {
        const V128 v = Shuffle(Load(tile + half * 16), Mask(ROWS));
        const V128 lo = And(v, Splat8(0x0F));
        const V128 hi = And(Shr16<4>(v), Splat8(0x0F));
        Store(px.data() + half * 32, Expand4To8(ZipLo8(lo, hi)));
        Store(px.data() + half * 32 + 16, Expand4To8(ZipHi8(lo, hi)));
    }
#else
    for (u32 y = 0; y < 8; y++) {
        for (u32 k = 0; k < 4; k++) {
            const u8 b = tile[(MORTON_ROW[y] + MORTON_PAIR[k]) >> 1];
            px[y * 8 + 2 * k] = Common::Color::Convert4To8(b & 0xF);
            px[y * 8 + 2 * k + 1] = Common::Color::Convert4To8(b >> 4);
        }
    }
#endif
    for (u32 y = 0; y < 8; y++) {
        Decode<expanded, false>(px.data() + y * 8, linear + (7 - y) * row_pitch, 8);
    }
}

template <PixelFormat format>
inline void EncodeTile4(const u8* linear, u8* tile, u32 stride) {
    const std::size_t row_pitch = static_cast<std::size_t>(stride) * 4;
    for (u32 y = 0; y < 8; y++) {
        const u8* src = linear + (7 - y) * row_pitch;
        for (u32 k = 0; k < 4; k++) {
            u8 nibble[2];
            for (u32 j = 0; j < 2; j++) {
                const u8* p = src + (2 * k + j) * 4;
                u8 value;
                if constexpr (format == PixelFormat::I4) {
                    value = static_cast<u8>((static_cast<u32>(p[0]) + p[1] + p[2]) / 3);
                } else {
                    value = p[3];
                }
                nibble[j] = Common::Color::Convert8To4(value);
            }
            tile[(MORTON_ROW[y] + MORTON_PAIR[k]) >> 1] =
                static_cast<u8>(nibble[1] << 4 | nibble[0]);
        }
    }
}

// ETC1 decoding

inline constexpr std::array<std::array<s32, 4>, 8> ETC1_MODIFIERS = {{
    {2, 8, -2, -8},
    {5, 17, -5, -17},
    {9, 29, -9, -29},
    {13, 42, -13, -42},
    {18, 60, -18, -60},
    {24, 80, -24, -80},
    {33, 106, -33, -106},
    {47, 183, -47, -183},
}};

constexpr std::array<std::array<u8, 16>, 8> MakeETC1ModTable(bool negative) {
    std::array<std::array<u8, 16>, 8> t{};
    for (u32 i = 0; i < 8; i++) {
        for (u32 m = 0; m < 4; m++) {
            const s32 mod = ETC1_MODIFIERS[i][m];
            const bool is_negative = mod < 0;
            for (u32 c = 0; c < 3; c++) {
                t[i][m * 4 + c] =
                    is_negative == negative ? static_cast<u8>(mod < 0 ? -mod : mod) : u8{0};
            }
        }
    }
    return t;
}
inline constexpr auto ETC1_POS = MakeETC1ModTable(false);
inline constexpr auto ETC1_NEG = MakeETC1ModTable(true);

inline constexpr s32 SignExtend3(u32 v) {
    return static_cast<s32>(v << 29) >> 29;
}

// Decode ETC1 block, this code path only evaluates the block header once unlike the one in
// etc1.h which is meant for single pixel lookups.
template <bool has_alpha>
inline void DecodeETC1Block(u64 block, u64 alpha, u8* dst, std::ptrdiff_t pitch) {
    using Common::Color::Convert4To8;
    using Common::Color::Convert5To8;

    const bool flip = (block >> 32) & 1;
    const bool differential = (block >> 33) & 1;
    const u32 tables[2] = {static_cast<u32>(block >> 37) & 7, static_cast<u32>(block >> 34) & 7};

    u32 base[2] = {0, 0};
    constexpr u32 shifts[3] = {56, 48, 40};
    for (u32 c = 0; c < 3; c++) {
        const u32 sh = shifts[c];
        const u32 value = static_cast<u32>(block >> (sh + 3)) & 0x1F;
        const s32 delta = SignExtend3(static_cast<u32>(block >> sh) & 7);
        const u32 diff_1 = Convert5To8(static_cast<u8>(value));
        const u32 diff_2 = Convert5To8(static_cast<u8>(static_cast<s32>(value) + delta));
        const u32 sep_1 = Convert4To8(static_cast<u8>((block >> (sh + 4)) & 0xF));
        const u32 sep_2 = Convert4To8(static_cast<u8>((block >> sh) & 0xF));
        base[0] |= (differential ? diff_1 : sep_1) << (8 * c);
        base[1] |= (differential ? diff_2 : sep_2) << (8 * c);
    }

    alignas(16) u32 palette[8];
#ifdef TEXCODEC_SIMD
    for (u32 s = 0; s < 2; s++) {
        const V128 pal = SubSat8(AddSat8(Splat32(base[s]), Load(ETC1_POS[tables[s]].data())),
                                 Load(ETC1_NEG[tables[s]].data()));
        Store(reinterpret_cast<u8*>(palette + 4 * s),
              has_alpha ? pal : Or(pal, Splat32(0xFF000000)));
    }
#else
    for (u32 s = 0; s < 2; s++) {
        for (u32 m = 0; m < 4; m++) {
            const s32 mod = ETC1_MODIFIERS[tables[s]][m];
            u32 color = has_alpha ? 0u : 0xFF000000u;
            for (u32 c = 0; c < 3; c++) {
                const s32 v = static_cast<s32>((base[s] >> (8 * c)) & 0xFF) + mod;
                color |= static_cast<u32>(std::clamp(v, 0, 255)) << (8 * c);
            }
            palette[s * 4 + m] = color;
        }
    }
#endif

    const u32 lsb = static_cast<u32>(block) & 0xFFFF;
    const u32 msb = static_cast<u32>(block >> 16) & 0xFFFF;

    // ETC1A4: handle alpha
    alignas(16) std::array<u8, 16> alpha8{};
    if constexpr (has_alpha) {
#ifdef TEXCODEC_SIMD
        const V128 packed = Load64(reinterpret_cast<const u8*>(&alpha));
        const V128 lo = And(packed, Splat8(0x0F));
        const V128 hi = And(Shr16<4>(packed), Splat8(0x0F));
        Store(alpha8.data(), Expand4To8(ZipLo8(lo, hi)));
#else
        for (u32 t = 0; t < 16; t++) {
            alpha8[t] = Convert4To8(static_cast<u8>((alpha >> (4 * t)) & 0xF));
        }
#endif
    }

    // Loop unrolling has shown better results here
#if defined(__clang__)
#pragma clang loop unroll(full)
#elif defined(__GNUC__)
#pragma GCC unroll 4
#endif
    for (u32 y = 0; y < 4; y++) {
        const u32 sub_bits = flip ? (y >= 2 ? 0x4444u : 0u) : 0x4400u;
        const u32 idx = ((lsb >> y) & 0x1111u) | (((msb >> y) & 0x1111u) << 1) | sub_bits;
        u32 row[4] = {palette[idx & 7], palette[(idx >> 4) & 7], palette[(idx >> 8) & 7],
                      palette[(idx >> 12) & 7]};
        if constexpr (has_alpha) {
            row[0] |= static_cast<u32>(alpha8[0 + y]) << 24;
            row[1] |= static_cast<u32>(alpha8[4 + y]) << 24;
            row[2] |= static_cast<u32>(alpha8[8 + y]) << 24;
            row[3] |= static_cast<u32>(alpha8[12 + y]) << 24;
        }
        std::memcpy(dst + y * pitch, row, sizeof(row));
    }
}

template <PixelFormat format>
inline void DecodeTileETC1(const u8* tile, u8* linear, u32 stride) {
    constexpr bool has_alpha = format == PixelFormat::ETC1A4;
    constexpr std::size_t block_size = has_alpha ? 16 : 8;
    const std::ptrdiff_t pitch = static_cast<std::ptrdiff_t>(stride) * 4;

    for (u32 i = 0; i < 4; i++) {
        const u32 bx = i & 1;
        const u32 by = i >> 1;
        const u8* src = tile + i * block_size;
        u64 alpha = 0;
        if constexpr (has_alpha) {
            alpha = MakeInt<u64>(src);
            src += 8;
        }

        u8* dst = linear + (7 - 4 * by) * pitch + bx * 16;
        DecodeETC1Block<has_alpha>(MakeInt<u64>(src), alpha, dst, -pitch);
    }
}

template <bool morton_to_linear, PixelFormat format, bool converted>
inline void MortonCopyTile(u32 stride, u8* tile_buffer, u8* linear_buffer) {
    constexpr bool is_compressed = format == PixelFormat::ETC1 || format == PixelFormat::ETC1A4;
    constexpr bool is_4bit = format == PixelFormat::I4 || format == PixelFormat::A4;

    if constexpr (morton_to_linear) {
        if constexpr (is_compressed) {
            DecodeTileETC1<format>(tile_buffer, linear_buffer, stride);
        } else if constexpr (is_4bit) {
            DecodeTile4<format>(tile_buffer, linear_buffer, stride);
        } else {
            DecodeTile<format, converted>(tile_buffer, linear_buffer, stride);
        }
    } else {
        static_assert(!is_compressed, "ETC1 encoding is not supported");
        if constexpr (is_4bit) {
            EncodeTile4<format>(linear_buffer, tile_buffer, stride);
        } else {
            EncodeTile<format, converted>(linear_buffer, tile_buffer, stride);
        }
    }
}

} // namespace TextureCodec

/**
 * @brief Performs morton to/from linear convertions on the provided pixel data
 * @param converted If true performs RGBA8 to/from convertion to all color formats
 * @param width, height The dimentions of the rectangular region of pixels in linear_buffer
 * @param start_offset The number of bytes from the start of the first tile to the start of
 * tiled_buffer
 * @param end_offset The number of bytes from the start of the first tile to the end of tiled_buffer
 * @param linear_buffer The linear pixel data
 * @param tiled_buffer The tiled pixel data
 *
 * The MortonCopy is at the heart of the PICA texture implementation, as it's responsible for
 * converting between linear and morton tiled layouts. The function handles both convertions but
 * there are slightly different paths and inputs for each:
 *
 * Morton to Linear:
 * During uploads, tiled_buffer is always aligned to the tile or scanline boundary depending if the
 * linear rectangle spans multiple vertical tiles. linear_buffer does not reference the entire
 * texture area, but rather the specific rectangle affected by the upload.
 *
 * Linear to Morton:
 * This is similar to the other convertion but with some differences. In this case tiled_buffer is
 * not required to be aligned to any specific boundary which requires special care.
 * start_offset/end_offset are useful here as they tell us exactly where the data should be placed
 * in the linear_buffer.
 */
template <bool morton_to_linear, PixelFormat format, bool converted = false>
static void MortonCopy(u32 width, u32 height, u32 start_offset, u32 end_offset,
                       std::span<u8> linear_buffer, std::span<u8> tiled_buffer) {
    constexpr u32 aligned_bytes_per_pixel = converted ? 4 : GetFormatBytesPerPixel(format);
    constexpr u32 tile_size = GetFormatBpp(format) * 64 / 8;
    static_assert(aligned_bytes_per_pixel >= GetFormatBpp(format) / 8, "");

    const u32 linear_tile_stride = (7 * width + 8) * aligned_bytes_per_pixel;
    const u32 aligned_down_start_offset = Common::AlignDown(start_offset, tile_size);
    const u32 aligned_start_offset = Common::AlignUp(start_offset, tile_size);
    const u32 aligned_end_offset = Common::AlignDown(end_offset, tile_size);
    const u32 begin_pixel_index = aligned_down_start_offset * 8 / GetFormatBpp(format);

    ASSERT(!morton_to_linear ||
           (aligned_start_offset == start_offset && aligned_end_offset == end_offset));

    // In OpenGL the texture origin is in the bottom left corner as opposed to other
    // APIs that have it at the top left. To avoid flipping texture coordinates in
    // the shader we read/write the linear buffer from the bottom up
    u32 x = (begin_pixel_index % (width * 8)) / 8;
    u32 y = (begin_pixel_index / (width * 8)) * 8;
    u32 linear_offset = ((height - 8 - y) * width + x) * aligned_bytes_per_pixel;
    u32 tiled_offset = 0;

    const auto linear_next_tile = [&] {
        x = (x + 8) % width;
        linear_offset += 8 * aligned_bytes_per_pixel;
        if (!x) {
            y = (y + 8) % height;
            if (!y) {
                return;
            }

            linear_offset -= width * 9 * aligned_bytes_per_pixel;
        }
    };

    // Bounds are validated once per tile.
    const auto linear_tile = [&] {
        return linear_buffer.subspan(linear_offset, linear_tile_stride).data();
    };

    // If during a texture download the start coordinate is not tile aligned, swizzle
    // the tile affected to a temporary buffer and copy the part we are interested in
    if (start_offset < aligned_start_offset && !morton_to_linear) {
        std::array<u8, tile_size> tmp_buf{};
        TextureCodec::MortonCopyTile<morton_to_linear, format, converted>(width, tmp_buf.data(),
                                                                          linear_tile());

        std::memcpy(tiled_buffer.data(), tmp_buf.data() + start_offset - aligned_down_start_offset,
                    std::min(aligned_start_offset, end_offset) - start_offset);

        tiled_offset += aligned_start_offset - start_offset;
        linear_next_tile();
    }

    // If the copy spans multiple tiles, copy the fully aligned tiles in between.
    if (aligned_start_offset < aligned_end_offset) {
        const u32 tile_buffer_size = static_cast<u32>(tiled_buffer.size());
        const u32 buffer_end =
            std::min(tiled_offset + aligned_end_offset - aligned_start_offset, tile_buffer_size);
        while (tiled_offset < buffer_end) {
            u8* tiled_data = tiled_buffer.subspan(tiled_offset, tile_size).data();
            TextureCodec::MortonCopyTile<morton_to_linear, format, converted>(width, tiled_data,
                                                                              linear_tile());
            tiled_offset += tile_size;
            linear_next_tile();
        }
    }

    // If during a texture download the end coordinate is not tile aligned, swizzle
    // the tile affected to a temporary buffer and copy the part we are interested in
    if (end_offset > std::max(aligned_start_offset, aligned_end_offset) && !morton_to_linear) {
        std::array<u8, tile_size> tmp_buf{};
        TextureCodec::MortonCopyTile<morton_to_linear, format, converted>(width, tmp_buf.data(),
                                                                          linear_tile());
        std::memcpy(tiled_buffer.data() + tiled_offset, tmp_buf.data(),
                    end_offset - aligned_end_offset);
    }
}

/**
 * Performs a linear copy, converting pixel formats if required.
 * @tparam decode If true, decodes the texture if needed. Otherwise, encodes if needed.
 * @tparam format Pixel format to copy.
 * @tparam converted If true, converts the texture to/from the appropriate format.
 * @param src_buffer The source pixel data
 * @param dst_buffer The destination pixel data
 */
template <bool decode, PixelFormat format, bool converted = false>
static void LinearCopy(std::span<u8> src_buffer, std::span<u8> dst_buffer) {
    const std::size_t src_size = src_buffer.size();
    const std::size_t dst_size = dst_buffer.size();

    if constexpr (converted) {
        constexpr u32 encoded_bytes_per_pixel = GetFormatBpp(format) / 8;
        constexpr u32 decoded_bytes_per_pixel = 4;
        constexpr u32 src_bytes_per_pixel =
            decode ? encoded_bytes_per_pixel : decoded_bytes_per_pixel;
        constexpr u32 dst_bytes_per_pixel =
            decode ? decoded_bytes_per_pixel : encoded_bytes_per_pixel;

        const std::size_t count =
            std::min(src_size / src_bytes_per_pixel, dst_size / dst_bytes_per_pixel);
        if constexpr (decode) {
            TextureCodec::Decode<format, converted>(src_buffer.data(), dst_buffer.data(), count);
        } else {
            TextureCodec::Encode<format, converted>(src_buffer.data(), dst_buffer.data(), count);
        }
    } else {
        std::memcpy(dst_buffer.data(), src_buffer.data(), std::min(src_size, dst_size));
    }
}

using MortonFunc = void (*)(u32, u32, u32, u32, std::span<u8>, std::span<u8>);

static constexpr std::array<MortonFunc, 18> UNSWIZZLE_TABLE = {
    MortonCopy<true, PixelFormat::RGBA8>,  // 0
    MortonCopy<true, PixelFormat::RGB8>,   // 1
    MortonCopy<true, PixelFormat::RGB5A1>, // 2
    MortonCopy<true, PixelFormat::RGB565>, // 3
    MortonCopy<true, PixelFormat::RGBA4>,  // 4
    MortonCopy<true, PixelFormat::IA8>,    // 5
    MortonCopy<true, PixelFormat::RG8>,    // 6
    MortonCopy<true, PixelFormat::I8>,     // 7
    MortonCopy<true, PixelFormat::A8>,     // 8
    MortonCopy<true, PixelFormat::IA4>,    // 9
    MortonCopy<true, PixelFormat::I4>,     // 10
    MortonCopy<true, PixelFormat::A4>,     // 11
    MortonCopy<true, PixelFormat::ETC1>,   // 12
    MortonCopy<true, PixelFormat::ETC1A4>, // 13
    MortonCopy<true, PixelFormat::D16>,    // 14
    nullptr,                               // 15
    MortonCopy<true, PixelFormat::D24>,    // 16
    MortonCopy<true, PixelFormat::D24S8>,  // 17
};

static constexpr std::array<MortonFunc, 18> UNSWIZZLE_TABLE_CONVERTED = {
    MortonCopy<true, PixelFormat::RGBA8, true>,  // 0
    MortonCopy<true, PixelFormat::RGB8, true>,   // 1
    MortonCopy<true, PixelFormat::RGB5A1, true>, // 2
    MortonCopy<true, PixelFormat::RGB565, true>, // 3
    MortonCopy<true, PixelFormat::RGBA4, true>,  // 4
    // The following formats are implicitly converted to RGBA regardless, so ignore them.
    nullptr,                                  // 5
    nullptr,                                  // 6
    nullptr,                                  // 7
    nullptr,                                  // 8
    nullptr,                                  // 9
    nullptr,                                  // 10
    nullptr,                                  // 11
    nullptr,                                  // 12
    nullptr,                                  // 13
    MortonCopy<true, PixelFormat::D16, true>, // 14
    nullptr,                                  // 15
    MortonCopy<true, PixelFormat::D24, true>, // 16
    // No conversion here as we need to do a special deinterleaving conversion elsewhere.
    nullptr, // 17
};

static constexpr std::array<MortonFunc, 18> SWIZZLE_TABLE = {
    MortonCopy<false, PixelFormat::RGBA8>,  // 0
    MortonCopy<false, PixelFormat::RGB8>,   // 1
    MortonCopy<false, PixelFormat::RGB5A1>, // 2
    MortonCopy<false, PixelFormat::RGB565>, // 3
    MortonCopy<false, PixelFormat::RGBA4>,  // 4
    MortonCopy<false, PixelFormat::IA8>,    // 5
    MortonCopy<false, PixelFormat::RG8>,    // 6
    MortonCopy<false, PixelFormat::I8>,     // 7
    MortonCopy<false, PixelFormat::A8>,     // 8
    MortonCopy<false, PixelFormat::IA4>,    // 9
    MortonCopy<false, PixelFormat::I4>,     // 10
    MortonCopy<false, PixelFormat::A4>,     // 11
    nullptr,                                // 12
    nullptr,                                // 13
    MortonCopy<false, PixelFormat::D16>,    // 14
    nullptr,                                // 15
    MortonCopy<false, PixelFormat::D24>,    // 16
    MortonCopy<false, PixelFormat::D24S8>,  // 17
};

static constexpr std::array<MortonFunc, 18> SWIZZLE_TABLE_CONVERTED = {
    MortonCopy<false, PixelFormat::RGBA8, true>,  // 0
    MortonCopy<false, PixelFormat::RGB8, true>,   // 1
    MortonCopy<false, PixelFormat::RGB5A1, true>, // 2
    MortonCopy<false, PixelFormat::RGB565, true>, // 3
    MortonCopy<false, PixelFormat::RGBA4, true>,  // 4
    // The following formats are implicitly converted from RGBA regardless, so ignore them.
    nullptr,                                   // 5
    nullptr,                                   // 6
    nullptr,                                   // 7
    nullptr,                                   // 8
    nullptr,                                   // 9
    nullptr,                                   // 10
    nullptr,                                   // 11
    nullptr,                                   // 12
    nullptr,                                   // 13
    MortonCopy<false, PixelFormat::D16, true>, // 14
    nullptr,                                   // 15
    MortonCopy<false, PixelFormat::D24, true>, // 16
    // No conversion here as we need to do a special interleaving conversion elsewhere.
    nullptr, // 17
};

using LinearFunc = void (*)(std::span<u8>, std::span<u8>);

static constexpr std::array<LinearFunc, 18> LINEAR_DECODE_TABLE = {
    LinearCopy<true, PixelFormat::RGBA8>,  // 0
    LinearCopy<true, PixelFormat::RGB8>,   // 1
    LinearCopy<true, PixelFormat::RGB5A1>, // 2
    LinearCopy<true, PixelFormat::RGB565>, // 3
    LinearCopy<true, PixelFormat::RGBA4>,  // 4
    // These formats cannot be used linearly and can be ignored.
    nullptr,                              // 5
    nullptr,                              // 6
    nullptr,                              // 7
    nullptr,                              // 8
    nullptr,                              // 9
    nullptr,                              // 10
    nullptr,                              // 11
    nullptr,                              // 12
    nullptr,                              // 13
    LinearCopy<true, PixelFormat::D16>,   // 14
    nullptr,                              // 15
    LinearCopy<true, PixelFormat::D24>,   // 16
    LinearCopy<true, PixelFormat::D24S8>, // 17
};

static constexpr std::array<LinearFunc, 18> LINEAR_DECODE_TABLE_CONVERTED = {
    LinearCopy<true, PixelFormat::RGBA8, true>,  // 0
    LinearCopy<true, PixelFormat::RGB8, true>,   // 1
    LinearCopy<true, PixelFormat::RGB5A1, true>, // 2
    LinearCopy<true, PixelFormat::RGB565, true>, // 3
    LinearCopy<true, PixelFormat::RGBA4, true>,  // 4
    // These formats cannot be used linearly and can be ignored.
    nullptr,                                  // 5
    nullptr,                                  // 6
    nullptr,                                  // 7
    nullptr,                                  // 8
    nullptr,                                  // 9
    nullptr,                                  // 10
    nullptr,                                  // 11
    nullptr,                                  // 12
    nullptr,                                  // 13
    LinearCopy<true, PixelFormat::D16, true>, // 14
    nullptr,                                  // 15
    LinearCopy<true, PixelFormat::D24, true>, // 16
    // No conversion here as we need to do a special deinterleaving conversion elsewhere.
    nullptr, // 17
};

static constexpr std::array<LinearFunc, 18> LINEAR_ENCODE_TABLE = {
    LinearCopy<false, PixelFormat::RGBA8>,  // 0
    LinearCopy<false, PixelFormat::RGB8>,   // 1
    LinearCopy<false, PixelFormat::RGB5A1>, // 2
    LinearCopy<false, PixelFormat::RGB565>, // 3
    LinearCopy<false, PixelFormat::RGBA4>,  // 4
    // These formats cannot be used linearly and can be ignored.
    nullptr,                               // 5
    nullptr,                               // 6
    nullptr,                               // 7
    nullptr,                               // 8
    nullptr,                               // 9
    nullptr,                               // 10
    nullptr,                               // 11
    nullptr,                               // 12
    nullptr,                               // 13
    LinearCopy<false, PixelFormat::D16>,   // 14
    nullptr,                               // 15
    LinearCopy<false, PixelFormat::D24>,   // 16
    LinearCopy<false, PixelFormat::D24S8>, // 17
};

static constexpr std::array<LinearFunc, 18> LINEAR_ENCODE_TABLE_CONVERTED = {
    LinearCopy<false, PixelFormat::RGBA8, true>,  // 0
    LinearCopy<false, PixelFormat::RGB8, true>,   // 1
    LinearCopy<false, PixelFormat::RGB5A1, true>, // 2
    LinearCopy<false, PixelFormat::RGB565, true>, // 3
    LinearCopy<false, PixelFormat::RGBA4, true>,  // 4
    // These formats cannot be used linearly and can be ignored.
    nullptr,                                   // 5
    nullptr,                                   // 6
    nullptr,                                   // 7
    nullptr,                                   // 8
    nullptr,                                   // 9
    nullptr,                                   // 10
    nullptr,                                   // 11
    nullptr,                                   // 12
    nullptr,                                   // 13
    LinearCopy<false, PixelFormat::D16, true>, // 14
    nullptr,                                   // 15
    LinearCopy<false, PixelFormat::D24, true>, // 16
    // No conversion here as we need to do a special interleaving conversion elsewhere.
    nullptr, // 17
};

} // namespace VideoCore
