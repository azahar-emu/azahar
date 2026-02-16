// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <optional>
#include <sstream>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/file_sys/certificate.h"
#include "core/file_sys/otp.h"
#include "core/hle/service/fs/archive.h"
#include "core/hw/aes/arithmetic128.h"
#include "core/hw/aes/key.h"
#include "core/hw/default_keys.h"
#include "core/hw/rsa/rsa.h"
#include "core/loader/loader.h"

namespace HW::AES {

namespace {

// The generator constant was calculated using the 0x39 KeyX and KeyY retrieved from a 3DS and the
// normal key dumped from a Wii U solving the equation:
// NormalKey = (((KeyX ROL 2) XOR KeyY) + constant) ROL 87
// On a real 3DS the generation for the normal key is hardware based, and thus the constant can't
// get dumped. Generated normal keys are also not accessible on a 3DS. The used formula for
// calculating the constant is a software implementation of what the hardware generator does.
AESKey generator_constant;

AESKey HexToKey(const std::string& hex) {
    if (hex.size() < 32) {
        throw std::invalid_argument("hex string is too short");
    }

    AESKey key;
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<u8>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
    }

    return key;
}

std::vector<u8> HexToVector(const std::string& hex) {
    std::vector<u8> vector(hex.size() / 2);
    for (std::size_t i = 0; i < vector.size(); ++i) {
        vector[i] = static_cast<u8>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
    }

    return vector;
}

std::optional<std::size_t> ParseCommonKeyName(const std::string& full_name) {
    std::size_t index;
    int end;
    if (std::sscanf(full_name.c_str(), "common%zd%n", &index, &end) == 1 &&
        end == static_cast<int>(full_name.size())) {
        return index;
    } else {
        return std::nullopt;
    }
}

std::optional<std::pair<std::size_t, std::string>> ParseNfcSecretName(
    const std::string& full_name) {
    std::size_t index;
    int end;
    if (std::sscanf(full_name.c_str(), "nfcSecret%zd%n", &index, &end) == 1) {
        return std::make_pair(index, full_name.substr(end));
    } else {
        return std::nullopt;
    }
}

std::optional<std::pair<std::size_t, char>> ParseKeySlotName(const std::string& full_name) {
    std::size_t slot;
    char type;
    int end;
    if (std::sscanf(full_name.c_str(), "slot0x%zXKey%c%n", &slot, &type, &end) == 2 &&
        end == static_cast<int>(full_name.size())) {
        return std::make_pair(slot, type);
    } else {
        return std::nullopt;
    }
}

struct KeySlot {
    std::optional<AESKey> x;
    std::optional<AESKey> y;
    std::optional<AESKey> normal;

    void SetKeyX(std::optional<AESKey> key) {
        x = key;
        GenerateNormalKey();
    }

    void SetKeyY(std::optional<AESKey> key) {
        y = key;
        GenerateNormalKey();
    }

    void SetNormalKey(std::optional<AESKey> key) {
        normal = key;
    }

    void GenerateNormalKey() {
        if (x && y) {
            normal = Lrot128(Add128(Xor128(Lrot128(*x, 2), *y), generator_constant), 87);
        } else {
            normal.reset();
        }
    }

    void Clear() {
        x.reset();
        y.reset();
        normal.reset();
    }
};

std::array<KeySlot, KeySlotID::MaxKeySlotID> key_slots;
std::array<std::optional<AESKey>, MaxCommonKeySlot> common_key_y_slots;
std::array<std::optional<AESKey>, NumDlpNfcKeyYs> dlp_nfc_key_y_slots;
std::array<NfcSecret, NumNfcSecrets> nfc_secrets;
AESIV nfc_iv;

AESKey otp_key;
AESIV otp_iv;

// gets xor'd with the mac address to produce the final iv
AESIV dlp_checksum_mod_iv;

KeySlot movable_key;
KeySlot movable_cmac;

struct KeyDesc {
    char key_type;
    std::size_t slot_id;
    // This key is identical to the key with the same key_type and slot_id -1
    bool same_as_before;
};

void LoadPresetKeys() {
    auto s = GetKeysStream();

    std::string mode = "";

    while (!s.eof()) {
        std::string line;
        std::getline(s, line);

        // Ignore empty or commented lines.
        if (line.empty() || line.starts_with("#")) {
            continue;
        }

        if (line.starts_with(":")) {
            mode = line.substr(1);
            continue;
        }

        if (mode != "AES") {
            continue;
        }

        const auto parts = Common::SplitString(line, '=');
        if (parts.size() != 2) {
            LOG_ERROR(HW_AES, "Failed to parse {}", line);
            continue;
        }

        const std::string& name = parts[0];

        const auto nfc_secret = ParseNfcSecretName(name);
        if (nfc_secret) {
            auto value = HexToVector(parts[1]);
            if (nfc_secret->first >= nfc_secrets.size()) {
                LOG_ERROR(HW_AES, "Invalid NFC secret index {}", nfc_secret->first);
            } else if (nfc_secret->second == "Phrase") {
                nfc_secrets[nfc_secret->first].phrase = value;
            } else if (nfc_secret->second == "Seed") {
                nfc_secrets[nfc_secret->first].seed = value;
            } else if (nfc_secret->second == "HmacKey") {
                nfc_secrets[nfc_secret->first].hmac_key = value;
            } else {
                LOG_ERROR(HW_AES, "Invalid NFC secret '{}'", name);
            }
            continue;
        }

        AESKey key;
        try {
            key = HexToKey(parts[1]);
        } catch (const std::logic_error& e) {
            LOG_ERROR(HW_AES, "Invalid key {}: {}", parts[1], e.what());
            continue;
        }

        const auto common_key = ParseCommonKeyName(name);
        if (common_key) {
            if (common_key >= common_key_y_slots.size()) {
                LOG_ERROR(HW_AES, "Invalid common key index {}", common_key.value());
            } else {
                common_key_y_slots[common_key.value()] = key;
            }
            continue;
        }

        if (name == "generatorConstant") {
            generator_constant = key;
            continue;
        }

        if (name == "otpKey") {
            otp_key = key;
            continue;
        }

        if (name == "otpIV") {
            otp_iv = key;
            continue;
        }

        if (name == "movableKeyY") {
            movable_key.SetKeyY(key);
            continue;
        }

        if (name == "movableCmacY") {
            movable_cmac.SetKeyY(key);
            continue;
        }

        if (name == "dlpKeyY") {
            dlp_nfc_key_y_slots[DlpNfcKeyY::Dlp] = key;
            continue;
        }

        if (name == "nfcKeyY") {
            dlp_nfc_key_y_slots[DlpNfcKeyY::Nfc] = key;
            continue;
        }

        if (name == "nfcIv") {
            nfc_iv = key;
            continue;
        }

        if (name == "dlpChecksumModIv") {
            dlp_checksum_mod_iv = key;
        }

        const auto key_slot = ParseKeySlotName(name);
        if (!key_slot) {
            LOG_ERROR(HW_AES, "Invalid key name '{}'", name);
            continue;
        }

        if (key_slot->first >= MaxKeySlotID) {
            LOG_ERROR(HW_AES, "Out of range key slot ID {:#X}", key_slot->first);
            continue;
        }

        switch (key_slot->second) {
        case 'X':
            key_slots.at(key_slot->first).SetKeyX(key);
            break;
        case 'Y':
            key_slots.at(key_slot->first).SetKeyY(key);
            break;
        case 'N':
            key_slots.at(key_slot->first).SetNormalKey(key);
            break;
        default:
            LOG_ERROR(HW_AES, "Invalid key type '{}'", key_slot->second);
            break;
        }
    }
}

} // namespace

