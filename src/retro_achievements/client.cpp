// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "client.h"

#include <string>
#include <string_view>
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

static std::pair<std::string_view, std::string_view> parse_url(std::string_view full_url) {
    constexpr std::string_view protocol_separator = "://";
    const size_t protocol_end = full_url.find(protocol_separator);
    const size_t host_start =
        protocol_end == std::string_view::npos ? 0 : protocol_end + protocol_separator.size();
    const size_t path_start = full_url.find('/', host_start);

    if (path_start == std::string_view::npos) {
        return {full_url, "/"};
    }

    return {full_url.substr(0, path_start), full_url.substr(path_start)};
}

static uint32_t read_memory(uint32_t address, uint8_t* buffer, uint32_t num_bytes,
                            rc_client_t* rc_client) {
    LOG_DEBUG(RetroAchievements, "Reading {} bytes from 0x{:x}", num_bytes, address);

    Core::System& system = Core::System::GetInstance();
    system.Memory().ReadBlock(static_cast<VAddr>(address), buffer, num_bytes);

    return num_bytes;
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

static void call_server(const rc_api_request_t* request, rc_client_server_callback_t callback,
                        void* callback_data, rc_client_t* rc_client) {
    auto* client = static_cast<RetroAchievements::Client*>(rc_client_get_userdata(rc_client));
    if (!client) {
        return;
    }

    RetroAchievements::Client::HttpRequest http_request = {
        .url = request->url != nullptr ? request->url : "",
        .post_data = request->post_data != nullptr ? std::optional<std::string>{request->post_data}
                                                   : std::nullopt,
        .content_type = request->content_type != nullptr ? request->content_type : "",
    };

    LOG_DEBUG(RetroAchievements, "Server request: {} {}",
              http_request.post_data.has_value() ? "POST" : "GET", http_request.url);

    client->QueueHttpRequest(
        std::move(http_request),
        [callback, callback_data](RetroAchievements::Client::HttpResponse&& response) {
            if (response.success) {
                LOG_DEBUG(RetroAchievements, "Server response status: {}", response.status);
                LOG_DEBUG(RetroAchievements, "Server response body: {}", response.body);
            } else {
                LOG_ERROR(RetroAchievements, "httplib error: {}", response.body);
            }

            rc_api_server_response_t server_response = {
                .body = response.body.c_str(),
                .body_length = response.body.length(),
                .http_status_code =
                    response.success ? response.status : RC_API_SERVER_RESPONSE_CLIENT_ERROR,
            };
            callback(&server_response, callback_data);
        });
}

namespace RetroAchievements {

Client::Client() {
    m_rc_client = rc_client_create(read_memory, call_server);

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

void Client::QueueHttpRequest(HttpRequest&& request, HttpCallback callback) {
    m_http_worker.QueueWork([request, callback = std::move(callback)]() {
        const auto [base_url, path] = parse_url(request.url);

        httplib::Client http_client{std::string{base_url}};
        const std::string request_path{path};

        httplib::Result result =
            request.post_data.has_value()
                ? http_client.Post(request_path, headers, request.post_data->data(),
                                   request.post_data->size(), request.content_type.c_str())
                : http_client.Get(request_path, headers);

        if (result) {
            callback({
                .body = std::move(result->body),
                .status = result->status,
                .success = true,
            });
        } else {
            callback({
                .body = httplib::to_string(result.error()),
                .status = 0,
                .success = false,
            });
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

void Client::FetchImage(const char* url, ImageCallback callback) {
    if (!m_enabled)
        return;

    HttpRequest request = {
        .url = url != nullptr ? url : "",
        .post_data = std::nullopt,
        .content_type = "",
    };
    QueueHttpRequest(std::move(request), [callback = std::move(callback)](HttpResponse&& response) {
        std::vector<uint8_t> image_data;
        if (response.success) {
            image_data.assign(response.body.begin(), response.body.end());
        } else {
            LOG_ERROR(RetroAchievements, "Image fetch failed: {}", response.body);
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
