// Copyright 2018-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <optional>
#include <vector>
#include "common/common_types.h"
#include "common/swap.h"

namespace FileSys {

constexpr std::size_t SEEDDB_PADDING_BYTES{12};

struct Seed {
    using Data = std::array<u8, 16>;

    u64_le title_id;
    Data data;
    std::array<u8, 8> reserved;
};

struct SeedDB {
    std::vector<Seed> seeds;

    bool Load();
    bool Save();
    void Add(const Seed& seed);
    bool Delete(u64 title_id);

    std::size_t GetCount() const;
    auto FindSeedByTitleID(u64 title_id) const;
};

bool AddSeed(const Seed& seed);
std::optional<Seed::Data> GetSeed(u64 title_id);
bool DeleteSeed(u64 title_id);
u32 GetSeedCount();

} // namespace FileSys
