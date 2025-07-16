// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <boost/serialization/array.hpp>
#include <boost/serialization/shared_ptr.hpp>
#include "common/common_types.h"
#include "common/construct.h"
#include "common/swap.h"
#include "core/file_sys/cia_container.h"
#include "core/file_sys/file_backend.h"
#include "core/file_sys/ncch_container.h"
#include "core/global.h"
#include "core/hle/kernel/mutex.h"
#include "core/hle/result.h"
#include "core/hle/service/service.h"
#include "network/artic_base/artic_base_client.h"

namespace Core {
class System;
}

namespace FileUtil {
class IOFile;
}

namespace Service::FS {
enum class MediaType : u32;
}

namespace Kernel {
class Mutex;
}

namespace IPC {
class RequestParser;
}

namespace Service::AM {

namespace ErrCodes {
enum {
    InvalidImportState = 4,
    InvalidTID = 31,
    EmptyCIA = 32,
    TryingToUninstallSystemApp = 44,
    InvalidTIDInList = 60,
    InvalidCIAHeader = 104,
};
} // namespace ErrCodes

enum class CIAInstallState : u32 {
    InstallStarted,
    HeaderLoaded,
    CertLoaded,
    TicketLoaded,
    TMDLoaded,
    ContentWritten,
};

enum class InstallStatus : u32 {
    Success,
    ErrorFailedToOpenFile,
    ErrorFileNotFound,
    ErrorAborted,
    ErrorInvalid,
    ErrorEncrypted,
};

enum class ImportTitleContextState : u8 {
    NONE = 0,
    WAITING_FOR_IMPORT = 1,
    RESUMABLE = 2,
    WAITING_FOR_COMMIT = 3,
    ALREADY_EXISTS = 4,
    DELETING = 5,
    NEEDS_CLEANUP = 6,
};

struct ImportTitleContext {
    u64 title_id;
    u16 version;
    ImportTitleContextState state;
    u32 type;
    u64 size;
};
static_assert(sizeof(ImportTitleContext) == 0x18, "Invalid ImportTitleContext size");

struct ImportContentContext {
    u32 content_id;
    u16 index;
    ImportTitleContextState state;
    u64 size;
    u64 current_size;
};
static_assert(sizeof(ImportContentContext) == 0x18, "Invalid ImportContentContext size");

struct TitleInfo {
    u64_le tid;
    u64_le size;
    u16_le version;
    u16_le unused;
    u32_le type;
};
static_assert(sizeof(TitleInfo) == 0x18, "Title info structure size is wrong");

// Title ID valid length
constexpr std::size_t TITLE_ID_VALID_LENGTH = 16;

constexpr u64 TWL_TITLE_ID_FLAG = 0x0000800000000000ULL;

// Progress callback for InstallCIA, receives bytes written and total bytes
using ProgressCallback = void(std::size_t, std::size_t);

class NCCHCryptoFile final {
public:
    NCCHCryptoFile(const std::string& out_file, bool encrypted_content);

    void Write(const u8* buffer, std::size_t length);
    bool IsError() {
        return is_error;
    }

private:
    friend class CIAFile;
    std::unique_ptr<FileUtil::IOFile> file;
    bool is_error = false;
    bool is_not_ncch = false;
    bool decryption_authorized = false;

    std::size_t written = 0;

    NCCH_Header ncch_header{};
    std::size_t header_size = 0;
    bool header_parsed = false;

    bool is_encrypted = false;
    std::array<u8, 16>
        primary_key{}; // for decrypting exheader, exefs header and icon/banner section
    std::array<u8, 16> secondary_key{}; // for decrypting romfs and .code section
    std::array<u8, 16> exheader_ctr{};
    std::array<u8, 16> exefs_ctr{};
    std::array<u8, 16> romfs_ctr{};

