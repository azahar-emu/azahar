// Copyright 2024-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

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
    std::size_t Load(std::span<const u8> data, std::size_t offset = 0);

    void BuildECC(Certificate& parent, const std::array<u8, 0x40> issuer,
                  const std::array<u8, 0x40> name, u32 expiration);

    void BuildECC(const std::array<u8, 0x40> issuer, const std::array<u8, 0x40> name,
                  u32 expiration, HW::ECC::PrivateKey private_key, HW::ECC::Signature signature);

    bool VerifyMyself(const HW::ECC::PublicKey& parent_public);

    /// Verifies a signature made with the key of this certificate. Only the SHA-256 signature
    /// types are accepted.
    bool VerifySignature(u32 signature_type, std::span<const u8> signature,
                         std::span<const u8> data) const;

    /// Verifies the signature of this certificate with the key of its issuer's certificate.
    bool VerifyIssuedBy(const Certificate& issuer) const;

    std::vector<u8> GetSignedData() const;
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

    std::string GetIssuerString() const;
    std::string GetNameString() const;

    /// "<issuer>-<name>", the issuer string of the objects this certificate signs.
    std::string GetFullName() const;

    u32 GetKeyType() const {
        return certificate_body.key_type;
    }

    u32 GetSignatureType() const {
        return certificate_signature_type;
    }

    const std::vector<u8>& GetSignature() const {
        return certificate_signature;
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
    // Signed part (body, public key and its padding) as loaded, empty for built certificates.
    std::vector<u8> loaded_body;

    HW::ECC::PrivateKey certificate_private_key_ecc;
};
} // namespace FileSys