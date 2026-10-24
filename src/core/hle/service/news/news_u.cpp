// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include "common/archives.h"
#include "core/hle/service/news/news_u.h"

SERIALIZE_EXPORT_IMPL(Service::NEWS::NEWS_U)

namespace Service::NEWS {

NEWS_U::NEWS_U(std::shared_ptr<Module> news) : Module::Interface(std::move(news), "news:u", 1) {
    const FunctionInfo functions[] = {
        // clang-format off
        {0x0001, &NEWS_U::AddNotification, "AddNotification"},
        // clang-format on
    };
    RegisterHandlers(functions);
}

} // namespace Service::NEWS
