// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <filesystem>
#include "common/alignment.h"
#include "common/common_paths.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/play_time_manager.h"
#include "common/settings.h"
#include "common/thread.h"

namespace PlayTime {

namespace {

struct PlayTimeElement {
    ProgramId program_id;
    PlayTime play_time;
};

#define PLAY_TIME_FILENAME "play_time.bin"

std::string GetCurrentUserPlayTimePath() {
    return FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir) + DIR_SEP + PLAY_TIME_FILENAME;
}

[[nodiscard]] bool ReadPlayTimeFile(PlayTimeDatabase& out_play_time_db) {
    const auto filepath = GetCurrentUserPlayTimePath();
    const auto old_filepath =
        FileUtil::GetUserPath(FileUtil::UserPath::LogDir) + DIR_SEP + PLAY_TIME_FILENAME;

    if (FileUtil::Exists(old_filepath) && !FileUtil::Exists(filepath)) {
        static_cast<void>(FileUtil::Rename(old_filepath, filepath));
    }

    out_play_time_db.clear();

    if (FileUtil::Exists(filepath)) {
        FileUtil::IOFile file{filepath, "rb"};
        if (!file.IsOpen()) {
            LOG_ERROR(Frontend, "Failed to open play time file: {}", filepath);
            return false;
        }

        const size_t num_elements = file.GetSize() / sizeof(PlayTimeElement);
        std::vector<PlayTimeElement> elements(num_elements);

        if (file.ReadSpan<PlayTimeElement>(elements) != num_elements) {
            return false;
        }

        for (const auto& [program_id, play_time] : elements) {
            if (program_id != 0) {
                out_play_time_db[program_id] = play_time;
            }
        }
    }

    return true;
}

[[nodiscard]] bool WritePlayTimeFile(const PlayTimeDatabase& play_time_db) {
    const auto filepath = GetCurrentUserPlayTimePath();

    FileUtil::IOFile file{filepath, "wb"};
    if (!file.IsOpen()) {
        LOG_ERROR(Frontend, "Failed to open play time file: {}", filepath);
        return false;
    }

    std::vector<PlayTimeElement> elements;
    elements.reserve(play_time_db.size());

    for (auto& [program_id, play_time] : play_time_db) {
        if (program_id != 0) {
            elements.push_back(PlayTimeElement{program_id, play_time});
        }
    }

    return file.WriteSpan<PlayTimeElement>(elements) == elements.size();
}

} // namespace

PlayTimeManager::PlayTimeManager() {
    if (!ReadPlayTimeFile(database)) {
        LOG_ERROR(Frontend, "Failed to read play time database! Resetting to default.");
    }
}

PlayTimeManager::~PlayTimeManager() {
    Save();
}

void PlayTimeManager::SetProgramId(u64 program_id) {
    running_program_id = program_id;
}

void PlayTimeManager::Start() {
    play_time_thread = std::jthread([&](std::stop_token stop_token) { AutoTimestamp(stop_token); });
}

void PlayTimeManager::Stop() {
    play_time_thread = {};
}

void PlayTimeManager::AutoTimestamp(std::stop_token stop_token) {
    Common::SetCurrentThreadName("PlayTimeReport");

    using namespace std::literals::chrono_literals;
    using std::chrono::seconds;
    using std::chrono::steady_clock;

    auto timestamp = steady_clock::now();

    const auto GetDuration = [&]() -> u64 {
        const auto last_timestamp = std::exchange(timestamp, steady_clock::now());
        const auto duration = std::chrono::duration_cast<seconds>(timestamp - last_timestamp);
        return static_cast<u64>(duration.count());
    };

    while (!stop_token.stop_requested()) {
        Common::StoppableTimedWait(stop_token, 30s);

        database[running_program_id] += GetDuration();
        Save();
    }
}

void PlayTimeManager::Save() {
    if (!WritePlayTimeFile(database)) {
        LOG_ERROR(Frontend, "Failed to update play time database!");
    }
}

u64 PlayTimeManager::GetPlayTime(u64 program_id) const {
    auto it = database.find(program_id);
    if (it != database.end()) {
        return it->second;
    } else {
        return 0;
    }
}

void PlayTimeManager::ResetProgramPlayTime(u64 program_id) {
    database.erase(program_id);
    Save();
}

} // namespace PlayTime
