// Copyright 2014-2024 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/archives.h"
#include "core/hle/service/act/act_u.h"

namespace Service::ACT {

ACT_U::ACT_U(std::shared_ptr<Module> act) : Module::Interface(std::move(act), "act:u") {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x0001, &ACT_U::Initialize, "Initialize"},
        {0x0002, &ACT_U::GetErrorCode, "GetErrorCode"},
        {0x0003, nullptr, "GetLastResponseCode"},
        {0x0005, nullptr, "GetCommonInfo"},
        {0x0006, &ACT_U::GetAccountInfo, "GetAccountInfo"},
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
        // clang-format on
    };
    RegisterHandlers(functions);
}

} // namespace Service::ACT

SERIALIZE_EXPORT_IMPL(Service::ACT::ACT_U)
