// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

// Exactly one of these is ever compiled per platform (see CMakeLists.txt), so a compile-time
// alias is all that's needed here - no runtime-polymorphic base class, since there both never
// are two implementations present at once to choose between at runtime.
#if defined(_WIN32)
#include "citra_qt/network_streaming/mdns_publisher.h"
#else
#include "citra_qt/network_streaming/avahi_publisher.h"
#endif

namespace NetworkStreaming {

#if defined(_WIN32)
using ServicePublisher = MdnsPublisher;
#else
using ServicePublisher = AvahiPublisher;
#endif

} // namespace NetworkStreaming