std::istringstream GetKeysStream() {
    const std::string filepath = FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + KEYS_FILE;
    FileUtil::CreateFullPath(filepath); // Create path if not already created

    boost::iostreams::stream<boost::iostreams::file_descriptor_source> file;
    FileUtil::OpenFStream<std::ios_base::in>(file, filepath);
    std::istringstream ret;
    if (file.is_open()) {
        return std::istringstream(std::string(std::istreambuf_iterator<char>(file), {}));
    } else {
        // The key data is encrypted in the source to prevent easy access to it for unintended
        // purposes.
        std::vector<u8> kiv(16);
        std::string s(default_keys_enc_size, ' ');
        CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption(kiv.data(), kiv.size(), kiv.data())
            .ProcessData(reinterpret_cast<u8*>(s.data()), default_keys_enc, s.size());
        return std::istringstream(s);
    }
}

void InitKeys(bool force) {
    static bool initialized = false;
    if (initialized && !force) {
        return;
    }
    initialized = true;
    LoadPresetKeys();
    movable_key.SetKeyX(key_slots[0x35].x);
    movable_cmac.SetKeyX(key_slots[0x35].x);

    HW::RSA::InitSlots();
    HW::ECC::InitSlots();
}

void SetKeyX(std::size_t slot_id, const AESKey& key) {
    key_slots.at(slot_id).SetKeyX(key);
}

void SetKeyY(std::size_t slot_id, const AESKey& key) {
    key_slots.at(slot_id).SetKeyY(key);
}

void SetNormalKey(std::size_t slot_id, const AESKey& key) {
    key_slots.at(slot_id).SetNormalKey(key);
}

bool IsKeyXAvailable(std::size_t slot_id) {
    return key_slots.at(slot_id).x.has_value();
}

bool IsNormalKeyAvailable(std::size_t slot_id) {
    return key_slots.at(slot_id).normal.has_value();
}

AESKey GetNormalKey(std::size_t slot_id) {
    return key_slots.at(slot_id).normal.value_or(AESKey{});
}

void SelectCommonKeyIndex(u8 index) {
    key_slots[KeySlotID::TicketCommonKey].SetKeyY(common_key_y_slots.at(index));
}

void SelectDlpNfcKeyYIndex(u8 index) {
    key_slots[KeySlotID::DLPNFCDataKey].SetKeyY(dlp_nfc_key_y_slots.at(index));
}

bool NfcSecretsAvailable() {
    auto missing_secret =
        std::find_if(nfc_secrets.begin(), nfc_secrets.end(), [](auto& nfc_secret) {
            return nfc_secret.phrase.empty() || nfc_secret.seed.empty() ||
                   nfc_secret.hmac_key.empty();
        });
    SelectDlpNfcKeyYIndex(DlpNfcKeyY::Nfc);
    return IsNormalKeyAvailable(KeySlotID::DLPNFCDataKey) && missing_secret == nfc_secrets.end();
}

const NfcSecret& GetNfcSecret(NfcSecretId secret_id) {
    return nfc_secrets[secret_id];
}

const AESIV& GetNfcIv() {
    return nfc_iv;
}

std::pair<AESKey, AESIV> GetOTPKeyIV() {
    return {otp_key, otp_iv};
}

const AESKey& GetMovableKey(bool cmac_key) {
    return cmac_key ? movable_cmac.normal.value() : movable_key.normal.value();
}

const AESIV& GetDlpChecksumModIv() {
    return dlp_checksum_mod_iv;
}

} // namespace HW::AES
