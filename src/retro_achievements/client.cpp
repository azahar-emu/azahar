// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "client.h"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

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

    Core::System& system = Core::System::GetInstance();
    system.Memory().ReadBlock(static_cast<VAddr>(address), buffer, num_bytes);

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
    m_rc_client = rc_client_create(read_memory, &Client::CallServer);

    rc_client_enable_logging(m_rc_client, RC_CLIENT_LOG_LEVEL_VERBOSE, log_message);
    rc_client_set_event_handler(m_rc_client, event_handler);
    rc_client_set_userdata(m_rc_client, this);
    rc_client_set_allow_background_memory_reads(m_rc_client, 0);
    rc_client_set_hardcore_enabled(m_rc_client, 0);

#ifdef USE_RETRO_ACHIEVEMENTS_DEV_SERVER
    rc_client_set_host(m_rc_client, "http://localhost:64000");
#endif
}

Client::~Client() {
    m_enabled = false;
    m_http_worker.WaitForRequests();

    if (m_rc_client) {
        rc_client_destroy(m_rc_client);
        m_rc_client = nullptr;
    }
}

void Client::CallServer(const rc_api_request_t* request, rc_client_server_callback_t callback,
                        void* callback_data, rc_client_t* rc_client) {
    auto* client = static_cast<Client*>(rc_client_get_userdata(rc_client));
    if (!client) {
        return;
    }

    const std::string url = request->url != nullptr ? request->url : "";

    const bool is_post = request->post_data != nullptr;
    const std::string post_data = is_post ? request->post_data : "";
    const std::string content_type = request->content_type != nullptr ? request->content_type : "";

    client->m_http_worker.QueueWork([url, is_post, post_data, content_type, callback,
                                     callback_data]() {
        const auto [base_url, path] = parse_url(url.c_str());
        httplib::Client http_client(base_url);

        LOG_DEBUG(RetroAchievements, "Server request: {} {}", is_post ? "POST" : "GET", url);

        httplib::Result result = is_post ? http_client.Post(path, headers, post_data.data(),
                                                            post_data.size(), content_type.c_str())
                                         : http_client.Get(path, headers);

        if (result) {
            LOG_DEBUG(RetroAchievements, "Server response status: {}", result->status);
            LOG_DEBUG(RetroAchievements, "Server response body: {}", result->body);

            rc_api_server_response_t server_response = {
                .body = result->body.c_str(),
                .body_length = result->body.length(),
                .http_status_code = result->status,
            };
            callback(&server_response, callback_data);
        } else {
            const std::string error_message = httplib::to_string(result.error());
            LOG_ERROR(RetroAchievements, "httplib error: {}", error_message);

            rc_api_server_response_t server_response = {
                .body = error_message.c_str(),
                .body_length = error_message.length(),
                .http_status_code = RC_API_SERVER_RESPONSE_CLIENT_ERROR,
            };
            callback(&server_response, callback_data);
        }
    });
}

void Client::RegisterObserver(ClientObserver& observer) {
    m_observers.push_back(&observer);
}

void Client::SetEnabled(bool enabled) {
    m_enabled = enabled;
}

void Client::AttemptLogin(const char* username, const char* password) {
    if (!m_enabled)
        return;

    rc_client_begin_login_with_password(m_rc_client, username, password, login_callback, this);
}

void Client::AttemptLoginWithToken(const char* username, const char* token) {
    if (!m_enabled)
        return;

    rc_client_begin_login_with_token(m_rc_client, username, token, login_callback, this);
}

void Client::LogOut() {
    rc_client_logout(m_rc_client);
    m_user = nullptr;
}

void Client::LoadGame(const char* file_path) {
    if (!m_enabled)
        return;

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
    if (!m_enabled) {
        rc_client_unload_game(m_rc_client);
        return;
    }

    rc_client_do_frame(m_rc_client);
}

void Client::FetchImage(const char* url, ImageCallback callback) const {
    if (!m_enabled)
        return;

    const std::string image_url = url ? url : "";
    m_http_worker.QueueWork([image_url, callback]() {
        const auto [base_url, path] = parse_url(image_url.c_str());
        httplib::Client http_client(base_url);

        std::vector<uint8_t> image_data;
        if (auto result = http_client.Get(path, headers)) {
            image_data.assign(result->body.begin(), result->body.end());
        } else {
            LOG_ERROR(RetroAchievements, "Image fetch failed: {}",
                      httplib::to_string(result.error()));
        }

        callback(std::move(image_data));
    });
}

const rc_client_user_t* Client::GetUser() const {
    return m_user;
}

void Client::OnLoginCallback(int result, const char* error_message) {
    if (!m_enabled)
        return;

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
    if (!m_enabled)
        return;

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
    if (!m_enabled)
        return;

    LOG_DEBUG(RetroAchievements, "Event! ({})", event->type);
    for (ClientObserver* observer : m_observers) {
        observer->OnEvent(event);
    }
}

} // namespace RetroAchievements
