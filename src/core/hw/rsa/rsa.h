// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <span>
#include <vector>
#include "common/common_types.h"

namespace HW::RSA {

class RsaSlot {
public:
    RsaSlot() = default;
    RsaSlot(std::vector<u8> exponent, std::vector<u8> modulus)
        : init_exponent(true), init_modulus(true), exponent(std::move(exponent)),
          modulus(std::move(modulus)) {}

    std::vector<u8> ModularExponentiation(std::span<const u8> message,
                                          int out_size_bytes = -1) const;

    std::vector<u8> Sign(std::span<const u8> message) const;

    bool Verify(std::span<const u8> message, std::span<const u8> signature) const;

    explicit operator bool() const {
        // TODO(B3N30): Maybe check if exponent and modulus are vailid
        return init_exponent && init_modulus;
    }

    void SetExponent(const std::vector<u8>& e) {
        exponent = e;
        init_exponent = true;
    }

    const std::vector<u8>& GetExponent() const {
        return exponent;
    }

    void SetModulus(const std::vector<u8>& m) {
        modulus = m;
        init_modulus = true;
    }

    const std::vector<u8>& GetModulus() const {
        return modulus;
    }

    void SetPrivateD(const std::vector<u8>& d) {
        private_d = d;
        init_private_d = true;
    }

    const std::vector<u8>& GetPrivateD() const {
        return private_d;
    }

private:
    bool init_exponent = false;
    bool init_modulus = false;
    bool init_private_d = false;
    std::vector<u8> exponent;
    std::vector<u8> modulus;
    std::vector<u8> private_d;
};

void InitSlots();

const RsaSlot& GetSlot(std::size_t slot_id);

const RsaSlot& GetTicketWrapSlot();

const RsaSlot& GetSecureInfoSlot();
const RsaSlot& GetLocalFriendCodeSeedSlot();

} // namespace HW::RSA
