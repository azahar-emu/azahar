// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <fmt/format.h>
#include <openssl/rand.h>
#include "common/alignment.h"
#include "common/archives.h"
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/hacks/hack_manager.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "common/zstd_compression.h"
#include "core/core.h"
#include "core/file_sys/certificate.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/ncch_container.h"
#include "core/file_sys/otp.h"
#include "core/file_sys/seed_db.h"
#include "core/file_sys/title_metadata.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/client_session.h"
#include "core/hle/kernel/errors.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/server_session.h"
#include "core/hle/kernel/session.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/am/am_app.h"
#include "core/hle/service/am/am_net.h"
#include "core/hle/service/am/am_sys.h"
#include "core/hle/service/am/am_u.h"
#include "core/hle/service/fs/archive.h"
#include "core/hle/service/fs/fs_user.h"
#include "core/hw/aes/key.h"
#include "core/hw/rsa/rsa.h"
#include "core/hw/unique_data.h"
#include "core/loader/loader.h"
#include "core/loader/smdh.h"
#include "core/nus_download.h"

SERIALIZE_EXPORT_IMPL(Service::AM::Module)
SERVICE_CONSTRUCT_IMPL(Service::AM::Module)

namespace Service::AM {

constexpr u16 PLATFORM_CTR = 0x0004;
constexpr u16 CATEGORY_SYSTEM = 0x0010;
constexpr u16 CATEGORY_DLP = 0x0001;
constexpr u8 VARIATION_SYSTEM = 0x02;
constexpr u32 TID_HIGH_UPDATE = 0x0004000E;
constexpr u32 TID_HIGH_DLC = 0x0004008C;

constexpr u8 OWNERSHIP_DOWNLOADED = 0x01;
constexpr u8 OWNERSHIP_OWNED = 0x02;

struct ContentInfo {
    u16_le index;
    u16_le type;
    u32_le content_id;
    u64_le size;
    u8 ownership;
    INSERT_PADDING_BYTES(0x7);
};

static_assert(sizeof(ContentInfo) == 0x18, "Content info structure size is wrong");

struct TicketInfo {
    u64_le title_id;
    u64_le ticket_id;
    u16_le version;
    u16_le unused;
    u32_le size;
};

static_assert(sizeof(TicketInfo) == 0x18, "Ticket info structure size is wrong");

class CIAFile::DecryptionState {
public:
    std::vector<CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption> content;
};

NCCHCryptoFile::NCCHCryptoFile(const std::string& out_file, bool encrypted_content) {
    if (encrypted_content) {
        // A console unique crypto file is used to store the decrypted NCCH file. This is done
        // to prevent Azahar being used as a tool to download easy shareable decrypted contents
        // from the eshop.
        file = HW::UniqueData::OpenUniqueCryptoFile(out_file, "wb",
                                                    HW::UniqueData::UniqueCryptoFileID::NCCH);
    } else {
        file = std::make_unique<FileUtil::IOFile>(out_file, "wb");
    }

    if (Settings::values.compress_cia_installs) {
        std::array<u8, 4> magic = {'N', 'C', 'C', 'H'};
        file = std::make_unique<FileUtil::Z3DSWriteIOFile>(
            std::move(file), magic, FileUtil::Z3DSWriteIOFile::DEFAULT_FRAME_SIZE);
    }

    if (!file->IsOpen()) {
        is_error = true;
    }
}

void NCCHCryptoFile::Write(const u8* buffer, std::size_t length) {
    if (is_error)
        return;

    if (is_not_ncch) {
        file->WriteBytes(buffer, length);
        return;
    }

    const int kBlockSize = 0x200; ///< Size of ExeFS blocks (in bytes)

    if (header_size != sizeof(NCCH_Header)) {
        std::size_t to_copy = std::min(length, sizeof(NCCH_Header) - header_size);
        memcpy(reinterpret_cast<u8*>(&ncch_header) + header_size, buffer, to_copy);
        header_size += to_copy;
        buffer += to_copy;
        length -= to_copy;
    }

    if (!header_parsed && header_size == sizeof(NCCH_Header)) {
        if (Loader::MakeMagic('N', 'C', 'C', 'H') != ncch_header.magic) {
            // Most likely DS contents, store without additional operations
            is_not_ncch = true;
            file->WriteBytes(&ncch_header, sizeof(ncch_header));
            file->WriteBytes(buffer, length);
            return;
        }

        if (!ncch_header.no_crypto) {
            if (!decryption_authorized) {
                LOG_ERROR(Service_AM, "Blocked unauthorized encrypted CIA installation.");
                is_error = true;
                return;
            }
            is_encrypted = true;

            // Find primary and secondary keys
            if (ncch_header.fixed_key) {
                LOG_DEBUG(Service_AM, "Fixed-key crypto");
                primary_key.fill(0);
                secondary_key.fill(0);
            } else {
                using namespace HW::AES;
                InitKeys();
                std::array<u8, 16> key_y_primary, key_y_secondary;

                std::copy(ncch_header.signature, ncch_header.signature + key_y_primary.size(),
                          key_y_primary.begin());

                if (!ncch_header.seed_crypto) {
                    key_y_secondary = key_y_primary;
                } else {
                    auto opt{FileSys::GetSeed(ncch_header.program_id)};
                    if (!opt.has_value()) {
                        LOG_ERROR(Service_AM, "Seed for program {:016X} not found",
                                  ncch_header.program_id);
                        is_error = true;
                    } else {
                        auto seed{*opt};
                        std::array<u8, 32> input;
                        std::memcpy(input.data(), key_y_primary.data(), key_y_primary.size());
                        std::memcpy(input.data() + key_y_primary.size(), seed.data(), seed.size());
                        CryptoPP::SHA256 sha;
                        std::array<u8, CryptoPP::SHA256::DIGESTSIZE> hash;
                        sha.CalculateDigest(hash.data(), input.data(), input.size());
                        std::memcpy(key_y_secondary.data(), hash.data(), key_y_secondary.size());
                    }
                }

                SetKeyY(KeySlotID::NCCHSecure1, key_y_primary);
                if (!IsNormalKeyAvailable(KeySlotID::NCCHSecure1)) {
                    LOG_ERROR(Service_AM, "Secure1 KeyX missing");
                    is_error = true;
                }
                primary_key = GetNormalKey(KeySlotID::NCCHSecure1);

                switch (ncch_header.secondary_key_slot) {
                case 0:
                    LOG_DEBUG(Service_AM, "Secure1 crypto");
                    SetKeyY(KeySlotID::NCCHSecure1, key_y_secondary);
                    if (!IsNormalKeyAvailable(KeySlotID::NCCHSecure1)) {
                        LOG_ERROR(Service_AM, "Secure1 KeyX missing");
                        is_error = true;
                    }
                    secondary_key = GetNormalKey(KeySlotID::NCCHSecure1);
                    break;
                case 1:
                    LOG_DEBUG(Service_AM, "Secure2 crypto");
                    SetKeyY(KeySlotID::NCCHSecure2, key_y_secondary);
                    if (!IsNormalKeyAvailable(KeySlotID::NCCHSecure2)) {
                        LOG_ERROR(Service_AM, "Secure2 KeyX missing");
                        is_error = true;
                    }
                    secondary_key = GetNormalKey(KeySlotID::NCCHSecure2);
                    break;
                case 10:
                    LOG_DEBUG(Service_AM, "Secure3 crypto");
                    SetKeyY(KeySlotID::NCCHSecure3, key_y_secondary);
                    if (!IsNormalKeyAvailable(KeySlotID::NCCHSecure3)) {
                        LOG_ERROR(Service_AM, "Secure3 KeyX missing");
                        is_error = true;
                    }
                    secondary_key = GetNormalKey(KeySlotID::NCCHSecure3);
                    break;
                case 11:
                    LOG_DEBUG(Service_AM, "Secure4 crypto");
                    SetKeyY(KeySlotID::NCCHSecure4, key_y_secondary);
                    if (!IsNormalKeyAvailable(KeySlotID::NCCHSecure4)) {
                        LOG_ERROR(Service_AM, "Secure4 KeyX missing");
                        is_error = true;
                    }
                    secondary_key = GetNormalKey(KeySlotID::NCCHSecure4);
                    break;
                }
            }

            // Find CTR for each section
            // Written with reference to
            // https://github.com/d0k3/GodMode9/blob/99af6a73be48fa7872649aaa7456136da0df7938/arm9/source/game/ncch.c#L34-L52
            if (ncch_header.version == 0 || ncch_header.version == 2) {
                LOG_DEBUG(Service_AM, "NCCH version 0/2");
                // In this version, CTR for each section is a magic number prefixed by partition ID
                // (reverse order)
                std::reverse_copy(ncch_header.partition_id, ncch_header.partition_id + 8,
                                  exheader_ctr.begin());
                exefs_ctr = romfs_ctr = exheader_ctr;
                exheader_ctr[8] = 1;
                exefs_ctr[8] = 2;
                romfs_ctr[8] = 3;
            } else if (ncch_header.version == 1) {
                LOG_DEBUG(Service_AM, "NCCH version 1");
                // In this version, CTR for each section is the section offset prefixed by partition
                // ID, as if the entire NCCH image is encrypted using a single CTR stream.
                std::copy(ncch_header.partition_id, ncch_header.partition_id + 8,
                          exheader_ctr.begin());
                exefs_ctr = romfs_ctr = exheader_ctr;
                auto u32ToBEArray = [](u32 value) -> std::array<u8, 4> {
                    return std::array<u8, 4>{
                        static_cast<u8>(value >> 24),
                        static_cast<u8>((value >> 16) & 0xFF),
                        static_cast<u8>((value >> 8) & 0xFF),
                        static_cast<u8>(value & 0xFF),
                    };
                };
                auto offset_exheader = u32ToBEArray(0x200); // exheader offset
                auto offset_exefs = u32ToBEArray(ncch_header.exefs_offset * kBlockSize);
                auto offset_romfs = u32ToBEArray(ncch_header.romfs_offset * kBlockSize);
                std::copy(offset_exheader.begin(), offset_exheader.end(),
                          exheader_ctr.begin() + 12);
                std::copy(offset_exefs.begin(), offset_exefs.end(), exefs_ctr.begin() + 12);
                std::copy(offset_romfs.begin(), offset_romfs.end(), romfs_ctr.begin() + 12);
            } else {
                LOG_ERROR(Service_AM, "Unknown NCCH version {}", ncch_header.version);
                is_error = true;
            }
        } else {
            LOG_DEBUG(Service_AM, "No crypto");
            is_encrypted = false;
        }
        header_parsed = true;

        if (is_error) {
            return;
        }

        if (is_encrypted) {
            if (ncch_header.extended_header_size) {
                regions.push_back(CryptoRegion{.type = CryptoRegion::EXHEADER,
                                               .offset = sizeof(NCCH_Header),
                                               .size = sizeof(ExHeader_Header),
                                               .seek_from = sizeof(NCCH_Header)});
            }
            if (ncch_header.exefs_size) {
                regions.push_back(CryptoRegion{.type = CryptoRegion::EXEFS_HDR,
                                               .offset = ncch_header.exefs_offset * kBlockSize,
                                               .size = sizeof(ExeFs_Header),
                                               .seek_from = ncch_header.exefs_offset * kBlockSize});
            }
            if (ncch_header.romfs_size) {
                regions.push_back(CryptoRegion{.type = CryptoRegion::ROMFS,
                                               .offset = ncch_header.romfs_offset * kBlockSize,
                                               .size = ncch_header.romfs_size * kBlockSize,
                                               .seek_from = ncch_header.romfs_offset * kBlockSize});
            }
        }

        u8 prev_crypto = ncch_header.no_crypto;
        ncch_header.no_crypto.Assign(1);
        file->WriteBytes(&ncch_header, sizeof(ncch_header));
        written += sizeof(ncch_header);
        ncch_header.no_crypto.Assign(prev_crypto);
    }

    while (length) {
        auto find_closest_region = [this](size_t offset) -> std::optional<CryptoRegion> {
            CryptoRegion* closest = nullptr;
            for (auto& reg : regions) {
                if (offset >= reg.offset && offset < reg.offset + reg.size) {
                    return reg;
                }
                if (offset < reg.offset) {
                    size_t dist = reg.offset - offset;
                    if ((closest && closest->offset - offset > dist) || !closest) {
                        closest = &reg;
                    }
                }
            }
            // Return the closest one
            if (closest) {
                return *closest;
            } else {
                return std::nullopt;
            }
        };

        const auto reg = find_closest_region(written);
        if (!reg.has_value()) {
            // This file has no encryption
            size_t to_write = length;
            file->WriteBytes(buffer, to_write);
            written += to_write;
            buffer += to_write;
            length -= to_write;
        } else {
            if (written < reg->offset) {
                // Not inside a crypto region
                size_t to_write = std::min(length, reg->offset - written);
                file->WriteBytes(buffer, to_write);
                written += to_write;
                buffer += to_write;
                length -= to_write;
            } else {
                size_t to_write = std::min(length, (reg->offset + reg->size) - written);
                if (is_encrypted) {
                    std::vector<u8> temp(to_write);

                    std::array<u8, 16>* key = nullptr;
                    std::array<u8, 16>* ctr = nullptr;

                    if (reg->type == CryptoRegion::EXHEADER) {
                        key = &primary_key;
                        ctr = &exheader_ctr;
                    } else if (reg->type == CryptoRegion::EXEFS_HDR ||
                               reg->type == CryptoRegion::EXEFS_PRI) {
                        key = &primary_key;
                        ctr = &exefs_ctr;
                    } else if (reg->type == CryptoRegion::EXEFS_SEC) {
                        key = &secondary_key;
                        ctr = &exefs_ctr;
                    } else if (reg->type == CryptoRegion::ROMFS) {
                        key = &secondary_key;
                        ctr = &romfs_ctr;
                    }

                    CryptoPP::CTR_Mode<CryptoPP::AES>::Decryption d(key->data(), key->size(),
                                                                    ctr->data());
                    size_t offset = written - reg->seek_from;
                    if (offset != 0) {
                        d.Seek(offset);
                    }
                    d.ProcessData(temp.data(), buffer, to_write);
                    file->WriteBytes(temp.data(), to_write);

                    if (reg->type == CryptoRegion::EXEFS_HDR) {
                        if (exefs_header_written != sizeof(ExeFs_Header)) {
                            memcpy(reinterpret_cast<u8*>(&exefs_header) + exefs_header_written,
                                   temp.data(), to_write);
                            exefs_header_written += to_write;
                        }
                        if (!exefs_header_processed &&
                            exefs_header_written == sizeof(ExeFs_Header)) {
                            for (int i = 0; i < 8; i++) {
                                if (exefs_header.section[i].size != 0) {
                                    bool is_primary =
                                        memcmp(exefs_header.section[i].name, "icon", 4) == 0 ||
                                        memcmp(exefs_header.section[i].name, "banner", 6) == 0;
                                    regions.push_back(CryptoRegion{
                                        .type = is_primary ? CryptoRegion::EXEFS_PRI
                                                           : CryptoRegion::EXEFS_SEC,
                                        .offset = reg->offset + sizeof(ExeFs_Header) +
                                                  exefs_header.section[i].offset,
                                        .size =
                                            Common::AlignUp(exefs_header.section[i].size, 0x200),
                                        .seek_from = reg->offset});
                                }
                            }
                            exefs_header_processed = true;
                        }
                    }
                } else {
                    file->WriteBytes(buffer, to_write);
                }
                written += to_write;
                buffer += to_write;
                length -= to_write;
            }
        }
    }
}

void AuthorizeCIAFileDecryption(CIAFile* cia_file, Kernel::HLERequestContext& ctx) {
    u64 caller_tid = ctx.ClientThread()->owner_process.lock()->codeset->program_id;
    if (Common::Hacks::hack_manager.GetHackAllowMode(
            Common::Hacks::HackType::DECRYPTION_AUTHORIZED, caller_tid,
            Common::Hacks::HackAllowMode::DISALLOW) == Common::Hacks::HackAllowMode::ALLOW) {
        LOG_INFO(Service_AM, "Authorized encrypted CIA installation.");
        cia_file->decryption_authorized = true;
    }
}

CIAFile::CIAFile(Core::System& system_, Service::FS::MediaType media_type, bool from_cdn_)
    : system(system_), from_cdn(from_cdn_), decryption_authorized(false), media_type(media_type),
      decryption_state(std::make_unique<DecryptionState>()) {
    // If data is being installing from CDN, provide a fake header to the container so that
    // it's not uninitialized.
    if (from_cdn) {
        FileSys::CIAHeader fake_header{
            .header_size = sizeof(FileSys::CIAHeader),
            .type = 0,
            .version = 0,
            .cert_size = 0,
            .tik_size = 0,
            .tmd_size = 0,
            .meta_size = 0,
        };
        container.LoadHeader({reinterpret_cast<u8*>(&fake_header), sizeof(fake_header)});
        install_state = CIAInstallState::HeaderLoaded;
    }
}

CIAFile::~CIAFile() {
    Close();
}

ResultVal<std::size_t> CIAFile::Read(u64 offset, std::size_t length, u8* buffer) const {
    UNIMPLEMENTED();
    return length;
}

CIAFile::InstallResult CIAFile::WriteTicket() {
    InstallResult res{};
    res.type = InstallResult::Type::TIK;
    auto load_result = container.LoadTicket(data, container.GetTicketOffset());
    if (load_result != Loader::ResultStatus::Success) {
        LOG_ERROR(Service_AM, "Could not read ticket from CIA.");
        // TODO: Correct result code.
        res.result = {ErrCodes::InvalidCIAHeader, ErrorModule::AM, ErrorSummary::InvalidArgument,
                      ErrorLevel::Permanent};
        return res;
    }

    const auto& ticket = container.GetTicket();
    const auto ticket_path = GetTicketPath(ticket.GetTitleID(), ticket.GetTicketID());

    // Create ticket folder if it does not exist
    std::string ticket_folder;
    Common::SplitPath(ticket_path, &ticket_folder, nullptr, nullptr);
    FileUtil::CreateFullPath(ticket_folder);

    // Save ticket
    res.install_full_path = ticket_path;
    if (ticket.Save(ticket_path) != Loader::ResultStatus::Success) {
        LOG_ERROR(Service_AM, "Failed to install ticket file from CIA.");
        // TODO: Correct result code.
        res.result = FileSys::ResultFileNotFound;
        return res;
    }

    install_state = CIAInstallState::TicketLoaded;
    res.result = ResultSuccess;
    return res;
}

CIAFile::InstallResult CIAFile::WriteTitleMetadata(std::span<const u8> tmd_data,
                                                   std::size_t offset) {
    InstallResult res{};
    res.type = InstallResult::Type::TMD;
    auto load_result = container.LoadTitleMetadata(tmd_data, offset);
    if (load_result != Loader::ResultStatus::Success) {
        LOG_ERROR(Service_AM, "Could not read title metadata.");
        // TODO: Correct result code.
        res.result = {ErrCodes::InvalidCIAHeader, ErrorModule::AM, ErrorSummary::InvalidArgument,
                      ErrorLevel::Permanent};
        return res;
    }

    FileSys::TitleMetadata tmd = container.GetTitleMetadata();
    tmd.Print();

    // If a TMD already exists for this app (ie 00000000.tmd), the incoming TMD
    // will be the same plus one, (ie 00000001.tmd), both will be kept until
    // the install is finalized and old contents can be discarded.
    if (FileUtil::Exists(GetTitleMetadataPath(media_type, tmd.GetTitleID()))) {
        is_update = true;
    }

    std::string tmd_path = GetTitleMetadataPath(media_type, tmd.GetTitleID(), is_update);

    // Create content/ folder if it doesn't exist
    std::string tmd_folder;
    Common::SplitPath(tmd_path, &tmd_folder, nullptr, nullptr);
    FileUtil::CreateFullPath(tmd_folder);

    // Save TMD so that we can start getting new .app paths
    res.install_full_path = tmd_path;
    if (tmd.Save(tmd_path) != Loader::ResultStatus::Success) {
        LOG_ERROR(Service_AM, "Failed to install title metadata file from CIA.");
        // TODO: Correct result code.
        res.result = FileSys::ResultFileNotFound;
        return res;
    }

    res.result = PrepareToImportContent(tmd);
    return res;
}

ResultVal<std::size_t> CIAFile::WriteContentData(u64 offset, std::size_t length, const u8* buffer) {
    // Data is not being buffered, so we have to keep track of how much of each <ID>.app
    // has been written since we might get a written buffer which contains multiple .app
    // contents or only part of a larger .app's contents.
    const u64 offset_max = offset + length;
    for (std::size_t i = 0; i < content_written.size(); i++) {
        if (content_written[i] < container.GetContentSize(i)) {
            // The size, minimum unwritten offset, and maximum unwritten offset of this content
            const u64 size = container.GetContentSize(i);
            const u64 range_min = container.GetContentOffset(i) + content_written[i];
            const u64 range_max = container.GetContentOffset(i) + size;

            // The unwritten range for this content is beyond the buffered data we have
            // or comes before the buffered data we have, so skip this content ID.
            if (range_min > offset_max || range_max < offset) {
                continue;
            }

            // Figure out how much of this content ID we have just recieved/can write out
            const u64 available_to_write = std::min(offset_max, range_max) - range_min;

            // Since the incoming TMD has already been written, we can use GetTitleContentPath
            // to get the content paths to write to.
            const FileSys::TitleMetadata& tmd = container.GetTitleMetadata();
            if (i != current_content_index) {
                // A previous content file was being installed, save it first
                if (current_content_install_result.type == InstallResult::Type::APP) {
                    install_results.push_back(current_content_install_result);
                }
                current_content_index = static_cast<u16>(i);
                current_content_file =
                    std::make_unique<NCCHCryptoFile>(content_file_paths[i], decryption_authorized);
                current_content_file->decryption_authorized = decryption_authorized;

                current_content_install_result.type = InstallResult::Type::APP;
                current_content_install_result.install_full_path = content_file_paths[i];
                current_content_install_result.result = ResultSuccess;
            }
            auto& file = *current_content_file;

            std::vector<u8> temp(buffer + (range_min - offset),
                                 buffer + (range_min - offset) + available_to_write);

            if ((tmd.GetContentTypeByIndex(i) & FileSys::TMDContentTypeFlag::Encrypted) != 0) {
                if (!decryption_authorized) {
                    LOG_ERROR(Service_AM, "Blocked unauthorized encrypted CIA installation.");
                    current_content_install_result.result =
                        Result(ErrorDescription::NotAuthorized, ErrorModule::AM,
                               ErrorSummary::InvalidState, ErrorLevel::Permanent);
                    install_results.push_back(current_content_install_result);
                    return current_content_install_result.result;
                }
                decryption_state->content[i].ProcessData(temp.data(), temp.data(), temp.size());
            }

            file.Write(temp.data(), temp.size());
            if (file.IsError()) {
                // This can never happen in real HW
                current_content_install_result.result =
                    Result(ErrCodes::InvalidImportState, ErrorModule::AM,
                           ErrorSummary::InvalidState, ErrorLevel::Permanent);
                install_results.push_back(current_content_install_result);
                return current_content_install_result.result;
            }

            // Keep tabs on how much of this content ID has been written so new range_min
            // values can be calculated.
            content_written[i] += available_to_write;
            LOG_DEBUG(Service_AM, "Wrote {} to content {}, total {}", available_to_write, i,
                      content_written[i]);
        }
    }

    return length;
}

ResultVal<std::size_t> CIAFile::Write(u64 offset, std::size_t length, bool flush,
                                      bool update_timestamp, const u8* buffer) {
    written += length;

    // TODO(shinyquagsire23): Can we assume that things will only be written in sequence?
    // Does AM send an error if we write to things out of order?
    // Or does it just ignore offsets and assume a set sequence of incoming data?

    // The data in CIAs is always stored CIA Header > Cert > Ticket > TMD > Content > Meta.
    // The CIA Header describes Cert, Ticket, TMD, total content sizes, and TMD is needed for
    // content sizes so it ends up becoming a problem of keeping track of how much has been
    // written and what we have been able to pick up.
    if (install_state == CIAInstallState::InstallStarted) {
        std::size_t buf_copy_size = std::min(length, FileSys::CIA_HEADER_SIZE);
        std::size_t buf_max_size =
            std::min(static_cast<std::size_t>(offset + length), FileSys::CIA_HEADER_SIZE);
        data.resize(buf_max_size);
        std::memcpy(data.data() + offset, buffer, buf_copy_size);

        // We have enough data to load a CIA header and parse it.
        if (written >= FileSys::CIA_HEADER_SIZE) {
            container.LoadHeader(data);
            container.Print();
            install_state = CIAInstallState::HeaderLoaded;
        }
    }

    // If we don't have a header yet, we can't pull offsets of other sections
    if (install_state == CIAInstallState::InstallStarted) {
        return length;
    }

    // If we have been given data before (or including) .app content, pull it into
    // our buffer, but only pull *up to* the content offset, no further.
    if (offset < container.GetContentOffset()) {
        std::size_t buf_loaded = data.size();
        std::size_t copy_offset = std::max(static_cast<std::size_t>(offset), buf_loaded);
        std::size_t buf_offset = buf_loaded - offset;
        std::size_t buf_copy_size =
            std::min(length, static_cast<std::size_t>(container.GetContentOffset() - offset)) -
            buf_offset;
        std::size_t buf_max_size = std::min(offset + length, container.GetContentOffset());
        data.resize(buf_max_size);
        std::memcpy(data.data() + copy_offset, buffer + buf_offset, buf_copy_size);
    }

    // The end of our TMD is at the beginning of Content data, so ensure we have that much
    // buffered before trying to parse.
    if (written >= container.GetContentOffset() && install_state != CIAInstallState::TMDLoaded) {
        InstallResult result = WriteTicket();
        install_results.push_back(result);
        if (result.result.IsError()) {
            return result.result;
        }

        result = WriteTitleMetadata(data, container.GetTitleMetadataOffset());
        install_results.push_back(result);
        if (result.result.IsError()) {
            return result.result;
        }
    }

    // Content data sizes can only be retrieved from TMD data
    if (install_state != CIAInstallState::TMDLoaded) {
        return length;
    }

    // From this point forward, data will no longer be buffered in data
    auto result = WriteContentData(offset, length, buffer);
    if (result.Failed()) {
        current_content_install_result.type = InstallResult::Type::NONE;
        return result;
    }

    return length;
}

Result CIAFile::PrepareToImportContent(const FileSys::TitleMetadata& tmd) {

    // Create any other .app folders which may not exist yet
    std::string app_folder;
    auto main_content_path = GetTitleContentPath(media_type, tmd.GetTitleID(),
                                                 FileSys::TMDContentIndex::Main, is_update);
    Common::SplitPath(main_content_path, &app_folder, nullptr, nullptr);
    FileUtil::CreateFullPath(app_folder);

    auto content_count = container.GetTitleMetadata().GetContentCount();
    content_written.resize(content_count);

    current_content_file.reset();
    current_content_index = -1;
    content_file_paths.clear();
    for (std::size_t i = 0; i < content_count; i++) {
        auto path = GetTitleContentPath(media_type, tmd.GetTitleID(), i, is_update);
        content_file_paths.emplace_back(path);
    }

    if (container.GetTitleMetadata().HasEncryptedContent(from_cdn ? nullptr
                                                                  : container.GetHeader())) {
        if (!decryption_authorized) {
            LOG_ERROR(Service_AM, "Blocked unauthorized encrypted CIA installation.");
            return {ErrorDescription::NotAuthorized, ErrorModule::AM, ErrorSummary::InvalidState,
                    ErrorLevel::Permanent};
        } else {
            if (auto title_key = container.GetTicket().GetTitleKey()) {
                decryption_state->content.resize(content_count);
                for (std::size_t i = 0; i < content_count; ++i) {
                    auto ctr = tmd.GetContentCTRByIndex(i);
                    decryption_state->content[i].SetKeyWithIV(title_key->data(), title_key->size(),
                                                              ctr.data());
                }
            } else {
                LOG_ERROR(Service_AM, "Could not read title key from ticket for encrypted CIA.");
                // TODO: Correct error code.
                return FileSys::ResultFileNotFound;
            }
        }
    } else {
        LOG_INFO(Service_AM,
                 "Title has no encrypted content, skipping initializing decryption state.");
    }

    install_state = CIAInstallState::TMDLoaded;

    return ResultSuccess;
}

Result CIAFile::ProvideTicket(const FileSys::Ticket& ticket) {
    // There is no need to write the ticket to nand, as that will
    ASSERT_MSG(from_cdn, "This method should only be used when installing from CDN");

    auto load_result = container.LoadTicket(ticket);
    if (load_result != Loader::ResultStatus::Success) {
        LOG_ERROR(Service_AM, "Could not read ticket from CIA.");
        // TODO: Correct result code.
        return {ErrCodes::InvalidCIAHeader, ErrorModule::AM, ErrorSummary::InvalidArgument,
                ErrorLevel::Permanent};
    }

    install_state = CIAInstallState::TicketLoaded;
    return ResultSuccess;
}

Result CIAFile::ProvideTMDForAdditionalContent(const FileSys::TitleMetadata& tmd) {
    ASSERT_MSG(from_cdn, "This method should only be used when installing from CDN");

    if (install_state != CIAInstallState::TicketLoaded) {
        LOG_ERROR(Service_AM, "Ticket not provided yet");
        // TODO: Correct result code.
        return {ErrCodes::InvalidImportState, ErrorModule::AM, ErrorSummary::InvalidArgument,
                ErrorLevel::Permanent};
    }

    auto load_result = container.LoadTitleMetadata(tmd);
    if (load_result != Loader::ResultStatus::Success) {
        LOG_ERROR(Service_AM, "Could not read ticket from CIA.");
        // TODO: Correct result code.
        return {ErrCodes::InvalidCIAHeader, ErrorModule::AM, ErrorSummary::InvalidArgument,
                ErrorLevel::Permanent};
    }

    is_additional_content = true;

    return PrepareToImportContent(container.GetTitleMetadata());
}

const FileSys::TitleMetadata& CIAFile::GetTMD() {
    return container.GetTitleMetadata();
}

FileSys::Ticket& CIAFile::GetTicket() {
    return container.GetTicket();
}

ResultVal<std::size_t> CIAFile::WriteContentDataIndexed(u16 content_index, u64 offset,
                                                        std::size_t length, const u8* buffer) {

    ASSERT_MSG(from_cdn, "This method should only be used when installing from CDN");

    const FileSys::TitleMetadata& tmd = container.GetTitleMetadata();

    u64 remaining_to_write =
        tmd.GetContentSizeByIndex(content_index) - content_written[content_index];

    if (content_index != current_content_index) {
        // A previous content file was being installed, save it first
        if (current_content_install_result.type == InstallResult::Type::APP) {
            install_results.push_back(current_content_install_result);
        }

        current_content_index = content_index;
        current_content_file = std::make_unique<NCCHCryptoFile>(content_file_paths[content_index],
                                                                decryption_authorized);
        current_content_file->decryption_authorized = decryption_authorized;

        current_content_install_result.type = InstallResult::Type::APP;
        current_content_install_result.install_full_path = content_file_paths[content_index];
        current_content_install_result.result = ResultSuccess;
    }
    auto& file = *current_content_file;

    std::vector<u8> temp(buffer, buffer + std::min(static_cast<u64>(length), remaining_to_write));

    if ((tmd.GetContentTypeByIndex(content_index) & FileSys::TMDContentTypeFlag::Encrypted) != 0) {
        if (!decryption_authorized) {
            LOG_ERROR(Service_AM, "Blocked unauthorized encrypted CIA installation.");
            current_content_install_result.result =
                Result(ErrorDescription::NotAuthorized, ErrorModule::AM, ErrorSummary::InvalidState,
                       ErrorLevel::Permanent);
            install_results.push_back(current_content_install_result);
            return current_content_install_result.result;
        }
        decryption_state->content[content_index].ProcessData(temp.data(), temp.data(), temp.size());
    }

    file.Write(temp.data(), temp.size());
    if (file.IsError()) {
        // This can never happen in real HW
        current_content_install_result.result =
            Result(ErrCodes::InvalidImportState, ErrorModule::AM, ErrorSummary::InvalidState,
                   ErrorLevel::Permanent);
        install_results.push_back(current_content_install_result);
        return current_content_install_result.result;
    }

    content_written[content_index] += temp.size();
    LOG_DEBUG(Service_AM, "Wrote {} to content {}, total {}", temp.size(), content_index,
              content_written[content_index]);

    return temp.size();
}

u64 CIAFile::GetSize() const {
    return written;
}

bool CIAFile::SetSize(u64 size) const {
    return false;
}

bool CIAFile::Close() {
    if (is_closed)
        return true;
    is_closed = true;

    // Commit last pending install result
    if (current_content_install_result.type != InstallResult::Type::NONE) {
        install_results.push_back(current_content_install_result);
        current_content_install_result.type = InstallResult::Type::NONE;
    }

    bool complete =
        from_cdn ? is_done
                 : (install_state >= CIAInstallState::TMDLoaded &&
                    content_written.size() == container.GetTitleMetadata().GetContentCount() &&
                    std::all_of(content_written.begin(), content_written.end(),
                                [this, i = 0](auto& bytes_written) mutable {
                                    return bytes_written >=
                                           container.GetContentSize(static_cast<u16>(i++));
                                }));

    // Install aborted
    if (!complete) {
        LOG_ERROR(Service_AM, "CIAFile closed prematurely, aborting install...");
        if (!is_additional_content) {
            FileUtil::DeleteDirRecursively(
                GetTitlePath(media_type, container.GetTitleMetadata().GetTitleID()));
        }
        return true;
    }

    // Clean up older content data if we installed newer content on top
    std::string old_tmd_path =
        GetTitleMetadataPath(media_type, container.GetTitleMetadata().GetTitleID(), false);
    std::string new_tmd_path = old_tmd_path;

    for (auto result : install_results) {
        if (result.type == InstallResult::Type::TMD && result.result.IsSuccess()) {
            new_tmd_path = result.install_full_path;
            break;
        }
    }

    if (FileUtil::Exists(new_tmd_path) && old_tmd_path != new_tmd_path) {
        FileSys::TitleMetadata old_tmd;
        FileSys::TitleMetadata new_tmd;

        old_tmd.Load(old_tmd_path);
        new_tmd.Load(new_tmd_path);

        // For each content ID in the old TMD, check if there is a matching ID in the new
        // TMD. If a CIA contains (and wrote to) an identical ID, it should be kept while
        // IDs which only existed for the old TMD should be deleted.
        for (std::size_t old_index = 0; old_index < old_tmd.GetContentCount(); old_index++) {
            bool abort = false;
            for (std::size_t new_index = 0; new_index < new_tmd.GetContentCount(); new_index++) {
                if (old_tmd.GetContentIDByIndex(old_index) ==
                    new_tmd.GetContentIDByIndex(new_index)) {
                    abort = true;
                }
            }
            if (abort) {
                break;
            }

            // If the file to delete is the current launched rom, signal the system to delete
            // the current rom instead of deleting it now, once all the handles to the file
            // are closed.
            std::string to_delete =
                GetTitleContentPath(media_type, old_tmd.GetTitleID(), old_index);
            if (!system.IsPoweredOn() || !system.SetSelfDelete(to_delete)) {
                FileUtil::Delete(to_delete);
            }
        }

        FileUtil::Delete(old_tmd_path);
    }
    return true;
}

void CIAFile::Flush() const {}

TicketFile::TicketFile() {}

TicketFile::~TicketFile() {
    Close();
}

ResultVal<std::size_t> TicketFile::Read(u64 offset, std::size_t length, u8* buffer) const {
    UNIMPLEMENTED();
    return length;
}

ResultVal<std::size_t> TicketFile::Write(u64 offset, std::size_t length, bool flush,
                                         bool update_timestamp, const u8* buffer) {
    written += length;
    data.resize(written);
    std::memcpy(data.data() + offset, buffer, length);
    return length;
}

u64 TicketFile::GetSize() const {
    return written;
}

bool TicketFile::SetSize(u64 size) const {
    return false;
}

bool TicketFile::Close() {
    return true;
}

void TicketFile::Flush() const {}

Result TicketFile::Commit() {
    FileSys::Ticket ticket;
    if (ticket.Load(data, 0) == Loader::ResultStatus::Success) {
        if (ticket.DoTitlekeyFixup() != Loader::ResultStatus::Success) {
            LOG_ERROR(Service_AM, "Failed to do ticket title key fixup");
            return ResultUnknown;
        }

        title_id = ticket.GetTitleID();
        ticket_id = ticket.GetTicketID();
        const auto ticket_path = GetTicketPath(ticket.GetTitleID(), ticket.GetTicketID());

        // Save ticket
        if (ticket.Save(ticket_path) != Loader::ResultStatus::Success) {
            LOG_ERROR(Service_AM, "Failed to install ticket provided to TicketFile.");
            return ResultUnknown;
        }
        return ResultSuccess;
    } else {
        LOG_ERROR(Service_AM, "Invalid ticket provided to TicketFile.");
        return ResultUnknown;
    }
}

TMDFile::~TMDFile() {
    Close();
}

ResultVal<std::size_t> TMDFile::Read(u64 offset, std::size_t length, u8* buffer) const {
    UNIMPLEMENTED();
    return length;
}

ResultVal<std::size_t> TMDFile::Write(u64 offset, std::size_t length, bool flush,
                                      bool update_timestamp, const u8* buffer) {
    written += length;
    data.resize(written);
    std::memcpy(data.data() + offset, buffer, length);
    return length;
}

u64 TMDFile::GetSize() const {
    return written;
}

bool TMDFile::SetSize(u64 size) const {
    return false;
}

bool TMDFile::Close() {
    return true;
}

void TMDFile::Flush() const {}

Result TMDFile::Commit() {
    return importing_title->cia_file.WriteTitleMetadata(data, 0).result;
}

ContentFile::~ContentFile() {
    Close();
}

ResultVal<std::size_t> ContentFile::Read(u64 offset, std::size_t length, u8* buffer) const {
    UNIMPLEMENTED();
    return length;
}

ResultVal<std::size_t> ContentFile::Write(u64 offset, std::size_t length, bool flush,
                                          bool update_timestamp, const u8* buffer) {
    auto res = importing_title->cia_file.WriteContentDataIndexed(index, offset, length, buffer);
    if (res.Succeeded()) {
        import_context.current_size += static_cast<u64>(res.Unwrap());
    }
    return res;
}

u64 ContentFile::GetSize() const {
    return written;
}

bool ContentFile::SetSize(u64 size) const {
    return false;
}

bool ContentFile::Close() {
    return false;
}

void ContentFile::Flush() const {}

void ContentFile::Cancel(FS::MediaType media_type, u64 title_id) {
    auto path = GetTitleContentPath(media_type, title_id, index, true);
    FileUtil::Delete(path);
}

InstallStatus InstallCIA(const std::string& path,
                         std::function<ProgressCallback>&& update_callback) {
    LOG_INFO(Service_AM, "Installing {}...", path);

    if (!FileUtil::Exists(path)) {
        LOG_ERROR(Service_AM, "File {} does not exist!", path);
        return InstallStatus::ErrorFileNotFound;
    }

    std::unique_ptr<FileUtil::IOFile> in_file = std::make_unique<FileUtil::IOFile>(path, "rb");
    bool is_compressed =
        FileUtil::Z3DSReadIOFile::GetUnderlyingFileMagic(in_file.get()) != std::nullopt;
    if (is_compressed) {
        in_file = std::make_unique<FileUtil::Z3DSReadIOFile>(std::move(in_file));
    }

    FileSys::CIAContainer container;
    if (container.Load(in_file.get()) == Loader::ResultStatus::Success) {
        in_file->Seek(0, SEEK_SET);
        Service::AM::CIAFile installFile(
            Core::System::GetInstance(),
            Service::AM::GetTitleMediaType(container.GetTitleMetadata().GetTitleID()));

        if (container.GetTitleMetadata().HasEncryptedContent(container.GetHeader())) {
            LOG_ERROR(Service_AM, "File {} is encrypted! Aborting...", path);
            return InstallStatus::ErrorEncrypted;
        }

        std::vector<u8> buffer;
        buffer.resize(0x10000);
        auto file_size = in_file->GetSize();
        std::size_t total_bytes_read = 0;
        while (total_bytes_read != file_size) {
            std::size_t bytes_read = in_file->ReadBytes(buffer.data(), buffer.size());
            auto result = installFile.Write(static_cast<u64>(total_bytes_read), bytes_read, true,
                                            false, static_cast<u8*>(buffer.data()));

            if (update_callback) {
                update_callback(total_bytes_read, file_size);
            }
            if (result.Failed()) {
                LOG_ERROR(Service_AM, "CIA file installation aborted with error code {:08x}",
                          result.Code().raw);
                return InstallStatus::ErrorAborted;
            }
            total_bytes_read += bytes_read;
        }
        installFile.Close();

        InstallStatus install_res = InstallStatus::Success;
        for (auto result : installFile.GetInstallResults()) {
            if (result.type != CIAFile::InstallResult::Type::APP || result.result.IsError()) {
                continue;
            }

            std::unique_ptr<Loader::AppLoader> loader = Loader::GetLoader(result.install_full_path);
            if (!loader) {
                continue;
            }

            bool executable = false;
            const auto res = loader->IsExecutable(executable);
            if (res == Loader::ResultStatus::ErrorEncrypted) {
                LOG_ERROR(Service_AM, "CIA contains encrypted content: {}", path,
                          result.install_full_path);
                install_res = InstallStatus::ErrorEncrypted;
            }
        }
        if (install_res == InstallStatus::Success) {
            LOG_INFO(Service_AM, "Installed {} successfully.", path);
        }
        return install_res;
    }

    LOG_ERROR(Service_AM, "CIA file {} is invalid!", path);
    return InstallStatus::ErrorInvalid;
}

InstallStatus CheckCIAToInstall(const std::string& path, bool& is_compressed,
                                bool check_encryption) {
    if (!FileUtil::Exists(path)) {
        LOG_ERROR(Service_AM, "File {} does not exist!", path);
        return InstallStatus::ErrorFileNotFound;
    }

    std::unique_ptr<FileUtil::IOFile> in_file = std::make_unique<FileUtil::IOFile>(path, "rb");
    is_compressed = FileUtil::Z3DSReadIOFile::GetUnderlyingFileMagic(in_file.get()) != std::nullopt;
    if (is_compressed) {
        in_file = std::make_unique<FileUtil::Z3DSReadIOFile>(std::move(in_file));
    }

    FileSys::CIAContainer container;
    if (container.Load(in_file.get()) == Loader::ResultStatus::Success) {
        in_file->Seek(0, SEEK_SET);
        const FileSys::TitleMetadata& tmd = container.GetTitleMetadata();

        if (check_encryption) {
            if (tmd.HasEncryptedContent(container.GetHeader())) {
                return InstallStatus::ErrorEncrypted;
            }

            for (size_t i = 0; i < tmd.GetContentCount(); i++) {
                u64 offset = container.GetContentOffset(i);
                NCCH_Header ncch;
                const auto read = in_file->ReadAtBytes(&ncch, sizeof(ncch), offset);
                if (read != sizeof(ncch)) {
                    return InstallStatus::ErrorInvalid;
                }
                if (ncch.magic != Loader::MakeMagic('N', 'C', 'C', 'H')) {
                    return InstallStatus::ErrorInvalid;
                }
                if (!ncch.no_crypto) {
                    return InstallStatus::ErrorEncrypted;
                }
            }
        }

        return InstallStatus::Success;
    }

    return InstallStatus::ErrorInvalid;
}

ResultVal<std::pair<TitleInfo, std::unique_ptr<Loader::SMDH>>> GetCIAInfos(
    const std::string& path) {
    if (!FileUtil::Exists(path)) {
        LOG_ERROR(Service_AM, "File {} does not exist!", path);
        return ResultUnknown;
    }

    std::unique_ptr<FileUtil::IOFile> in_file = std::make_unique<FileUtil::IOFile>(path, "rb");
    FileSys::CIAContainer container;
    if (container.Load(in_file.get()) == Loader::ResultStatus::Success) {
        in_file->Seek(0, SEEK_SET);
        const FileSys::TitleMetadata& tmd = container.GetTitleMetadata();

        TitleInfo info{};
        info.tid = tmd.GetTitleID();
        info.version = tmd.GetTitleVersion();
        info.size = tmd.GetCombinedContentSize(container.GetHeader());
        info.type = tmd.GetTitleType();

        const auto& cia_smdh = container.GetSMDH();
        std::unique_ptr<Loader::SMDH> smdh{};
        if (cia_smdh) {
            smdh = std::make_unique<Loader::SMDH>(*cia_smdh);
        }

        return std::pair<TitleInfo, std::unique_ptr<Loader::SMDH>>(info, std::move(smdh));
    }

    return ResultUnknown;
}

u64 GetTitleUpdateId(u64 title_id) {
    // Real services seem to just discard and replace the whole high word.
    return (title_id & 0xFFFFFFFF) | (static_cast<u64>(TID_HIGH_UPDATE) << 32);
}

Service::FS::MediaType GetTitleMediaType(u64 titleId) {
    u16 platform = static_cast<u16>(titleId >> 48);
    u16 category = static_cast<u16>((titleId >> 32) & 0xFFFF);
    u8 variation = static_cast<u8>(titleId & 0xFF);

    if (platform != PLATFORM_CTR)
        return Service::FS::MediaType::NAND;

    if (category & CATEGORY_SYSTEM || category & CATEGORY_DLP || variation & VARIATION_SYSTEM)
        return Service::FS::MediaType::NAND;

    return Service::FS::MediaType::SDMC;
}

std::string GetTicketDirectory() {
    return fmt::format("{}/dbs/ticket.db/", FileUtil::GetUserPath(FileUtil::UserPath::NANDDir));
}

std::string GetTicketPath(u64 title_id, u64 ticket_id) {
    return GetTicketDirectory() + fmt::format("{:016X}.{:016X}.tik", title_id, ticket_id);
}

std::string GetTitleMetadataPath(Service::FS::MediaType media_type, u64 tid, bool update) {
    std::string content_path = GetTitlePath(media_type, tid) + "content/";

    if (media_type == Service::FS::MediaType::GameCard) {
        LOG_ERROR(Service_AM, "Invalid request for nonexistent gamecard title metadata!");
        return "";
    }

    // The TMD ID is usually held in the title databases, which we don't implement.
    // For now, just scan for any .tmd files which exist, the smallest will be the
    // base ID and the largest will be the (currently installing) update ID.
    constexpr u32 MAX_TMD_ID = 0xFFFFFFFF;
    u32 base_id = MAX_TMD_ID;
    u32 update_id = 0;
    FileUtil::FSTEntry entries;
    FileUtil::ScanDirectoryTree(content_path, entries);
    for (const FileUtil::FSTEntry& entry : entries.children) {
        std::string filename_filename, filename_extension;
        Common::SplitPath(entry.virtualName, nullptr, &filename_filename, &filename_extension);

        if (filename_extension == ".tmd") {
            const u32 id = static_cast<u32>(std::stoul(filename_filename, nullptr, 16));
            base_id = std::min(base_id, id);
            update_id = std::max(update_id, id);
        }
    }

    // If we didn't find anything, default to 00000000.tmd for it to be created.
    if (base_id == MAX_TMD_ID)
        base_id = 0;

    // Update ID should be one more than the last, if it hasn't been created yet.
    if (base_id == update_id)
        update_id++;

    return content_path + fmt::format("{:08x}.tmd", (update ? update_id : base_id));
}

std::string GetTitleContentPath(Service::FS::MediaType media_type, u64 tid, std::size_t index,
                                bool update) {

    if (media_type == Service::FS::MediaType::GameCard) {
        // TODO(B3N30): check if TID matches
        auto fs_user =
            Core::System::GetInstance().ServiceManager().GetService<Service::FS::FS_USER>(
                "fs:USER");
        return fs_user->GetCurrentGamecardPath();
    }

    std::string content_path = GetTitlePath(media_type, tid) + "content/";

    std::string tmd_path = GetTitleMetadataPath(media_type, tid, update);

    u32 content_id = 0;
    FileSys::TitleMetadata tmd;
    if (tmd.Load(tmd_path) == Loader::ResultStatus::Success) {
        if (index < tmd.GetContentCount()) {
            content_id = tmd.GetContentIDByIndex(index);
        } else {
            LOG_ERROR(Service_AM, "Attempted to get path for non-existent content index {:04x}.",
                      index);
            return "";
        }

        // TODO(shinyquagsire23): how does DLC actually get this folder on hardware?
        // For now, check if the second (index 1) content has the optional flag set, for most
        // apps this is usually the manual and not set optional, DLC has it set optional.
        // All .apps (including index 0) will be in the 00000000/ folder for DLC.
        if (tmd.GetContentCount() > 1 &&
            tmd.GetContentTypeByIndex(1) & FileSys::TMDContentTypeFlag::Optional) {
            content_path += "00000000/";
        }
    }

    return fmt::format("{}{:08x}.app", content_path, content_id);
}

std::string GetTitlePath(Service::FS::MediaType media_type, u64 tid) {
    // TODO(PabloMK7) TWL titles should be in TWL Nand. Assuming CTR Nand for now.

    u32 high = static_cast<u32>(tid >> 32);
    u32 low = static_cast<u32>(tid & 0xFFFFFFFF);

    if (media_type == Service::FS::MediaType::NAND || media_type == Service::FS::MediaType::SDMC)
        return fmt::format("{}{:08x}/{:08x}/", GetMediaTitlePath(media_type), high, low);

    if (media_type == Service::FS::MediaType::GameCard) {
        // TODO(B3N30): check if TID matches
        auto fs_user =
            Core::System::GetInstance().ServiceManager().GetService<Service::FS::FS_USER>(
                "fs:USER");
        return fs_user->GetCurrentGamecardPath();
    }

    return "";
}

std::string GetMediaTitlePath(Service::FS::MediaType media_type) {
    if (media_type == Service::FS::MediaType::NAND)
        return fmt::format("{}{}/title/", FileUtil::GetUserPath(FileUtil::UserPath::NANDDir),
                           SYSTEM_ID);

    if (media_type == Service::FS::MediaType::SDMC)
        return fmt::format("{}Nintendo 3DS/{}/{}/title/",
                           FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir), SYSTEM_ID,
                           SDCARD_ID);

