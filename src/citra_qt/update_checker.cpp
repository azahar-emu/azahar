// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <optional>
#include <string>
#include <httplib.h>
#include <json.hpp>
#include "common/logging/log.h"
#include "update_checker.h"

std::optional<std::string> GetResponse(std::string url, std::string path) {
    constexpr std::size_t timeout_seconds = 15;

    std::unique_ptr<httplib::Client> client = std::make_unique<httplib::Client>(url);
    client->set_connection_timeout(timeout_seconds);
    client->set_read_timeout(timeout_seconds);
    client->set_write_timeout(timeout_seconds);

    if (client == nullptr) {
        LOG_ERROR(Frontend, "Invalid URL {}{}", url, path);
        return {};
    }

    httplib::Request request{
        .method = "GET",
        .path = path,
    };

    client->set_follow_location(true);
    const auto result = client->send(request);
    if (!result) {
        LOG_ERROR(Frontend, "GET to {}{} returned null", url, path);
        return {};
    }

    const auto& response = result.value();
    if (response.status >= 400) {
        LOG_ERROR(Frontend, "GET to {}{} returned error status code: {}", url, path,
                  response.status);
        return {};
    }
    if (!response.headers.contains("content-type")) {
        LOG_ERROR(Frontend, "GET to {}{} returned no content", url, path);
        return {};
    }

    return response.body;
}

std::optional<std::string> UpdateChecker::GetLatestRelease(bool include_prereleases) {
    constexpr auto update_check_url = "http://api.github.com";
    std::string update_check_path = "/repos/azahar-emu/azahar/releases";
    try {
        if (include_prereleases) {
            // This can return either a prerelease or a normal release, whichever is more recent
            const auto response = GetResponse(update_check_url, update_check_path);
            if (!response)
                return {};
            return nlohmann::json::parse(response.value()).at(0).at("tag_name");
        } else {
            update_check_path += "/latest";
            const auto response = GetResponse(update_check_url, update_check_path);
            if (!response)
                return {};
            return nlohmann::json::parse(response.value()).at("tag_name");
        }

    } catch (nlohmann::detail::out_of_range&) {
        LOG_ERROR(Frontend,
                  "Parsing JSON response from {}{} failed during update check: "
                  "nlohmann::detail::out_of_range",
                  update_check_url, update_check_path);
        return {};
    } catch (nlohmann::detail::type_error&) {
        LOG_ERROR(Frontend,
                  "Parsing JSON response from {}{} failed during update check: "
                  "nlohmann::detail::type_error",
                  update_check_url, update_check_path);
        return {};
    }
}
