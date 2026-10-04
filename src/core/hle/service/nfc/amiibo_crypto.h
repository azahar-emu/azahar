// Copyright 2022 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <array>
#include <vector>

#include "core/hle/service/nfc/nfc_types.h"

namespace CryptoPP {
class SHA256;
template <class T>
class HMAC;
} // namespace CryptoPP

namespace HW::AES {
struct NfcSecret;
} // namespace HW::AES

namespace Service::NFC::AmiiboCrypto {
// Byte locations in Service::NFC::NTAG215File
constexpr std::size_t HMAC_DATA_START = 0x8;
constexpr std::size_t SETTINGS_START = 0x2c;
constexpr std::size_t WRITE_COUNTER_START = 0x29;
constexpr std::size_t HMAC_TAG_START = 0x1B4;
constexpr std::size_t UUID_START = 0x1D4;
constexpr std::size_t DYNAMIC_LOCK_START = 0x208;

using DrgbOutput = std::array<u8, 0x20>;

struct HashSeed {
    u16_be magic;
    std::array<u8, 0xE> padding;
    UniqueSerialNumber uid_1;
    u8 nintendo_id_1;
    UniqueSerialNumber uid_2;
    u8 nintendo_id_2;
    std::array<u8, 0x20> keygen_salt;
};
static_assert(sizeof(HashSeed) == 0x40, "HashSeed is an invalid size");

struct CryptoCtx {
    std::array<char, 480> buffer;
    bool used;
    std::size_t buffer_size;
    s16 counter;
};

struct DerivedKeys {
    std::array<u8, 0x10> aes_key;
    std::array<u8, 0x10> aes_iv;
    std::array<u8, 0x10> hmac_key;
};
static_assert(sizeof(DerivedKeys) == 0x30, "DerivedKeys is an invalid size");

/// Validates that the amiibo file is not corrupted
bool IsAmiiboValid(const EncryptedNTAG215File& ntag_file);

/// Validates that the amiibo file is not corrupted
bool IsAmiiboValid(const NTAG215File& ntag_file);

/// Converts from encrypted file format to encoded file format
NTAG215File NfcDataToEncodedData(const EncryptedNTAG215File& nfc_data);

/// Converts from encoded file format to encrypted file format
EncryptedNTAG215File EncodedDataToNfcData(const NTAG215File& encoded_data);

// Generates Seed needed for key derivation
HashSeed GetSeed(const NTAG215File& data);

// Middle step on the generation of derived keys
std::vector<u8> GenerateInternalKey(const HW::AES::NfcSecret& secret, const HashSeed& seed);

// Initializes mbedtls context
void CryptoInit(CryptoCtx& ctx, CryptoPP::HMAC<CryptoPP::SHA256>& hmac_ctx,
                std::span<const u8> hmac_key, std::span<const u8> seed);

// Feeds data to mbedtls context to generate the derived key
void CryptoStep(CryptoCtx& ctx, CryptoPP::HMAC<CryptoPP::SHA256>& hmac_ctx, DrgbOutput& output);

// Generates the derived key from amiibo data
DerivedKeys GenerateKey(const HW::AES::NfcSecret& secret, const NTAG215File& data);

// Encodes or decodes amiibo data
void Cipher(const DerivedKeys& keys, const NTAG215File& in_data, NTAG215File& out_data);

/// Decodes encripted amiibo data returns true if output is valid
bool DecodeAmiibo(const EncryptedNTAG215File& encrypted_tag_data, NTAG215File& tag_data);

/// Encodes plain amiibo data returns true if output is valid
bool EncodeAmiibo(const NTAG215File& tag_data, EncryptedNTAG215File& encrypted_tag_data);

} // namespace Service::NFC::AmiiboCrypto
