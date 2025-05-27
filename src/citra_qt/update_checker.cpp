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
    std::string update_check_path = "/repos/azahar-emu/azahar";
    try {
        if (include_prereleases) { // This can return either a prerelease or a stable release,
                                   // whichever is more recent.
            const auto update_check_tags_path = update_check_path + "/tags";
            const auto update_check_releases_path = update_check_path + "/releases";

            const auto tags_response = GetResponse(update_check_url, update_check_tags_path);
            const auto releases_response =
                GetResponse(update_check_url, update_check_releases_path);

            if (!tags_response || !releases_response)
                return {};

            const std::string latest_tag =
                nlohmann::json::parse(tags_response.value()).at(0).at("name");
            const bool latest_tag_has_release =
                releases_response.value().find(std::format("\"{}\"", latest_tag)) !=
                std::string::npos;

            // If there is a newer tag, but that tag has no associated release, don't prompt the
            // user to update.
            if (!latest_tag_has_release)
                return {};

            return latest_tag;
        } else { // This is a stable release, only check for other stable releases.
            update_check_path += "/releases/latest";
            const auto response = GetResponse(update_check_url, update_check_path);

            if (!response)
                return {};

            const std::string latest_tag = nlohmann::json::parse(response.value()).at("tag_name");
            return latest_tag;
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
