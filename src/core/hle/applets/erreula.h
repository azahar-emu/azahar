// Copyright 2016-2025 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/applets/applet.h"
#include "core/hle/kernel/shared_memory.h"
#include "core/hle/result.h"

namespace HLE::Applets {

class ErrEula final : public Applet {
public:
    enum class DisplayType : u8 { ErrorCode = 0, Error = 1, Agree = 5 };
    struct ErrEulaParam {
        static constexpr u32 MESSAGE_SIZE = 1900;

        // Input data
        DisplayType type;
        INSERT_PADDING_BYTES(0x3); // Unknown
        s32 error_code;
        INSERT_PADDING_BYTES(0x2); // Unknown
        u16 language;

        char16_t message[MESSAGE_SIZE];

        bool allow_home_button;
        INSERT_PADDING_BYTES(0x1); // Unknown
        bool launch_system_settings;
        INSERT_PADDING_BYTES(0x89); // Unknown

        // Output data
        u32 result;
        u8 agreed_eula_minor;
        u8 agreed_eula_major;
        INSERT_PADDING_BYTES(0xA); // Unknown
    };
    static_assert(sizeof(ErrEulaParam) == 0xF80, "Invalid ErrEulaParam size");

    explicit ErrEula(Core::System& system, Service::APT::AppletId id, Service::APT::AppletId parent,
                     bool preload, std::weak_ptr<Service::APT::AppletManager> manager)
        : Applet(system, id, parent, preload, std::move(manager)) {}

    Result ReceiveParameterImpl(const Service::APT::MessageParameter& parameter) override;
    Result Start(const Service::APT::MessageParameter& parameter) override;
    Result Finalize() override;
    void Update() override;

private:
    /// This SharedMemory will be created when we receive the LibAppJustStarted message.
    /// It holds the framebuffer info retrieved by the application with
    /// GSPGPU::ImportDisplayCaptureInfo
    std::shared_ptr<Kernel::SharedMemory> framebuffer_memory;

    /// Parameter received by the applet on start.
    ErrEulaParam param{};
};

} // namespace HLE::Applets