    if (media_type == Service::FS::MediaType::GameCard) {
        // TODO(B3N30): check if TID matchess
        auto fs_user =
            Core::System::GetInstance().ServiceManager().GetService<Service::FS::FS_USER>(
                "fs:USER");
        return fs_user->GetCurrentGamecardPath();
    }

    return "";
}

void Module::ScanForTickets() {
    if (Settings::values.deterministic_async_operations) {
        ScanForTicketsImpl();
    } else {
        scan_tickets_future = std::async([this]() {
            std::scoped_lock lock(am_lists_mutex);
            ScanForTicketsImpl();
        });
    }
}

void Module::ScanForTicketsImpl() {
    am_ticket_list.clear();

    LOG_DEBUG(Service_AM, "Starting ticket scan");

    std::string ticket_path = GetTicketDirectory();

    FileUtil::FSTEntry entries;
    FileUtil::ScanDirectoryTree(ticket_path, entries, 0, &stop_scan_flag);
    for (const FileUtil::FSTEntry& ticket : entries.children) {
        if (stop_scan_flag)
            break;
        if (ticket.virtualName.ends_with(".tik")) {
            std::string file_name = ticket.virtualName.substr(0, ticket.virtualName.size() - 4);
            auto pos = file_name.find('.');
            if (pos != file_name.npos) {
                std::string title_id_str = file_name.substr(0, pos);
                std::string ticket_id_str = file_name.substr(pos + 1);
                try {
                    u64 title_id = std::stoull(title_id_str, nullptr, 16);
                    u64 ticket_id = std::stoull(ticket_id_str, nullptr, 16);
                    am_ticket_list.insert(std::make_pair(title_id, ticket_id));
                } catch (...) {
                }
            }
        }
    }
    LOG_DEBUG(Service_AM, "Finished ticket scan");
}

