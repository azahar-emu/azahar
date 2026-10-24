// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include "network/verify_user.h"

namespace Network::VerifyUser {

Backend::~Backend() = default;

NullBackend::~NullBackend() = default;

UserData NullBackend::LoadUserData([[maybe_unused]] const std::string& verify_UID,
                                   [[maybe_unused]] const std::string& token) {
    return {};
}

} // namespace Network::VerifyUser
