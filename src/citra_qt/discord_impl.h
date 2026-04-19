// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "citra_qt/discord.h"

namespace Core {
class System;
}

namespace DiscordRPC {

class DiscordImpl : public DiscordInterface {
public:
    DiscordImpl(const Core::System& system);
    ~DiscordImpl() override;

    void Pause() override;
    void Update(bool is_powered_on) override;

private:
    const Core::System& system;
};

} // namespace DiscordRPC