void Module::ScanForTitles(Service::FS::MediaType media_type) {
    if (Settings::values.deterministic_async_operations) {
        ScanForTitlesImpl(media_type);
    } else {
        scan_titles_future = std::async([this, media_type]() {
            std::scoped_lock lock(am_lists_mutex);
            ScanForTitlesImpl(media_type);
        });
    }
}

void Module::ScanForTitlesImpl(Service::FS::MediaType media_type) {
    am_title_list[static_cast<u32>(media_type)].clear();

    LOG_DEBUG(Service_AM, "Starting title scan for media_type={}", static_cast<int>(media_type));

    std::string title_path = GetMediaTitlePath(media_type);

    FileUtil::FSTEntry entries;
    FileUtil::ScanDirectoryTree(title_path, entries, 1, &stop_scan_flag);
    for (const FileUtil::FSTEntry& tid_high : entries.children) {
        if (stop_scan_flag) {
            break;
        }
        for (const FileUtil::FSTEntry& tid_low : tid_high.children) {
            if (stop_scan_flag) {
                break;
            }
            std::string tid_string = tid_high.virtualName + tid_low.virtualName;

            if (tid_string.length() == TITLE_ID_VALID_LENGTH) {
                const u64 tid = std::stoull(tid_string, nullptr, 16);

                if (tid & TWL_TITLE_ID_FLAG) {
                    // TODO(PabloMK7) Move to TWL Nand, for now only check that
                    // the contents exists in CTR Nand as this is a SRL file
                    // instead of NCCH.
                    if (FileUtil::Exists(GetTitleContentPath(media_type, tid))) {
                        am_title_list[static_cast<u32>(media_type)].push_back(tid);
                    }
                } else {
                    FileSys::NCCHContainer container(GetTitleContentPath(media_type, tid));
                    if (container.Load() == Loader::ResultStatus::Success) {
                        am_title_list[static_cast<u32>(media_type)].push_back(tid);
                    }
                }
            }
        }
    }
    LOG_DEBUG(Service_AM, "Finished title scan for media_type={}", static_cast<int>(media_type));
}

void Module::ScanForAllTitles() {
    if (Settings::values.deterministic_async_operations) {
        ScanForTicketsImpl();
        ScanForTitlesImpl(Service::FS::MediaType::NAND);
        ScanForTitlesImpl(Service::FS::MediaType::SDMC);
    } else {
        scan_all_future = std::async([this]() {
            std::scoped_lock lock(am_lists_mutex);
            ScanForTicketsImpl();
            if (!stop_scan_flag) {
                ScanForTitlesImpl(Service::FS::MediaType::NAND);
            }
            if (!stop_scan_flag) {
                ScanForTitlesImpl(Service::FS::MediaType::SDMC);
            }
        });
    }
}

Module::Interface::Interface(std::shared_ptr<Module> am, const char* name, u32 max_session)
    : ServiceFramework(name, max_session), am(std::move(am)) {}

Module::Interface::~Interface() = default;

void Module::Interface::GetNumPrograms(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    u8 media_type = rp.Pop<u8>();

    LOG_DEBUG(Service_AM, "media_type={}", media_type);

    if (artic_client.get()) {
        struct AsyncData {
            u8 media_type;

            ResultVal<s32> res;
        };
        auto async_data = std::make_shared<AsyncData>();
        async_data->media_type = media_type;

        ctx.RunAsync(
            [this, async_data](Kernel::HLERequestContext& ctx) {
                auto req = artic_client->NewRequest("AM_GetTitleCount");

                req.AddParameterU8(async_data->media_type);

                auto resp = artic_client->Send(req);

                if (!resp.has_value() || !resp->Succeeded()) {
                    async_data->res = Result(-1);
                    return 0;
                }

                auto res = Result(static_cast<u32>(resp->GetMethodResult()));
                if (res.IsError()) {
                    async_data->res = res;
                    return 0;
                }

                auto count = resp->GetResponseS32(0);
                if (!count.has_value()) {
                    async_data->res = Result(-1);
                    return 0;
                }

                async_data->res = *count;
                return 0;
            },
            [async_data](Kernel::HLERequestContext& ctx) {
                IPC::RequestBuilder rb(ctx, 2, 0);

                rb.Push(async_data->res.Code());
                rb.Push<u32>(
                    static_cast<u32>(async_data->res.Succeeded() ? async_data->res.Unwrap() : 0));
            },
            true);
    } else {
        std::scoped_lock lock(am->am_lists_mutex);
        IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
        rb.Push(ResultSuccess);
        rb.Push<u32>(static_cast<u32>(am->am_title_list[media_type].size()));
    }
}

void Module::Interface::FindDLCContentInfos(Kernel::HLERequestContext& ctx) {

    IPC::RequestParser rp(ctx);
    auto media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    u64 title_id = rp.Pop<u64>();
    u32 content_count = rp.Pop<u32>();
    auto& content_requested_in = rp.PopMappedBuffer();

    LOG_DEBUG(Service_AM, "title_id={:016X}", title_id);

    if (artic_client.get()) {
        struct AsyncData {
            u8 media_type;
            u64 title_id;
            std::vector<u16> content_requested;

            Result res{0};
            std::vector<u8> out;
            Kernel::MappedBuffer* content_info_out;
        };
        auto async_data = std::make_shared<AsyncData>();
        async_data->media_type = static_cast<u8>(media_type);
        async_data->title_id = title_id;
        async_data->content_requested.resize(content_count);
        content_requested_in.Read(async_data->content_requested.data(), 0,
                                  content_count * sizeof(u16));
        async_data->content_info_out = &rp.PopMappedBuffer();

        ctx.RunAsync(
            [this, async_data](Kernel::HLERequestContext& ctx) {
                auto req = artic_client->NewRequest("AMAPP_FindDLCContentInfos");

                req.AddParameterU8(async_data->media_type);
                req.AddParameterU64(async_data->title_id);
                req.AddParameterBuffer(async_data->content_requested.data(),
                                       async_data->content_requested.size() * sizeof(u16));

                auto resp = artic_client->Send(req);

                if (!resp.has_value() || !resp->Succeeded()) {
                    async_data->res = Result(-1);
                    return 0;
                }

                auto res = Result(static_cast<u32>(resp->GetMethodResult()));
                if (res.IsError()) {
                    async_data->res = res;
                    return 0;
                }

                auto content_info = resp->GetResponseBuffer(0);
                if (!content_info.has_value()) {
                    async_data->res = Result(-1);
                    return 0;
                }

                async_data->out.resize(content_info->second);
                memcpy(async_data->out.data(), content_info->first, content_info->second);
                return 0;
            },
            [async_data](Kernel::HLERequestContext& ctx) {
                if (async_data->res.IsSuccess()) {
                    async_data->content_info_out->Write(async_data->out.data(), 0,
                                                        async_data->out.size());
                }
                IPC::RequestBuilder rb(ctx, 1, 0);
                rb.Push(async_data->res);
            },
            true);
    } else {

        struct AsyncData {
            Service::FS::MediaType media_type;
            u64 title_id;
            std::vector<u16> content_requested;

            Result res{0};
            std::vector<ContentInfo> out_vec;
            Kernel::MappedBuffer* content_info_out;
        };
        auto async_data = std::make_shared<AsyncData>();
        async_data->media_type = media_type;
        async_data->title_id = title_id;
        async_data->content_requested.resize(content_count);
        content_requested_in.Read(async_data->content_requested.data(), 0,
                                  content_count * sizeof(u16));
        async_data->content_info_out = &rp.PopMappedBuffer();

        ctx.RunAsync(
            [this, async_data](Kernel::HLERequestContext& ctx) {
                // Validate that only DLC TIDs are passed in
                u32 tid_high = static_cast<u32>(async_data->title_id >> 32);
                if (tid_high != TID_HIGH_DLC) {
                    async_data->res = Result(ErrCodes::InvalidTIDInList, ErrorModule::AM,
                                             ErrorSummary::InvalidArgument, ErrorLevel::Usage);
                    return 0;
                }

                std::string tmd_path =
                    GetTitleMetadataPath(async_data->media_type, async_data->title_id);

                // In normal circumstances, if there is no ticket we shouldn't be able to have
                // any contents either. However to keep compatibility with older emulator builds,
                // we give rights anyway if the ticket is not installed.
                bool has_ticket = false;
                FileSys::Ticket ticket;
                std::scoped_lock lock(am->am_lists_mutex);
                auto entries = am->am_ticket_list.find(async_data->title_id);
                if (entries != am->am_ticket_list.end() &&
                    ticket.Load(async_data->title_id, (*entries).second) ==
                        Loader::ResultStatus::Success) {
                    has_ticket = true;
                }

                FileSys::TitleMetadata tmd;
                if (tmd.Load(tmd_path) == Loader::ResultStatus::Success) {
                    // Get info for each content index requested
                    for (std::size_t i = 0; i < async_data->content_requested.size(); i++) {
                        u16_le index = async_data->content_requested[i];
                        if (index >= tmd.GetContentCount()) {
                            LOG_ERROR(
                                Service_AM,
                                "Attempted to get info for non-existent content index {:04x}.",
                                index);

                            async_data->res = Result(0xFFFFFFFF);
                            return 0;
                        }

                        ContentInfo content_info = {};
                        content_info.index = index;
                        content_info.type = tmd.GetContentTypeByIndex(index);
                        content_info.content_id = tmd.GetContentIDByIndex(index);
                        content_info.size = tmd.GetContentSizeByIndex(index);
                        content_info.ownership =
                            (!has_ticket || ticket.HasRights(index)) ? OWNERSHIP_OWNED : 0;

                        if (FileUtil::Exists(GetTitleContentPath(async_data->media_type,
                                                                 async_data->title_id, index))) {
                            bool pending = false;
                            for (auto& import_ctx : am->import_content_contexts) {
                                if (import_ctx.first == async_data->title_id &&
                                    import_ctx.second.index == index &&
                                    (import_ctx.second.state ==
                                         ImportTitleContextState::WAITING_FOR_IMPORT ||
                                     import_ctx.second.state ==
                                         ImportTitleContextState::WAITING_FOR_COMMIT ||
                                     import_ctx.second.state ==
                                         ImportTitleContextState::RESUMABLE)) {
                                    LOG_DEBUG(Service_AM, "content pending commit index={:016X}",
                                              i);
                                    pending = true;
                                    break;
                                }
                            }
                            if (!pending) {
                                content_info.ownership |= OWNERSHIP_DOWNLOADED;
                            }
                        }

                        async_data->out_vec.push_back(content_info);
                    }
                }

                return 0;
            },
            [async_data](Kernel::HLERequestContext& ctx) {
                IPC::RequestBuilder rb(ctx, 2, 0);
                rb.Push(async_data->res);
                if (async_data->res.IsSuccess()) {
                    async_data->content_info_out->Write(async_data->out_vec.data(), 0,
                                                        async_data->out_vec.size() *
                                                            sizeof(ContentInfo));
                }
            },
            true);
    }
}

