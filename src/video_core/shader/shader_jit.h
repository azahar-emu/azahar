// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/arch.h"
#if CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)

#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "common/common_types.h"
#include "video_core/shader/shader.h"

namespace Pica::Shader {

class JitShader;

class JitEngine final : public ShaderEngine {
public:
    JitEngine();
    ~JitEngine() override;

    void SetupBatch(ShaderSetup& setup, u32 entry_point) override;
    void Run(const ShaderSetup& setup, ShaderUnit& state) const override;

private:
    static constexpr size_t MAX_CACHE_SIZE = 1000; // Maximum number of shaders to cache
    std::unordered_map<u64, std::unique_ptr<JitShader>> cache;
    std::list<u64> lru_list; // Track LRU order of shaders
    mutable std::mutex cache_mutex;

    void EvictLRU();
    void UpdateLRU(u64 key);
};

} // namespace Pica::Shader

#endif // CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)
