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
class Ticket {
public:
#pragma pack(push, 1)
    struct Body {
        std::array<u8, 0x40> issuer;
        std::array<u8, 0x3C> ecc_public_key;
        u8 version;
        u8 ca_crl_version;
        u8 signer_crl_version;
        std::array<u8, 0x10> title_key;
        INSERT_PADDING_BYTES(1);
        u64_be ticket_id;
        u32_be console_id;
        u64_be title_id;
        INSERT_PADDING_BYTES(2);
        u16_be ticket_title_version;
        INSERT_PADDING_BYTES(8);
        u8 license_type;
        u8 common_key_index;
        INSERT_PADDING_BYTES(0x2A);
        u32_be eshop_account_id;
        INSERT_PADDING_BYTES(1);
        u8 audit;
        INSERT_PADDING_BYTES(0x42);
        std::array<u8, 0x40> limits;
    };
    static_assert(sizeof(Body) == 0x164, "Ticket body structure size is wrong");
#pragma pack(pop)

    Loader::ResultStatus DoTitlekeyFixup();
    Loader::ResultStatus Load(std::span<const u8> file_data, std::size_t offset = 0);
    Loader::ResultStatus Load(u64 title_id, u64 ticket_id);
    std::vector<u8> Serialize() const;
    Loader::ResultStatus Save(const std::string& file_path) const;

    std::optional<std::array<u8, 16>> GetTitleKey() const;
    u64 GetTitleID() const {
        return ticket_body.title_id;
    }
    u64 GetTicketID() const {
        return ticket_body.ticket_id;
    }
    u16 GetVersion() const {
        return ticket_body.ticket_title_version;
    }
    size_t GetSerializedSize() {
        return serialized_size;
    }

    bool IsPersonal();

private:
    Body ticket_body;
    u32_be signature_type;
    std::vector<u8> ticket_signature;
    std::vector<u8> content_index;

    size_t serialized_size = 0;
};
} // namespace FileSys
