// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <chrono>
#include <string>
#include <discord_rpc.h>
#include "citra_qt/discord_impl.h"
#include "citra_qt/uisettings.h"
#include "common/common_types.h"
#include "core/core.h"
#include "core/loader/loader.h"

namespace DiscordRPC {

DiscordImpl::DiscordImpl(const Core::System& system_) : system{system_} {
    DiscordEventHandlers handlers{};

    // The number is the client ID for Citra, it's used for images and the
    // application name
    Discord_Initialize("1345366770436800533", &handlers, 1, nullptr);
}

DiscordImpl::~DiscordImpl() {
    Discord_ClearPresence();
    Discord_Shutdown();
}

void DiscordImpl::Pause() {
    Discord_ClearPresence();
}

void DiscordImpl::Update(bool is_powered_on) {
    s64 start_time = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    auto truncate = [](const std::string& str, std::size_t maxLen = 128) -> std::string {
        if (str.length() <= maxLen) {
            return str;
        }
        return str.substr(0, maxLen - 3) + "...";
    };

    std::string title;
    if (is_powered_on) {
        system.GetAppLoader().ReadTitle(title);
        title = truncate("Playing: " + title);
    }

    DiscordRichPresence presence{};
    presence.largeImageKey = "logo";
    presence.largeImageText = "An open source emulator for the Nintendo 3DS";
    if (is_powered_on) {
        presence.state = title.c_str();
    }
    presence.startTimestamp = start_time;
    Discord_UpdatePresence(&presence);
}
} // namespace DiscordRPC
