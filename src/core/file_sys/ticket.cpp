// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/sha.h>
#include "common/alignment.h"
#include "core/file_sys/certificate.h"
#include "core/file_sys/otp.h"
#include "core/file_sys/signature.h"
#include "core/file_sys/ticket.h"
#include "core/hle/service/am/am.h"
#include "core/hw/aes/key.h"
#include "core/hw/ecc.h"
#include "core/hw/unique_data.h"
#include "core/loader/loader.h"

namespace FileSys {
Loader::ResultStatus Ticket::DoTitlekeyFixup() {

    if (ticket_body.console_id == 0u) {
        // Common ticket, no need to fix up
        return Loader::ResultStatus::Success;
    }

    auto& otp = HW::UniqueData::GetOTP();
    auto& ct_cert = HW::UniqueData::GetCTCert();
    if (!otp.Valid() || !ct_cert.IsValid()) {
        LOG_ERROR(HW_AES, "Tried to fixup a ticket without a valid OTP/CTCert");
        return Loader::ResultStatus::Error;
    }

    if (ticket_body.console_id != otp.GetDeviceID()) {
        // Ticket does not correspond to this console, cannot fixup
        LOG_ERROR(HW_AES, "Tried to fixup a ticket that does not correspond to this console");
        return Loader::ResultStatus::Error;
    }

    HW::ECC::PublicKey ticket_ecc_public = HW::ECC::CreateECCPublicKey(ticket_body.ecc_public_key);

    auto agreement = ct_cert.ECDHAgree(ticket_ecc_public);
    if (agreement.empty()) {
        LOG_ERROR(HW_AES, "Failed to perform ECDH agreement");
        return Loader::ResultStatus::Error;
    }

    CryptoPP::SHA1 hash;
    u8 digest[CryptoPP::SHA1::DIGESTSIZE];
    hash.CalculateDigest(digest, agreement.data(), agreement.size());

    std::vector<u8> key(0x10);
    memcpy(key.data(), digest, key.size());

    std::vector<u8> iv(0x10);
    *reinterpret_cast<u64_be*>(iv.data()) = ticket_body.ticket_id;

    CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption{key.data(), key.size(), iv.data()}.ProcessData(
        ticket_body.title_key.data(), ticket_body.title_key.data(), ticket_body.title_key.size());

    return Loader::ResultStatus::Success;
}

Loader::ResultStatus Ticket::Load(std::span<const u8> file_data, std::size_t offset) {
    std::size_t total_size = static_cast<std::size_t>(file_data.size() - offset);
    serialized_size = total_size;
    if (total_size < sizeof(u32))
        return Loader::ResultStatus::Error;

    std::memcpy(&signature_type, &file_data[offset], sizeof(u32));

    // Signature lengths are variable, and the body follows the signature
    u32 signature_size = GetSignatureSize(signature_type);
    if (signature_size == 0) {
        return Loader::ResultStatus::Error;
    }

    // The ticket body start position is rounded to the nearest 0x40 after the signature
    std::size_t body_start = Common::AlignUp(signature_size + sizeof(u32), 0x40);
    std::size_t body_end = body_start + sizeof(Body);

    if (total_size < body_end)
        return Loader::ResultStatus::Error;

    // Read signature + ticket body
    ticket_signature.resize(signature_size);
    std::memcpy(ticket_signature.data(), &file_data[offset + sizeof(u32)], signature_size);
    std::memcpy(&ticket_body, &file_data[offset + body_start], sizeof(Body));

    std::size_t content_index_start = body_end;
    if (total_size < content_index_start + (2 * sizeof(u32)))
        return Loader::ResultStatus::Error;

    // Read content index size from the second u32 into it. Actual format is undocumented.
    const size_t content_index_size =
        static_cast<size_t>(static_cast<u32>(*reinterpret_cast<const u32_be*>(
            &file_data[offset + content_index_start + 1 * sizeof(u32)])));
    const size_t content_index_end = content_index_start + content_index_size;

    if (total_size < content_index_end)
        return Loader::ResultStatus::Error;
    std::vector<u8> content_index_vec;
    content_index_vec.resize(content_index_size);
    std::memcpy(content_index_vec.data(), &file_data[offset + content_index_start],
                content_index_size);
    content_index.Load(this, content_index_vec);

    return Loader::ResultStatus::Success;
}

Loader::ResultStatus Ticket::Load(u64 title_id, u64 ticket_id) {
    FileUtil::IOFile f(Service::AM::GetTicketPath(title_id, ticket_id), "rb");
    if (!f.IsOpen()) {
        return Loader::ResultStatus::Error;
    }

    std::vector<u8> ticket_data(f.GetSize());
    f.ReadBytes(ticket_data.data(), ticket_data.size());

    return Load(ticket_data, 0);
}

std::vector<u8> Ticket::Serialize() const {
    std::vector<u8> ret(sizeof(u32_be));

    *reinterpret_cast<u32_be*>(ret.data()) = signature_type;

    ret.insert(ret.end(), ticket_signature.begin(), ticket_signature.end());

    u32 padding = 0x40 - (ret.size() % 0x40);
    ret.insert(ret.end(), padding, 0);

    std::span<const u8> body_span{reinterpret_cast<const u8*>(&ticket_body),
                                  reinterpret_cast<const u8*>(&ticket_body) + sizeof(ticket_body)};
    ret.insert(ret.end(), body_span.begin(), body_span.end());

    ret.insert(ret.end(), content_index.GetRaw().cbegin(), content_index.GetRaw().cend());

    return ret;
}

Loader::ResultStatus Ticket::Save(const std::string& file_path) const {
    FileUtil::IOFile file(file_path, "wb");
    if (!file.IsOpen())
        return Loader::ResultStatus::Error;

    auto serialized = Serialize();

    if (file.WriteBytes(serialized.data(), serialized.size()) != serialized.size()) {
        return Loader::ResultStatus::Error;
    }

    return Loader::ResultStatus::Success;
}

std::optional<std::array<u8, 16>> Ticket::GetTitleKey() const {
    HW::AES::InitKeys();
    std::array<u8, 16> ctr{};
    std::memcpy(ctr.data(), &ticket_body.title_id, sizeof(u64));
    HW::AES::SelectCommonKeyIndex(ticket_body.common_key_index);
    if (!HW::AES::IsNormalKeyAvailable(HW::AES::KeySlotID::TicketCommonKey)) {
        LOG_ERROR(Service_FS, "CommonKey {} missing", ticket_body.common_key_index);
        return {};
    }
    auto key = HW::AES::GetNormalKey(HW::AES::KeySlotID::TicketCommonKey);
    auto title_key = ticket_body.title_key;
    CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption{key.data(), key.size(), ctr.data()}.ProcessData(
        title_key.data(), title_key.data(), title_key.size());
    return title_key;
}

bool Ticket::IsPersonal() const {
    if (ticket_body.console_id == 0u) {
        // Common ticket
        return false;
    }

    auto& otp = HW::UniqueData::GetOTP();
    if (!otp.Valid()) {
        LOG_ERROR(HW_AES, "Invalid OTP");
        return false;
    }

    return ticket_body.console_id == otp.GetDeviceID();
}

void Ticket::ContentIndex::Initialize() {
    if (!parent || initialized) {
        return;
    }

    if (content_index.size() < sizeof(MainHeader)) {
        LOG_ERROR(Service_FS, "Ticket content index is too small");
        return;
    }
    MainHeader* main_header = reinterpret_cast<MainHeader*>(content_index.data());
    if (main_header->always1 != 1 || main_header->header_size != sizeof(MainHeader) ||
        main_header->context_index_size != content_index.size() ||
        main_header->index_header_size != sizeof(IndexHeader)) {
        u16 always1 = main_header->always1;
        u16 header_size = main_header->header_size;
        u32 context_index_size = main_header->context_index_size;
        u16 index_header_size = main_header->index_header_size;
        LOG_ERROR(Service_FS,
                  "Ticket content index has unexpected parameters title_id={}, ticket_id={}, "
                  "always1={}, header_size={}, "
                  "size={}, index_header_size={}",
                  parent->GetTitleID(), parent->GetTicketID(), always1, header_size,
                  context_index_size, index_header_size);
        return;
    }
    for (u32 i = 0; i < main_header->index_headers_count; i++) {
        IndexHeader* curr_header = reinterpret_cast<IndexHeader*>(
            content_index.data() + main_header->index_headers_offset +
            main_header->index_header_size * i);
        if (curr_header->type != 3 || curr_header->entry_size != sizeof(RightsField)) {
            u16 type = curr_header->type;
            LOG_WARNING(Service_FS,
                        "Found unsupported index header type, skiping... title_id={}, "
                        "ticket_id={}, type={}",
                        parent->GetTitleID(), parent->GetTicketID(), type);
            continue;
        }
        for (u32 j = 0; j < curr_header->entry_count; j++) {
            RightsField* field = reinterpret_cast<RightsField*>(
                content_index.data() + curr_header->data_offset + curr_header->entry_size * j);
            rights.push_back(*field);
        }
    }
    initialized = true;
}

bool Ticket::ContentIndex::HasRights(u16 content_index) {
    if (!initialized) {
        Initialize();
        if (!initialized)
            return false;
    }
    // From:
    // https://github.com/d0k3/GodMode9/blob/4424c37a89337ffb074c80807da1e80f358779b7/arm9/source/game/ticket.c#L198
    if (rights.empty()) {
        return content_index < 256; // when no fields, true if below 256
    }

    bool has_right = false;

    // it loops until one of these happens:
    // - we run out of bit fields
    // - at the first encounter of an index offset field that's bigger than index
    // - at the first encounter of a positive indicator of content rights
    for (u32 i = 0; i < rights.size(); i++) {
        u16 start_index = rights[i].start_index;
        if (content_index < start_index) {
            break;
        }

        u16 bit_pos = content_index - start_index;
        if (bit_pos >= 1024) {
            continue; // not in this field
        }

        if (rights[i].rights[bit_pos / 8] & (1 << (bit_pos % 8))) {
            has_right = true;
            break;
        }
    }

    return has_right;
}

} // namespace FileSys