    struct CryptoRegion {
        enum Type {
            EXHEADER = 0,
            EXEFS_HDR = 1,
            EXEFS_PRI = 2,
            EXEFS_SEC = 3,
            ROMFS = 4,
        };
        Type type;
        size_t offset;
        size_t size;
        size_t seek_from;
    };

    std::vector<CryptoRegion> regions;

    ExeFs_Header exefs_header{};
    std::size_t exefs_header_written = 0;
    bool exefs_header_processed = false;
};

class CIAFile;
void AuthorizeCIAFileDecryption(CIAFile* cia_file, Kernel::HLERequestContext& ctx);

// A file handled returned for CIAs to be written into and subsequently installed.
class CIAFile final : public FileSys::FileBackend {
public:
    class InstallResult {
    public:
        enum class Type {
            NONE,
            TIK,
            TMD,
            APP,
        };
        Type type{Type::NONE};
        std::string install_full_path{};
        Result result{0};
    };

    explicit CIAFile(Core::System& system_, Service::FS::MediaType media_type,
                     bool from_cdn = false);
    ~CIAFile();

    ResultVal<std::size_t> Read(u64 offset, std::size_t length, u8* buffer) const override;
    InstallResult WriteTicket();
    InstallResult WriteTitleMetadata(std::span<const u8> tmd_data, std::size_t offset);
    ResultVal<std::size_t> WriteContentData(u64 offset, std::size_t length, const u8* buffer);
    ResultVal<std::size_t> Write(u64 offset, std::size_t length, bool flush, bool update_timestamp,
                                 const u8* buffer) override;

    Result PrepareToImportContent(const FileSys::TitleMetadata& tmd);
    Result ProvideTicket(const FileSys::Ticket& ticket);
    Result ProvideTMDForAdditionalContent(const FileSys::TitleMetadata& tmd);
    const FileSys::TitleMetadata& GetTMD();
    FileSys::Ticket& GetTicket();
    CIAInstallState GetCiaInstallState() {
        return install_state;
    }

    ResultVal<std::size_t> WriteContentDataIndexed(u16 content_index, u64 offset,
                                                   std::size_t length, const u8* buffer);

    u64 GetSize() const override;
    bool SetSize(u64 size) const override;
    bool Close() override;
    void Flush() const override;

    void SetDone() {
        is_done = true;
    }

    const std::vector<InstallResult>& GetInstallResults() const {
        return install_results;
    }

private:
    friend void AuthorizeCIAFileDecryption(CIAFile* cia_file, Kernel::HLERequestContext& ctx);
    Core::System& system;

    // Sections (tik, tmd, contents) are being imported individually
    bool from_cdn;
    bool decryption_authorized;
    bool is_done = false;
    bool is_closed = false;
    bool is_additional_content = false;

    // Whether it's installing an update, and what step of installation it is at
    bool is_update = false;
    CIAInstallState install_state = CIAInstallState::InstallStarted;

    // How much has been written total, CIAContainer for the installing CIA, buffer of all data
    // prior to content data, how much of each content index has been written, and where the CIA
    // is being installed to
    u64 written = 0;
    FileSys::CIAContainer container;
    std::vector<u8> data;
    std::vector<u64> content_written;
    std::vector<std::string> content_file_paths;
    u16 current_content_index = -1;
    std::unique_ptr<NCCHCryptoFile> current_content_file;
    InstallResult current_content_install_result{};
    std::vector<InstallResult> install_results;
    Service::FS::MediaType media_type;

    class DecryptionState;
    std::unique_ptr<DecryptionState> decryption_state;
};

class CurrentImportingTitle {
public:
    explicit CurrentImportingTitle(Core::System& system_, u64 title_id_,
                                   Service::FS::MediaType media_type_)
        : cia_file(system_, media_type_, true), title_id(title_id_), media_type(media_type_),
          tmd_provided(false) {}

    CIAFile cia_file;
    u64 title_id;
    Service::FS::MediaType media_type;
    bool tmd_provided;
};

// A file handled returned for Tickets to be written into and subsequently installed.
class TicketFile final : public FileSys::FileBackend {
public:
    explicit TicketFile();
    ~TicketFile();