void Module::Interface::ListDLCContentInfos(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    u32 content_count = rp.Pop<u32>();
    auto media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    u64 title_id = rp.Pop<u64>();
    u32 start_index = rp.Pop<u32>();

    LOG_DEBUG(Service_AM, "title_id={:016X}", title_id);

    if (artic_client.get()) {
        struct AsyncData {
            u8 media_type;
            u64 title_id;
            u32 content_count;
            u32 start_index;

            Result res{0};
            std::vector<u8> out;
            Kernel::MappedBuffer* content_info_out;
        };
        auto async_data = std::make_shared<AsyncData>();
        async_data->media_type = static_cast<u8>(media_type);
        async_data->title_id = title_id;
        async_data->content_count = content_count;
        async_data->start_index = start_index;
        async_data->content_info_out = &rp.PopMappedBuffer();

        ctx.RunAsync(
            [this, async_data](Kernel::HLERequestContext& ctx) {
                auto req = artic_client->NewRequest("AMAPP_ListDLCContentInfos");

                req.AddParameterU32(async_data->content_count);
                req.AddParameterU8(async_data->media_type);
                req.AddParameterU64(async_data->title_id);
                req.AddParameterU32(async_data->start_index);

                auto resp = artic_client->Send(req);

                if (!resp.has_value() || !resp->Succeeded()) {
                    async_data->res = Result(-1);
                    return 0;
                }

                auto res = Result(static_cast<u32>(resp->GetMethodResult()));
                if (res.IsError()) {
                    async_data->res = res;
                    return 0;
                }

                auto content_info = resp->GetResponseBuffer(0);
                if (!content_info.has_value()) {
                    async_data->res = Result(-1);
                    return 0;
                }

                async_data->out.resize(content_info->second);
                memcpy(async_data->out.data(), content_info->first, content_info->second);
                return 0;
            },
            [async_data](Kernel::HLERequestContext& ctx) {
                if (async_data->res.IsSuccess()) {
                    async_data->content_info_out->Write(async_data->out.data(), 0,
                                                        async_data->out.size());
                }
                IPC::RequestBuilder rb(ctx, 2, 0);
                rb.Push(async_data->res);
                rb.Push<u32>(static_cast<u32>(async_data->out.size() / sizeof(ContentInfo)));
            },
            true);
    } else {

        struct AsyncData {
            Service::FS::MediaType media_type;
            u64 title_id;
            u32 content_count;
            u32 start_index;

            Result res{0};
            std::vector<ContentInfo> out_vec;
            Kernel::MappedBuffer* content_info_out;
        };
        auto async_data = std::make_shared<AsyncData>();
        async_data->media_type = media_type;
        async_data->title_id = title_id;
        async_data->content_count = content_count;
        async_data->start_index = start_index;
        async_data->content_info_out = &rp.PopMappedBuffer();

        ctx.RunAsync(
            [this, async_data](Kernel::HLERequestContext& ctx) {
                // Validate that only DLC TIDs are passed in
                u32 tid_high = static_cast<u32>(async_data->title_id >> 32);
                if (tid_high != TID_HIGH_DLC) {
                    async_data->res = Result(ErrCodes::InvalidTIDInList, ErrorModule::AM,
                                             ErrorSummary::InvalidArgument, ErrorLevel::Usage);
                    return 0;
                }

                std::string tmd_path =
                    GetTitleMetadataPath(async_data->media_type, async_data->title_id);

                // In normal circumstances, if there is no ticket we shouldn't be able to have
                // any contents either. However to keep compatibility with older emulator builds,
                // we give rights anyway if the ticket is not installed.
                bool has_ticket = false;
                FileSys::Ticket ticket;
                std::scoped_lock lock(am->am_lists_mutex);
                auto entries = am->am_ticket_list.find(async_data->title_id);
                if (entries != am->am_ticket_list.end() &&
                    ticket.Load(async_data->title_id, (*entries).second) ==
                        Loader::ResultStatus::Success) {
                    has_ticket = true;
                }

                FileSys::TitleMetadata tmd;
                if (tmd.Load(tmd_path) == Loader::ResultStatus::Success) {
                    u32 end_index = std::min(async_data->start_index + async_data->content_count,
                                             static_cast<u32>(tmd.GetContentCount()));
                    for (u32 i = async_data->start_index; i < end_index; i++) {
                        ContentInfo content_info = {};
                        content_info.index = static_cast<u16>(i);
                        content_info.type = tmd.GetContentTypeByIndex(i);
                        content_info.content_id = tmd.GetContentIDByIndex(i);
                        content_info.size = tmd.GetContentSizeByIndex(i);
                        content_info.ownership =
                            (!has_ticket || ticket.HasRights(static_cast<u16>(i))) ? OWNERSHIP_OWNED
                                                                                   : 0;

                        if (FileUtil::Exists(GetTitleContentPath(async_data->media_type,
                                                                 async_data->title_id, i))) {
                            bool pending = false;
                            for (auto& import_ctx : am->import_content_contexts) {
                                if (import_ctx.first == async_data->title_id &&
                                    import_ctx.second.index == i &&
                                    (import_ctx.second.state ==
                                         ImportTitleContextState::WAITING_FOR_IMPORT ||
                                     import_ctx.second.state ==
                                         ImportTitleContextState::WAITING_FOR_COMMIT ||
                                     import_ctx.second.state ==
                                         ImportTitleContextState::RESUMABLE)) {
                                    LOG_DEBUG(Service_AM, "content pending commit index={:016X}",
                                              i);
                                    pending = true;
                                    break;
                                }
                            }
                            if (!pending) {
                                content_info.ownership |= OWNERSHIP_DOWNLOADED;
                            }
                        }

                        async_data->out_vec.push_back(content_info);
                    }
                }
                return 0;
            },
            [async_data](Kernel::HLERequestContext& ctx) {
                IPC::RequestBuilder rb(ctx, 2, 0);
                rb.Push(async_data->res);
                rb.Push(static_cast<u32>(async_data->out_vec.size()));
                if (async_data->res.IsSuccess()) {
                    async_data->content_info_out->Write(async_data->out_vec.data(), 0,
                                                        async_data->out_vec.size() *
                                                            sizeof(ContentInfo));
                }
            },
            true);
    }
}

void Module::Interface::DeleteContents(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    u8 media_type = rp.Pop<u8>();
    u64 title_id = rp.Pop<u64>();
    u32 content_count = rp.Pop<u32>();
    auto& content_ids_in = rp.PopMappedBuffer();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess);
    rb.PushMappedBuffer(content_ids_in);
    LOG_WARNING(Service_AM, "(STUBBED) media_type={}, title_id=0x{:016x}, content_count={}",
                media_type, title_id, content_count);
}

void Module::Interface::GetProgramList(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    u32 count = rp.Pop<u32>();
    u8 media_type = rp.Pop<u8>();

    LOG_DEBUG(Service_AM, "media_type={}", media_type);

    if (artic_client.get()) {
        struct AsyncData {
            u32 count;
            u8 media_type;

            Result res{0};
            std::vector<u8> out;
            Kernel::MappedBuffer* title_ids_output;
        };
        auto async_data = std::make_shared<AsyncData>();
        async_data->count = count;
        async_data->media_type = media_type;
        async_data->title_ids_output = &rp.PopMappedBuffer();

        ctx.RunAsync(
            [this, async_data](Kernel::HLERequestContext& ctx) {
                auto req = artic_client->NewRequest("AM_GetTitleList");

                req.AddParameterU32(async_data->count);
                req.AddParameterU8(async_data->media_type);

                auto resp = artic_client->Send(req);

                if (!resp.has_value() || !resp->Succeeded()) {
                    async_data->res = Result(-1);
                    return 0;
                }

                auto res = Result(static_cast<u32>(resp->GetMethodResult()));
                if (res.IsError()) {
                    async_data->res = res;
                    return 0;
                }
                async_data->res = res;

                auto title_ids = resp->GetResponseBuffer(0);
                if (!title_ids.has_value()) {
                    async_data->res = Result(-1);
                    return 0;
                }

                async_data->out.resize(title_ids->second);
                memcpy(async_data->out.data(), title_ids->first, title_ids->second);
                return 0;
            },
            [async_data](Kernel::HLERequestContext& ctx) {
                if (!async_data->res.IsSuccess()) {
                    IPC::RequestBuilder rb(ctx, 2, 0);
                    rb.Push(async_data->res);
                    rb.Push<u32>(0);
                } else {
                    async_data->title_ids_output->Write(async_data->out.data(), 0,
                                                        async_data->out.size());

                    IPC::RequestBuilder rb(ctx, 2, 0);
                    rb.Push(async_data->res);
                    rb.Push<u32>(static_cast<u32>(async_data->out.size() / sizeof(u64)));
                }
            },
            true);

    } else {
        auto& title_ids_output = rp.PopMappedBuffer();

        if (media_type > 2) {
            IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
            rb.Push<u32>(-1); // TODO(shinyquagsire23): Find the right error code
            rb.Push<u32>(0);
            return;
        }

        std::scoped_lock lock(am->am_lists_mutex);
        u32 media_count = static_cast<u32>(am->am_title_list[media_type].size());
        u32 copied = std::min(media_count, count);

        title_ids_output.Write(am->am_title_list[media_type].data(), 0, copied * sizeof(u64));

        IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
        rb.Push(ResultSuccess);
        rb.Push(copied);
    }
}

Result GetTitleInfoFromList(std::span<const u64> title_id_list, Service::FS::MediaType media_type,
                            std::vector<TitleInfo>& title_info_out) {
    title_info_out.reserve(title_id_list.size());
    for (u32 i = 0; i < title_id_list.size(); i++) {
        std::string tmd_path = GetTitleMetadataPath(media_type, title_id_list[i]);

        TitleInfo title_info = {};
        title_info.tid = title_id_list[i];

        FileSys::TitleMetadata tmd;
        if (tmd.Load(tmd_path) == Loader::ResultStatus::Success) {
            // TODO(shinyquagsire23): This is the total size of all files this process owns,
            // including savefiles and other content. This comes close but is off.
            title_info.size = tmd.GetContentSizeByIndex(FileSys::TMDContentIndex::Main);
            title_info.version = tmd.GetTitleVersion();
            title_info.type = tmd.GetTitleType();
        } else {
            LOG_DEBUG(Service_AM, "not found title_id={:016X}", title_id_list[i]);
            return Result(ErrorDescription::NotFound, ErrorModule::AM, ErrorSummary::InvalidState,
                          ErrorLevel::Permanent);
        }
        LOG_DEBUG(Service_AM, "found title_id={:016X} version={:04X}", title_id_list[i],
                  title_info.version);
        title_info_out.push_back(title_info);
    }

    return ResultSuccess;
}

void Module::Interface::GetProgramInfosImpl(Kernel::HLERequestContext& ctx, bool ignore_platform) {
    IPC::RequestParser rp(ctx);

    auto media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    u32 title_count = rp.Pop<u32>();

    LOG_DEBUG(Service_AM, "media_type={}, ignore_platform={}", media_type, ignore_platform);

    if (artic_client.get()) {
        struct AsyncData {
            u8 media_type;
            bool ignore_platform;
            std::vector<u64> title_id_list;

            Result res{0};
            std::vector<TitleInfo> out;
            Kernel::MappedBuffer* title_id_list_buffer;
            Kernel::MappedBuffer* title_info_out;
        };
        auto async_data = std::make_shared<AsyncData>();
        async_data->media_type = static_cast<u8>(media_type);
        async_data->ignore_platform = ignore_platform;
        async_data->title_id_list.resize(title_count);
        async_data->title_id_list_buffer = &rp.PopMappedBuffer();
        async_data->title_id_list_buffer->Read(async_data->title_id_list.data(), 0,
                                               title_count * sizeof(u64));
        async_data->title_info_out = &rp.PopMappedBuffer();

        ctx.RunAsync(
            [this, async_data](Kernel::HLERequestContext& ctx) {
                auto req = artic_client->NewRequest("AM_GetTitleInfo");

                req.AddParameterU8(async_data->media_type);
                req.AddParameterBuffer(async_data->title_id_list.data(),
                                       async_data->title_id_list.size() * sizeof(u64));
                req.AddParameterU8(async_data->ignore_platform ? 1 : 0);

                auto resp = artic_client->Send(req);

                if (!resp.has_value() || !resp->Succeeded()) {
                    async_data->res = Result(-1);
                    return 0;
                }

                auto res = Result(static_cast<u32>(resp->GetMethodResult()));
                if (res.IsError()) {
                    async_data->res = res;
                    return 0;
                }
                async_data->res = res;

                auto title_infos = resp->GetResponseBuffer(0);
                if (!title_infos.has_value()) {
                    async_data->res = Result(-1);
                    return 0;
                }

                async_data->out.resize(title_infos->second / sizeof(TitleInfo));
                memcpy(async_data->out.data(), title_infos->first, title_infos->second);
                return 0;
            },
            [async_data](Kernel::HLERequestContext& ctx) {
                if (!async_data->res.IsSuccess()) {
                    IPC::RequestBuilder rb(ctx, 1, async_data->ignore_platform ? 0 : 4);
                    rb.Push(async_data->res);
                    if (!async_data->ignore_platform) {
                        rb.PushMappedBuffer(*async_data->title_id_list_buffer);
                        rb.PushMappedBuffer(*async_data->title_info_out);
                    }
                } else {
                    async_data->title_info_out->Write(async_data->out.data(), 0,
                                                      async_data->out.size() / sizeof(TitleInfo));

                    IPC::RequestBuilder rb(ctx, 1, async_data->ignore_platform ? 0 : 4);
                    rb.Push(async_data->res);
                    if (!async_data->ignore_platform) {
                        rb.PushMappedBuffer(*async_data->title_id_list_buffer);
                        rb.PushMappedBuffer(*async_data->title_info_out);
                    }
                }
            },
            true);

    } else {
        struct AsyncData {
            Service::FS::MediaType media_type;
            std::vector<u64> title_id_list;

            Result res{0};
            std::vector<TitleInfo> out;
            Kernel::MappedBuffer* title_id_list_buffer;
            Kernel::MappedBuffer* title_info_out;
        };
        auto async_data = std::make_shared<AsyncData>();
        async_data->media_type = media_type;
        async_data->title_id_list.resize(title_count);
        async_data->title_id_list_buffer = &rp.PopMappedBuffer();
        async_data->title_id_list_buffer->Read(async_data->title_id_list.data(), 0,
                                               title_count * sizeof(u64));
        async_data->title_info_out = &rp.PopMappedBuffer();

        ctx.RunAsync(
            [this, async_data](Kernel::HLERequestContext& ctx) {
                // nim checks if the current importing title already exists during installation.
                // Normally, since the program wouldn't be commited, getting the title info returns
                // not found. However, since GetTitleInfoFromList does not care if the program was
                // commited and only checks for the tmd, it will detect the title and return
                // information while it shouldn't. To prevent this, we check if the importing
                // context is present and not committed. If that's the case, return not found
                for (auto tid : async_data->title_id_list) {
                    for (auto& import_ctx : am->import_title_contexts) {
                        if (import_ctx.first == tid &&
                            (import_ctx.second.state ==
                                 ImportTitleContextState::WAITING_FOR_IMPORT ||
                             import_ctx.second.state ==
                                 ImportTitleContextState::WAITING_FOR_COMMIT ||
                             import_ctx.second.state == ImportTitleContextState::RESUMABLE)) {
                            LOG_DEBUG(Service_AM, "title pending commit title_id={:016X}", tid);
                            async_data->res =
                                Result(ErrorDescription::NotFound, ErrorModule::AM,
                                       ErrorSummary::InvalidState, ErrorLevel::Permanent);
                        }
                    }
                }

                if (async_data->res.IsSuccess()) {
                    async_data->res = GetTitleInfoFromList(async_data->title_id_list,
                                                           async_data->media_type, async_data->out);
                }
                return 0;
            },
            [async_data](Kernel::HLERequestContext& ctx) {
                if (async_data->res.IsSuccess()) {
                    async_data->title_info_out->Write(async_data->out.data(), 0,
                                                      async_data->out.size() * sizeof(TitleInfo));
                }
                IPC::RequestBuilder rb(ctx, 1, 4);
                rb.Push(async_data->res);
                rb.PushMappedBuffer(*async_data->title_id_list_buffer);
                rb.PushMappedBuffer(*async_data->title_info_out);
            },
            true);
    }
}

void Module::Interface::GetProgramInfos(Kernel::HLERequestContext& ctx) {
    GetProgramInfosImpl(ctx, false);
}

void Module::Interface::GetProgramInfosIgnorePlatform(Kernel::HLERequestContext& ctx) {
    GetProgramInfosImpl(ctx, true);
}

void Module::Interface::DeleteUserProgram(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    auto media_type = rp.PopEnum<FS::MediaType>();
    u64 title_id = rp.Pop<u64>();

    LOG_DEBUG(Service_AM, "title_id={:016X}", title_id);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    u16 category = static_cast<u16>((title_id >> 32) & 0xFFFF);
    u8 variation = static_cast<u8>(title_id & 0xFF);
    if (category & CATEGORY_SYSTEM || category & CATEGORY_DLP || variation & VARIATION_SYSTEM) {
        LOG_ERROR(Service_AM, "Trying to uninstall system app");
        rb.Push(Result(ErrCodes::TryingToUninstallSystemApp, ErrorModule::AM,
                       ErrorSummary::InvalidArgument, ErrorLevel::Usage));
        return;
    }
    LOG_INFO(Service_AM, "Deleting title 0x{:016x}", title_id);
    std::string path = GetTitlePath(media_type, title_id);
    if (!FileUtil::Exists(path)) {
        rb.Push(Result(ErrorDescription::NotFound, ErrorModule::AM, ErrorSummary::InvalidState,
                       ErrorLevel::Permanent));
        LOG_ERROR(Service_AM, "Title not found");
        return;
    }
    bool success = FileUtil::DeleteDirRecursively(path);
    am->ScanForAllTitles();
    rb.Push(ResultSuccess);
    if (!success)
        LOG_ERROR(Service_AM, "FileUtil::DeleteDirRecursively unexpectedly failed");
}

void Module::Interface::GetProductCode(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    FS::MediaType media_type = rp.PopEnum<FS::MediaType>();
    u64 title_id = rp.Pop<u64>();
    std::string path = GetTitleContentPath(media_type, title_id);

    LOG_DEBUG(Service_AM, "title_id={:016X}", title_id);

    if (!FileUtil::Exists(path)) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrorDescription::NotFound, ErrorModule::AM, ErrorSummary::InvalidState,
                       ErrorLevel::Permanent));
    } else {
        struct ProductCode {
            u8 code[0x10];
        };

        ProductCode product_code;

        IPC::RequestBuilder rb = rp.MakeBuilder(6, 0);
        FileSys::NCCHContainer ncch(path);
        ncch.Load();
        std::memcpy(&product_code.code, &ncch.ncch_header.product_code, 0x10);
        rb.Push(ResultSuccess);
        rb.PushRaw(product_code);
    }
}

void Module::Interface::GetDLCTitleInfos(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    auto media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    u32 title_count = rp.Pop<u32>();

    LOG_DEBUG(Service_AM, "media_type={}", media_type);

    if (artic_client.get()) {
        struct AsyncData {
            u8 media_type;
            std::vector<u64> title_id_list;

            Result res{0};
            std::vector<u8> out;
            Kernel::MappedBuffer* title_id_list_buffer;
            Kernel::MappedBuffer* title_info_out;
        };
        auto async_data = std::make_shared<AsyncData>();
        async_data->media_type = static_cast<u8>(media_type);
        async_data->title_id_list.resize(title_count);
        async_data->title_id_list_buffer = &rp.PopMappedBuffer();
        async_data->title_id_list_buffer->Read(async_data->title_id_list.data(), 0,
                                               title_count * sizeof(u64));
        async_data->title_info_out = &rp.PopMappedBuffer();

        ctx.RunAsync(
            [this, async_data](Kernel::HLERequestContext& ctx) {
                auto req = artic_client->NewRequest("AMAPP_GetDLCTitleInfos");

                req.AddParameterU8(async_data->media_type);
                req.AddParameterBuffer(async_data->title_id_list.data(),
                                       async_data->title_id_list.size() * sizeof(u64));

                auto resp = artic_client->Send(req);

                if (!resp.has_value() || !resp->Succeeded()) {
                    async_data->res = Result(-1);
                    return 0;
                }

                auto res = Result(static_cast<u32>(resp->GetMethodResult()));
                if (res.IsError()) {
                    async_data->res = res;
                    return 0;
                }
                async_data->res = res;

                auto title_infos = resp->GetResponseBuffer(0);
                if (!title_infos.has_value()) {
                    async_data->res = Result(-1);
                    return 0;
                }

                async_data->out.resize(title_infos->second);
                memcpy(async_data->out.data(), title_infos->first, title_infos->second);
                return 0;
            },
            [async_data](Kernel::HLERequestContext& ctx) {
                if (!async_data->res.IsSuccess()) {
                    IPC::RequestBuilder rb(ctx, 1, 4);
                    rb.Push(async_data->res);
                    rb.PushMappedBuffer(*async_data->title_id_list_buffer);
                    rb.PushMappedBuffer(*async_data->title_info_out);
                } else {
                    async_data->title_info_out->Write(async_data->out.data(), 0,
                                                      async_data->out.size());

                    IPC::RequestBuilder rb(ctx, 1, 4);
                    rb.Push(async_data->res);
                    rb.PushMappedBuffer(*async_data->title_id_list_buffer);
                    rb.PushMappedBuffer(*async_data->title_info_out);
                }
            },
            true);
    } else {
        struct AsyncData {
            Service::FS::MediaType media_type;
            std::vector<u64> title_id_list;

            Result res{0};
            std::vector<TitleInfo> out;
            Kernel::MappedBuffer* title_id_list_buffer;
            Kernel::MappedBuffer* title_info_out;
        };
        auto async_data = std::make_shared<AsyncData>();
        async_data->media_type = media_type;
        async_data->title_id_list.resize(title_count);
        async_data->title_id_list_buffer = &rp.PopMappedBuffer();
        async_data->title_id_list_buffer->Read(async_data->title_id_list.data(), 0,
                                               title_count * sizeof(u64));
        async_data->title_info_out = &rp.PopMappedBuffer();

        ctx.RunAsync(
            [this, async_data](Kernel::HLERequestContext& ctx) {
                // Validate that DLC TIDs were passed in
                for (u32 i = 0; i < async_data->title_id_list.size(); i++) {
                    u32 tid_high = static_cast<u32>(async_data->title_id_list[i] >> 32);
                    if (tid_high != TID_HIGH_DLC) {
                        async_data->res = Result(ErrCodes::InvalidTIDInList, ErrorModule::AM,
                                                 ErrorSummary::InvalidArgument, ErrorLevel::Usage);
                        break;
                    }
                }

                // nim checks if the current importing title already exists during installation.
                // Normally, since the program wouldn't be commited, getting the title info returns
                // not found. However, since GetTitleInfoFromList does not care if the program was
                // commited and only checks for the tmd, it will detect the title and return
                // information while it shouldn't. To prevent this, we check if the importing
                // context is present and not committed. If that's the case, return not found
                for (auto tid : async_data->title_id_list) {
                    for (auto& import_ctx : am->import_title_contexts) {
                        if (import_ctx.first == tid &&
                            (import_ctx.second.state ==
                                 ImportTitleContextState::WAITING_FOR_IMPORT ||
                             import_ctx.second.state ==
                                 ImportTitleContextState::WAITING_FOR_COMMIT ||
                             import_ctx.second.state == ImportTitleContextState::RESUMABLE)) {
                            LOG_DEBUG(Service_AM, "title pending commit title_id={:016X}", tid);
                            async_data->res =
                                Result(ErrorDescription::NotFound, ErrorModule::AM,
                                       ErrorSummary::InvalidState, ErrorLevel::Permanent);
                        }
                    }
                }

                if (async_data->res.IsSuccess()) {
                    async_data->res = GetTitleInfoFromList(async_data->title_id_list,
                                                           async_data->media_type, async_data->out);
                }
                return 0;
            },
            [async_data](Kernel::HLERequestContext& ctx) {
                if (async_data->res.IsSuccess()) {
                    async_data->title_info_out->Write(async_data->out.data(), 0,
                                                      async_data->out.size() * sizeof(TitleInfo));
                }
                IPC::RequestBuilder rb(ctx, 1, 4);
                rb.Push(async_data->res);
                rb.PushMappedBuffer(*async_data->title_id_list_buffer);
                rb.PushMappedBuffer(*async_data->title_info_out);
            },
            true);
    }
}

