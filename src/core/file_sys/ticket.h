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
    struct LimitEntry {
        u32_be type; // 4 -> Play times?
        u32_be value;
    };
    static_assert(sizeof(LimitEntry) == 0x8, "LimitEntry structure size is wrong");

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
        std::array<LimitEntry, 0x8> limits;
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

    bool IsPersonal() const;

    bool HasRights(u16 index) {
        return content_index.HasRights(index);
    }

    class ContentIndex {
    public:
        struct MainHeader {
            u16_be always1;
            u16_be header_size;
            u32_be context_index_size;
            u32_be index_headers_offset;
            u16_be index_headers_count;
            u16_be index_header_size;
            u32_be padding;
        };

        struct IndexHeader {
            u32_be data_offset;
            u32_be entry_count;
            u32_be entry_size;
            u32_be total_size;
            u16_be type;
            u16_be padding;
        };

        struct RightsField {
            u16_be unknown;
            u16_be start_index;
            std::array<u8, 0x80> rights;
        };

        ContentIndex() {}

        void Load(Ticket* p, const std::vector<u8>& data) {
            parent = p;
            content_index = data;
        }

        const std::vector<u8>& GetRaw() const {
            return content_index;
        }

        bool HasRights(u16 content_index);

    private:
        void Initialize();

        bool initialized = false;
        std::vector<u8> content_index;
        std::vector<RightsField> rights;
        Ticket* parent = nullptr;
    };

private:
    Body ticket_body;
    u32_be signature_type;
    std::vector<u8> ticket_signature;
    ContentIndex content_index;

    size_t serialized_size = 0;
};
} // namespace FileSys
