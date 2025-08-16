// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "core/core.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/apt/ns.h"
#include "core/loader/loader.h"

namespace Service::NS {

std::shared_ptr<Kernel::Process> LaunchTitle(Core::System& system, FS::MediaType media_type,
                                             u64 title_id) {
    std::string path;

    if (media_type == FS::MediaType::GameCard) {
        path = system.GetCartridge();
    } else {
        path = AM::GetTitleContentPath(media_type, title_id);
    }

    auto loader = Loader::GetLoader(path);
    u64 program_id;

    if (!loader || loader->ReadProgramId(program_id) != Loader::ResultStatus::Success ||
        program_id != title_id) {
        LOG_WARNING(Service_NS, "Could not load title=0x{:016x} media_type={}", title_id,
                    static_cast<int>(media_type));
        return nullptr;
    }

    auto plg_ldr = Service::PLGLDR::GetService(system);
    if (plg_ldr) {
        const auto& plg_context = plg_ldr->GetPluginLoaderContext();
        if (plg_context.is_enabled && plg_context.use_user_load_parameters &&
            plg_context.user_load_parameters.low_title_Id == static_cast<u32>(title_id) &&
            plg_context.user_load_parameters.plugin_memory_strategy ==
                PLGLDR::PLG_LDR::PluginMemoryStrategy::PLG_STRATEGY_MODE3) {
            loader->SetKernelMemoryModeOverride(Kernel::MemoryMode::Dev2);
        }
    }

    std::shared_ptr<Kernel::Process> process;
    Loader::ResultStatus result = loader->Load(process);

    if (result != Loader::ResultStatus::Success) {
        LOG_WARNING(Service_NS, "Error loading .app for title 0x{:016x}", title_id);
        return nullptr;
    }

    return process;
}

void RebootToTitle(Core::System& system, FS::MediaType media_type, u64 title_id) {
    std::string new_path;
    if (media_type == FS::MediaType::GameCard) {
        new_path = system.GetCartridge();
    } else {
        new_path = AM::GetTitleContentPath(media_type, title_id);
    }

    if (new_path.empty() || !FileUtil::Exists(new_path)) {
        // TODO: This can happen if the requested title is not installed. Need a way to find
        // non-installed titles in the game list.
        LOG_CRITICAL(Service_APT,
                     "Failed to find title '{}' to jump to, resetting current title instead.",
                     new_path);
        new_path.clear();
    }
    system.RequestReset(new_path);
}

} // namespace Service::NS
