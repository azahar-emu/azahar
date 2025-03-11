// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/archives.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/shared_memory.h"
#include "core/hle/service/act/act.h"
#include "core/hle/service/act/act_a.h"
#include "core/hle/service/act/act_errors.h"
#include "core/hle/service/act/act_u.h"

SERIALIZE_EXPORT_IMPL(Service::ACT::Module)
SERVICE_CONSTRUCT_IMPL(Service::ACT::Module)

namespace Service::ACT {

Module::Module(Core::System& system_) : system(system_) {}

Module::Interface::Interface(std::shared_ptr<Module> act, const char* name)
    : ServiceFramework(name, 3), act(std::move(act)) {}

Module::Interface::~Interface() = default;

void Module::Interface::Initialize(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto sdk_version = rp.Pop<u32>();
    const auto shared_memory_size = rp.Pop<u32>();
    const auto caller_pid = rp.PopPID();
    [[maybe_unused]] const auto shared_memory = rp.PopObject<Kernel::SharedMemory>();

    LOG_DEBUG(Service_ACT,
              "(STUBBED) called sdk_version={:08X}, shared_memory_size={:08X}, caller_pid={}",
              sdk_version, shared_memory_size, caller_pid);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

void Module::Interface::GetErrorCode(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto result = rp.Pop<Result>();

    LOG_DEBUG(Service_ACT, "called result={:08X}", result.raw);

    const u32 error_code = GetACTErrorCode(result);

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(ResultSuccess);
    rb.Push(error_code);
}

void Module::Interface::GetAccountInfo(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    const auto account_slot = rp.Pop<u8>();
    const auto size = rp.Pop<u32>();
    const auto block_id = rp.Pop<u32>();
    [[maybe_unused]] auto output_buffer = rp.PopMappedBuffer();

    LOG_DEBUG(Service_ACT, "(STUBBED) called account_slot={:02X}, size={:08X}, block_id={:08X}",
              account_slot, size, block_id);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(ResultSuccess);
}

template <class Archive>
void Module::serialize(Archive& ar, const unsigned int) {
    DEBUG_SERIALIZATION_POINT;
}
SERIALIZE_IMPL(Module)

void InstallInterfaces(Core::System& system) {
    auto& service_manager = system.ServiceManager();
    auto act = std::make_shared<Module>(system);
    std::make_shared<ACT_A>(act)->InstallAsService(service_manager);
    std::make_shared<ACT_U>(act)->InstallAsService(service_manager);
}

} // namespace Service::ACT
