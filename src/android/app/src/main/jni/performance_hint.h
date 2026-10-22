// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <chrono>

struct APerformanceHintSession;

// Represents an ADPF hint session (APerformanceHint, Android 13 and later).
class PerformanceHintSession {
public:
    explicit PerformanceHintSession(std::chrono::nanoseconds target);
    ~PerformanceHintSession();

    PerformanceHintSession(const PerformanceHintSession&) = delete;
    PerformanceHintSession& operator=(const PerformanceHintSession&) = delete;

    /// Reports the duration of the last frame's work, and the target duration for the next one.
    void ReportWork(std::chrono::nanoseconds actual, std::chrono::nanoseconds target);

private:
    APerformanceHintSession* session{};
    std::chrono::nanoseconds current_target{};
};
