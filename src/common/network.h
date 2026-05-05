// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include <optional>

namespace Common {
struct URLInfo {
    bool is_https;
    std::string host;
    int port;
    std::string path;
};

// Splits URL into its components. Example: https://citra-emu.org:443/index.html
// is_https: true; host: citra-emu.org; port: 443; path: /index.html
URLInfo SplitUrl(const std::string& url);

extern std::optional<URLInfo> http_proxy;
extern std::optional<URLInfo> https_proxy;

}