// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <optional>
#include <string>
#include <httplib.h>
#include <json.hpp>
#include "common/logging/log.h"
#include "update_checker.h"

std::optional<std::string> UpdateChecker::CheckForUpdate() {
    constexpr auto UPDATE_CHECK_URL = "http://api.github.com";
    constexpr auto UPDATE_CHECK_PATH = "/repos/azahar-emu/azahar/releases/latest";

    std::unique_ptr<httplib::Client> client = std::make_unique<httplib::Client>(UPDATE_CHECK_URL);
    if (client == nullptr) {
        LOG_ERROR(Frontend, "Invalid URL {}{}", UPDATE_CHECK_URL, UPDATE_CHECK_PATH);
        return {};
    }

    httplib::Request request{
        .method = "GET",
        .path = UPDATE_CHECK_PATH,
    };

    client->set_follow_location(true);
    const auto result = client->send(request);
    if (!result) {
        LOG_ERROR(Frontend, "GET to {}{} returned null", UPDATE_CHECK_URL, UPDATE_CHECK_PATH);
        return {};
    }

    const auto& response = result.value();
    if (response.status >= 400) {
        LOG_ERROR(Frontend, "GET to {}{} returned error status code: {}", UPDATE_CHECK_URL,
                  UPDATE_CHECK_PATH, response.status);
        return {};
    }
    if (!response.headers.contains("content-type")) {
        LOG_ERROR(Frontend, "GET to {}{} returned no content", UPDATE_CHECK_URL, UPDATE_CHECK_PATH);
        return {};
    }

    try {
        return nlohmann::json::parse(response.body).at("tag_name");
    } catch (nlohmann::detail::out_of_range&) {
        return {};
    }
}
