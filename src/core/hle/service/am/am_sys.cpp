// Copyright 2015-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/archives.h"
#include "core/hle/service/am/am_sys.h"

namespace Service::AM {

AM_SYS::AM_SYS(std::shared_ptr<Module> am) : Module::Interface(std::move(am), "am:sys", 5) {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x0001, &AM_SYS::GetNumPrograms, "GetNumPrograms"},
        {0x0002, &AM_SYS::GetProgramList, "GetProgramList"},
        {0x0003, &AM_SYS::GetProgramInfos, "GetProgramInfos"},
        {0x0004, &AM_SYS::DeleteUserProgram, "DeleteUserProgram"},
        {0x0005, &AM_SYS::GetProductCode, "GetProductCode"},
        {0x0006, nullptr, "GetStorageId"},
        {0x0007, &AM_SYS::DeleteTicket, "DeleteTicket"},
        {0x0008, &AM_SYS::GetNumTickets, "GetNumTickets"},
        {0x0009, &AM_SYS::GetTicketList, "GetTicketList"},
        {0x000A, &AM_SYS::GetDeviceID, "GetDeviceID"},
        {0x000B, &AM_SYS::GetNumImportTitleContexts, "GetNumImportTitleContexts"},
        {0x000C, &AM_SYS::GetImportTitleContextList, "GetImportTitleContextList"},
        {0x000D, &AM_SYS::GetImportTitleContexts, "GetImportTitleContexts"},
        {0x000E, &AM_SYS::DeleteImportTitleContext, "DeleteImportTitleContext"},
        {0x000F, &AM_SYS::GetNumImportContentContexts, "GetNumImportContentContexts"},
        {0x0010, &AM_SYS::GetImportContentContextList, "GetImportContentContextList"},
        {0x0011, &AM_SYS::GetImportContentContexts, "GetImportContentContexts"},
        {0x0012, nullptr, "DeleteImportContentContexts"},
        {0x0013, &AM_SYS::NeedsCleanup, "NeedsCleanup"},
        {0x0014, &AM_SYS::DoCleanup, "DoCleanup"},
        {0x0015, nullptr, "DeleteAllImportContexts"},
        {0x0016, nullptr, "DeleteAllTemporaryPrograms"},
        {0x0017, nullptr, "ImportTwlBackupLegacy"},
        {0x0018, nullptr, "InitializeTitleDatabase"},
        {0x0019, &AM_SYS::QueryAvailableTitleDatabase, "QueryAvailableTitleDatabase"},
        {0x001A, nullptr, "CalcTwlBackupSize"},
        {0x001B, nullptr, "ExportTwlBackup"},
        {0x001C, nullptr, "ImportTwlBackup"},
        {0x001D, nullptr, "DeleteAllTwlUserPrograms"},
        {0x001E, nullptr, "ReadTwlBackupInfo"},
        {0x001F, nullptr, "DeleteAllExpiredUserPrograms"},
        {0x0020, nullptr, "GetTwlArchiveResourceInfo"},
        {0x0021, &AM_SYS::GetPersonalizedTicketInfoList, "GetPersonalizedTicketInfoList"},
        {0x0022, nullptr, "DeleteAllImportContextsFiltered"},
        {0x0023, &AM_SYS::GetNumImportTitleContextsFiltered, "GetNumImportTitleContextsFiltered"},
        {0x0024, &AM_SYS::GetImportTitleContextListFiltered, "GetImportTitleContextListFiltered"},
        {0x0025, &AM_SYS::CheckContentRights, "CheckContentRights"},
        {0x0026, nullptr, "GetTicketLimitInfos"},
        {0x0027, nullptr, "GetDemoLaunchInfos"},
        {0x0028, nullptr, "ReadTwlBackupInfoEx"},
        {0x0029, nullptr, "DeleteUserProgramsAtomically"},
        {0x002A, nullptr, "GetNumExistingContentInfosSystem"},
        {0x002B, nullptr, "ListExistingContentInfosSystem"},
        {0x002C, &AM_SYS::GetProgramInfosIgnorePlatform, "GetProgramInfosIgnorePlatform"},
        {0x002D, &AM_SYS::CheckContentRightsIgnorePlatform, "CheckContentRightsIgnorePlatform"},
        {0x1001, &AM_SYS::GetDLCContentInfoCount, "GetDLCContentInfoCount"},
        {0x1002, &AM_SYS::FindDLCContentInfos, "FindDLCContentInfos"},
        {0x1003, &AM_SYS::ListDLCContentInfos, "ListDLCContentInfos"},
        {0x1004, &AM_SYS::DeleteContents, "DeleteContents"},
        {0x1005, &AM_SYS::GetDLCTitleInfos, "GetDLCTitleInfos"},
        {0x1006, nullptr, "GetNumDataTitleTickets"},
        {0x1007, &AM_SYS::ListDataTitleTicketInfos, "ListDataTitleTicketInfos"},
        {0x1008, nullptr, "GetItemRights"},
        {0x1009, nullptr, "IsDataTitleInUse"},
        {0x100A, nullptr, "IsExternalTitleDatabaseInitialized"},
        {0x100B, nullptr, "GetNumExistingContentInfos"},
        {0x100C, nullptr, "ListExistingContentInfos"},
        {0x100D, &AM_SYS::GetPatchTitleInfos, "GetPatchTitleInfos"},
        // clang-format on
    };
    RegisterHandlers(functions);
}

} // namespace Service::AM

SERIALIZE_EXPORT_IMPL(Service::AM::AM_SYS)
