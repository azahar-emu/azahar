// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "web_util.h"

namespace Common {
// Splits URL into its components. Example: https://citra-emu.org:443/index.html
// is_https: true; host: citra-emu.org; port: 443; path: /index.html
URLInfo SplitUrl(const std::string& url) {
    const std::string prefix = "://";
    constexpr int default_http_port = 80;
    constexpr int default_https_port = 443;

    std::string host;
    int port = -1;
    std::string path;

    const auto scheme_end = url.find(prefix);
    const auto prefix_end = scheme_end == std::string::npos ? 0 : scheme_end + prefix.length();
    bool is_https = scheme_end != std::string::npos && url.starts_with("https");
    const auto path_index = url.find("/", prefix_end);

    if (path_index == std::string::npos) {
        // If no path is specified after the host, set it to "/"
        host = url.substr(prefix_end);
        path = "/";
    } else {
        host = url.substr(prefix_end, path_index - prefix_end);
        path = url.substr(path_index);
    }

    const auto port_start = host.find(":");
    if (port_start != std::string::npos) {
        std::string port_str = host.substr(port_start + 1);
        host = host.substr(0, port_start);
        char* p_end = nullptr;
        port = std::strtol(port_str.c_str(), &p_end, 10);
        if (*p_end) {
            port = -1;
        }
    }

    if (port == -1) {
        port = is_https ? default_https_port : default_http_port;
    }
    return URLInfo{
        .is_https = is_https,
        .host = host,
        .port = port,
        .path = path,
    };
}
} // namespace Common