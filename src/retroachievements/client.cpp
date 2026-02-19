#include "client.h"

#include <cstring>
#include <string>

#include <httplib.h>
#include <rc_client.h>
#include <rc_error.h>

#include "common/logging/log.h"
#include "common/scm_rev.h"

namespace RetroAchievements {
namespace Callbacks {
// This is the function the rc_client will use to read memory for the emulator. we don't need it
// yet, so just provide a dummy function that returns "no memory read".
static uint32_t read_memory(uint32_t address, uint8_t* buffer, uint32_t num_bytes,
                            rc_client_t* client) {
    // TODO: implement later
    LOG_DEBUG(RetroAchievements, "Attempting to read memory.");

    return 0;
}

static void server_call(const rc_api_request_t* request, rc_client_server_callback_t callback,
                        void* callback_data, rc_client_t* rc_client) {
    LOG_DEBUG(RetroAchievements, "Attempting to call server.");

    std::string user_agent =
        std::string("Azahar/") +
        Common::
            g_build_fullname; // TODO: Make this a numeric version as per
                              // https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration#user-agent-header

    // TODO: Should make this async?
    // TODO: Use a persistent client since base URL will maybe be the same? Or instead just need to
    // parse the URL into scheme-host-port and path.

    // httplib::Client client(request->url);
    httplib::Client client("https://retroachievements.org");

    httplib::Result result;
    if (request->post_data) {
        result = client.Post("/dorequest.php", request->post_data, std::strlen(request->post_data),
                             request->content_type);
    } else {
        result = client.Get("...");
    }

    if (result) {
        LOG_DEBUG(RetroAchievements, "Status: {}", result->status);
        LOG_DEBUG(RetroAchievements, "Body: {}", result->body);

        rc_api_server_response_t server_response = {.body = result->body.c_str(),
                                                    .body_length = result->body.length(),
                                                    .http_status_code = result->status};
        callback(&server_response, callback_data);
    } else {
        LOG_DEBUG(RetroAchievements, "HTTP error {}", result.error());
    }
}

// Write log messages to the console
static void log_message(const char* message, const rc_client_t* client) {
    LOG_DEBUG(RetroAchievements, "RetroAchievements internal message: \"{}\"", message);
}
} // namespace Callbacks

Client::Client(const Core::System& _system) : system{_system} {}

Client::~Client() {
    if (rc_client) {
        rc_client_destroy(rc_client);
        rc_client = NULL;
    }
}

void Client::Initialize() {
    LOG_DEBUG(RetroAchievements, "Initializing RetroAchievements client.");

    rc_client = rc_client_create(Callbacks::read_memory, Callbacks::server_call);
    rc_client_enable_logging(rc_client, RC_CLIENT_LOG_LEVEL_VERBOSE, Callbacks::log_message);
    rc_client_set_hardcore_enabled(rc_client, 0);
}

static void login_callback(int result, const char* error_message, rc_client_t* client,
                           void* userdata) {
    // If not successful, just report the error and bail.
    if (result != RC_OK) {
        LOG_ERROR(RetroAchievements, "Login failed.");
        return;
    }

    // Login was successful. Capture the token for future logins so we don't have to store the
    // password anywhere.
    const rc_client_user_t* user = rc_client_get_user_info(client);
    // store_retroachievements_credentials(user->username, user->token);

    // Inform user of successful login
    LOG_INFO(RetroAchievements, "Logged in as {} ({} points)", user->display_name, user->score);
}

void Client::LogInUser(const char* username, const char* password) {
    rc_client_begin_login_with_password(rc_client, username, password, login_callback, NULL);
}
} // namespace RetroAchievements
