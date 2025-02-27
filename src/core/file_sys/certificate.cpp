// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cryptopp/oids.h>
#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/file_sys/certificate.h"
#include "core/file_sys/signature.h"
#include "cryptopp/eccrypto.h"
#include "cryptopp/osrng.h"

namespace FileSys {
void Certificate::BuildECC(Certificate& parent, const std::array<u8, 0x40> issuer,
                           const std::array<u8, 0x40> name, u32 expiration) {

    certificate_signature_type = SignatureType::EcdsaSha256;
    certificate_body.key_type = PublicKeyType::ECC;

    certificate_body.issuer = issuer;
    certificate_body.name = name;
    certificate_body.expiration = expiration;

    auto [private_key, public_key] = HW::ECC::GenerateKeyPair();

    SetPrivateKeyECC(private_key);
    SetPublicKeyECC(public_key);
    SetSignatureECC(parent.Sign(SerializeBody()));

    if (!VerifyMyself(parent.GetPublicKeyECC())) {
        LOG_ERROR(HW, "Failed to verify newly generated certificate");
    }
}

void Certificate::BuildECC(const std::array<u8, 0x40> issuer, const std::array<u8, 0x40> name,
                           u32 expiration, HW::ECC::PrivateKey private_key,
                           HW::ECC::Signature signature) {

    certificate_signature_type = SignatureType::EcdsaSha256;
    certificate_body.key_type = PublicKeyType::ECC;

    certificate_body.issuer = issuer;
    certificate_body.name = name;
    certificate_body.expiration = expiration;

    SetPrivateKeyECC(private_key);
    SetPublicKeyECC(HW::ECC::MakePublicKey(private_key));
    SetSignatureECC(signature);
}

bool Certificate::VerifyMyself(const HW::ECC::PublicKey& parent_public) {
    if (certificate_signature_type == SignatureType::EcdsaSha256) {
        return HW::ECC::Verify(SerializeBody(), HW::ECC::CreateECCSignature(certificate_signature),
                               parent_public);
    } else {
        UNIMPLEMENTED();
        return false;
    }
}

bool Certificate::Verify(std::span<const u8> data, HW::ECC::Signature signature) {

    if (certificate_body.key_type == PublicKeyType::ECC) {
        return HW::ECC::Verify(data, signature, GetPublicKeyECC());
    } else {
        UNIMPLEMENTED();
    }
    return false;
}

HW::ECC::Signature Certificate::Sign(std::span<const u8> data) {
    if (certificate_body.key_type == PublicKeyType::ECC) {
        return HW::ECC::Sign(data, certificate_private_key_ecc);
    } else {
        UNIMPLEMENTED();
    }
    return HW::ECC::Signature();
}

std::vector<u8> Certificate::ECDHAgree(const HW::ECC::PublicKey& others_public_key) {
    if (certificate_body.key_type != PublicKeyType::ECC) {
        LOG_ERROR(HW, "Tried to agree with a non ECC certificate");
        return {};
    }
    return HW::ECC::Agree(certificate_private_key_ecc, others_public_key);
}

std::vector<u8> Certificate::SerializeSignature() const {
    std::vector<u8> ret;
    ret.resize(Common::AlignUp(certificate_signature.size() + sizeof(u32) + 1, 0x40));
    memcpy(ret.data(), &certificate_signature_type, sizeof(u32_be));
    memcpy(ret.data() + sizeof(u32_be), certificate_signature.data(), certificate_signature.size());
    return ret;
}

std::vector<u8> Certificate::SerializeBody() const {
    std::vector<u8> ret;
    ret.resize(Common::AlignUp(sizeof(Body) + certificate_public_key.size() + 1, 0x40));
    memcpy(ret.data(), &certificate_body, sizeof(certificate_body));
    memcpy(ret.data() + sizeof(certificate_body), certificate_public_key.data(),
           certificate_public_key.size());
    return ret;
}

std::vector<u8> Certificate::Serialize() const {
    if (!IsValid()) {
        return {};
    }

    auto signature = SerializeSignature();
    auto body = SerializeBody();
    signature.insert(signature.end(), body.begin(), body.end());

    return signature;
}

void Certificate::SetPrivateKeyECC(const HW::ECC::PrivateKey& private_key) {
    if (certificate_body.key_type != PublicKeyType::ECC) {
        LOG_ERROR(HW, "Certificate is not ECC");
        return;
    }
    certificate_private_key_ecc = private_key;
}

const HW::ECC::PrivateKey& Certificate::GetPrivateKeyECC() {
    if (certificate_body.key_type != PublicKeyType::ECC) {
        LOG_ERROR(HW, "Certificate is not ECC");
    }
    return certificate_private_key_ecc;
}

void Certificate::SetPublicKeyECC(const HW::ECC::PublicKey& public_key) {
    if (certificate_body.key_type != PublicKeyType::ECC) {
        LOG_ERROR(HW, "Certificate is not ECC");
        return;
    }
    certificate_public_key.resize(public_key.xy.size());
    memcpy(certificate_public_key.data(), public_key.xy.data(), public_key.xy.size());
}

HW::ECC::PublicKey Certificate::GetPublicKeyECC() {
    if (certificate_body.key_type != PublicKeyType::ECC) {
        LOG_ERROR(HW, "Certificate is not ECC");
        return HW::ECC::PublicKey();
    }
    return HW::ECC::CreateECCPublicKey(certificate_public_key);
}
void Certificate::SetSignatureECC(const HW::ECC::Signature& signature) {
    if (certificate_signature_type != SignatureType::EcdsaSha256) {
        LOG_ERROR(HW, "Signature is not ECC");
        return;
    }
    certificate_signature.resize(signature.rs.size());
    memcpy(certificate_signature.data(), signature.rs.data(), signature.rs.size());
}

HW::ECC::Signature Certificate::GetSignatureECC() {
    if (certificate_signature_type != SignatureType::EcdsaSha256) {
        LOG_ERROR(HW, "Signature is not ECC");
        return HW::ECC::Signature();
    }
    return HW::ECC::CreateECCSignature(certificate_signature);
}
} // namespace FileSys
