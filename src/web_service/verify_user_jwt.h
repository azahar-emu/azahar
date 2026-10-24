// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <fmt/format.h>
#include "network/verify_user.h"
#include "web_service/web_backend.h"

namespace WebService {

class VerifyUserJWT final : public Network::VerifyUser::Backend {
public:
    VerifyUserJWT(const std::string& host);
    ~VerifyUserJWT() = default;

    Network::VerifyUser::UserData LoadUserData(const std::string& verify_UID,
                                               const std::string& token) override;

private:
    std::string pub_key;
};

} // namespace WebService