void Module::Interface::GetPatchTitleInfos(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    auto media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    u32 title_count = rp.Pop<u32>();

    LOG_DEBUG(Service_AM, "media_type={}", media_type);

    if (artic_client.get()) {
        struct AsyncData {
            u8 media_type;
            std::vector<u64> title_id_list;

            Result res{0};
            std::vector<TitleInfo> out;
            Kernel::MappedBuffer* title_id_list_buffer;
            Kernel::MappedBuffer* title_info_out;
        };
        auto async_data = std::make_shared<AsyncData>();
        async_data->media_type = static_cast<u8>(media_type);
        async_data->title_id_list.resize(title_count);
        async_data->title_id_list_buffer = &rp.PopMappedBuffer();
        async_data->title_id_list_buffer->Read(async_data->title_id_list.data(), 0,
                                               title_count * sizeof(u64));
        async_data->title_info_out = &rp.PopMappedBuffer();

        ctx.RunAsync(
            [this, async_data](Kernel::HLERequestContext& ctx) {
                auto req = artic_client->NewRequest("AMAPP_GetPatchTitleInfos");

                req.AddParameterU8(async_data->media_type);
                req.AddParameterBuffer(async_data->title_id_list.data(),
                                       async_data->title_id_list.size() * sizeof(u64));

                auto resp = artic_client->Send(req);

                if (!resp.has_value() || !resp->Succeeded()) {
                    async_data->res = Result(-1);
                    return 0;
                }

                auto res = Result(static_cast<u32>(resp->GetMethodResult()));
                if (res.IsError()) {
                    async_data->res = res;
                    return 0;
                }
                async_data->res = res;

                auto title_infos = resp->GetResponseBuffer(0);
                if (!title_infos.has_value()) {
                    async_data->res = Result(-1);
                    return 0;
                }

                async_data->out.resize(title_infos->second / sizeof(TitleInfo));
                memcpy(async_data->out.data(), title_infos->first, title_infos->second);
                return 0;
            },
            [async_data](Kernel::HLERequestContext& ctx) {
                if (!async_data->res.IsSuccess()) {
                    IPC::RequestBuilder rb(ctx, 1, 4);
                    rb.Push(async_data->res);
                    rb.PushMappedBuffer(*async_data->title_id_list_buffer);
                    rb.PushMappedBuffer(*async_data->title_info_out);
                } else {
                    async_data->title_info_out->Write(async_data->out.data(), 0,
                                                      async_data->out.size() * sizeof(TitleInfo));

                    IPC::RequestBuilder rb(ctx, 1, 4);
                    rb.Push(async_data->res);
                    rb.PushMappedBuffer(*async_data->title_id_list_buffer);
                    rb.PushMappedBuffer(*async_data->title_info_out);
                }
            },
            true);
    } else {
        struct AsyncData {
            Service::FS::MediaType media_type;
            std::vector<u64> title_id_list;

            Result res{0};
            std::vector<TitleInfo> out;
            Kernel::MappedBuffer* title_id_list_buffer;
            Kernel::MappedBuffer* title_info_out;
        };
        auto async_data = std::make_shared<AsyncData>();
        async_data->media_type = media_type;
        async_data->title_id_list.resize(title_count);
        async_data->title_id_list_buffer = &rp.PopMappedBuffer();
        async_data->title_id_list_buffer->Read(async_data->title_id_list.data(), 0,
                                               title_count * sizeof(u64));
        async_data->title_info_out = &rp.PopMappedBuffer();

        ctx.RunAsync(
            [this, async_data](Kernel::HLERequestContext& ctx) {
                // Validate that update TIDs were passed in
                for (u32 i = 0; i < async_data->title_id_list.size(); i++) {
                    u32 tid_high = static_cast<u32>(async_data->title_id_list[i] >> 32);
                    if (tid_high != TID_HIGH_UPDATE) {
                        async_data->res = Result(ErrCodes::InvalidTIDInList, ErrorModule::AM,
                                                 ErrorSummary::InvalidArgument, ErrorLevel::Usage);
                        break;
                    }
                }

                // nim checks if the current importing title already exists during installation.
                // Normally, since the program wouldn't be commited, getting the title info returns
                // not found. However, since GetTitleInfoFromList does not care if the program was
                // commited and only checks for the tmd, it will detect the title and return
                // information while it shouldn't. To prevent this, we check if the importing
                // context is present and not committed. If that's the case, return not found
                for (auto tid : async_data->title_id_list) {
                    for (auto& import_ctx : am->import_title_contexts) {
                        if (import_ctx.first == tid &&
                            (import_ctx.second.state ==
                                 ImportTitleContextState::WAITING_FOR_IMPORT ||
                             import_ctx.second.state ==
                                 ImportTitleContextState::WAITING_FOR_COMMIT ||
                             import_ctx.second.state == ImportTitleContextState::RESUMABLE)) {
                            LOG_DEBUG(Service_AM, "title pending commit title_id={:016X}", tid);
                            async_data->res =
                                Result(ErrorDescription::NotFound, ErrorModule::AM,
                                       ErrorSummary::InvalidState, ErrorLevel::Permanent);
                        }
                    }
                }

                if (async_data->res.IsSuccess()) {
                    async_data->res = GetTitleInfoFromList(async_data->title_id_list,
                                                           async_data->media_type, async_data->out);
                }
                return 0;
            },
            [async_data](Kernel::HLERequestContext& ctx) {
                if (async_data->res.IsSuccess()) {
                    async_data->title_info_out->Write(async_data->out.data(), 0,
                                                      async_data->out.size() * sizeof(TitleInfo));
                }
                IPC::RequestBuilder rb(ctx, 1, 4);
                rb.Push(async_data->res);
                rb.PushMappedBuffer(*async_data->title_id_list_buffer);
                rb.PushMappedBuffer(*async_data->title_info_out);
            },
            true);
    }
}

void Module::Interface::ListDataTitleTicketInfos(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    u32 ticket_count = rp.Pop<u32>();
    u64 title_id = rp.Pop<u64>();
    u32 start_index = rp.Pop<u32>();

    if (artic_client.get()) {
        struct AsyncData {
            u64 title_id;
            u32 ticket_count;
            u32 start_index;

            Result res{0};
            std::vector<u8> out;
            Kernel::MappedBuffer* ticket_info_out;
        };
        auto async_data = std::make_shared<AsyncData>();
        async_data->title_id = title_id;
        async_data->ticket_count = ticket_count;
        async_data->start_index = start_index;
        async_data->ticket_info_out = &rp.PopMappedBuffer();

        ctx.RunAsync(
            [this, async_data](Kernel::HLERequestContext& ctx) {
                auto req = artic_client->NewRequest("AMAPP_ListDataTitleTicketInfos");

                req.AddParameterU32(async_data->ticket_count);
                req.AddParameterU64(async_data->title_id);
                req.AddParameterU32(async_data->start_index);

                auto resp = artic_client->Send(req);

                if (!resp.has_value() || !resp->Succeeded()) {
                    async_data->res = Result(-1);
                    return 0;
                }

                auto res = Result(static_cast<u32>(resp->GetMethodResult()));
                if (res.IsError()) {
                    async_data->res = res;
                    return 0;
                }

                auto content_info = resp->GetResponseBuffer(0);
                if (!content_info.has_value()) {
                    async_data->res = Result(-1);
                    return 0;
                }

                async_data->out.resize(content_info->second);
                memcpy(async_data->out.data(), content_info->first, content_info->second);
                return 0;
            },
            [async_data](Kernel::HLERequestContext& ctx) {
                if (async_data->res.IsSuccess()) {
                    async_data->ticket_info_out->Write(async_data->out.data(), 0,
                                                       async_data->out.size());
                }
                IPC::RequestBuilder rb(ctx, 2, 0);
                rb.Push(async_data->res);
                rb.Push<u32>(static_cast<u32>(async_data->out.size() / sizeof(TicketInfo)));
            },
            true);
    } else {
        LOG_DEBUG(Service_AM, "(STUBBED) called, ticket_count={}", ticket_count);

        u32 tid_high = static_cast<u32>(title_id >> 32);
        if (tid_high != 0x0004008C && tid_high != 0x0004000D) {
            LOG_ERROR(Service_AM, "Tried to get infos for non-data title title_id={:016X}",
                      title_id);
            IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
            rb.Push(Result(60, ErrorModule::AM, ErrorSummary::InvalidArgument, ErrorLevel::Usage));
        }

        auto& out_buffer = rp.PopMappedBuffer();

        std::scoped_lock lock(am->am_lists_mutex);
        auto range = am->am_ticket_list.equal_range(title_id);
        auto it = range.first;
        std::advance(it, std::min(static_cast<size_t>(start_index),
                                  static_cast<size_t>(std::distance(range.first, range.second))));

        u32 written = 0;
        for (; it != range.second && written < ticket_count; it++) {
            FileSys::Ticket ticket;
            if (ticket.Load(title_id, it->second) != Loader::ResultStatus::Success)
                continue;

            TicketInfo info = {};
            info.title_id = ticket.GetTitleID();
            info.ticket_id = ticket.GetTicketID();
            info.version = ticket.GetVersion();
            info.size = static_cast<u32>(ticket.GetSerializedSize());

            out_buffer.Write(&info, written * sizeof(TicketInfo), sizeof(TicketInfo));
            written++;
        }

        IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
        rb.Push(ResultSuccess); // No error
        rb.Push(written);
    }
}

void Module::Interface::GetDLCContentInfoCount(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    auto media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    u64 title_id = rp.Pop<u64>();

    LOG_DEBUG(Service_AM, "title_id={:016X}", title_id);

    if (artic_client.get()) {
        struct AsyncData {
            u8 media_type;
            u64 title_id;

            ResultVal<s32> res;
        };
        auto async_data = std::make_shared<AsyncData>();
        async_data->media_type = static_cast<u8>(media_type);
        async_data->title_id = title_id;

        ctx.RunAsync(
            [this, async_data](Kernel::HLERequestContext& ctx) {
                auto req = artic_client->NewRequest("AMAPP_GetDLCContentInfoCount");

                req.AddParameterU8(async_data->media_type);
                req.AddParameterU64(async_data->title_id);

                auto resp = artic_client->Send(req);

                if (!resp.has_value() || !resp->Succeeded()) {
                    async_data->res = Result(-1);
                    return 0;
                }

                auto res = Result(static_cast<u32>(resp->GetMethodResult()));
                if (res.IsError()) {
                    async_data->res = res;
                    return 0;
                }

                auto count = resp->GetResponseS32(0);
                if (!count.has_value()) {
                    async_data->res = Result(-1);
                    return 0;
                }

                async_data->res = *count;
                return 0;
            },
            [async_data](Kernel::HLERequestContext& ctx) {
                IPC::RequestBuilder rb(ctx, 2, 0);

                rb.Push(async_data->res.Code());
                rb.Push<u32>(
                    static_cast<u32>(async_data->res.Succeeded() ? async_data->res.Unwrap() : 0));
            },
            true);
    } else {

        // Validate that only DLC TIDs are passed in
        u32 tid_high = static_cast<u32>(title_id >> 32);
        if (tid_high != TID_HIGH_DLC) {
            IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
            rb.Push(Result(ErrCodes::InvalidTID, ErrorModule::AM, ErrorSummary::InvalidArgument,
                           ErrorLevel::Usage));
            rb.Push<u32>(0);
            return;
        }

        IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
        rb.Push(ResultSuccess); // No error

        std::string tmd_path = GetTitleMetadataPath(media_type, title_id);

        FileSys::TitleMetadata tmd;
        if (tmd.Load(tmd_path) == Loader::ResultStatus::Success) {
            rb.Push<u32>(static_cast<u32>(tmd.GetContentCount()));
        } else {
            rb.Push<u32>(1); // Number of content infos plus one
            LOG_WARNING(Service_AM, "missing TMD media_type={}, title_id=0x{:016x}", media_type,
                        title_id);
        }
    }
}

void Module::Interface::DeleteTicket(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    u64 title_id = rp.Pop<u64>();

    LOG_DEBUG(Service_AM, "title_id={:016X}", title_id);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    std::scoped_lock lock(am->am_lists_mutex);
    auto range = am->am_ticket_list.equal_range(title_id);
    if (range.first == range.second) {
        rb.Push(Result(ErrorDescription::AlreadyDone, ErrorModule::AM, ErrorSummary::Success,
                       ErrorLevel::Success));
        return;
    }
    auto it = range.first;
    for (; it != range.second; it++) {
        auto path = GetTicketPath(title_id, it->second);
        FileUtil::Delete(path);
    }

    am->am_ticket_list.erase(range.first, range.second);

    rb.Push(ResultSuccess);
}

void Module::Interface::GetNumTickets(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    LOG_DEBUG(Service_AM, "");

    std::scoped_lock lock(am->am_lists_mutex);
    u32 ticket_count = static_cast<u32>(am->am_ticket_list.size());

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(ticket_count);
}

void Module::Interface::GetTicketList(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    u32 ticket_list_count = rp.Pop<u32>();
    u32 ticket_index = rp.Pop<u32>();
    auto& ticket_tids_out = rp.PopMappedBuffer();

    LOG_DEBUG(Service_AM, "ticket_list_count={}, ticket_index={}", ticket_list_count, ticket_index);

    u32 tickets_written = 0;
    std::scoped_lock lock(am->am_lists_mutex);
    auto it = am->am_ticket_list.begin();
    std::advance(it, std::min(static_cast<size_t>(ticket_index), am->am_ticket_list.size()));

    for (; it != am->am_ticket_list.end() && tickets_written < ticket_list_count;
         it++, tickets_written++) {
        ticket_tids_out.Write(&it->first, tickets_written * sizeof(u64), sizeof(u64));
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 2);
    rb.Push(ResultSuccess);
    rb.Push(tickets_written);
    rb.PushMappedBuffer(ticket_tids_out);
}

void Module::Interface::GetDeviceID(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    LOG_DEBUG(Service_AM, "");

    const auto& otp = HW::UniqueData::GetOTP();
    if (!otp.Valid()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrorDescription::NotFound, ErrorModule::AM, ErrorSummary::NotFound,
                       ErrorLevel::Permanent));
        return;
    }

    u32 deviceID = otp.GetDeviceID();
    if (am->force_new_device_id) {
        deviceID |= 0x80000000;
    }
    if (am->force_old_device_id) {
        deviceID &= ~0x80000000;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(3, 0);
    rb.Push(ResultSuccess);
    rb.Push(0);
    rb.Push(deviceID);
}

void Module::Interface::GetNumImportTitleContextsImpl(IPC::RequestParser& rp,
                                                      FS::MediaType media_type,
                                                      bool include_installing,
                                                      bool include_finalizing) {

    IPC::RequestBuilder rb = rp.MakeBuilder(3, 0);
    rb.Push(ResultSuccess);

    u32 count = 0;
    for (auto it = am->import_title_contexts.begin(); it != am->import_title_contexts.end(); it++) {
        if ((include_installing &&
             (it->second.state == ImportTitleContextState::WAITING_FOR_IMPORT ||
              it->second.state == ImportTitleContextState::RESUMABLE)) ||
            (include_finalizing &&
             it->second.state == ImportTitleContextState::WAITING_FOR_COMMIT)) {
            count++;
        }
    }

    rb.Push<u32>(count);
}

void Module::Interface::GetImportTitleContextListImpl(IPC::RequestParser& rp,
                                                      FS::MediaType media_type, u32 list_count,
                                                      bool include_installing,
                                                      bool include_finalizing) {

    auto out_buf = rp.PopMappedBuffer();
    u32 written = 0;

    for (auto& key_value : am->import_content_contexts) {
        if ((include_installing &&
             (key_value.second.state == ImportTitleContextState::WAITING_FOR_IMPORT ||
              key_value.second.state == ImportTitleContextState::RESUMABLE)) ||
            (include_finalizing &&
             key_value.second.state == ImportTitleContextState::WAITING_FOR_COMMIT)) {

            out_buf.Write(&key_value.first, written * sizeof(u64), sizeof(u64));
            written++;
            if (written >= list_count)
                break;
        }
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(3, 0);
    rb.Push(ResultSuccess);
    rb.Push(written);
}

void Module::Interface::GetNumImportTitleContexts(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    const FS::MediaType media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());

    GetNumImportTitleContextsImpl(rp, media_type, true, true);

    LOG_WARNING(Service_AM, "(STUBBED) media_type={}", media_type);
}

void Module::Interface::GetImportTitleContextList(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    const u32 list_count = rp.Pop<u32>();
    [[maybe_unused]] const FS::MediaType media_type =
        static_cast<Service::FS::MediaType>(rp.Pop<u8>());

    GetImportTitleContextListImpl(rp, media_type, list_count, true, true);

    LOG_WARNING(Service_AM, "(STUBBED) media_type={}", media_type);
}

void Module::Interface::GetImportTitleContexts(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    const u32 list_count = rp.Pop<u32>();
    [[maybe_unused]] const FS::MediaType media_type =
        static_cast<Service::FS::MediaType>(rp.Pop<u8>());

    auto in_buf = rp.PopMappedBuffer();
    auto out_buf = rp.PopMappedBuffer();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    LOG_WARNING(Service_AM, "(STUBBED) media_type={} list_count={}", media_type, list_count);

    u32 written = 0;
    for (u32 i = 0; i < list_count; i++) {
        u64 title_id;
        in_buf.Read(&title_id, 0, sizeof(title_id));

        LOG_WARNING(Service_AM, "title_id={:016X}", title_id);

        auto it = am->import_title_contexts.find(title_id);
        if (it == am->import_title_contexts.end()) {
            rb.Push(Result(ErrorDescription::NotFound, ErrorModule::AM, ErrorSummary::InvalidState,
                           ErrorLevel::Permanent));
            return;
        } else {
            out_buf.Write(&it->second, written * sizeof(ImportTitleContext),
                          sizeof(ImportTitleContext));
            written++;
        }
    }

    rb.Push(ResultSuccess);
}

void Module::Interface::DeleteImportTitleContext(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const FS::MediaType media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    const u64 title_id = rp.Pop<u64>();

    LOG_WARNING(Service_AM, "(STUBBED) media_type={} title_id={:016X}", media_type, title_id);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    auto range = am->import_title_contexts.equal_range(title_id);
    if (range.first == range.second) {
        rb.Push(Result(ErrorDescription::NotFound, ErrorModule::AM, ErrorSummary::InvalidState,
                       ErrorLevel::Permanent));
        return;
    }

    am->import_title_contexts.erase(title_id);
    rb.Push(ResultSuccess);
}

