// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/arch.h"
#if CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)

#include "common/assert.h"
#include "common/hash.h"
#include "common/microprofile.h"
#include "video_core/shader/shader.h"
#include "video_core/shader/shader_jit.h"
#if CITRA_ARCH(arm64)
#include "video_core/shader/shader_jit_a64_compiler.h"
#endif
#if CITRA_ARCH(x86_64)
#include "video_core/shader/shader_jit_x64_compiler.h"
#endif

namespace Pica::Shader {

JitEngine::JitEngine() = default;
JitEngine::~JitEngine() = default;

void JitEngine::SetupBatch(ShaderSetup& setup, u32 entry_point) {
    ASSERT(entry_point < MAX_PROGRAM_CODE_LENGTH);
    setup.entry_point = entry_point;

    const u64 code_hash = setup.GetProgramCodeHash();
    const u64 swizzle_hash = setup.GetSwizzleDataHash();
    const u64 cache_key = Common::HashCombine(code_hash, swizzle_hash);

    std::lock_guard<std::mutex> lock(cache_mutex);
    auto iter = cache.find(cache_key);
    if (iter != cache.end()) {
        setup.cached_shader = iter->second.get();
        UpdateLRU(cache_key);
    } else {
        if (cache.size() >= MAX_CACHE_SIZE) {
            EvictLRU();
        }
        auto shader = std::make_unique<JitShader>();
        shader->Compile(&setup.program_code, &setup.swizzle_data);
        setup.cached_shader = shader.get();
        cache.emplace_hint(iter, cache_key, std::move(shader));
        lru_list.push_front(cache_key);
    }
}

void JitEngine::EvictLRU() {
    if (lru_list.empty()) {
        return;
    }
    const u64 key = lru_list.back();
    lru_list.pop_back();
    cache.erase(key);
}

void JitEngine::UpdateLRU(u64 key) {
    auto it = std::find(lru_list.begin(), lru_list.end(), key);
    if (it != lru_list.end()) {
        lru_list.erase(it);
    }
    lru_list.push_front(key);
}

MICROPROFILE_DECLARE(GPU_Shader);

void JitEngine::Run(const ShaderSetup& setup, ShaderUnit& state) const {
    ASSERT(setup.cached_shader != nullptr);

    MICROPROFILE_SCOPE(GPU_Shader);

    const JitShader* shader = static_cast<const JitShader*>(setup.cached_shader);
    shader->Run(setup, state, setup.entry_point);
}

} // namespace Pica::Shader

#endif // CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)
