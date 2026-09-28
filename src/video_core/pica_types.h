// Copyright 2015-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cmath>
#include <cstring>
#include <boost/serialization/access.hpp>
#include "common/common_types.h"

namespace Pica {

/**
 * Template class for converting arbitrary Pica float types to IEEE 754 32-bit single-precision
 * floating point.
 *
 * When decoding, format is as follows:
 *  - The first `M` bits are the mantissa
 *  - The next `E` bits are the exponent
 *  - The last bit is the sign bit
 *
 * @todo Verify on HW if this conversion is sufficiently accurate.
 */
template <unsigned M, unsigned E>
struct Float {
public:
    static constexpr Float<M, E> FromFloat32(float val) {
        Float<M, E> ret;
        ret.value = val;
        return ret;
    }

    static constexpr Float<M, E> FromRaw(u32 hex) {
        constexpr s32 bias = 127 - ((1 << (E - 1)) - 1);

        Float<M, E> res;

#ifdef __FLT16_MANT_DIG__
        if constexpr (M == 10 && E == 5) {
            res.value = std::bit_cast<_Float16>(static_cast<u16>(hex));
            return res;
        }
#endif

        s32 exponent = (hex >> M) & EXPONENT_MASK;
        u32 mantissa = hex & MANTISSA_MASK;
        const u32 fp32_sign = (hex >> (E + M)) << 31;

        if (exponent == 0 && mantissa == 0) {
            // Zero
            hex = fp32_sign;
        } else [[likely]] {
            if (exponent == 0) {
                // PICA200 flushes denormals, but in our case, we want these values to be converted
                // into 32-bit floats in a lossless way
                // Denormals might map to normal values in fp32 and must be renormalized to have the
                // exact same value
                exponent = bias + 1;
                while ((mantissa & (1 << M)) == 0) {
                    exponent--;
                    mantissa <<= 1;
                }
                mantissa &= MANTISSA_MASK;
                hex = fp32_sign | (exponent << 23) | (mantissa << (23 - M));
            } else if (exponent == EXPONENT_MASK) {
                // Inf or NaN
                hex = fp32_sign | (0xFF << 23) | (mantissa << (23 - M));
            } else [[likely]] {
                // Normal
                hex = fp32_sign | ((bias + exponent) << 23) | (mantissa << (23 - M));
            }
        }

        res.value = std::bit_cast<float>(hex);

        return res;
    }

    static constexpr Float<M, E> Zero() {
        return FromFloat32(0.f);
    }

    static constexpr Float<M, E> One() {
        return FromFloat32(1.f);
    }

    // Not recommended for anything but logging
    constexpr float ToFloat32() const {
        return value;
    }

    constexpr Float<M, E> operator*(const Float<M, E>& flt) const {
        float result = value * flt.ToFloat32();
        // PICA gives 0 instead of NaN when multiplying by inf
        if (std::isnan(result))
            if (!std::isnan(value) && !std::isnan(flt.ToFloat32()))
                result = 0.f;
        return Float<M, E>::FromFloat32(result);
    }

    constexpr Float<M, E> operator/(const Float<M, E>& flt) const {
        return Float<M, E>::FromFloat32(ToFloat32() / flt.ToFloat32());
    }

    constexpr Float<M, E> operator+(const Float<M, E>& flt) const {
        return Float<M, E>::FromFloat32(ToFloat32() + flt.ToFloat32());
    }

    constexpr Float<M, E> operator-(const Float<M, E>& flt) const {
        return Float<M, E>::FromFloat32(ToFloat32() - flt.ToFloat32());
    }

    constexpr Float<M, E>& operator*=(const Float<M, E>& flt) {
        value = operator*(flt).value;
        return *this;
    }

    constexpr Float<M, E>& operator/=(const Float<M, E>& flt) {
        value /= flt.ToFloat32();
        return *this;
    }

    constexpr Float<M, E>& operator+=(const Float<M, E>& flt) {
        value += flt.ToFloat32();
        return *this;
    }

    constexpr Float<M, E>& operator-=(const Float<M, E>& flt) {
        value -= flt.ToFloat32();
        return *this;
    }

    constexpr Float<M, E> operator-() const {
        return Float<M, E>::FromFloat32(-ToFloat32());
    }

    constexpr bool operator<(const Float<M, E>& flt) const {
        return ToFloat32() < flt.ToFloat32();
    }

    constexpr bool operator>(const Float<M, E>& flt) const {
        return ToFloat32() > flt.ToFloat32();
    }

    constexpr bool operator>=(const Float<M, E>& flt) const {
        return ToFloat32() >= flt.ToFloat32();
    }

    constexpr bool operator<=(const Float<M, E>& flt) const {
        return ToFloat32() <= flt.ToFloat32();
    }

    constexpr bool operator==(const Float<M, E>& flt) const {
        return ToFloat32() == flt.ToFloat32();
    }

    constexpr bool operator!=(const Float<M, E>& flt) const {
        return ToFloat32() != flt.ToFloat32();
    }

private:
    static constexpr u32 MASK = static_cast<u32>(1 << (M + E + 1)) - 1;
    static constexpr u32 MANTISSA_MASK = (1 << M) - 1;
    static constexpr u32 EXPONENT_MASK = (1 << E) - 1;

    // Stored as a regular float, merely for convenience
    // TODO: Perform proper arithmetic on this!
    float value;

    friend class boost::serialization::access;
    template <class Archive>
    void serialize(Archive& ar, const unsigned int file_version) {
        ar & value;
    }
};

using f24 = Pica::Float<16, 7>;
using f20 = Pica::Float<12, 7>;
using f16 = Pica::Float<10, 5>;

} // namespace Pica
