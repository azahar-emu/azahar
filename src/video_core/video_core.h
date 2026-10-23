// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <memory>

namespace Frontend {
class EmuWindow;
}

namespace Core {
class System;
}

namespace Pica {
class PicaCore;
}

namespace VideoCore {

class RendererBase;

std::unique_ptr<RendererBase> CreateRenderer(Frontend::EmuWindow& emu_window,
                                             Frontend::EmuWindow* secondary_window,
                                             Pica::PicaCore& pica, Core::System& system);

} // namespace VideoCore