    ResultVal<std::size_t> Read(u64 offset, std::size_t length, u8* buffer) const override;
    ResultVal<std::size_t> Write(u64 offset, std::size_t length, bool flush, bool update_timestamp,
                                 const u8* buffer) override;
    u64 GetSize() const override;
    bool SetSize(u64 size) const override;
    bool Close() override;
    void Flush() const override;

    Result Commit();
    u64 GetTitleID() {
        return title_id;
    }
    u64 GetTicketID() {
        return ticket_id;
    }

private:
    u64 written = 0;
    u64 title_id, ticket_id;
    std::vector<u8> data;
};

// A file handled returned for TMDs to be written into and subsequently installed.
class TMDFile final : public FileSys::FileBackend {
public:
    explicit TMDFile(const std::shared_ptr<CurrentImportingTitle>& import_context)
        : importing_title(import_context) {}
    ~TMDFile();

    ResultVal<std::size_t> Read(u64 offset, std::size_t length, u8* buffer) const override;
    ResultVal<std::size_t> Write(u64 offset, std::size_t length, bool flush, bool update_timestamp,
                                 const u8* buffer) override;
    u64 GetSize() const override;
    bool SetSize(u64 size) const override;
    bool Close() override;
    void Flush() const override;

    Result Commit();

private:
    u64 written = 0;
    std::vector<u8> data;
    std::shared_ptr<CurrentImportingTitle> importing_title;
};

// A file handled returned for contents to be written into and subsequently installed.
class ContentFile final : public FileSys::FileBackend {
public:
    explicit ContentFile(const std::shared_ptr<CurrentImportingTitle>& import_context, u16 index_,
                         ImportContentContext& import_context_)
        : import_context(import_context_), importing_title(import_context), index(index_) {}
    ~ContentFile();

    ResultVal<std::size_t> Read(u64 offset, std::size_t length, u8* buffer) const override;
    ResultVal<std::size_t> Write(u64 offset, std::size_t length, bool flush, bool update_timestamp,
                                 const u8* buffer) override;
    u64 GetSize() const override;
    bool SetSize(u64 size) const override;
    bool Close() override;
    void Flush() const override;

    void Cancel(FS::MediaType media_type, u64 title_id);

    ImportContentContext& GetImportContext() {
        return import_context;
    }

    void SetWritten(u64 written_) {
        written = written_;
    }

private:
    ImportContentContext& import_context;