void Module::Interface::GetNumImportContentContextsImpl(IPC::RequestParser& rp, u64 title_id,
                                                        FS::MediaType media_type) {

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    auto range = am->import_content_contexts.equal_range(title_id);
    rb.Push(static_cast<u32>(std::distance(range.first, range.second)));
}

void Module::Interface::GetImportContentContextListImpl(IPC::RequestParser& rp, u32 list_count,
                                                        u64 title_id, FS::MediaType media_type) {
    auto out_buf = rp.PopMappedBuffer();

    auto range = am->import_content_contexts.equal_range(title_id);

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);

    u32 written = 0;
    for (auto it = range.first; it != range.second && written < list_count; it++, written++) {
        out_buf.Write(&it->second.index, written * sizeof(u16), sizeof(u16));
    }

    rb.Push(written);
}

void Module::Interface::GetImportContentContextsImpl(IPC::RequestParser& rp, u32 list_count,
                                                     u64 title_id, FS::MediaType media_type) {
    auto in_buf = rp.PopMappedBuffer();
    auto out_buf = rp.PopMappedBuffer();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    auto range = am->import_content_contexts.equal_range(title_id);

    for (u32 i = 0; i < list_count; i++) {
        u16 index;
        in_buf.Read(&index, i * sizeof(u16), sizeof(u16));

        LOG_WARNING(Service_AM, "index={}", index);

        auto it = range.first;
        for (; it != range.second; it++)
            if (it->second.index == index)
                break;

        if (it == range.second) {
            rb.Push(Result(ErrorDescription::NotFound, ErrorModule::AM, ErrorSummary::InvalidState,
                           ErrorLevel::Permanent));
            return;
        }

        out_buf.Write(&it->second, i * sizeof(ImportContentContext), sizeof(ImportContentContext));
    }

    rb.Push(ResultSuccess);
}

void Module::Interface::GetNumImportContentContexts(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const FS::MediaType media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    const u64 title_id = rp.Pop<u64>();

    GetNumImportContentContextsImpl(rp, title_id, media_type);

    LOG_WARNING(Service_AM, "(STUBBED) media_type={} title_id={:016X}", media_type, title_id);
}

void Module::Interface::GetImportContentContextList(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 list_count = rp.Pop<u32>();
    const FS::MediaType media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    const u64 title_id = rp.Pop<u64>();

    GetImportContentContextListImpl(rp, list_count, title_id, media_type);

    LOG_WARNING(Service_AM, "(STUBBED) media_type={} title_id={:016X}", media_type, title_id);
}

void Module::Interface::GetImportContentContexts(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 list_count = rp.Pop<u32>();
    const FS::MediaType media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    const u64 title_id = rp.Pop<u64>();

    LOG_WARNING(Service_AM, "(STUBBED) media_type={} title_id={:016X}", media_type, title_id);

    GetImportContentContextsImpl(rp, list_count, title_id, media_type);
}

void Module::Interface::NeedsCleanup(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto media_type = rp.Pop<u8>();

    LOG_DEBUG(Service_AM, "(STUBBED) media_type=0x{:02x}", media_type);

    bool needs_cleanup = false;
    for (auto& import_ctx : am->import_title_contexts) {
        if (import_ctx.second.state == ImportTitleContextState::RESUMABLE ||
            import_ctx.second.state == ImportTitleContextState::WAITING_FOR_IMPORT) {
            needs_cleanup = true;
            break;
        }
    }

    if (!needs_cleanup) {
        for (auto& import_ctx : am->import_content_contexts) {
            if (import_ctx.second.state == ImportTitleContextState::RESUMABLE ||
                import_ctx.second.state == ImportTitleContextState::WAITING_FOR_IMPORT) {
                needs_cleanup = true;
            }
        }
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push<bool>(needs_cleanup);
}

void Module::Interface::DoCleanup(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto media_type = rp.Pop<u8>();

    LOG_DEBUG(Service_AM, "(STUBBED) called, media_type={:#02x}", media_type);

    for (auto it = am->import_content_contexts.begin(); it != am->import_content_contexts.end();) {
        if (it->second.state == ImportTitleContextState::RESUMABLE ||
            it->second.state == ImportTitleContextState::WAITING_FOR_IMPORT ||
            it->second.state == ImportTitleContextState::NEEDS_CLEANUP) {
            it = am->import_content_contexts.erase(it);
        } else {
            it++;
        }
    }

    for (auto it = am->import_title_contexts.begin(); it != am->import_title_contexts.end();) {
        if (it->second.state == ImportTitleContextState::RESUMABLE ||
            it->second.state == ImportTitleContextState::WAITING_FOR_IMPORT ||
            it->second.state == ImportTitleContextState::NEEDS_CLEANUP) {
            if (am->importing_title) {
                if (am->importing_title->title_id == it->second.title_id &&
                    am->importing_title->media_type ==
                        static_cast<Service::FS::MediaType>(media_type)) {
                    am->importing_title.reset();
                }
            }
            it = am->import_title_contexts.erase(it);
        } else {
            it++;
        }
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void Module::Interface::QueryAvailableTitleDatabase(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    u8 media_type = rp.Pop<u8>();

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess); // No error
    rb.Push(true);

    LOG_WARNING(Service_AM, "(STUBBED) media_type={}", media_type);
}

void Module::Interface::GetPersonalizedTicketInfoList(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    struct AsyncData {
        u32 ticket_count;

        std::vector<TicketInfo> out;
        Kernel::MappedBuffer* out_buffer;
    };
    std::shared_ptr<AsyncData> async_data = std::make_shared<AsyncData>();
    async_data->ticket_count = rp.Pop<u32>();
    async_data->out_buffer = &rp.PopMappedBuffer();

    LOG_DEBUG(Service_AM, "called, ticket_count={}", async_data->ticket_count);

    // TODO(PabloMK7): Properly figure out how to detect personalized tickets.

    ctx.RunAsync(
        [this, async_data](Kernel::HLERequestContext& ctx) {
            u32 written = 0;
            std::scoped_lock lock(am->am_lists_mutex);
            for (auto it = am->am_ticket_list.begin();
                 it != am->am_ticket_list.end() && written < async_data->ticket_count; it++) {
                u64 title_id = it->first;
                u32 tid_high = static_cast<u32>(title_id << 32);
                if ((tid_high & 0x00048010) == 0x00040010 || (tid_high & 0x00048001) == 0x00048001)
                    continue;

                FileSys::Ticket ticket;
                if (ticket.Load(title_id, it->second) != Loader::ResultStatus::Success ||
                    !ticket.IsPersonal())
                    continue;

                TicketInfo info = {};
                info.title_id = ticket.GetTitleID();
                info.ticket_id = ticket.GetTicketID();
                info.version = ticket.GetVersion();
                info.size = static_cast<u32>(ticket.GetSerializedSize());

                async_data->out.push_back(info);
                written++;
            }
            return 0;
        },
        [async_data](Kernel::HLERequestContext& ctx) {
            u32 written = 0;
            for (auto& info : async_data->out) {
                async_data->out_buffer->Write(&info, written * sizeof(TicketInfo),
                                              sizeof(TicketInfo));
                written++;
            }

            IPC::RequestBuilder rb(ctx, 2, 0);
            rb.Push(ResultSuccess); // No error
            rb.Push(written);
        },
        true);
}

void Module::Interface::GetNumImportTitleContextsFiltered(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    const FS::MediaType media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    const u8 filter = rp.Pop<u8>();

    GetNumImportTitleContextsImpl(rp, media_type, (filter & (1 << 0)) != 0,
                                  (filter & (1 << 1)) != 0);

    LOG_WARNING(Service_AM, "(STUBBED) called, media_type={}, filter={}", media_type, filter);
}

void Module::Interface::GetImportTitleContextListFiltered(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    const u32 list_count = rp.Pop<u32>();
    [[maybe_unused]] const FS::MediaType media_type =
        static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    const u8 filter = rp.Pop<u8>();

    GetImportTitleContextListImpl(rp, media_type, list_count, (filter & (1 << 0)) != 0,
                                  (filter & (1 << 1)) != 0);

    LOG_WARNING(Service_AM, "(STUBBED) called, media_type={}, filter={}", media_type, filter);
}

void Module::Interface::CheckContentRights(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    u64 tid = rp.Pop<u64>();
    u16 content_index = rp.Pop<u16>();

    bool has_ticket = false;
    FileSys::Ticket ticket;
    std::scoped_lock lock(am->am_lists_mutex);
    auto entries = am->am_ticket_list.find(tid);
    if (entries != am->am_ticket_list.end() &&
        ticket.Load(tid, (*entries).second) == Loader::ResultStatus::Success) {
        has_ticket = true;
    }

    bool has_rights = (!has_ticket || ticket.HasRights(content_index));

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess); // No error
    rb.Push(has_rights);

    LOG_DEBUG(Service_AM, "tid={:016x}, content_index={}", tid, content_index);
}

void Module::Interface::CheckContentRightsIgnorePlatform(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    u64 tid = rp.Pop<u64>();
    u16 content_index = rp.Pop<u16>();

    bool has_ticket = false;
    FileSys::Ticket ticket;
    std::scoped_lock lock(am->am_lists_mutex);
    auto entries = am->am_ticket_list.find(tid);
    if (entries != am->am_ticket_list.end() &&
        ticket.Load(tid, (*entries).second) == Loader::ResultStatus::Success) {
        has_ticket = true;
    }

    bool has_rights = (!has_ticket || ticket.HasRights(content_index));

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess); // No error
    rb.Push(has_rights);

    LOG_DEBUG(Service_AM, "tid={:016x}, content_index={}", tid, content_index);
}

void Module::Interface::BeginImportProgram(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    auto media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());

    if (am->cia_installing) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrCodes::InvalidImportState, ErrorModule::AM, ErrorSummary::InvalidState,
                       ErrorLevel::Permanent));
        return;
    }

    // Create our CIAFile handle for the app to write to, and while the app writes
    // Citra will store contents out to sdmc/nand
    const FileSys::Path cia_path = {};
    auto file = std::make_shared<Service::FS::File>(
        am->system.Kernel(), std::make_unique<CIAFile>(am->system, media_type), cia_path);

    am->cia_installing = true;

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess); // No error
    rb.PushCopyObjects(file->Connect());

    LOG_WARNING(Service_AM, "(STUBBED) media_type={}", media_type);
}

void Module::Interface::BeginImportProgramTemporarily(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    if (am->cia_installing) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrCodes::InvalidImportState, ErrorModule::AM, ErrorSummary::InvalidState,
                       ErrorLevel::Permanent));
        return;
    }

    // Note: This function should register the title in the temp_i.db database, but we can get away
    // with not doing that because we traverse the file system to detect installed titles.
    // Create our CIAFile handle for the app to write to, and while the app writes Citra will store
    // contents out to sdmc/nand
    const FileSys::Path cia_path = {};
    auto file = std::make_shared<Service::FS::File>(
        am->system.Kernel(), std::make_unique<CIAFile>(am->system, FS::MediaType::NAND), cia_path);

    am->cia_installing = true;

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess); // No error
    rb.PushCopyObjects(file->Connect());

    LOG_WARNING(Service_AM, "(STUBBED)");
}

void Module::Interface::EndImportProgram(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    [[maybe_unused]] const auto cia = rp.PopObject<Kernel::ClientSession>();

    LOG_DEBUG(Service_AM, "");

    am->ScanForAllTitles();

    am->cia_installing = false;
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void Module::Interface::EndImportProgramWithoutCommit(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    [[maybe_unused]] const auto cia = rp.PopObject<Kernel::ClientSession>();

    // Note: This function is basically a no-op for us since we don't use title.db or ticket.db
    // files to keep track of installed titles.
    am->ScanForAllTitles();

    am->cia_installing = false;
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);

    LOG_WARNING(Service_AM, "(STUBBED)");
}

void Module::Interface::CommitImportPrograms(Kernel::HLERequestContext& ctx) {
    CommitImportTitlesImpl(ctx, false, false);
}

/// Wraps all File operations to allow adding an offset to them.
class AMFileWrapper : public FileSys::FileBackend {
public:
    AMFileWrapper(std::shared_ptr<Service::FS::File> file, std::size_t offset, std::size_t size)
        : file(std::move(file)), file_offset(offset), file_size(size) {}

    ResultVal<std::size_t> Read(u64 offset, std::size_t length, u8* buffer) const override {
        return file->backend->Read(offset + file_offset, length, buffer);
    }

    ResultVal<std::size_t> Write(u64 offset, std::size_t length, bool flush, bool update_timestamp,
                                 const u8* buffer) override {
        return file->backend->Write(offset + file_offset, length, flush, update_timestamp, buffer);
    }

    u64 GetSize() const override {
        return file_size;
    }
    bool SetSize(u64 size) const override {
        return false;
    }
    bool Close() override {
        return false;
    }
    void Flush() const override {}

private:
    std::shared_ptr<Service::FS::File> file;
    std::size_t file_offset;
    std::size_t file_size;
};

ResultVal<std::unique_ptr<AMFileWrapper>> GetCiaFileFromSession(
    std::shared_ptr<Kernel::ClientSession> file_session) {
    // Step up the chain from ClientSession->ServerSession and then
    // cast to File. For AM on 3DS, invalid handles actually hang the system.

    if (file_session->parent == nullptr) {
        LOG_WARNING(Service_AM, "Invalid file handle!");
        return Kernel::ResultInvalidHandle;
    }

    std::shared_ptr<Kernel::ServerSession> server =
        Kernel::SharedFrom(file_session->parent->server);
    if (server == nullptr) {
        LOG_WARNING(Service_AM, "File handle ServerSession disconnected!");
        return Kernel::ResultSessionClosed;
    }

    if (server->hle_handler != nullptr) {
        auto file = std::dynamic_pointer_cast<Service::FS::File>(server->hle_handler);

        // TODO(shinyquagsire23): This requires RTTI, use service calls directly instead?
        if (file != nullptr) {
            // Grab the session file offset in case we were given a subfile opened with
            // File::OpenSubFile
            std::size_t offset = file->GetSessionFileOffset(server);
            std::size_t size = file->GetSessionFileSize(server);
            return std::make_unique<AMFileWrapper>(file, offset, size);
        }

        LOG_ERROR(Service_AM, "Failed to cast handle to FSFile!");
        return Kernel::ResultInvalidHandle;
    }

    // Probably the best bet if someone is LLEing the fs service is to just have them LLE AM
    // while they're at it, so not implemented.
    LOG_ERROR(Service_AM, "Given file handle does not have an HLE handler!");
    return Kernel::ResultNotImplemented;
}

template <typename T>
ResultVal<T*> GetFileBackendFromSession(std::shared_ptr<Kernel::ClientSession> file_session) {
    // Step up the chain from ClientSession->ServerSession and then
    // cast to file backend. For AM on 3DS, invalid handles actually hang the system.

    if (file_session->parent == nullptr) {
        LOG_WARNING(Service_AM, "Invalid file handle!");
        return Kernel::ResultInvalidHandle;
    }

    std::shared_ptr<Kernel::ServerSession> server =
        Kernel::SharedFrom(file_session->parent->server);
    if (server == nullptr) {
        LOG_WARNING(Service_AM, "File handle ServerSession disconnected!");
        return Kernel::ResultSessionClosed;
    }

    if (server->hle_handler != nullptr) {
        auto file = std::dynamic_pointer_cast<Service::FS::File>(server->hle_handler);

        // TODO(shinyquagsire23): This requires RTTI, use service calls directly instead?
        if (file != nullptr) {
            // Grab the session file offset in case we were given a subfile opened with
            // File::OpenSubFile
            auto backing_file = dynamic_cast<T*>(file->backend.get());

            if (!backing_file) {
                LOG_ERROR(Service_AM, "Failed to cast to file backend!");
                return Kernel::ResultInvalidHandle;
            }

            return backing_file;
        }

        LOG_ERROR(Service_AM, "Failed to cast handle to FSFile!");
        return Kernel::ResultInvalidHandle;
    }

    // Probably the best bet if someone is LLEing the fs service is to just have them LLE AM
    // while they're at it, so not implemented.
    LOG_ERROR(Service_AM, "Given file handle does not have an HLE handler!");
    return Kernel::ResultNotImplemented;
}

void Module::Interface::GetProgramInfoFromCia(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    [[maybe_unused]] const auto media_type = static_cast<FS::MediaType>(rp.Pop<u8>());
    auto cia = rp.PopObject<Kernel::ClientSession>();

    LOG_DEBUG(Service_AM, "");

    auto file_res = GetCiaFileFromSession(cia);
    if (!file_res.Succeeded()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(file_res.Code());
        return;
    }

    FileSys::CIAContainer container;
    if (container.Load(*file_res.Unwrap()) != Loader::ResultStatus::Success) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrCodes::InvalidCIAHeader, ErrorModule::AM, ErrorSummary::InvalidArgument,
                       ErrorLevel::Permanent));
        return;
    }

    const FileSys::TitleMetadata& tmd = container.GetTitleMetadata();
    TitleInfo title_info = {};
    container.Print();

    // TODO(shinyquagsire23): Sizes allegedly depend on the mediatype, and will double
    // on some mediatypes. Since this is more of a required install size we'll report
    // what Citra needs, but it would be good to be more accurate here.
    title_info.tid = tmd.GetTitleID();
    title_info.size = tmd.GetContentSizeByIndex(FileSys::TMDContentIndex::Main);
    title_info.version = tmd.GetTitleVersion();
    title_info.type = tmd.GetTitleType();

    IPC::RequestBuilder rb = rp.MakeBuilder(8, 0);
    rb.Push(ResultSuccess);
    rb.PushRaw<TitleInfo>(title_info);
}

void Module::Interface::GetSystemMenuDataFromCia(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    auto cia = rp.PopObject<Kernel::ClientSession>();
    auto& output_buffer = rp.PopMappedBuffer();

    LOG_DEBUG(Service_AM, "");

    auto file_res = GetCiaFileFromSession(cia);
    if (!file_res.Succeeded()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
        rb.Push(file_res.Code());
        rb.PushMappedBuffer(output_buffer);
        return;
    }

    std::size_t output_buffer_size = std::min(output_buffer.GetSize(), sizeof(Loader::SMDH));

    auto file = std::move(file_res.Unwrap());
    FileSys::CIAContainer container;
    if (container.Load(*file) != Loader::ResultStatus::Success) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
        rb.Push(Result(ErrCodes::InvalidCIAHeader, ErrorModule::AM, ErrorSummary::InvalidArgument,
                       ErrorLevel::Permanent));
        rb.PushMappedBuffer(output_buffer);
        return;
    }
    std::vector<u8> temp(output_buffer_size);

    //  Read from the Meta offset + 0x400 for the 0x36C0-large SMDH
    auto read_result = file->Read(container.GetMetadataOffset() + FileSys::CIA_METADATA_SIZE,
                                  temp.size(), temp.data());
    if (read_result.Failed() || *read_result != temp.size()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
        rb.Push(Result(ErrCodes::InvalidCIAHeader, ErrorModule::AM, ErrorSummary::InvalidArgument,
                       ErrorLevel::Permanent));
        rb.PushMappedBuffer(output_buffer);
        return;
    }

    output_buffer.Write(temp.data(), 0, temp.size());

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess);
    rb.PushMappedBuffer(output_buffer);
}

void Module::Interface::GetDependencyListFromCia(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    auto cia = rp.PopObject<Kernel::ClientSession>();

    LOG_DEBUG(Service_AM, "");

    auto file_res = GetCiaFileFromSession(cia);
    if (!file_res.Succeeded()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(file_res.Code());
        return;
    }

    FileSys::CIAContainer container;
    if (container.Load(*file_res.Unwrap()) != Loader::ResultStatus::Success) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrCodes::InvalidCIAHeader, ErrorModule::AM, ErrorSummary::InvalidArgument,
                       ErrorLevel::Permanent));
        return;
    }

    std::vector<u8> buffer(FileSys::CIA_DEPENDENCY_SIZE);
    std::memcpy(buffer.data(), container.GetDependencies().data(), buffer.size());

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess);
    rb.PushStaticBuffer(std::move(buffer), 0);
}

void Module::Interface::GetTransferSizeFromCia(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    auto cia = rp.PopObject<Kernel::ClientSession>();

    LOG_DEBUG(Service_AM, "");

    auto file_res = GetCiaFileFromSession(cia);
    if (!file_res.Succeeded()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(file_res.Code());
        return;
    }

    FileSys::CIAContainer container;
    if (container.Load(*file_res.Unwrap()) != Loader::ResultStatus::Success) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrCodes::InvalidCIAHeader, ErrorModule::AM, ErrorSummary::InvalidArgument,
                       ErrorLevel::Permanent));
        return;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(3, 0);
    rb.Push(ResultSuccess);
    rb.Push(container.GetMetadataOffset());
}

void Module::Interface::GetCoreVersionFromCia(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    auto cia = rp.PopObject<Kernel::ClientSession>();

    LOG_DEBUG(Service_AM, "");

    auto file_res = GetCiaFileFromSession(cia);
    if (!file_res.Succeeded()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(file_res.Code());
        return;
    }

    FileSys::CIAContainer container;
    if (container.Load(*file_res.Unwrap()) != Loader::ResultStatus::Success) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrCodes::InvalidCIAHeader, ErrorModule::AM, ErrorSummary::InvalidArgument,
                       ErrorLevel::Permanent));
        return;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(container.GetCoreVersion());
}

