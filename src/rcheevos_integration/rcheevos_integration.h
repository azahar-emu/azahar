#pragma once

namespace Core {
class System;
}

struct rc_client_t;

class RcheevosClient {
public:
    explicit RcheevosClient(const Core::System& system);
    ~RcheevosClient();

    void InitializeClient();
    void LogInRetroachievementsUser(const char* username, const char* password);
private:
    const Core::System& system;
    rc_client_t* rc_client = nullptr;
};
