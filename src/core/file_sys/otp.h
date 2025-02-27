// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"

namespace Loader {
enum class ResultStatus;
}

namespace FileSys {
class OTP {
public:
    static constexpr u32 otp_magic = 0xDEADB00F; // Very dead, such boof
#pragma pack(push, 1)
    struct Body {
        u32 magic;
        u32 device_id;
        std::array<u8, 0x10> fallback_movable_keyY;
        u8 otp_version;
        u8 system_type;
        std::array<u8, 0x6> manufacture_date;
        struct {
            u32 expiry_date;
            std::array<u8, 0x20> priv_key;
            std::array<u8, 0x3C> signature;
        } ctcert;
        INSERT_PADDING_BYTES(0x10);
        std::array<u8, 0x50> random_key_seed_bytes;
    };
    struct OTPBin {
        Body body;
        std::array<u8, 0x20> hash;
    };
#pragma pack(pop)
    static_assert(sizeof(OTPBin) == 0x100, "Invalid OTP size");

    Loader::ResultStatus Load(const std::string& file_path, std::span<const u8> key,
                              std::span<const u8> iv);

    u8 GetSystemType() const {
        return otp.body.system_type;
    }

    bool IsDev() const {
        return GetSystemType() != 0;
    }

    u32 GetDeviceID() const {
        return otp.body.device_id;
    }

    u32 GetCTCertExpiration() const {
        if (otp.body.otp_version < 5) {
            return *reinterpret_cast<const u32_be*>(&otp.body.ctcert.expiry_date);
        } else {
            return otp.body.ctcert.expiry_date;
        }
    }

    const std::array<u8, 0x20> GetCTCertPrivateKey() const {
        return otp.body.ctcert.priv_key;
    }

    const std::array<u8, 0x3C> GetCTCertSignature() const {
        return otp.body.ctcert.signature;
    }

    bool Valid() const {
        return otp.body.magic == otp_magic;
    }

    void Invalidate() {
        otp.body.magic = 0;
    }

private:
    OTPBin otp{};
};
} // namespace FileSys
