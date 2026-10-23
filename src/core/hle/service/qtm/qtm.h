// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include "core/hle/service/service.h"
namespace Core {
class System;
}

namespace Service::QTM {

/// Initializes all QTM services.
void InstallInterfaces(Core::System& system);

} // namespace Service::QTM
