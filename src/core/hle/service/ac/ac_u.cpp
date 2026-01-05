// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/archives.h"
#include "core/hle/service/ac/ac_u.h"

namespace Service::AC {

AC_U::AC_U(std::shared_ptr<Module> ac) : Module::Interface(std::move(ac), "ac:u", 10) {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x0001, &AC_U::CreateDefaultConfig, "CreateDefaultConfig"},
        {0x0004, &AC_U::ConnectAsync, "ConnectAsync"},
        {0x0005, &AC_U::GetConnectResult, "GetConnectResult"},
        {0x0007, nullptr, "CancelConnectAsync"},
        {0x0008, &AC_U::CloseAsync, "CloseAsync"},
        {0x0009, &AC_U::GetCloseResult, "GetCloseResult"},
        {0x000A, nullptr, "GetLastErrorCode"},
        {0x000C, &AC_U::GetStatus, "GetStatus"},
        {0x000D, &AC_U::GetWifiStatus, "GetWifiStatus"},
        {0x000E, nullptr, "GetCurrentAPInfo"},
        {0x0010, nullptr, "GetCurrentNZoneInfo"},
        {0x0011, nullptr, "GetNZoneApNumService"},
        {0x001D, nullptr, "ScanAPs"},
        {0x0024, nullptr, "AddDenyApType"},
        {0x0027, &AC_U::GetInfraPriority, "GetInfraPriority"},
        {0x002D, &AC_U::SetRequestEulaVersion, "SetRequestEulaVersion"},
        {0x0030, &AC_U::RegisterDisconnectEvent, "RegisterDisconnectEvent"},
        {0x0036, &AC_U::GetConnectingProxyEnable, "GetConnectingProxyEnable"},
        {0x0037, &AC_U::GetConnectingProxyAuthType, "GetConnectingProxyAuthType"},
        {0x0038, &AC_U::GetConnectingProxyPort, "GetConnectingProxyPort"},
        {0x0039, &AC_U::GetConnectingProxyHost, "GetConnectingProxyHost"},
        {0x003A, &AC_U::GetConnectingProxyUserName, "GetConnectingProxyUserName"},
        {0x003B, &AC_U::GetConnectingProxyPassword, "GetConnectingProxyPassword"},
        {0x003C, nullptr, "GetAPSSIDList"},
        {0x003E, &AC_U::IsConnected, "IsConnected"},
        {0x0040, &AC_U::SetClientVersion, "SetClientVersion"},
        // clang-format on
    };
    RegisterHandlers(functions);
}

} // namespace Service::AC

SERIALIZE_EXPORT_IMPL(Service::AC::AC_U)
