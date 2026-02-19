#pragma once

namespace Core {
class System;
}

struct rc_client_t;

namespace RetroAchievements
{
    class Client {
    public:
        explicit Client(const Core::System& system);
        ~Client();

        void Initialize();

        void LogInUser(const char* username, const char* password);
    private:
        const Core::System& system;
        rc_client_t* rc_client = nullptr;
    };
} // namespace RetroAchievements