    u64 written = 0;
    std::shared_ptr<CurrentImportingTitle> importing_title;
    u16 index;
};

/**
 * Installs a CIA file from a specified file path.
 * @param path file path of the CIA file to install
 * @param update_callback callback function called during filesystem write
 * @returns bool whether the install was successful
 */
InstallStatus InstallCIA(const std::string& path,
                         std::function<ProgressCallback>&& update_callback = nullptr);

/**
 * Checks if the provided path is a valid CIA file
 * that can be installed.
 * @param path file path of the CIA file to check to install
 */
InstallStatus CheckCIAToInstall(const std::string& path, bool& is_compressed,
                                bool check_encryption);

/**
 * Get CIA metadata information from file.
 */
ResultVal<std::pair<TitleInfo, std::unique_ptr<Loader::SMDH>>> GetCIAInfos(const std::string& path);

/**
 * Get the update title ID for a title
 * @param titleId the title ID
 * @returns The update title ID
 */
u64 GetTitleUpdateId(u64 title_id);

/**
 * Get the mediatype for an installed title
 * @param titleId the installed title ID
 * @returns MediaType which the installed title will reside on
 */
Service::FS::MediaType GetTitleMediaType(u64 titleId);

/**
 * Get the .tik path for a title_id and ticket_id.
 */
std::string GetTicketPath(u64 title_id, u64 ticket_id);

/**
 * Get the .tmd path for a title
 * @param media_type the media the title exists on
 * @param tid the title ID to get
 * @param update set true if the incoming TMD should be used instead of the current TMD
 * @returns string path to the .tmd file if it exists, otherwise a path to create one is given.
 */
std::string GetTitleMetadataPath(Service::FS::MediaType media_type, u64 tid, bool update = false);

/**
 * Get the .app path for a title's installed content index.
 * @param media_type the media the title exists on
 * @param tid the title ID to get
 * @param index the content index to get
 * @param update set true if the incoming TMD should be used instead of the current TMD
 * @returns string path to the .app file
 */
std::string GetTitleContentPath(FS::MediaType media_type, u64 tid, std::size_t index = 0,
                                bool update = false);

/**
 * Get the folder for a title's installed content.
 * @param media_type the media the title exists on
 * @param tid the title ID to get
 * @returns string path to the title folder
 */
std::string GetTitlePath(Service::FS::MediaType media_type, u64 tid);

/**
 * Get the title/ folder for a storage medium.
 * @param media_type the storage medium to get the path for
 * @returns string path to the folder
 */
std::string GetMediaTitlePath(Service::FS::MediaType media_type);

/**
 * Uninstalls the specified title.
 * @param media_type the storage medium the title is installed to
 * @param title_id the title ID to uninstall
 * @return result of the uninstall operation
 */
Result UninstallProgram(const FS::MediaType media_type, const u64 title_id);

class Module final {
public:
    explicit Module(Core::System& system);
    ~Module();

    class Interface : public ServiceFramework<Interface> {
    public:
        Interface(std::shared_ptr<Module> am, const char* name, u32 max_session);
        ~Interface();

        std::shared_ptr<Module> GetModule() const {
            return am;
        }

        void UseArticClient(std::shared_ptr<Network::ArticBase::Client>& client) {
            artic_client = client;
        }

    protected:
        void GetProgramInfosImpl(Kernel::HLERequestContext& ctx, bool ignore_platform);

        void CommitImportTitlesImpl(Kernel::HLERequestContext& ctx, bool is_update_firm_auto,
                                    bool is_titles);

        /**
         * AM::GetNumPrograms service function
         * Gets the number of installed titles in the requested media type
         *  Inputs:
         *      0 : Command header (0x00010040)
         *      1 : Media type to load the titles from
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : The number of titles in the requested media type
         */
        void GetNumPrograms(Kernel::HLERequestContext& ctx);

        /**
         * AM::FindDLCContentInfos service function
         * Explicitly checks that TID high value is 0004008C or an error is returned.
         *  Inputs:
         *      1 : MediaType
         *    2-3 : u64, Title ID
         *      4 : Content count
         *      6 : Content IDs pointer
         *      8 : Content Infos pointer
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         */
        void FindDLCContentInfos(Kernel::HLERequestContext& ctx);

        /**
         * AM::ListDLCContentInfos service function
         * Explicitly checks that TID high value is 0004008C or an error is returned.
         *  Inputs:
         *      1 : Content count
         *      2 : MediaType
         *    3-4 : u64, Title ID
         *      5 : Start Index
         *      7 : Content Infos pointer
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : Number of content infos returned
         */
        void ListDLCContentInfos(Kernel::HLERequestContext& ctx);

