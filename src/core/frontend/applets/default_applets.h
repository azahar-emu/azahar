// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

namespace Frontend {
/**
 * Registers default, frontend-independent applet implementations.
 * Will be replaced later if any frontend-specific implementation is available.
 */
void RegisterDefaultApplets(Core::System& system);
} // namespace Frontend
