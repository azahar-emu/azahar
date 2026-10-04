// Copyright 2024 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/swap.h"
#include "core/hw/ecc.h"

namespace Loader {
enum class ResultStatus;
}

namespace FileSys {

class Certificate {
public:
#pragma pack(push, 1)
    struct Body {
        std::array<u8, 0x40> issuer;
        u32_be key_type;
        std::array<u8, 0x40> name;
        u32_be expiration;
    };

    struct PublicKeyType {
        enum : u32 {
            RSA_4096 = 0,
            RSA_2048 = 1,
            ECC = 2,
        };
    };

#pragma pack(pop)
    void BuildECC(Certificate& parent, const std::array<u8, 0x40> issuer,
                  const std::array<u8, 0x40> name, u32 expiration);

    void BuildECC(const std::array<u8, 0x40> issuer, const std::array<u8, 0x40> name,
                  u32 expiration, HW::ECC::PrivateKey private_key, HW::ECC::Signature signature);

    bool VerifyMyself(const HW::ECC::PublicKey& parent_public);
    bool Verify(std::span<const u8> data, HW::ECC::Signature signature);

    HW::ECC::Signature Sign(std::span<const u8> data);

    std::vector<u8> ECDHAgree(const HW::ECC::PublicKey& others_public_key);

    std::vector<u8> SerializeSignature() const;
    std::vector<u8> SerializeBody() const;
    std::vector<u8> Serialize() const;

    const std::array<u8, 0x40> GetIssuer() const {
        return certificate_body.issuer;
    }

    const std::array<u8, 0x40> GetName() const {
        return certificate_body.name;
    }

    void SetPrivateKeyECC(const HW::ECC::PrivateKey& private_key);
    const HW::ECC::PrivateKey& GetPrivateKeyECC();

    void SetPublicKeyECC(const HW::ECC::PublicKey& public_key);
    HW::ECC::PublicKey GetPublicKeyECC();

    void SetSignatureECC(const HW::ECC::Signature& signature);
    HW::ECC::Signature GetSignatureECC();

    const bool IsValid() const {
        return certificate_signature_type != 0u;
    }

    void Invalidate() {
        certificate_signature_type = 0u;
    }

private:
    Body certificate_body;
    u32_be certificate_signature_type;
    std::vector<u8> certificate_signature;
    std::vector<u8> certificate_public_key;

    HW::ECC::PrivateKey certificate_private_key_ecc;
};
} // namespace FileSys