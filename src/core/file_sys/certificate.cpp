// Copyright 2025-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <algorithm>
#include <cstring>
#include <cryptopp/oids.h>
#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "core/file_sys/certificate.h"
#include "core/file_sys/signature.h"
#include "core/hw/rsa/rsa.h"
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

namespace {
std::size_t GetPublicKeyAreaSize(u32 key_type) {
    switch (key_type) {
    case Certificate::PublicKeyType::RSA_4096:
        return 0x200 + 4 + 0x34;
    case Certificate::PublicKeyType::RSA_2048:
        return 0x100 + 4 + 0x34;
    case Certificate::PublicKeyType::ECC:
        return 0x3C + 0x3C;
    default:
        return 0;
    }
}

std::size_t GetPublicKeySize(u32 key_type) {
    switch (key_type) {
    case Certificate::PublicKeyType::RSA_4096:
        return 0x200 + 4;
    case Certificate::PublicKeyType::RSA_2048:
        return 0x100 + 4;
    case Certificate::PublicKeyType::ECC:
        return 0x3C;
    default:
        return 0;
    }
}

std::string StringFromField(const std::array<u8, 0x40>& field) {
    const auto end = std::find(field.begin(), field.end(), u8{0});
    return std::string(field.begin(), end);
}
} // namespace

std::size_t Certificate::Load(std::span<const u8> data, std::size_t offset) {
    Invalidate();
    if (offset > data.size() || data.size() - offset < sizeof(u32_be)) {
        return 0;
    }
    std::memcpy(&certificate_signature_type, data.data() + offset, sizeof(u32_be));
    const std::size_t signature_size = GetSignatureSize(certificate_signature_type);
    if (signature_size == 0) {
        Invalidate();
        return 0;
    }
    const std::size_t body_offset = Common::AlignUp(sizeof(u32_be) + signature_size, 0x40);
    if (data.size() - offset < body_offset + sizeof(Body)) {
        Invalidate();
        return 0;
    }
    std::memcpy(&certificate_body, data.data() + offset + body_offset, sizeof(Body));
    const std::size_t key_area_size = GetPublicKeyAreaSize(certificate_body.key_type);
    const std::size_t total_size = body_offset + sizeof(Body) + key_area_size;
    if (key_area_size == 0 || data.size() - offset < total_size) {
        Invalidate();
        return 0;
    }

    const u8* base = data.data() + offset;
    certificate_signature.assign(base + sizeof(u32_be), base + sizeof(u32_be) + signature_size);
    const u8* key = base + body_offset + sizeof(Body);
    certificate_public_key.assign(key, key + GetPublicKeySize(certificate_body.key_type));
    loaded_body.assign(base + body_offset, base + total_size);
    return total_size;
}

std::string Certificate::GetIssuerString() const {
    return StringFromField(certificate_body.issuer);
}

std::string Certificate::GetNameString() const {
    return StringFromField(certificate_body.name);
}

std::string Certificate::GetFullName() const {
    return GetIssuerString() + "-" + GetNameString();
}

std::vector<u8> Certificate::GetSignedData() const {
    return loaded_body.empty() ? SerializeBody() : loaded_body;
}

bool Certificate::VerifySignature(u32 signature_type, std::span<const u8> signature,
                                  std::span<const u8> data) const {
    const u32 key_type = certificate_body.key_type;
    if (key_type == PublicKeyType::RSA_4096 || key_type == PublicKeyType::RSA_2048) {
        const std::size_t modulus_size = key_type == PublicKeyType::RSA_4096 ? 0x200 : 0x100;
        const u32 expected_type = key_type == PublicKeyType::RSA_4096
                                      ? SignatureType::Rsa4096Sha256
                                      : SignatureType::Rsa2048Sha256;
        if (signature_type != expected_type || signature.size() != modulus_size ||
            certificate_public_key.size() != modulus_size + 4) {
            return false;
        }
        const HW::RSA::RsaSlot key(std::vector<u8>(certificate_public_key.begin() + modulus_size,
                                                   certificate_public_key.end()),
                                   std::vector<u8>(certificate_public_key.begin(),
                                                   certificate_public_key.begin() + modulus_size));
        return key.Verify(data, signature);
    }
    if (key_type == PublicKeyType::ECC) {
        if (signature_type != SignatureType::EcdsaSha256 || certificate_public_key.size() != 0x3C) {
            return false;
        }
        try {
            return HW::ECC::Verify(data, HW::ECC::CreateECCSignature(signature),
                                   HW::ECC::CreateECCPublicKey(certificate_public_key));
        } catch (const CryptoPP::Exception& e) {
            LOG_ERROR(HW, "ECC verification failed: {}", e.what());
            return false;
        }
    }
    return false;
}

bool Certificate::VerifyIssuedBy(const Certificate& issuer) const {
    return issuer.VerifySignature(certificate_signature_type, certificate_signature,
                                  GetSignedData());
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