void Module::Interface::GetRequiredSizeFromCia(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    [[maybe_unused]] const auto media_type = static_cast<FS::MediaType>(rp.Pop<u8>());
    auto cia = rp.PopObject<Kernel::ClientSession>();

    LOG_DEBUG(Service_AM, "");

    auto file_res = GetCiaFileFromSession(cia);
    if (!file_res.Succeeded()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(file_res.Code());
        return;
    }

    FileSys::CIAContainer container;
    if (container.Load(*file_res.Unwrap()) != Loader::ResultStatus::Success) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrCodes::InvalidCIAHeader, ErrorModule::AM, ErrorSummary::InvalidArgument,
                       ErrorLevel::Permanent));
        return;
    }

    // TODO(shinyquagsire23): Sizes allegedly depend on the mediatype, and will double
    // on some mediatypes. Since this is more of a required install size we'll report
    // what Citra needs, but it would be good to be more accurate here.
    IPC::RequestBuilder rb = rp.MakeBuilder(3, 0);
    rb.Push(ResultSuccess);
    rb.Push(container.GetTitleMetadata().GetContentSizeByIndex(FileSys::TMDContentIndex::Main));
}

void Module::Interface::CommitImportProgramsAndUpdateFirmwareAuto(Kernel::HLERequestContext& ctx) {
    CommitImportTitlesImpl(ctx, true, false);
}

void Module::Interface::CommitImportTitlesImpl(Kernel::HLERequestContext& ctx,
                                               bool is_update_firm_auto, bool is_titles) {
    IPC::RequestParser rp(ctx);
    const auto media_type = static_cast<FS::MediaType>(rp.Pop<u8>());
    [[maybe_unused]] u32 count = rp.Pop<u32>();
    bool cleanup = rp.Pop<bool>();

    LOG_WARNING(Service_AM, "(STUBBED) update_firm_auto={} is_titles={} cleanup={}",
                is_update_firm_auto, is_titles, cleanup);

    auto& title_id_buf = rp.PopMappedBuffer();

    std::vector<u64> title_ids(title_id_buf.GetSize() / sizeof(u64));
    title_id_buf.Read(title_ids.data(), 0, title_id_buf.GetSize());

    for (auto& key_value : am->import_content_contexts) {
        if (std::find(title_ids.begin(), title_ids.end(), key_value.first) != title_ids.end() &&
            key_value.second.state == ImportTitleContextState::WAITING_FOR_COMMIT) {
            key_value.second.state = ImportTitleContextState::NEEDS_CLEANUP;
        }
    }

    for (auto tid : title_ids) {
        auto it = am->import_title_contexts.find(tid);
        if (it != am->import_title_contexts.end() &&
            it->second.state == ImportTitleContextState::WAITING_FOR_COMMIT) {
            it->second.state = ImportTitleContextState::NEEDS_CLEANUP;
        }
    }

    if (cleanup) {
        for (auto it = am->import_content_contexts.begin();
             it != am->import_content_contexts.end();) {
            if (it->second.state == ImportTitleContextState::RESUMABLE ||
                it->second.state == ImportTitleContextState::WAITING_FOR_IMPORT ||
                it->second.state == ImportTitleContextState::NEEDS_CLEANUP) {
                it = am->import_content_contexts.erase(it);
            } else {
                it++;
            }
        }

        for (auto it = am->import_title_contexts.begin(); it != am->import_title_contexts.end();) {
            if (it->second.state == ImportTitleContextState::RESUMABLE ||
                it->second.state == ImportTitleContextState::WAITING_FOR_IMPORT ||
                it->second.state == ImportTitleContextState::NEEDS_CLEANUP) {
                it = am->import_title_contexts.erase(it);
            } else {
                it++;
            }
        }
    }

    am->ScanForTitles(media_type);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

Result UninstallProgram(const FS::MediaType media_type, const u64 title_id) {
    // Use the content folder so we don't delete the user's save data.
    const auto path = GetTitlePath(media_type, title_id) + "content/";
    if (!FileUtil::Exists(path)) {
        return {ErrorDescription::NotFound, ErrorModule::AM, ErrorSummary::InvalidState,
                ErrorLevel::Permanent};
    }
    if (!FileUtil::DeleteDirRecursively(path)) {
        // TODO: Determine the right error code for this.
        return {ErrorDescription::NotFound, ErrorModule::AM, ErrorSummary::InvalidState,
                ErrorLevel::Permanent};
    }
    return ResultSuccess;
}

void Module::Interface::DeleteProgram(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto media_type = rp.PopEnum<FS::MediaType>();
    const auto title_id = rp.Pop<u64>();

    LOG_INFO(Service_AM, "called, title={:016x}", title_id);

    const auto result = UninstallProgram(media_type, title_id);
    am->ScanForAllTitles();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(result);
}

void Module::Interface::GetSystemUpdaterMutex(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    LOG_DEBUG(Service_AM, "");

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess);
    rb.PushCopyObjects(am->system_updater_mutex);
}

void Module::Interface::GetMetaSizeFromCia(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    auto cia = rp.PopObject<Kernel::ClientSession>();

    LOG_DEBUG(Service_AM, "");

    auto file_res = GetCiaFileFromSession(cia);
    if (!file_res.Succeeded()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(file_res.Code());
        return;
    }

    FileSys::CIAContainer container;
    if (container.Load(*file_res.Unwrap()) != Loader::ResultStatus::Success) {

        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrCodes::InvalidCIAHeader, ErrorModule::AM, ErrorSummary::InvalidArgument,
                       ErrorLevel::Permanent));
        return;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(container.GetMetadataSize());
}

void Module::Interface::GetMetaDataFromCia(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    u32 output_size = rp.Pop<u32>();
    auto cia = rp.PopObject<Kernel::ClientSession>();
    auto& output_buffer = rp.PopMappedBuffer();

    LOG_DEBUG(Service_AM, "");

    auto file_res = GetCiaFileFromSession(cia);
    if (!file_res.Succeeded()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
        rb.Push(file_res.Code());
        rb.PushMappedBuffer(output_buffer);
        return;
    }
    // Don't write beyond the actual static buffer size.
    output_size = std::min(static_cast<u32>(output_buffer.GetSize()), output_size);

    auto file = std::move(file_res.Unwrap());
    FileSys::CIAContainer container;
    if (container.Load(*file) != Loader::ResultStatus::Success) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
        rb.Push(Result(ErrCodes::InvalidCIAHeader, ErrorModule::AM, ErrorSummary::InvalidArgument,
                       ErrorLevel::Permanent));
        rb.PushMappedBuffer(output_buffer);
        return;
    }

    //  Read from the Meta offset for the specified size
    std::vector<u8> temp(output_size);
    auto read_result = file->Read(container.GetMetadataOffset(), output_size, temp.data());
    if (read_result.Failed() || *read_result != output_size) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrCodes::InvalidCIAHeader, ErrorModule::AM, ErrorSummary::InvalidArgument,
                       ErrorLevel::Permanent));
        return;
    }

    output_buffer.Write(temp.data(), 0, output_size);
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess);
    rb.PushMappedBuffer(output_buffer);
}

void Module::Interface::BeginImportTicket(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    LOG_DEBUG(Service_AM, "");

    // Create our TicketFile handle for the app to write to
    auto file = std::make_shared<Service::FS::File>(
        am->system.Kernel(), std::make_unique<TicketFile>(), FileSys::Path{});

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess); // No error
    rb.PushCopyObjects(file->Connect());
}

void Module::Interface::EndImportTicket(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto ticket = rp.PopObject<Kernel::ClientSession>();

    auto ticket_file = GetFileBackendFromSession<TicketFile>(ticket);
    if (ticket_file.Succeeded()) {
        struct AsyncData {
            Service::AM::TicketFile* ticket_file;

            Result res{0};
        };
        std::shared_ptr<AsyncData> async_data = std::make_shared<AsyncData>();
        async_data->ticket_file = ticket_file.Unwrap();

        ctx.RunAsync(
            [this, async_data](Kernel::HLERequestContext& ctx) {
                async_data->res = async_data->ticket_file->Commit();

                std::scoped_lock lock(am->am_lists_mutex);
                am->am_ticket_list.insert(std::make_pair(async_data->ticket_file->GetTitleID(),
                                                         async_data->ticket_file->GetTicketID()));

                LOG_DEBUG(Service_AM, "EndImportTicket: title_id={:016X} ticket_id={:016X}",
                          async_data->ticket_file->GetTitleID(),
                          async_data->ticket_file->GetTicketID());
                return 0;
            },
            [async_data](Kernel::HLERequestContext& ctx) {
                IPC::RequestBuilder rb(ctx, 1, 0);
                rb.Push(async_data->res);
            },
            true);
    } else {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ticket_file.Code());
    }
}

void Module::Interface::BeginImportTitle(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto media_type = static_cast<FS::MediaType>(rp.Pop<u8>());
    const u64 title_id = rp.Pop<u64>();
    [[maybe_unused]] const u8 database = rp.Pop<u8>();

    LOG_DEBUG(Service_AM, "title_id={:016X} media_type={:016X}", title_id, media_type);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);

    am->importing_title =
        std::make_shared<CurrentImportingTitle>(Core::System::GetInstance(), title_id, media_type);

    std::scoped_lock lock(am->am_lists_mutex);
    auto entries = am->am_ticket_list.find(title_id);
    if (entries == am->am_ticket_list.end()) {
        // Ticket is not installed
        rb.Push(ResultUnknown);
        return;
    }
    FileSys::Ticket ticket;
    if (ticket.Load(title_id, (*entries).second) != Loader::ResultStatus::Success) {
        // Ticket failed to load
        rb.Push(ResultUnknown);
        return;
    }

    Result res = am->importing_title->cia_file.ProvideTicket(ticket);
    if (res.IsError()) {
        // Failed to load ticket
        rb.Push(res);
        return;
    }

    AuthorizeCIAFileDecryption(&am->importing_title->cia_file, ctx);

    rb.Push(ResultSuccess);
}

void Module::Interface::StopImportTitle(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    if (!am->importing_title) {
        // Not importing a title
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultUnknown);
        return;
    }

    auto it = am->import_title_contexts.find(am->importing_title->title_id);
    if (it != am->import_title_contexts.end()) {
        it->second.state = ImportTitleContextState::RESUMABLE;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);

    LOG_WARNING(Service_AM, "(STUBBED)");
}

void Module::Interface::ResumeImportTitle(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    const auto media_type = static_cast<FS::MediaType>(rp.Pop<u8>());
    const u64 title_id = rp.Pop<u64>();

    if (!am->importing_title || am->importing_title->title_id != title_id ||
        am->importing_title->media_type != media_type) {
        // Not importing a title
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultUnknown);
        return;
    }

    auto it = am->import_title_contexts.find(am->importing_title->title_id);
    if (it == am->import_title_contexts.end() ||
        it->second.state != ImportTitleContextState::RESUMABLE) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultUnknown);
        return;
    }

    it->second.state = ImportTitleContextState::WAITING_FOR_IMPORT;

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);

    LOG_WARNING(Service_AM, "(STUBBED) title_id={:016X}", title_id);
}

void Module::Interface::CancelImportTitle(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    if (!am->importing_title) {
        // Not importing a title
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultUnknown);
        return;
    }

    auto it = am->import_title_contexts.find(am->importing_title->title_id);
    if (it != am->import_title_contexts.end()) {
        it->second.state = ImportTitleContextState::DELETING;
    }

    am->importing_title.reset();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void Module::Interface::EndImportTitle(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    LOG_DEBUG(Service_AM, "");

    if (!am->importing_title) {
        // Not importing a title
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultUnknown);
        return;
    }

    auto it = am->import_title_contexts.find(am->importing_title->title_id);
    if (it != am->import_title_contexts.end()) {
        it->second.state = ImportTitleContextState::WAITING_FOR_COMMIT;
    }

    am->importing_title->cia_file.SetDone();
    am->importing_title.reset();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void Module::Interface::CommitImportTitles(Kernel::HLERequestContext& ctx) {
    CommitImportTitlesImpl(ctx, false, true);
}

void Module::Interface::BeginImportTmd(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    LOG_DEBUG(Service_AM, "");

    if (!am->importing_title) {
        // Not importing a title
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultUnknown);
        return;
    }

    // Create our TMD handle for the app to write to
    auto file = std::make_shared<Service::FS::File>(
        am->system.Kernel(), std::make_unique<TMDFile>(am->importing_title), FileSys::Path{});

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess); // No error
    rb.PushCopyObjects(file->Connect());
}

void Module::Interface::EndImportTmd(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const bool create_context = rp.Pop<bool>();
    const auto tmd = rp.PopObject<Kernel::ClientSession>();

    LOG_DEBUG(Service_AM, "");

    if (!am->importing_title) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrCodes::InvalidImportState, ErrorModule::AM, ErrorSummary::InvalidState,
                       ErrorLevel::Permanent));
        return;
    }

    auto tmd_file = GetFileBackendFromSession<TMDFile>(tmd);
    if (tmd_file.Succeeded()) {
        struct AsyncData {
            Service::AM::TMDFile* tmd_file;
            [[maybe_unused]] bool create_context;

            Result res{0};
        };
        std::shared_ptr<AsyncData> async_data = std::make_shared<AsyncData>();
        async_data->tmd_file = tmd_file.Unwrap();
        async_data->create_context = create_context;

        ctx.RunAsync(
            [async_data](Kernel::HLERequestContext& ctx) {
                async_data->res = async_data->tmd_file->Commit();
                return 0;
            },
            [this, async_data](Kernel::HLERequestContext& ctx) {
                IPC::RequestBuilder rb(ctx, 1, 0);
                rb.Push(async_data->res);

                if (async_data->res.IsSuccess()) {
                    am->importing_title->tmd_provided = true;
                    const FileSys::TitleMetadata& tmd_info = am->importing_title->cia_file.GetTMD();

                    ImportTitleContext& context = am->import_title_contexts[tmd_info.GetTitleID()];
                    context.title_id = tmd_info.GetTitleID();
                    context.version = tmd_info.GetTitleVersion();
                    context.type = 0;
                    context.state = ImportTitleContextState::WAITING_FOR_IMPORT;
                    context.size = 0;
                    for (size_t i = 0; i < tmd_info.GetContentCount(); i++) {
                        if (tmd_info.GetContentOptional(i)) {
                            continue;
                        }
                        ImportContentContext content_context;
                        content_context.content_id = tmd_info.GetContentIDByIndex(i);
                        content_context.index = static_cast<u16>(i);
                        content_context.state = ImportTitleContextState::WAITING_FOR_IMPORT;
                        content_context.size = tmd_info.GetContentSizeByIndex(i);
                        content_context.current_size = 0;
                        am->import_content_contexts.insert(
                            std::make_pair(context.title_id, content_context));

                        context.size += content_context.size;
                    }
                }
            },
            true);

    } else {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(tmd_file.Code());
        return;
    }
}

void Module::Interface::CreateImportContentContexts(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 content_count = rp.Pop<u32>();
    auto content_buf = rp.PopMappedBuffer();

    LOG_DEBUG(Service_AM, "");

    if (!am->importing_title) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrCodes::InvalidImportState, ErrorModule::AM, ErrorSummary::InvalidState,
                       ErrorLevel::Permanent));
        return;
    }

    struct AsyncData {
        std::vector<u16> content_indices;

        Result res{0};
    };
    std::shared_ptr<AsyncData> async_data = std::make_shared<AsyncData>();
    async_data->content_indices.resize(content_count);
    content_buf.Read(async_data->content_indices.data(), 0, content_buf.GetSize());

    ctx.RunAsync(
        [this, async_data](Kernel::HLERequestContext& ctx) {
            if (!am->importing_title->tmd_provided) {
                std::string tmd_path = GetTitleMetadataPath(am->importing_title->media_type,
                                                            am->importing_title->title_id);
                FileSys::TitleMetadata tmd;
                if (tmd.Load(tmd_path) != Loader::ResultStatus::Success) {
                    LOG_ERROR(Service_AM, "Couldn't load TMD for title_id={:016X}, mediatype={}",
                              am->importing_title->title_id, am->importing_title->media_type);

                    async_data->res =
                        Result(0xFFFFFFFF); // TODO(PabloMK7): Find the right error code
                    return 0;
                }
                am->importing_title->cia_file.ProvideTMDForAdditionalContent(tmd);
                am->importing_title->tmd_provided = true;
            }
            const FileSys::TitleMetadata& tmd = am->importing_title->cia_file.GetTMD();
            for (size_t i = 0; i < async_data->content_indices.size(); i++) {
                u16 index = async_data->content_indices[i];
                if (index > tmd.GetContentCount()) {
                    LOG_ERROR(Service_AM,
                              "Tried to create context for invalid index title_id={:016x} index={}",
                              am->importing_title->title_id, index);
                    async_data->res =
                        Result(0xFFFFFFFF); // TODO(PabloMK7): Find the right error code
                    return 0;
                }
                ImportContentContext content_context;
                content_context.content_id = tmd.GetContentIDByIndex(index);
                content_context.index = static_cast<u16>(index);
                content_context.state = ImportTitleContextState::WAITING_FOR_IMPORT;
                content_context.size = tmd.GetContentSizeByIndex(index);
                content_context.current_size = 0;
                am->import_content_contexts.insert(
                    std::make_pair(am->importing_title->title_id, content_context));
            }
            return 0;
        },
        [async_data](Kernel::HLERequestContext& ctx) {
            IPC::RequestBuilder rb(ctx, 1, 0);
            rb.Push(async_data->res);
        },
        true);
}

void Module::Interface::BeginImportContent(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    const u16 content_index = rp.Pop<u16>();

    LOG_DEBUG(Service_AM, "content_index={}", static_cast<u32>(content_index));

    if (!am->importing_title) {
        // Not importing a title
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultUnknown);
        return;
    }

    auto range = am->import_content_contexts.equal_range(am->importing_title->title_id);
    auto it = range.first;
    for (; it != range.second; it++)
        if (it->second.index == content_index)
            break;

    if (it == range.second) {
        // Index not found
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultUnknown);
        return;
    }

    // Create our TMD handle for the app to write to
    auto file = std::make_shared<Service::FS::File>(
        am->system.Kernel(),
        std::make_unique<ContentFile>(am->importing_title, content_index, it->second),
        FileSys::Path{});

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(ResultSuccess); // No error
    rb.PushCopyObjects(file->Connect());
}

void Module::Interface::ResumeImportContent(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    const u16 content_index = rp.Pop<u16>();

    LOG_DEBUG(Service_AM, "content_index={}", static_cast<u32>(content_index));

    if (!am->importing_title) {
        // Not importing a title
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultUnknown);
        return;
    }

    auto range = am->import_content_contexts.equal_range(am->importing_title->title_id);
    auto it = range.first;
    for (; it != range.second; it++)
        if (it->second.index == content_index)
            break;

    if (it == range.second || it->second.state != ImportTitleContextState::RESUMABLE) {
        // Index not found
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultUnknown);
        return;
    }

    it->second.state = ImportTitleContextState::WAITING_FOR_IMPORT;

    auto content_file =
        std::make_unique<ContentFile>(am->importing_title, content_index, it->second);
    content_file->SetWritten(it->second.current_size);

    // Create our TMD handle for the app to write to
    auto file = std::make_shared<Service::FS::File>(am->system.Kernel(), std::move(content_file),
                                                    FileSys::Path{});

    IPC::RequestBuilder rb = rp.MakeBuilder(3, 2);
    rb.Push(ResultSuccess); // No error
    rb.Push<u64>(it->second.current_size);
    rb.PushCopyObjects(file->Connect());
}

void Module::Interface::StopImportContent(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto content = rp.PopObject<Kernel::ClientSession>();
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);

    LOG_DEBUG(Service_AM, "");

    if (!am->importing_title) {
        // Not importing a title
        rb.Push(ResultUnknown);
        return;
    }

    auto content_file = GetFileBackendFromSession<ContentFile>(content);
    if (content_file.Failed()) {
        rb.Push(content_file.Code());
        return;
    }

    content_file.Unwrap()->GetImportContext().state = ImportTitleContextState::RESUMABLE;

    rb.Push(ResultSuccess);
}

void Module::Interface::CancelImportContent(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto content = rp.PopObject<Kernel::ClientSession>();
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);

    LOG_DEBUG(Service_AM, "");

    if (!am->importing_title) {
        // Not importing a title
        rb.Push(ResultUnknown);
        return;
    }

    auto content_file = GetFileBackendFromSession<ContentFile>(content);
    if (content_file.Failed()) {
        rb.Push(content_file.Code());
        return;
    }

    content_file.Unwrap()->GetImportContext().state = ImportTitleContextState::DELETING;
    content_file.Unwrap()->Cancel(am->importing_title->media_type, am->importing_title->title_id);

    rb.Push(ResultSuccess);
}

void Module::Interface::EndImportContent(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto content = rp.PopObject<Kernel::ClientSession>();
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);

    LOG_DEBUG(Service_AM, "");

    if (!am->importing_title) {
        // Not importing a title
        rb.Push(ResultUnknown);
        return;
    }

    auto content_file = GetFileBackendFromSession<ContentFile>(content);
    if (content_file.Failed()) {
        rb.Push(content_file.Code());
        return;
    }

    content_file.Unwrap()->GetImportContext().state = ImportTitleContextState::WAITING_FOR_COMMIT;

    rb.Push(ResultSuccess);
}

