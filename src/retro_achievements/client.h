// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
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

    void SetEnabled(bool enabled);

    void AttemptLogin(const char* username, const char* password);
    void AttemptLoginWithToken(const char* username, const char* token);
    void LogOut();

    void LoadGame(const char* file_path);
    void UnloadGame();
    void Reset();
    void DoFrame();

    using ImageCallback = std::function<void(std::vector<uint8_t>&& image_data)>;
    void FetchImage(const char* url, ImageCallback callback) const;

    const rc_client_user_t* GetUser() const;

    void OnLoginCallback(int result, const char* error_message);
    void OnLoadGameCallback(int result, const char* error_message);
    void OnEvent(const rc_client_event_t* event);

private:
    rc_client_t* m_rc_client;
    const rc_client_user_t* m_user = nullptr;
    std::atomic_bool m_enabled = false;

    std::vector<ClientObserver*> m_observers;
};

} // namespace RetroAchievements
