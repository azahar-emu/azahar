// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>
#include <fmt/format.h>
#include "common/file_util.h"
#include "core/savestate.h"

namespace {
Core::SaveStateInfo MakeState(
    u32 slot, u64 time,
    Core::SaveStateInfo::ValidationStatus status = Core::SaveStateInfo::ValidationStatus::OK) {
    Core::SaveStateInfo info{};
    info.slot = slot;
    info.time = time;
    info.status = status;
    return info;
}

std::string MakeUniqueTempDirName() {
    static std::atomic<u64> counter{0};
    const auto ticks =
        static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
    return fmt::format("azahar-savestate-slot-test-{:016x}-{:x}", ticks, counter.fetch_add(1));
}

// True if `path` names something at or under the system temp directory.
bool IsUnderTempDir(const std::string& path) {
    std::error_code ec;
    const auto temp = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(), ec);
    if (ec) {
        return false;
    }
    const auto candidate = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
    if (ec) {
        return false;
    }
    // generic_string() so the comparison below is a plain std::string one on every platform.
    const std::string rel = candidate.lexically_relative(temp).generic_string();
    return !rel.empty() && rel != "." && rel.compare(0, 2, "..") != 0;
}

// Points StatesDir at a disposable temp directory and restores it on destruction.
// UpdateUserPath(), not SetUserPath(): the latter uses emplace() and no-ops if the key exists.
class ScopedStatesDir {
public:
    ScopedStatesDir() {
        root = (std::filesystem::temp_directory_path() / MakeUniqueTempDirName()).string() + "/";
        REQUIRE(IsUnderTempDir(root));
        REQUIRE(FileUtil::CreateFullPath(root));

        // UpdateUserPath() refuses a path that does not exist, so ensure the old one does.
        previous = FileUtil::GetUserPath(FileUtil::UserPath::StatesDir);
        if (!FileUtil::IsDirectory(previous)) {
            REQUIRE(FileUtil::CreateFullPath(previous));
        }

        // Last statement on purpose: everything that can throw runs before it.
        FileUtil::UpdateUserPath(FileUtil::UserPath::StatesDir, root);

        // UpdateUserPath() fails silently, so confirm the redirect took.
        states_dir = FileUtil::GetUserPath(FileUtil::UserPath::StatesDir);
        REQUIRE(IsUnderTempDir(states_dir));
    }

    ~ScopedStatesDir() {
        FileUtil::UpdateUserPath(FileUtil::UserPath::StatesDir, previous);
        // Re-check the temp-dir invariant at the delete. Not assert() (compiled out at -O3
        // -DNDEBUG) and not REQUIRE (throwing from a destructor during unwinding terminates).
        if (root.empty() || !IsUnderTempDir(root)) {
            std::cerr << "\nFATAL: ScopedStatesDir destructor refusing to recursively delete \""
                      << root << "\" -- it is empty or not under the system temp directory.\n";
            std::abort();
        }
        FileUtil::DeleteDirRecursively(root);
    }

    ScopedStatesDir(const ScopedStatesDir&) = delete;
    ScopedStatesDir& operator=(const ScopedStatesDir&) = delete;

    const std::string& StatesDir() const {
        return states_dir;
    }

private:
    std::string root;
    std::string states_dir;
    std::string previous;
};

void PutU64LE(u8* dest, u64 value) {
    for (std::size_t i = 0; i < sizeof(u64); ++i) {
        dest[i] = static_cast<u8>((value >> (8 * i)) & 0xFF);
    }
}

// Mirrors CSTHeader in src/core/savestate.cpp, which is file-private: magic[4] at 0,
// program_id at 4, revision[20] at 12, time at 32; 256 bytes total.
constexpr std::size_t CSTHeaderSize = 256;
constexpr std::size_t CSTProgramIdOffset = 4;
constexpr std::size_t CSTTimeOffset = 32;

