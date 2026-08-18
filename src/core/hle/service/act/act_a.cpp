// Copyright 2016-2024 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/archives.h"
#include "core/hle/service/act/act_a.h"

namespace Service::ACT {

ACT_A::ACT_A(std::shared_ptr<Module> act) : Module::Interface(std::move(act), "act:a") {
    const FunctionInfo functions[] = {
        // act:u shared commands
        // clang-format off
        {0x0001, &ACT_A::Initialize, "Initialize"},
        {0x0002, &ACT_A::GetErrorCode, "GetErrorCode"},
        {0x0003, nullptr, "GetLastResponseCode"},
        {0x0005, nullptr, "GetCommonInfo"},
        {0x0006, &ACT_A::GetAccountInfo, "GetAccountInfo"},
        {0x0007, nullptr, "GetResultAsync"},
        {0x0008, nullptr, "GetMiiImageData"},
        {0x0009, nullptr, "SetNfsPassword"},
        {0x000B, nullptr, "AcquireEulaList"},
        {0x000C, nullptr, "AcquireTimeZoneList"},
        {0x000D, nullptr, "GenerateUuid"},
        {0x000F, nullptr, "FindSlotNoByUuid"},
        {0x0010, nullptr, "SaveData"},
        {0x0011, nullptr, "GetTransferableId"},
        {0x0012, nullptr, "AcquireNexServiceToken"},
        {0x0013, nullptr, "GetNexServiceToken"},
        {0x0014, nullptr, "AcquireIndependentServiceToken"},
        {0x0015, nullptr, "GetIndependentServiceToken"},
        {0x0016, nullptr, "AcquireAccountInfo"},
        {0x0017, nullptr, "AcquireAccountIdByPrincipalId"},
        {0x0018, nullptr, "AcquirePrincipalIdByAccountId"},
        {0x0019, nullptr, "AcquireMii"},
        {0x001A, nullptr, "AcquireAccountInfoEx"},
        {0x001D, nullptr, "InquireMailAddress"},
        {0x001E, nullptr, "AcquireEula"},
        {0x001F, nullptr, "AcquireEulaLanguageList"},
        // act:a
        {0x0402, nullptr, "CreateConsoleAccount"},
        {0x0403, nullptr, "CommitConsoleAccount"},
        {0x0404, nullptr, "UnbindServerAccount"},
        {0x0405, nullptr, "DeleteConsoleAccount"},
        {0x0407, nullptr, "UnloadConsoleAccount"},
        {0x0408, nullptr, "EnableAccountPasswordCache"},
        {0x0409, nullptr, "SetDefaultAccount"},
        {0x040A, nullptr, "ReplaceAccountId"},
        {0x040B, nullptr, "GetSupportContext"},
        {0x0412, nullptr, "UpdateMii"},
        {0x0413, nullptr, "UpdateMiiImage"},
        {0x0414, nullptr, "InquireAccountIdAvailability"},
        {0x0415, nullptr, "BindToNewServerAccount"},
        {0x0416, nullptr, "BindToExistentServerAccount"},
        {0x0417, nullptr, "InquireBindingToExistentServerAccount"},
        {0x041A, nullptr, "AcquireAccountTokenEx"},
        {0x041B, nullptr, "AgreeEula"},
        {0x041C, nullptr, "SyncAccountInfo"},
        {0x041E, nullptr, "UpdateAccountPassword"},
        {0x041F, nullptr, "ReissueAccountPassword"},
        {0x0420, nullptr, "SetAccountPasswordInput"},
        {0x0421, nullptr, "UploadMii"},
        {0x0423, nullptr, "ValidateMailAddress"},
        {0x0423, nullptr, "SendConfirmationMail"},
        {0x0428, nullptr, "ApproveByCreditCard"},
        {0x0428, nullptr, "SendCoppaCodeMail"},
        {0x042F, nullptr, "UpdateAccountInfoEx"},
        {0x0430, nullptr, "UpdateAccountMailAddress"},
        {0x0435, nullptr, "DeleteServerAccount"},
        // clang-format on
    };
    RegisterHandlers(functions);
}

} // namespace Service::ACT

SERIALIZE_EXPORT_IMPL(Service::ACT::ACT_A)
