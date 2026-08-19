// Copyright 2020-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <sstream>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include <cryptopp/hex.h>
#include <cryptopp/integer.h>
#include <cryptopp/nbtheory.h>
#include <cryptopp/sha.h>
#include <fmt/format.h>
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/hw/aes/key.h"
#include "core/hw/rsa/rsa.h"
#include "cryptopp/osrng.h"
#include "cryptopp/rsa.h"

namespace HW::RSA {

constexpr std::size_t SlotSize = 4;
std::array<RsaSlot, SlotSize> rsa_slots;

RsaSlot ticket_wrap_slot;
RsaSlot secure_info_slot;
RsaSlot local_friend_code_seed_slot;

std::vector<u8> RsaSlot::ModularExponentiation(std::span<const u8> message,
                                               int out_size_bytes) const {
    CryptoPP::Integer sig =
        CryptoPP::ModularExponentiation(CryptoPP::Integer(message.data(), message.size()),
                                        CryptoPP::Integer(exponent.data(), exponent.size()),
                                        CryptoPP::Integer(modulus.data(), modulus.size()));

    std::vector<u8> result((out_size_bytes == -1) ? sig.MinEncodedSize() : out_size_bytes);
    sig.Encode(result.data(), result.size());
    return result;
}

std::vector<u8> RsaSlot::Sign(std::span<const u8> message) const {
    if (private_d.empty()) {
        LOG_ERROR(HW, "Cannot sign, RSA slot does not have a private key");
        return {};
    }

    CryptoPP::RSASS<CryptoPP::PKCS1v15, CryptoPP::SHA256>::PrivateKey private_key;
    private_key.Initialize(CryptoPP::Integer(modulus.data(), modulus.size()),
                           CryptoPP::Integer(exponent.data(), exponent.size()),
                           CryptoPP::Integer(private_d.data(), private_d.size()));

    CryptoPP::RSASS<CryptoPP::PKCS1v15, CryptoPP::SHA256>::Signer signer(private_key);
    CryptoPP::AutoSeededRandomPool prng;
    std::vector<u8> ret(signer.SignatureLength());

    signer.SignMessage(prng, message.data(), message.size(), ret.data());

    return ret;
}

bool RsaSlot::Verify(std::span<const u8> message, std::span<const u8> signature) const {
    CryptoPP::RSASS<CryptoPP::PKCS1v15, CryptoPP::SHA256>::PublicKey public_key;
    public_key.Initialize(CryptoPP::Integer(modulus.data(), modulus.size()),
                          CryptoPP::Integer(exponent.data(), exponent.size()));

    CryptoPP::RSASS<CryptoPP::PKCS1v15, CryptoPP::SHA256>::Verifier verifier(public_key);

    return verifier.VerifyMessage(message.data(), message.size(), signature.data(),
                                  signature.size());
}

std::vector<u8> HexToVector(const std::string& hex) {
    std::vector<u8> vector(hex.size() / 2);
    for (std::size_t i = 0; i < vector.size(); ++i) {
        vector[i] = static_cast<u8>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
    }

    return vector;
}

std::optional<std::pair<std::size_t, char>> ParseKeySlotName(const std::string& full_name) {
    std::size_t slot;
    char type;
    int end;
    if (std::sscanf(full_name.c_str(), "slot0x%zX%c%n", &slot, &type, &end) == 2 &&
        end == static_cast<int>(full_name.size())) {
        return std::make_pair(slot, type);
    } else {
        return std::nullopt;
    }
}

void InitSlots() {
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

    auto s = HW::AES::GetKeysStream();

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

        if (mode != "RSA") {
            continue;
        }

        const auto parts = Common::SplitString(line, '=');
        if (parts.size() != 2) {
            LOG_ERROR(HW_RSA, "Failed to parse {}", line);
            continue;
        }

        const std::string& name = parts[0];

        std::vector<u8> key;
        try {
            key = HexToVector(parts[1]);
        } catch (const std::logic_error& e) {
            LOG_ERROR(HW_RSA, "Invalid key {}: {}", parts[1], e.what());
            continue;
        }

        if (name == "ticketWrapExp") {
            ticket_wrap_slot.SetExponent(key);
            continue;
        }

        if (name == "ticketWrapMod") {
            ticket_wrap_slot.SetModulus(key);
            continue;
        }

        if (name == "secureInfoExp") {
            secure_info_slot.SetExponent(key);
            continue;
        }

        if (name == "secureInfoMod") {
            secure_info_slot.SetModulus(key);
            continue;
        }

        if (name == "lfcsExp") {
            local_friend_code_seed_slot.SetExponent(key);
            continue;
        }

        if (name == "lfcsMod") {
            local_friend_code_seed_slot.SetModulus(key);
            continue;
        }

        const auto key_slot = ParseKeySlotName(name);
        if (!key_slot) {
            LOG_ERROR(HW_RSA, "Invalid key name '{}'", name);
            continue;
        }

        if (key_slot->first >= SlotSize) {
            LOG_ERROR(HW_RSA, "Out of range key slot ID {:#X}", key_slot->first);
            continue;
        }

        switch (key_slot->second) {
        case 'X':
            rsa_slots.at(key_slot->first).SetExponent(key);
            break;
        case 'M':
            rsa_slots.at(key_slot->first).SetModulus(key);
            break;
        case 'P':
            rsa_slots.at(key_slot->first).SetPrivateD(key);
            break;
        default:
            LOG_ERROR(HW_RSA, "Invalid key type '{}'", key_slot->second);
            break;
        }
    }
}

static RsaSlot empty_slot;
const RsaSlot& GetSlot(std::size_t slot_id) {
    if (slot_id >= rsa_slots.size())
        return empty_slot;
    return rsa_slots[slot_id];
}

const RsaSlot& GetTicketWrapSlot() {
    return ticket_wrap_slot;
}

const RsaSlot& GetSecureInfoSlot() {
    return secure_info_slot;
}

const RsaSlot& GetLocalFriendCodeSeedSlot() {
    return local_friend_code_seed_slot;
}

} // namespace HW::RSA