// Writes a savestate file holding nothing but a valid CSTHeader; ListSaveStates reads no more.
// `revision` is left zeroed, giving a mismatched status -- PickInitialSlot ignores it.
void WriteSaveStateFile(const std::string& path, u64 program_id, u64 time) {
    std::array<u8, CSTHeaderSize> header{};
    constexpr std::array<u8, 4> magic{{'C', 'S', 'T', 0x1B}};
    std::memcpy(header.data(), magic.data(), magic.size());
    PutU64LE(header.data() + CSTProgramIdOffset, program_id);
    PutU64LE(header.data() + CSTTimeOffset, time);

    FileUtil::IOFile file(path, "wb");
    REQUIRE(file.IsOpen());
    REQUIRE(file.WriteBytes(header.data(), header.size()) == header.size());
}

// Both helpers mirror GetSaveStatePath() in src/core/savestate.cpp.
void WriteState(const ScopedStatesDir& dir, u64 program_id, u32 slot, u64 time) {
    WriteSaveStateFile(fmt::format("{}{:016X}.{:02d}.cst", dir.StatesDir(), program_id, slot),
                       program_id, time);
}

void WriteMovieState(const ScopedStatesDir& dir, u64 program_id, u64 movie_id, u32 slot, u64 time) {
    WriteSaveStateFile(fmt::format("{}{:016X}.movie{:016X}.{:02d}.cst", dir.StatesDir(), program_id,
                                   movie_id, slot),
                       program_id, time);
}
} // Anonymous namespace

TEST_CASE("Slot cursor clamps out-of-range slots", "[core][savestate]") {
    Core::SetCurrentSlot(Core::QuicksaveSlot);
    REQUIRE(Core::GetCurrentSlot() == Core::FirstUserSlot);

    Core::SetCurrentSlot(99);
    REQUIRE(Core::GetCurrentSlot() == Core::LastUserSlot);

    Core::SetCurrentSlot(4);
    REQUIRE(Core::GetCurrentSlot() == 4);
}

TEST_CASE("Slot cursor wraps in both directions", "[core][savestate]") {
    // Assert GetCurrentSlot() too: AdvanceSlot must store the new cursor, not just return it.
    Core::SetCurrentSlot(Core::LastUserSlot);
    REQUIRE(Core::AdvanceSlot(1) == Core::FirstUserSlot);
    REQUIRE(Core::GetCurrentSlot() == Core::FirstUserSlot);

    Core::SetCurrentSlot(Core::FirstUserSlot);
    REQUIRE(Core::AdvanceSlot(-1) == Core::LastUserSlot);
    REQUIRE(Core::GetCurrentSlot() == Core::LastUserSlot);

    Core::SetCurrentSlot(Core::FirstUserSlot);
    REQUIRE(Core::AdvanceSlot(3) == Core::FirstUserSlot + 3);
    REQUIRE(Core::GetCurrentSlot() == Core::FirstUserSlot + 3);

    // A delta larger than the range still wraps correctly, in both directions.
    Core::SetCurrentSlot(Core::FirstUserSlot);
    REQUIRE(Core::AdvanceSlot(-25) == 6);
    REQUIRE(Core::GetCurrentSlot() == 6);

    Core::SetCurrentSlot(Core::FirstUserSlot);
    REQUIRE(Core::AdvanceSlot(25) == 6);
    REQUIRE(Core::GetCurrentSlot() == 6);

    // A zero delta is a no-op.
    Core::SetCurrentSlot(5);
    REQUIRE(Core::AdvanceSlot(0) == 5);
    REQUIRE(Core::GetCurrentSlot() == 5);
}

TEST_CASE("Slot cursor advances from where the last advance left it", "[core][savestate]") {
    // Chained with no SetCurrentSlot between: only a persisting implementation passes.
    Core::SetCurrentSlot(Core::FirstUserSlot);
    REQUIRE(Core::AdvanceSlot(1) == 2);
    REQUIRE(Core::AdvanceSlot(1) == 3);
    REQUIRE(Core::GetCurrentSlot() == 3);

    // The same, walking backwards across the wrap.
    Core::SetCurrentSlot(2);
    REQUIRE(Core::AdvanceSlot(-1) == Core::FirstUserSlot);
    REQUIRE(Core::AdvanceSlot(-1) == Core::LastUserSlot);
    REQUIRE(Core::GetCurrentSlot() == Core::LastUserSlot);
}

