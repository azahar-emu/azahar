// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include "common/common_types.h"

namespace Common {
struct WebResult {
    enum class Code : u32 {
        Success,
        InvalidURL,
        CredentialsMissing,
        LibError,
        HttpError,
        WrongContent,
        NoWebservice,
    };
    Code result_code;
    std::string result_string;
    std::string returned_data;
};

/**
 * @brief Parsed URL components.
 */
struct URLInfo {
    bool is_https;    ///< True if the URL uses HTTPS, false for HTTP.
    std::string host; ///< Hostname or IP address.
    int port;         ///< Network port.
    std::string path; ///< Resource path.
};
URLInfo SplitUrl(const std::string& url);
} // namespace Common
