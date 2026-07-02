// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "client.h"

#include <rc_client.h>

#include "common/logging/log.h"

static uint32_t read_memory(uint32_t address, uint8_t* buffer, uint32_t num_bytes,
                            rc_client_t* client) {
    LOG_CRITICAL(RetroAchievements, "read_memory stub");
}

static void call_server(const rc_api_request_t* request, rc_client_server_callback_t callback,
                        void* callback_data, rc_client_t* client) {
    LOG_CRITICAL(RetroAchievements, "call_server stub");
}

static void log_message(const char* message, const rc_client_t* client) {
    LOG_INFO(RetroAchievements, "rcheevos message: \"{}\"", message);
}

namespace RetroAchievements {

Client::Client() {
    m_rc_client = rc_client_create(read_memory, call_server);

    rc_client_enable_logging(m_rc_client, RC_CLIENT_LOG_LEVEL_VERBOSE, log_message);
    rc_client_set_hardcore_enabled(m_rc_client, 0);
}

Client::~Client() {
    if (m_rc_client) {
        rc_client_destroy(m_rc_client);
        m_rc_client = nullptr;
    }
}

} // namespace RetroAchievements
