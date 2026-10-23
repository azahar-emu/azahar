// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <cstdint>
#include <dlfcn.h>
#include <unistd.h>
#include "common/logging/log.h"
#include "jni/performance_hint.h"

struct APerformanceHintManager;

namespace {

struct HintApi {
    APerformanceHintManager* (*get_manager)();
    APerformanceHintSession* (*create_session)(APerformanceHintManager*, const int32_t*, size_t,
                                               int64_t);
    int (*update_target)(APerformanceHintSession*, int64_t);
    int (*report_actual)(APerformanceHintSession*, int64_t);
    void (*close_session)(APerformanceHintSession*);

    bool Load() {
        void* lib = dlopen("libandroid.so", RTLD_NOW);
        if (!lib) {
            return false;
        }
        get_manager =
            reinterpret_cast<decltype(get_manager)>(dlsym(lib, "APerformanceHint_getManager"));
        create_session = reinterpret_cast<decltype(create_session)>(
            dlsym(lib, "APerformanceHint_createSession"));
        update_target = reinterpret_cast<decltype(update_target)>(
            dlsym(lib, "APerformanceHint_updateTargetWorkDuration"));
        report_actual = reinterpret_cast<decltype(report_actual)>(
            dlsym(lib, "APerformanceHint_reportActualWorkDuration"));
        close_session =
            reinterpret_cast<decltype(close_session)>(dlsym(lib, "APerformanceHint_closeSession"));
        return get_manager && create_session && update_target && report_actual && close_session;
    }
};

const HintApi* GetApi() {
    static HintApi api{};
    static const bool loaded = api.Load();
    return loaded ? &api : nullptr;
}

} // Anonymous namespace

PerformanceHintSession::PerformanceHintSession(std::chrono::nanoseconds target)
    : current_target{target} {
    const HintApi* api = GetApi();
    if (!api) {
        LOG_INFO(Frontend, "Performance hint sessions are not available");
        return;
    }
    APerformanceHintManager* manager = api->get_manager();
    const int32_t tid = gettid();
    if (manager) {
        session = api->create_session(manager, &tid, 1, target.count());
    }
    if (session) {
        LOG_INFO(Frontend, "Performance hint session created, target {} us", target.count() / 1000);
    } else {
        LOG_WARNING(Frontend, "Could not create a performance hint session");
    }
}

PerformanceHintSession::~PerformanceHintSession() {
    if (session) {
        GetApi()->close_session(session);
    }
}

void PerformanceHintSession::ReportWork(std::chrono::nanoseconds actual,
                                        std::chrono::nanoseconds target) {
    if (!session) {
        return;
    }
    const HintApi* api = GetApi();
    if (target != current_target) {
        api->update_target(session, target.count());
        current_target = target;
    }
    if (actual.count() > 0) {
        api->report_actual(session, actual.count());
    }
}
