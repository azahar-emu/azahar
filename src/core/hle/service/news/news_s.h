// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include "core/hle/service/news/news.h"

namespace Service::NEWS {

class NEWS_S final : public Module::Interface {
public:
    explicit NEWS_S(std::shared_ptr<Module> news);

private:
    SERVICE_SERIALIZATION(NEWS_S, news, Module)
};

} // namespace Service::NEWS

BOOST_CLASS_EXPORT_KEY(Service::NEWS::NEWS_S)
BOOST_SERIALIZATION_CONSTRUCT(Service::NEWS::NEWS_S)