void Module::Interface::GetNumCurrentImportContentContexts(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    if (!am->importing_title) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrCodes::InvalidImportState, ErrorModule::AM, ErrorSummary::InvalidState,
                       ErrorLevel::Permanent));
        return;
    }

    GetNumImportContentContextsImpl(rp, am->importing_title->title_id,
                                    am->importing_title->media_type);

    LOG_WARNING(Service_AM, "(STUBBED) title_id={:016X}", am->importing_title->title_id);
}

void Module::Interface::GetCurrentImportContentContextList(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    if (!am->importing_title) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrCodes::InvalidImportState, ErrorModule::AM, ErrorSummary::InvalidState,
                       ErrorLevel::Permanent));
        return;
    }

    const u32 list_count = rp.Pop<u32>();

    GetImportContentContextListImpl(rp, list_count, am->importing_title->title_id,
                                    am->importing_title->media_type);

    LOG_WARNING(Service_AM, "(STUBBED) title_id={:016X}", am->importing_title->title_id);
}

void Module::Interface::GetCurrentImportContentContexts(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    if (!am->importing_title) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrCodes::InvalidImportState, ErrorModule::AM, ErrorSummary::InvalidState,
                       ErrorLevel::Permanent));
        return;
    }

    const u32 list_count = rp.Pop<u32>();

    LOG_WARNING(Service_AM, "(STUBBED) title_id={:016X}", am->importing_title->title_id);

    GetImportContentContextsImpl(rp, list_count, am->importing_title->title_id,
                                 am->importing_title->media_type);
}

void Module::Interface::Sign(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    size_t signature_size = rp.Pop<u32>();
    size_t certificate_size = rp.Pop<u32>();
    u64 title_id = rp.Pop<u64>();
    size_t data_size = rp.Pop<u32>();

    auto& data_buf = rp.PopMappedBuffer();
    auto& signature_buf = rp.PopMappedBuffer();
    auto& certificate_buf = rp.PopMappedBuffer();

    LOG_DEBUG(Service_AM, "title_id={:016X}", title_id);

    std::vector<u8> data(data_size);
    data_buf.Read(data.data(), 0, data_size);

    FileSys::Certificate& ct_cert = HW::UniqueData::GetCTCert();
    if (!ct_cert.IsValid()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrorDescription::NotFound, ErrorModule::AM, ErrorSummary::NotFound,
                       ErrorLevel::Permanent));
        return;
    }

    FileSys::Certificate ap_cert;
    std::string new_issuer_str =
        fmt::format("{}-{}", reinterpret_cast<const char*>(ct_cert.GetIssuer().data()),
                    reinterpret_cast<const char*>(ct_cert.GetName().data()));
    std::string new_name_str = fmt::format("AP{:016x}", title_id);

    std::array<u8, 0x40> new_issuer = {0};
    std::array<u8, 0x40> new_name = {0};
    memcpy(new_issuer.data(), new_issuer_str.data(), new_issuer_str.size());
    memcpy(new_name.data(), new_name_str.data(), new_name_str.size());

    ap_cert.BuildECC(ct_cert, new_issuer, new_name, 0);

    HW::ECC::Signature signature = ap_cert.Sign(data);
    std::vector<u8> certificate = ap_cert.Serialize();

    signature_buf.Write(signature.rs.data(), 0, std::min(signature_size, signature.rs.size()));
    certificate_buf.Write(certificate.data(), 0, std::min(certificate_size, certificate.size()));

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(0);
}

template <class Archive>
void Module::serialize(Archive& ar, const unsigned int) {
    std::scoped_lock lock(am_lists_mutex);
    DEBUG_SERIALIZATION_POINT;
    ar & cia_installing;
    ar & force_old_device_id;
    ar & force_new_device_id;
    ar & system_updater_mutex;
}
SERIALIZE_IMPL(Module)

void Module::Interface::GetDeviceCert(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    [[maybe_unused]] u32 size = rp.Pop<u32>();
    auto buffer = rp.PopMappedBuffer();

    LOG_DEBUG(Service_AM, "");

    const auto& ct_cert = HW::UniqueData::GetCTCert();
    if (!ct_cert.IsValid()) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrorDescription::NotFound, ErrorModule::AM, ErrorSummary::NotFound,
                       ErrorLevel::Permanent));
        return;
    }

    auto ct_cert_bin = ct_cert.Serialize();

    buffer.Write(ct_cert_bin.data(), 0, std::min(ct_cert_bin.size(), buffer.GetSize()));
    IPC::RequestBuilder rb = rp.MakeBuilder(2, 2);
    rb.Push(ResultSuccess);
    rb.Push(0);
    rb.PushMappedBuffer(buffer);
}

void Module::Interface::CommitImportTitlesAndUpdateFirmwareAuto(Kernel::HLERequestContext& ctx) {
    CommitImportTitlesImpl(ctx, true, true);
}

void Module::Interface::DeleteTicketId(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    u64 title_id = rp.Pop<u64>();
    u64 ticket_id = rp.Pop<u64>();

    LOG_DEBUG(Service_AM, "title_id={:016X} ticket_id={}", title_id, ticket_id);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    std::scoped_lock lock(am->am_lists_mutex);
    auto range = am->am_ticket_list.equal_range(title_id);
    auto it = range.first;
    for (; it != range.second; it++) {
        if (it->second == ticket_id) {
            break;
        }
    }
    if (range.first == range.second) {
        rb.Push(Result(ErrorDescription::AlreadyDone, ErrorModule::AM, ErrorSummary::Success,
                       ErrorLevel::Success));
        return;
    }

    auto path = GetTicketPath(title_id, ticket_id);
    FileUtil::Delete(path);

    am->am_ticket_list.erase(it);

    rb.Push(ResultSuccess);
}

void Module::Interface::GetNumTicketIds(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    u64 title_id = rp.Pop<u64>();

    LOG_DEBUG(Service_AM, "title_id={:016X}", title_id);

    std::scoped_lock lock(am->am_lists_mutex);
    auto range = am->am_ticket_list.equal_range(title_id);
    u32 count = static_cast<u32>(std::distance(range.first, range.second));

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(count);
}

void Module::Interface::GetTicketIdList(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 list_count = rp.Pop<u32>();
    const u64 title_id = rp.Pop<u64>();
    [[maybe_unused]] const bool unk = rp.Pop<bool>();

    LOG_DEBUG(Service_AM, "title_id={:016X}", title_id);

    auto out_buf = rp.PopMappedBuffer();

    u32 index = 0;
    std::scoped_lock lock(am->am_lists_mutex);
    for (auto [it, rangeEnd] = am->am_ticket_list.equal_range(title_id);
         it != rangeEnd && index < list_count; index++, it++) {
        u64 ticket_id = it->second;
        out_buf.Write(&ticket_id, index * sizeof(u64), sizeof(u64));
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(index);
}

void Module::Interface::GetNumTicketsOfProgram(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    u64 title_id = rp.Pop<u64>();

    LOG_DEBUG(Service_AM, "title_id={:016X}", title_id);

    std::scoped_lock lock(am->am_lists_mutex);
    auto range = am->am_ticket_list.equal_range(title_id);
    u32 count = static_cast<u32>(std::distance(range.first, range.second));

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(count);
}

void Module::Interface::ListTicketInfos(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    u32 ticket_count = rp.Pop<u32>();
    u64 title_id = rp.Pop<u64>();
    u32 skip = rp.Pop<u32>();
    auto& out_buffer = rp.PopMappedBuffer();

    LOG_DEBUG(Service_AM, "(STUBBED) called, ticket_count={}", ticket_count);

    std::scoped_lock lock(am->am_lists_mutex);
    auto range = am->am_ticket_list.equal_range(title_id);
    auto it = range.first;
    std::advance(it, std::min(static_cast<size_t>(skip),
                              static_cast<size_t>(std::distance(range.first, range.second))));

    u32 written = 0;
    for (; it != range.second && written < ticket_count; it++) {
        FileSys::Ticket ticket;
        if (ticket.Load(title_id, it->second) != Loader::ResultStatus::Success)
            continue;

        TicketInfo info = {};
        info.title_id = ticket.GetTitleID();
        info.ticket_id = ticket.GetTicketID();
        info.version = ticket.GetVersion();
        info.size = static_cast<u32>(ticket.GetSerializedSize());

        out_buffer.Write(&info, written * sizeof(TicketInfo), sizeof(TicketInfo));
        written++;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess); // No error
    rb.Push(written);
}

void Module::Interface::GetNumCurrentContentInfos(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    LOG_DEBUG(Service_AM, "");

    if (!am->importing_title) {
        // Not importing a title
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultUnknown);
        return;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess); // No error
    rb.Push(static_cast<u32>(am->importing_title->cia_file.GetTMD().GetContentCount()));
}

void Module::Interface::FindCurrentContentInfos(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    LOG_DEBUG(Service_AM, "");

    if (!am->importing_title) {
        // Not importing a title
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultUnknown);
        return;
    }

    struct AsyncData {
        u32 content_count;
        std::vector<u16_le> content_requested;

        std::vector<ContentInfo> out_vec;
        Kernel::MappedBuffer* content_info_out;
        Result res{0};
    };
    auto async_data = std::make_shared<AsyncData>();
    async_data->content_count = rp.Pop<u32>();

    auto& content_requested_in = rp.PopMappedBuffer();
    async_data->content_requested.resize(async_data->content_count);
    content_requested_in.Read(async_data->content_requested.data(), 0,
                              async_data->content_count * sizeof(u16));
    async_data->content_info_out = &rp.PopMappedBuffer();

    ctx.RunAsync(
        [this, async_data](Kernel::HLERequestContext& ctx) {
            const FileSys::TitleMetadata& tmd = am->importing_title->cia_file.GetTMD();
            FileSys::Ticket& ticket = am->importing_title->cia_file.GetTicket();
            // Get info for each content index requested
            for (std::size_t i = 0; i < async_data->content_count; i++) {
                u16_le index = async_data->content_requested[i];
                if (index >= tmd.GetContentCount()) {
                    LOG_ERROR(Service_AM,
                              "Attempted to get info for non-existent content index {:04x}.",
                              index);

                    async_data->res = Result(0xFFFFFFFF);
                    return 0;
                }

                ContentInfo content_info = {};
                content_info.index = index;
                content_info.type = tmd.GetContentTypeByIndex(index);
                content_info.content_id = tmd.GetContentIDByIndex(index);
                content_info.size = tmd.GetContentSizeByIndex(index);
                content_info.ownership = ticket.HasRights(index) ? OWNERSHIP_OWNED : 0;

                if (FileUtil::Exists(GetTitleContentPath(am->importing_title->media_type,
                                                         am->importing_title->title_id, index))) {
                    bool pending = false;
                    for (auto& import_ctx : am->import_content_contexts) {
                        if (import_ctx.first == am->importing_title->title_id &&
                            import_ctx.second.index == index &&
                            (import_ctx.second.state ==
                                 ImportTitleContextState::WAITING_FOR_IMPORT ||
                             import_ctx.second.state ==
                                 ImportTitleContextState::WAITING_FOR_COMMIT ||
                             import_ctx.second.state == ImportTitleContextState::RESUMABLE)) {
                            LOG_DEBUG(Service_AM, "content pending commit index={:016X}", index);
                            pending = true;
                            break;
                        }
                    }
                    if (!pending) {
                        content_info.ownership |= OWNERSHIP_DOWNLOADED;
                    }
                }
                async_data->out_vec.push_back(content_info);
            }
            return 0;
        },
        [async_data](Kernel::HLERequestContext& ctx) {
            IPC::RequestBuilder rb(ctx, 1, 0);
            rb.Push(async_data->res);
            if (async_data->res.IsSuccess()) {
                async_data->content_info_out->Write(async_data->out_vec.data(), 0,
                                                    async_data->out_vec.size() *
                                                        sizeof(ContentInfo));
            }
        },
        true);
}

void Module::Interface::ListCurrentContentInfos(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    LOG_DEBUG(Service_AM, "");

    if (!am->importing_title) {
        // Not importing a title
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultUnknown);
        return;
    }

    struct AsyncData {
        u32 content_count;
        u32 start_index;

        std::vector<ContentInfo> out_vec;
        Kernel::MappedBuffer* content_info_out;
        Result res{0};
    };
    auto async_data = std::make_shared<AsyncData>();
    async_data->content_count = rp.Pop<u32>();
    async_data->start_index = rp.Pop<u32>();

    async_data->content_info_out = &rp.PopMappedBuffer();

    ctx.RunAsync(
        [this, async_data](Kernel::HLERequestContext& ctx) {
            const FileSys::TitleMetadata& tmd = am->importing_title->cia_file.GetTMD();
            FileSys::Ticket& ticket = am->importing_title->cia_file.GetTicket();
            u32 end_index = std::min(async_data->start_index + async_data->content_count,
                                     static_cast<u32>(tmd.GetContentCount()));
            for (u32 i = async_data->start_index; i < end_index; i++) {
                ContentInfo content_info = {};
                content_info.index = static_cast<u16>(i);
                content_info.type = tmd.GetContentTypeByIndex(i);
                content_info.content_id = tmd.GetContentIDByIndex(i);
                content_info.size = tmd.GetContentSizeByIndex(i);
                content_info.ownership =
                    ticket.HasRights(static_cast<u16>(i)) ? OWNERSHIP_OWNED : 0;

                if (FileUtil::Exists(GetTitleContentPath(am->importing_title->media_type,
                                                         am->importing_title->title_id, i))) {
                    bool pending = false;
                    for (auto& import_ctx : am->import_content_contexts) {
                        if (import_ctx.first == am->importing_title->title_id &&
                            import_ctx.second.index == i &&
                            (import_ctx.second.state ==
                                 ImportTitleContextState::WAITING_FOR_IMPORT ||
                             import_ctx.second.state ==
                                 ImportTitleContextState::WAITING_FOR_COMMIT ||
                             import_ctx.second.state == ImportTitleContextState::RESUMABLE)) {
                            LOG_DEBUG(Service_AM, "content pending commit index={:016X}", i);
                            pending = true;
                            break;
                        }
                    }
                    if (!pending) {
                        content_info.ownership |= OWNERSHIP_DOWNLOADED;
                    }
                }

                async_data->out_vec.push_back(content_info);
            }
            return 0;
        },
        [async_data](Kernel::HLERequestContext& ctx) {
            IPC::RequestBuilder rb(ctx, 2, 0);
            rb.Push(async_data->res);
            rb.Push(static_cast<u32>(async_data->out_vec.size()));
            if (async_data->res.IsSuccess()) {
                async_data->content_info_out->Write(async_data->out_vec.data(), 0,
                                                    async_data->out_vec.size() *
                                                        sizeof(ContentInfo));
            }
        },
        true);
}

void Module::Interface::CalculateContextRequiredSize(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    LOG_DEBUG(Service_AM, "");

    auto media_type = static_cast<Service::FS::MediaType>(rp.Pop<u8>());
    u64 title_id = rp.Pop<u64>();
    u32 content_count = rp.Pop<u32>();
    auto& content_requested_in = rp.PopMappedBuffer();

    std::vector<u16_le> content_requested(content_count);
    content_requested_in.Read(content_requested.data(), 0, content_count * sizeof(u16));

    std::string tmd_path = GetTitleMetadataPath(media_type, title_id);
    FileSys::TitleMetadata tmd;
    if (tmd.Load(tmd_path) != Loader::ResultStatus::Success) {
        LOG_ERROR(Service_AM, "Couldn't load TMD for title_id={:016X}, mediatype={}", title_id,
                  media_type);

        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push<u32>(-1); // TODO(PabloMK7): Find the right error code
        return;
    }
    u64 size_out = 0;
    // Get info for each content index requested
    for (std::size_t i = 0; i < content_count; i++) {
        if (content_requested[i] >= tmd.GetContentCount()) {
            LOG_ERROR(Service_AM, "Attempted to get info for non-existent content index {:04x}.",
                      content_requested[i]);

            IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
            rb.Push<u32>(-1); // TODO(PabloMK7): Find the right error code
            return;
        }
        if (!tmd.GetContentOptional(content_requested[i])) {
            LOG_ERROR(Service_AM, "Attempted to get info for non-optional content index {:04x}.",
                      content_requested[i]);

            IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
            rb.Push<u32>(-1); // TODO(PabloMK7): Find the right error code
            return;
        }

        size_out += tmd.GetContentSizeByIndex(content_requested[i]);
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(3, 0);
    rb.Push(ResultSuccess);
    rb.Push<u64>(size_out);
}

void Module::Interface::UpdateImportContentContexts(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const u32 content_count = rp.Pop<u32>();
    auto content_buf = rp.PopMappedBuffer();

    std::vector<u16> content_indices(content_count);
    content_buf.Read(content_indices.data(), 0, content_buf.GetSize());

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);

    LOG_WARNING(Service_AM, "(STUBBED)");
}

void Module::Interface::ExportTicketWrapped(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    const u32 ticket_buf_size = rp.Pop<u32>();
    const u32 keyiv_buf_size = rp.Pop<u32>();

    const u64 title_id = rp.Pop<u64>();
    const u64 ticket_id = rp.Pop<u64>();

    auto ticket_buf = rp.PopMappedBuffer();
    auto keyiv_buf = rp.PopMappedBuffer();

    LOG_DEBUG(Service_AM, "title_id={:016X} ticket_id={:016X}", title_id, ticket_id);

    u32 tid_high = static_cast<u32>(title_id >> 32);
    if ((tid_high & 0x00048001) == 0x00048001 || tid_high == 0x00040001 || (tid_high & 0x10) != 0) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrCodes::InvalidTIDInList, ErrorModule::AM, ErrorSummary::InvalidArgument,
                       ErrorLevel::Usage));
        return;
    }

    std::scoped_lock lock(am->am_lists_mutex);
    auto range = am->am_ticket_list.equal_range(title_id);
    auto it = range.first;
    for (; it != range.second; it++)
        if (it->second == ticket_id)
            break;
    if (it == range.second) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrorDescription::NotFound, ErrorModule::AM, ErrorSummary::InvalidState,
                       ErrorLevel::Permanent));
        return;
    }

    FileSys::Ticket ticket;
    if (ticket.Load(title_id, ticket_id) != Loader::ResultStatus::Success) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(Result(ErrorDescription::NotFound, ErrorModule::AM, ErrorSummary::InvalidState,
                       ErrorLevel::Permanent));
        return;
    }

    std::vector<u8> ticket_data(Common::AlignUp(ticket.GetSerializedSize(), 0x10));
    memcpy(ticket_data.data(), ticket.Serialize().data(), ticket.GetSerializedSize());

    std::vector<u8> key(0x10);
    std::vector<u8> iv(0x10);

    RAND_bytes(key.data(), static_cast<int>(key.size()));
    RAND_bytes(iv.data(), static_cast<int>(iv.size()));

    CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption e(key.data(), key.size(), iv.data());
    e.ProcessData(ticket_data.data(), ticket_data.data(), ticket_data.size());

    const auto& wrap_key = HW::RSA::GetTicketWrapSlot();
    u32 padding_len = static_cast<u32>(
        ((CryptoPP::Integer(wrap_key.GetModulus().data(), wrap_key.GetModulus().size()).BitCount() +
          7) /
         8) -
        (key.size() + iv.size()) - 3);

    std::vector<u8> m;
    m.reserve(3 + padding_len + (key.size() + iv.size()));
    m.push_back(0x00);
    m.push_back(0x01);
    for (u32 i = 0; i < padding_len; i++)
        m.push_back(0xFF);
    m.push_back(0x00);
    m.insert(m.end(), key.begin(), key.end());
    m.insert(m.end(), iv.begin(), iv.end());

    auto rsa_out = wrap_key.ModularExponentiation(m, static_cast<int>(m.size()));

    ticket_buf.Write(ticket_data.data(), 0,
                     std::min(static_cast<size_t>(ticket_buf_size), ticket_data.size()));
    keyiv_buf.Write(rsa_out.data(), 0,
                    std::min(static_cast<size_t>(keyiv_buf_size), rsa_out.size()));

    IPC::RequestBuilder rb = rp.MakeBuilder(3, 0);
    rb.Push(ResultSuccess);
    rb.Push(static_cast<u32>(ticket_data.size()));
    rb.Push(static_cast<u32>(rsa_out.size()));
}

Module::Module(Core::System& _system) : system(_system) {
    FileUtil::CreateFullPath(GetTicketDirectory());
    ScanForAllTitles();
    system_updater_mutex = system.Kernel().CreateMutex(false, "AM::SystemUpdaterMutex");
}

Module::~Module() {
    stop_scan_flag = true;
}

std::shared_ptr<Module> GetModule(Core::System& system) {
    auto am = system.ServiceManager().GetService<Service::AM::Module::Interface>("am:u");
    if (!am)
        return nullptr;
    return am->GetModule();
}

void InstallInterfaces(Core::System& system) {
    auto& service_manager = system.ServiceManager();
    auto am = std::make_shared<Module>(system);
    std::make_shared<AM_APP>(am)->InstallAsService(service_manager);
    std::make_shared<AM_NET>(am)->InstallAsService(service_manager);
    std::make_shared<AM_SYS>(am)->InstallAsService(service_manager);
    std::make_shared<AM_U>(am)->InstallAsService(service_manager);
}

} // namespace Service::AM