TEST_CASE("PickInitialSlot chooses the newest user slot", "[core][savestate]") {
    const std::vector<Core::SaveStateInfo> states{
        MakeState(Core::QuicksaveSlot, 5000), // newest overall, but must be ignored
        MakeState(2, 1000),
        MakeState(7, 4000),
        MakeState(4, 2000),
    };
    REQUIRE(Core::PickInitialSlot(states) == 7);
}

TEST_CASE("PickInitialSlot falls back when no user slot has a state", "[core][savestate]") {
    REQUIRE(Core::PickInitialSlot({}) == Core::FirstUserSlot);

    const std::vector<Core::SaveStateInfo> quicksave_only{
        MakeState(Core::QuicksaveSlot, 5000),
    };
    REQUIRE(Core::PickInitialSlot(quicksave_only) == Core::FirstUserSlot);
}

TEST_CASE("PickInitialSlot rejects slots above LastUserSlot", "[core][savestate]") {
    const std::vector<Core::SaveStateInfo> states{
        MakeState(Core::LastUserSlot + 1, 9000), // newest overall, but out of range
        MakeState(3, 1000),
    };
    REQUIRE(Core::PickInitialSlot(states) == 3);
}

TEST_CASE("PickInitialSlot keeps the first slot on a time tie", "[core][savestate]") {
    const std::vector<Core::SaveStateInfo> states{
        MakeState(3, 4000),
        MakeState(6, 4000),
    };
    REQUIRE(Core::PickInitialSlot(states) == 3);
}

TEST_CASE("PickInitialSlot ignores ValidationStatus", "[core][savestate]") {
    // Matches UpdateSaveStates, which also enables load for mismatched states.
    const std::vector<Core::SaveStateInfo> states{
        MakeState(5, 9000, Core::SaveStateInfo::ValidationStatus::RevisionMismatch),
    };
    REQUIRE(Core::PickInitialSlot(states) == 5);
}

TEST_CASE("InitCurrentSlot lands on the newest state on disk", "[core][savestate]") {
    constexpr u64 program_id = 0x0004000000ABCDEF;
    ScopedStatesDir dir;

    // Seeded away from both the expected answer and the fallback.
    Core::SetCurrentSlot(2);

    WriteState(dir, program_id, 3, 1000);
    WriteState(dir, program_id, 7, 4000);
    WriteState(dir, program_id, 5, 2000);
    // Newest of all, but the quicksave slot is never the cursor.
    WriteState(dir, program_id, Core::QuicksaveSlot, 9000);

    Core::InitCurrentSlot(program_id, 0);
    REQUIRE(Core::GetCurrentSlot() == 7);

    // A title with nothing on disk falls back rather than inheriting the previous title's slot.
    Core::InitCurrentSlot(program_id + 1, 0);
    REQUIRE(Core::GetCurrentSlot() == Core::FirstUserSlot);
}

TEST_CASE("InitCurrentSlot keys the listing on the movie id", "[core][savestate]") {
    constexpr u64 program_id = 0x0004000000ABCDEF;
    constexpr u64 movie_id = 0x1122334455667788;
    ScopedStatesDir dir;

    // Two disjoint sets; a listing that ignored the movie id would answer 9 for both.
    WriteState(dir, program_id, 9, 9000);
    WriteState(dir, program_id, 3, 8000);
    WriteMovieState(dir, program_id, movie_id, 4, 1000);
    WriteMovieState(dir, program_id, movie_id, 2, 500);

    Core::SetCurrentSlot(6);
    Core::InitCurrentSlot(program_id, movie_id);
    REQUIRE(Core::GetCurrentSlot() == 4);

    // And the movie states are equally invisible to the non-movie listing.
    Core::SetCurrentSlot(6);
    Core::InitCurrentSlot(program_id, 0);
    REQUIRE(Core::GetCurrentSlot() == 9);

    // A movie id with no states of its own falls back, even though the title has states.
    Core::SetCurrentSlot(6);
    Core::InitCurrentSlot(program_id, movie_id + 1);
    REQUIRE(Core::GetCurrentSlot() == Core::FirstUserSlot);
}