        /**
         * AM::DeleteContents service function
         *  Inputs:
         *      1 : MediaType
         *    2-3 : u64, Title ID
         *      4 : Content count
         *      6 : Content IDs pointer
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         */
        void DeleteContents(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetProgramList service function
         * Loads information about the desired number of titles from the desired media type into an
         * array
         *  Inputs:
         *      1 : Title count
         *      2 : Media type to load the titles from
         *      4 : Title IDs output pointer
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : The number of titles loaded from the requested media type
         */
        void GetProgramList(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetProgramInfos service function
         *  Inputs:
         *      1 : u8 Mediatype
         *      2 : Total titles
         *      4 : TitleIDList pointer
         *      6 : TitleList pointer
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         */
        void GetProgramInfos(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetProgramInfosIgnorePlatform service function
         *  Inputs:
         *      1 : u8 Mediatype
         *      2 : Total titles
         *      4 : TitleIDList pointer
         *      6 : TitleList pointer
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         */
        void GetProgramInfosIgnorePlatform(Kernel::HLERequestContext& ctx);

        /**
         * AM::DeleteUserProgram service function
         * Deletes a user program
         *  Inputs:
         *      1 : Media Type
         *      2-3 : Title ID
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         */
        void DeleteUserProgram(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetProductCode service function
         * Gets the product code of a title
         *  Inputs:
         *      1 : Media Type
         *      2-3 : Title ID
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2-5 : Product Code
         */
        void GetProductCode(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetDLCTitleInfos service function
         * Wrapper for AM::GetProgramInfos, explicitly checks that TID high value is 0004008C.
         *  Inputs:
         *      1 : u8 Mediatype
         *      2 : Total titles
         *      4 : TitleIDList pointer
         *      6 : TitleList pointer
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         */
        void GetDLCTitleInfos(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetPatchTitleInfos service function
         * Wrapper for AM::GetProgramInfos, explicitly checks that TID high value is 0004000E.
         *  Inputs:
         *      1 : u8 Mediatype
         *      2 : Total titles
         *      4 : TitleIDList input pointer
         *      6 : TitleList output pointer
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : TitleIDList input pointer
         *      4 : TitleList output pointer
         */
        void GetPatchTitleInfos(Kernel::HLERequestContext& ctx);

        /**
         * AM::ListDataTitleTicketInfos service function
         *  Inputs:
         *      1 : Ticket count
         *    2-3 : u64, Title ID
         *      4 : Start Index?
         *      5 : (TicketCount * 24) << 8 | 0x4
         *      6 : Ticket Infos pointer
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : Number of ticket infos returned
         */
        void ListDataTitleTicketInfos(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetDLCContentInfoCount service function
         * Explicitly checks that TID high value is 0004008C or an error is returned.
         *  Inputs:
         *      0 : Command header (0x100100C0)
         *      1 : MediaType
         *    2-3 : u64, Title ID
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : Number of content infos plus one
         */
        void GetDLCContentInfoCount(Kernel::HLERequestContext& ctx);

        /**
         * AM::DeleteTicket service function
         *  Inputs:
         *    1-2 : u64, Title ID
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         */
        void DeleteTicket(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetNumTickets service function
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : Number of tickets
         */
        void GetNumTickets(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetTicketList service function
         *  Inputs:
         *      1 : Number of TicketList
         *      2 : Number to skip
         *      4 : TicketList pointer
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : Total TicketList
         */
        void GetTicketList(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetDeviceID service function
         *  Inputs:
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : Unknown
         *      3 : DeviceID
         */
        void GetDeviceID(Kernel::HLERequestContext& ctx);

        void GetNumImportTitleContextsImpl(IPC::RequestParser& rp, FS::MediaType media_type,
                                           bool include_installing, bool include_finalizing);

        void GetImportTitleContextListImpl(IPC::RequestParser& rp, FS::MediaType media_type,
                                           u32 list_count, bool include_installing,
                                           bool include_finalizing);

        void GetNumImportTitleContexts(Kernel::HLERequestContext& ctx);

        void GetImportTitleContextList(Kernel::HLERequestContext& ctx);

        void GetImportTitleContexts(Kernel::HLERequestContext& ctx);

        void DeleteImportTitleContext(Kernel::HLERequestContext& ctx);

        void GetNumImportContentContextsImpl(IPC::RequestParser& rp, u64 title_id,
                                             FS::MediaType media_type);

        void GetImportContentContextListImpl(IPC::RequestParser& rp, u32 list_count, u64 title_id,
                                             FS::MediaType media_type);

        void GetImportContentContextsImpl(IPC::RequestParser& rp, u32 list_count, u64 title_id,
                                          FS::MediaType media_type);

        void GetNumImportContentContexts(Kernel::HLERequestContext& ctx);

        void GetImportContentContextList(Kernel::HLERequestContext& ctx);

        void GetImportContentContexts(Kernel::HLERequestContext& ctx);

        /**
         * AM::NeedsCleanup service function
         *  Inputs:
         *      1 : Media Type
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : bool, Needs Cleanup
         */
        void NeedsCleanup(Kernel::HLERequestContext& ctx);

        /**
         * AM::DoCleanup service function
         *  Inputs:
         *      1 : Media Type
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         */
        void DoCleanup(Kernel::HLERequestContext& ctx);

        /**
         * AM::QueryAvailableTitleDatabase service function
         *  Inputs:
         *      1 : Media Type
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : Boolean, database availability
         */
        void QueryAvailableTitleDatabase(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetPersonalizedTicketInfoList service function
         *  Inputs:
         *      1 : Count
         *      2-3 : Buffer
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : Out count
         */
        void GetPersonalizedTicketInfoList(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetNumImportTitleContextsFiltered service function
         *  Inputs:
         *      1 : Count
         *      2 : Filter
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : Num import titles
         */
        void GetNumImportTitleContextsFiltered(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetImportTitleContextListFiltered service function
         *  Inputs:
         *      1 : Count
         *      2 : Media type
         *      3 : filter
         *      4-5 : Buffer
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : Out count
         *      3-4 : Out buffer
         */
        void GetImportTitleContextListFiltered(Kernel::HLERequestContext& ctx);

        /**
         * AM::CheckContentRights service function
         *  Inputs:
         *      1-2 : Title ID
         *      3 : Content Index
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : Boolean, whether we have rights to this content
         */
        void CheckContentRights(Kernel::HLERequestContext& ctx);

        /**
         * AM::CheckContentRightsIgnorePlatform service function
         *  Inputs:
         *      1-2 : Title ID
         *      3 : Content Index
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : Boolean, whether we have rights to this content
         */
        void CheckContentRightsIgnorePlatform(Kernel::HLERequestContext& ctx);

        /**
         * AM::BeginImportProgram service function
         * Begin importing from a CTR Installable Archive
         *  Inputs:
         *      0 : Command header (0x04020040)
         *      1 : Media type to install title to
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2-3 : CIAFile handle for application to write to
         */
        void BeginImportProgram(Kernel::HLERequestContext& ctx);

        /**
         * AM::BeginImportProgramTemporarily service function
         * Begin importing from a CTR Installable Archive into the temporary title database
         *  Inputs:
         *      0 : Command header (0x04030000)
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2-3 : CIAFile handle for application to write to
         */
        void BeginImportProgramTemporarily(Kernel::HLERequestContext& ctx);

        /**
         * AM::EndImportProgram service function
         * Finish importing from a CTR Installable Archive
         *  Inputs:
         *      0 : Command header (0x04050002)
         *      1-2 : CIAFile handle application wrote to
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         */
        void EndImportProgram(Kernel::HLERequestContext& ctx);

        /**
         * AM::EndImportProgramWithoutCommit service function
         * Finish importing from a CTR Installable Archive
         *  Inputs:
         *      0 : Command header (0x04060002)
         *      1-2 : CIAFile handle application wrote to
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         */
        void EndImportProgramWithoutCommit(Kernel::HLERequestContext& ctx);

        /**
         * AM::CommitImportPrograms service function
         * Commits changes from the temporary title database to the real title database (title.db).
         * This is a no-op for us, we don't use title.db
         *  Inputs:
         *      0 : Command header (0x040700C2)
         *      1 : Media type
         *      2 : Title count
         *      3 : Database type
         *    4-5 : Title list buffer
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         */
        void CommitImportPrograms(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetProgramInfoFromCia service function
         * Get TitleInfo from a CIA file handle
         *  Inputs:
         *      0 : Command header (0x04080042)
         *      1 : Media type of the title
         *      2-3 : File handle CIA data can be read from
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2-8: TitleInfo structure
         */
        void GetProgramInfoFromCia(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetSystemMenuDataFromCia service function
         * Loads a CIA file's SMDH data into a specified buffer
         *  Inputs:
         *      0 : Command header (0x04090004)
         *      1-2 : File handle CIA data can be read from
         *      3-4 : Output buffer
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         */
        void GetSystemMenuDataFromCia(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetDependencyListFromCia service function
         * Loads a CIA's dependency list into a specified buffer
         *  Inputs:
         *      0 : Command header (0x040A0002)
         *      1-2 : File handle CIA data can be read from
         *      64-65 : Output buffer
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         */
        void GetDependencyListFromCia(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetTransferSizeFromCia service function
         * Returns the total expected transfer size up to the CIA meta offset from a CIA
         *  Inputs:
         *      0 : Command header (0x040B0002)
         *      1-2 : File handle CIA data can be read from
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2-3 : Transfer size
         */
        void GetTransferSizeFromCia(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetCoreVersionFromCia service function
         * Returns the core version from a CIA
         *  Inputs:
         *      0 : Command header (0x040C0002)
         *      1-2 : File handle CIA data can be read from
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : Core version
         */
        void GetCoreVersionFromCia(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetRequiredSizeFromCia service function
         * Returns the required amount of free space required to install a given CIA file
         *  Inputs:
         *      0 : Command header (0x040D0042)
         *      1 : Media type to install title to
         *      2-3 : File handle CIA data can be read from
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2-3 : Required free space for CIA
         */
        void GetRequiredSizeFromCia(Kernel::HLERequestContext& ctx);

        void CommitImportProgramsAndUpdateFirmwareAuto(Kernel::HLERequestContext& ctx);

        /**
         * AM::DeleteProgram service function
         * Deletes a program
         *  Inputs:
         *      0 : Command header (0x041000C0)
         *      1 : Media type
         *      2-3 : Title ID
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         */
        void DeleteProgram(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetSystemUpdaterMutex service function
         *  Inputs:
         *      0 : Command header (0x04120000)
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : Copy handle descriptor
         *      3 : System updater mutex
         */
        void GetSystemUpdaterMutex(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetMetaSizeFromCia service function
         * Returns the size of a given CIA's meta section
         *  Inputs:
         *      0 : Command header (0x04130002)
         *      1-2 : File handle CIA data can be read from
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : Meta section size
         */
        void GetMetaSizeFromCia(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetMetaDataFromCia service function
         * Loads meta section data from a CIA file into a given buffer
         *  Inputs:
         *      0 : Command header (0x04140044)
         *      1-2 : File handle CIA data can be read from
         *      3-4 : Output buffer
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         */
        void GetMetaDataFromCia(Kernel::HLERequestContext& ctx);

        /**
         * AM::BeginImportTicket service function
         *  Inputs:
         *      1 : Media type to install title to
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2-3 : TicketHandle handle for application to write to
         */
        void BeginImportTicket(Kernel::HLERequestContext& ctx);

        /**
         * AM::EndImportTicket service function
         *  Inputs:
         *      1-2 : TicketHandle handle application wrote to
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         */
        void EndImportTicket(Kernel::HLERequestContext& ctx);

        void BeginImportTitle(Kernel::HLERequestContext& ctx);

        void StopImportTitle(Kernel::HLERequestContext& ctx);

        void ResumeImportTitle(Kernel::HLERequestContext& ctx);

        void CancelImportTitle(Kernel::HLERequestContext& ctx);

        void EndImportTitle(Kernel::HLERequestContext& ctx);

        void CommitImportTitles(Kernel::HLERequestContext& ctx);

        void BeginImportTmd(Kernel::HLERequestContext& ctx);

        void EndImportTmd(Kernel::HLERequestContext& ctx);

        void CreateImportContentContexts(Kernel::HLERequestContext& ctx);

        void BeginImportContent(Kernel::HLERequestContext& ctx);

        void ResumeImportContent(Kernel::HLERequestContext& ctx);

        void StopImportContent(Kernel::HLERequestContext& ctx);

        void CancelImportContent(Kernel::HLERequestContext& ctx);

        void EndImportContent(Kernel::HLERequestContext& ctx);

        void GetNumCurrentImportContentContexts(Kernel::HLERequestContext& ctx);

        void GetCurrentImportContentContextList(Kernel::HLERequestContext& ctx);

        void GetCurrentImportContentContexts(Kernel::HLERequestContext& ctx);

        void Sign(Kernel::HLERequestContext& ctx);

        /**
         * AM::GetDeviceCert service function
         *  Inputs:
         *  Outputs:
         *      1 : Result, 0 on success, otherwise error code
         *      2 : Unknown
         *      3-4 : Device cert
         */
        void GetDeviceCert(Kernel::HLERequestContext& ctx);

        void CommitImportTitlesAndUpdateFirmwareAuto(Kernel::HLERequestContext& ctx);

        void DeleteTicketId(Kernel::HLERequestContext& ctx);

        void GetNumTicketIds(Kernel::HLERequestContext& ctx);

        void GetTicketIdList(Kernel::HLERequestContext& ctx);

        void GetNumTicketsOfProgram(Kernel::HLERequestContext& ctx);

        void ListTicketInfos(Kernel::HLERequestContext& ctx);

        void GetNumCurrentContentInfos(Kernel::HLERequestContext& ctx);

        void FindCurrentContentInfos(Kernel::HLERequestContext& ctx);

        void ListCurrentContentInfos(Kernel::HLERequestContext& ctx);

        void CalculateContextRequiredSize(Kernel::HLERequestContext& ctx);

        void UpdateImportContentContexts(Kernel::HLERequestContext& ctx);

        void ExportTicketWrapped(Kernel::HLERequestContext& ctx);

    protected:
        std::shared_ptr<Module> am;

        // Placed on the interface level so that only am:net and am:app have it.
        std::shared_ptr<Network::ArticBase::Client> artic_client = nullptr;
    };

    void ForceO3DSDeviceID() {
        force_old_device_id = true;
    }

    void ForceN3DSDeviceID() {
        force_new_device_id = true;
    }

private:
    void ScanForTickets();

    void ScanForTicketsImpl();

    /**
     * Scans the for titles in a storage medium for listing.
     * @param media_type the storage medium to scan
     */
    void ScanForTitles(Service::FS::MediaType media_type);

    void ScanForTitlesImpl(Service::FS::MediaType media_type);

    /**
     * Scans all storage mediums for titles for listing.
     */
    void ScanForAllTitles();

    Core::System& system;
    bool cia_installing = false;
    bool force_old_device_id = false;
    bool force_new_device_id = false;

    std::atomic<bool> stop_scan_flag = false;
    std::future<void> scan_tickets_future;
    std::future<void> scan_titles_future;
    std::future<void> scan_all_future;
    std::mutex am_lists_mutex;
    std::array<std::vector<u64_le>, 3> am_title_list;
    std::multimap<u64, u64> am_ticket_list;

    std::shared_ptr<Kernel::Mutex> system_updater_mutex;
    std::shared_ptr<CurrentImportingTitle> importing_title;
    std::map<u64, ImportTitleContext> import_title_contexts;
    std::multimap<u64, ImportContentContext> import_content_contexts;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int);
    friend class boost::serialization::access;
};

std::shared_ptr<Module> GetModule(Core::System& system);

void InstallInterfaces(Core::System& system);

} // namespace Service::AM

BOOST_CLASS_EXPORT_KEY(Service::AM::Module)
SERVICE_CONSTRUCT(Service::AM::Module)
