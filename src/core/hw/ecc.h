// Copyright 2024 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <vector>
#include <cryptopp/oids.h>
#include "common/common_types.h"
#include "cryptopp/eccrypto.h"

namespace HW::ECC {
// All the supported 3DS ECC operations use the sect233r1 curve,
// so we default to sizes for this curve only.
static constexpr size_t INT_SIZE = 0x1E;

using CryptoPPInteger = CryptoPP::Integer;
using CryptoPPPoint = CryptoPP::EC2N::Point;

using CryptoPPECCPrivateKey = CryptoPP::ECDSA<CryptoPP::EC2N, CryptoPP::SHA256>::PrivateKey;
using CryptoPPECCPublicKey = CryptoPP::ECDSA<CryptoPP::EC2N, CryptoPP::SHA256>::PublicKey;

struct PrivateKey {
    std::array<u8, INT_SIZE> x;

    CryptoPPInteger AsCryptoPPInteger() const;

    CryptoPPECCPrivateKey AsCryptoPPPrivateKey() const;
};

union PublicKey {
    struct {
        std::array<u8, INT_SIZE> x;
        std::array<u8, INT_SIZE> y;
    };
    std::array<u8, INT_SIZE * 2> xy;

    CryptoPPPoint AsCryptoPPPoint() const;

    CryptoPPECCPublicKey AsCryptoPPPublicKey() const;
};

union Signature {
    struct {
        std::array<u8, INT_SIZE> r;
        std::array<u8, INT_SIZE> s;
    };
    std::array<u8, INT_SIZE * 2> rs;
};

void InitSlots();

PrivateKey CreateECCPrivateKey(std::span<const u8> private_key_x, bool fix_up = false);
PublicKey CreateECCPublicKey(std::span<const u8> public_key_xy);
Signature CreateECCSignature(std::span<const u8> signature_rs);

PublicKey MakePublicKey(const CryptoPPECCPrivateKey& private_key_cpp);
PublicKey MakePublicKey(const PrivateKey& private_key);
std::pair<PrivateKey, PublicKey> GenerateKeyPair();

Signature Sign(std::span<const u8> data, PrivateKey private_key);
bool Verify(std::span<const u8> data, Signature signature, PublicKey public_key);

std::vector<u8> Agree(PrivateKey private_key, PublicKey others_public_key);

const PublicKey& GetRootPublicKey();
} // namespace HW::ECC