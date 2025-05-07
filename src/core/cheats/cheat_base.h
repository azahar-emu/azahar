// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>
#include "common/common_types.h"

namespace Core {
class System;
}

namespace Cheats {
class CheatBase {
public:
    virtual ~CheatBase();
    virtual void Execute(Core::System& system, u32 process_id) const = 0;

    virtual bool IsEnabled() const = 0;
    virtual void SetEnabled(bool enabled) = 0;

    virtual std::string GetComments() const = 0;
    virtual std::string GetName() const = 0;
    virtual std::string GetType() const = 0;
    virtual std::string GetCode() const = 0;

    virtual std::string ToString() const = 0;
};
} // namespace Cheats
