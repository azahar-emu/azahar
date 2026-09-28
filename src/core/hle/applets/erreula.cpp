// Copyright 2016-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/string_util.h"
#include "core/core.h"
#include "core/hle/applets/erreula.h"
#include "core/hle/service/apt/apt.h"

namespace HLE::Applets {

Result ErrEula::ReceiveParameterImpl(const Service::APT::MessageParameter& parameter) {
    if (parameter.signal != Service::APT::SignalType::Request) {
        LOG_ERROR(Service_APT, "unsupported signal {}", parameter.signal);
        UNIMPLEMENTED();
        // TODO(Subv): Find the right error code
        return ResultUnknown;
    }

    // The LibAppJustStarted message contains a buffer with the size of the framebuffer shared
    // memory.
    // Create the SharedMemory that will hold the framebuffer data
    Service::APT::CaptureBufferInfo capture_info;
    ASSERT(sizeof(capture_info) == parameter.buffer.size());

    std::memcpy(&capture_info, parameter.buffer.data(), sizeof(capture_info));

    // TODO: allocated memory never released
    using Kernel::MemoryPermission;
    // Create a SharedMemory that directly points to this heap block.
    framebuffer_memory = system.Kernel().CreateSharedMemoryForApplet(
        0, capture_info.size, MemoryPermission::ReadWrite, MemoryPermission::ReadWrite,
        "ErrEula Memory");

    // Send the response message with the newly created SharedMemory
    SendParameter({
        .sender_id = id,
        .destination_id = parent,
        .signal = Service::APT::SignalType::Response,
        .object = framebuffer_memory,
    });

    return ResultSuccess;
}

Result ErrEula::Start(const Service::APT::MessageParameter& parameter) {
    memcpy(&param, parameter.buffer.data(), std::min(parameter.buffer.size(), sizeof(param)));

    // Do something here, like showing error codes, or prompting for EULA agreement.
    if (param.type == DisplayType::Agree) {
        param.result = 1;
    }

    // Let the application know that we're closing.
    Finalize();
    return ResultSuccess;
}

Result ErrEula::Finalize() {
    std::vector<u8> buffer(sizeof(param));
    memcpy(buffer.data(), &param, buffer.size());
    CloseApplet(nullptr, buffer);
    return ResultSuccess;
}

void ErrEula::Update() {}

} // namespace HLE::Applets
