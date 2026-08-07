// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "client.h"

#include <cstring>
#include <string>
#include <utility>

#include <httplib.h>
#include <rc_client.h>
#include <rc_consoles.h>

#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "core/core.h"
#include "core/memory.h"

#define USE_RETRO_ACHIEVEMENTS_DEV_SERVER

// TODO: Make this use a numeric version as per
// https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration#user-agent-header
static const std::string user_agent = std::string("Azahar/") + Common::g_build_fullname;
static const httplib::Headers headers = httplib::Headers({{"User-Agent", user_agent}});

static uint32_t read_memory(uint32_t address, uint8_t* buffer, uint32_t num_bytes,
                            rc_client_t* rc_client) {
    LOG_DEBUG(RetroAchievements, "Reading {} bytes from 0x{:x}", num_bytes, address);

    Memory::MemorySystem& memory = Core::System::GetInstance().Memory();
    const u8* memory_ptr = memory.GetPhysicalPointer(address);

    if (memory_ptr == nullptr)
        return 0;

    std::memcpy(buffer, memory_ptr, num_bytes);
    return num_bytes;
}

static std::pair<std::string, std::string> parse_url(const char* full_url_cstr) {
    if (!full_url_cstr) {
        return {"", "/"};
    }

    std::string full_url(full_url_cstr);

    size_t protocol_end = full_url.find("://");
    size_t host_start = (protocol_end == std::string::npos) ? 0 : protocol_end + 3;

    size_t path_start = full_url.find('/', host_start);

    if (path_start == std::string::npos) {
        return {full_url, "/"};
    } else {
        return {full_url.substr(0, path_start), full_url.substr(path_start)};
    }
}

static void call_server(const rc_api_request_t* request, rc_client_server_callback_t callback,
                        void* callback_data, rc_client_t* rc_client) {
    const auto [base_url, path] = parse_url(request->url);

    httplib::Client http_client(base_url);

    LOG_DEBUG(RetroAchievements, "Server request: {} {}", request->post_data ? "POST" : "GET",
              request->url);

    httplib::Result result =
        request->post_data
            ? http_client.Post(path, headers, request->post_data, std::strlen(request->post_data),
                               request->content_type)
            : http_client.Get(path, headers);

    if (result) {
        LOG_DEBUG(RetroAchievements, "Server response status: {}", result->status);
        LOG_DEBUG(RetroAchievements, "Server response body: {}", result->body);

        rc_api_server_response_t server_response = {.body = result->body.c_str(),
                                                    .body_length = result->body.length(),
                                                    .http_status_code = result->status};
        callback(&server_response, callback_data);
    } else {
        LOG_ERROR(RetroAchievements, "httplib error: {}", result.error());
    }
}

static void log_message(const char* message, const rc_client_t* rc_client) {
    LOG_INFO(RetroAchievements, "rcheevos message: \"{}\"", message);
}

static void login_callback(int result, const char* error_message, rc_client_t* rc_client,
                           void* userdata) {
    RetroAchievements::Client* client = static_cast<RetroAchievements::Client*>(userdata);
    client->OnLoginCallback(result, error_message);
}

static void load_game_callback(int result, const char* error_message, rc_client_t* rc_client,
                               void* userdata) {
    RetroAchievements::Client* client = static_cast<RetroAchievements::Client*>(userdata);
    client->OnLoadGameCallback(result, error_message);
}

static void event_handler(const rc_client_event_t* event, rc_client_t* client) {
    auto* ra_client = static_cast<RetroAchievements::Client*>(rc_client_get_userdata(client));
    if (ra_client) {
        ra_client->OnEvent(event);
    }
}

namespace RetroAchievements {

Client::Client() {
    m_rc_client = rc_client_create(read_memory, call_server);

    rc_client_enable_logging(m_rc_client, RC_CLIENT_LOG_LEVEL_VERBOSE, log_message);
    rc_client_set_event_handler(m_rc_client, event_handler);
    rc_client_set_userdata(m_rc_client, this);
    rc_client_set_hardcore_enabled(m_rc_client, 0);

#ifdef USE_RETRO_ACHIEVEMENTS_DEV_SERVER
    rc_client_set_host(m_rc_client, "http://localhost:64000");
#endif
}

Client::~Client() {
    if (m_rc_client) {
        rc_client_destroy(m_rc_client);
        m_rc_client = nullptr;
    }
}

void Client::RegisterObserver(ClientObserver& observer) {
    m_observers.push_back(&observer);
}

void Client::AttemptLogin(const char* username, const char* password) {
    rc_client_begin_login_with_password(m_rc_client, username, password, login_callback, this);
}

void Client::AttemptLoginWithToken(const char* username, const char* token) {
    rc_client_begin_login_with_token(m_rc_client, username, token, login_callback, this);
}

void Client::LogOut() {
    rc_client_logout(m_rc_client);
    m_user = nullptr;
}

void Client::LoadGame(const char* file_path) {
    rc_client_begin_identify_and_load_game(m_rc_client, RC_CONSOLE_NINTENDO_3DS, file_path, NULL, 0,
                                           load_game_callback, this);
}

void Client::UnloadGame() {
    rc_client_unload_game(m_rc_client);
}

void Client::Reset() {
    rc_client_reset(m_rc_client);
}

void Client::DoFrame() {
    rc_client_do_frame(m_rc_client);
}

void Client::FetchImage(const char* url, ImageCallback callback) const {
    const auto [base_url, path] = parse_url(url);
    httplib::Client http_client(base_url);

    if (auto result = http_client.Get(path, headers)) {
        std::vector<uint8_t> image_data(result->body.begin(), result->body.end());
        callback(std::move(image_data));
    } else {
        LOG_ERROR(RetroAchievements, "Image fetch failed: {}", result.error());
    }
}

const rc_client_user_t* Client::GetUser() const {
    return m_user;
}

void Client::OnLoginCallback(int result, const char* error_message) {
    if (result == 0) {
        m_user = rc_client_get_user_info(m_rc_client);
        for (ClientObserver* observer : m_observers) {
            observer->OnLoginSucceeded(m_user);
        }
    } else {
        for (ClientObserver* observer : m_observers) {
            observer->OnLoginFailed(result, error_message);
        }
    }
}

void Client::OnLoadGameCallback(int result, const char* error_message) {
    if (result == 0) {
        const rc_client_game_t* game = rc_client_get_game_info(m_rc_client);
        for (ClientObserver* observer : m_observers) {
            observer->OnLoadGameSucceeded(game);
        }
    } else {
        for (ClientObserver* observer : m_observers) {
            observer->OnLoadGameFailed(result, error_message);
        }
    }
}

void Client::OnEvent(const rc_client_event_t* event) {
    LOG_DEBUG(RetroAchievements, "Event! ({})", event->type);
    for (ClientObserver* observer : m_observers) {
        observer->OnEvent(event);
    }
}

} // namespace RetroAchievements
