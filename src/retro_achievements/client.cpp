// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "client.h"

#include <cstring>
#include <string>
#include <utility>

#include <httplib.h>
#include <rc_client.h>

#include "common/logging/log.h"
#include "common/scm_rev.h"

// TODO: Make this use a numeric version as per
// https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration#user-agent-header
static const std::string user_agent = std::string("Azahar/") + Common::g_build_fullname;
static const httplib::Headers headers = httplib::Headers({{"User-Agent", user_agent}});

static uint32_t read_memory(uint32_t address, uint8_t* buffer, uint32_t num_bytes,
                            rc_client_t* rc_client) {
    LOG_CRITICAL(RetroAchievements, "read_memory stub");
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

void Client::RegisterObserver(ClientObserver& observer) {
    m_observers.push_back(&observer);
}

void Client::AttemptLogin(const char* username, const char* password) {
    rc_client_begin_login_with_password(m_rc_client, username, password, login_callback, this);
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

void Client::OnLoginCallback(int result, const char* error_message) const {
    if (result == 0) {
        const rc_client_user_t* user = rc_client_get_user_info(m_rc_client);
        for (ClientObserver* observer : m_observers) {
            observer->OnLoginSucceeded(user);
        }
    } else {
        for (ClientObserver* observer : m_observers) {
            observer->OnLoginFailed(result, error_message);
        }
    }
}

} // namespace RetroAchievements
