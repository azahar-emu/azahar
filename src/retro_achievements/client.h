// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <vector>

#include <rc_client.h>

#include "client_observer.h"

namespace RetroAchievements {

class Client {
public:
    explicit Client();
    ~Client();

    void RegisterObserver(ClientObserver& observer);

    void AttemptLogin(const char* username, const char* password);

    using ImageCallback = std::function<void(std::vector<uint8_t>&& image_data)>;
    void FetchImage(const char* url, ImageCallback callback) const;

    void OnLoginCallback(int result, const char* error_message) const;

private:
    rc_client_t* m_rc_client;

    std::vector<ClientObserver*> m_observers;
};

} // namespace RetroAchievements