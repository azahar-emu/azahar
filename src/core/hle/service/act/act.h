// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Service::ACT {

/// Initializes all ACT services
class Module final {
public:
    explicit Module(Core::System& system_);
    ~Module() = default;

    class Interface : public ServiceFramework<Interface> {
    public:
        Interface(std::shared_ptr<Module> act, const char* name);
        ~Interface();

    protected:
        std::shared_ptr<Module> act;

        /**
         * ACT::Initialize service function.
         * Inputs:
         *     1 : SDK version
         *     2 : Shared Memory Size
         *     3 : PID Translation Header (0x20)
         *     4 : Caller PID
         *     5 : Handle Translation Header (0x0)
         *     6 : Shared Memory Handle
         * Outputs:
         *     1 : Result of function, 0 on success, otherwise error code
         */
        void Initialize(Kernel::HLERequestContext& ctx);

        /**
         * ACT::GetErrorCode service function.
         * Inputs:
         *     1 : Result code
         * Outputs:
         *     1 : Result of function, 0 on success, otherwise error code
         *     2 : Error code
         */
        void GetErrorCode(Kernel::HLERequestContext& ctx);

        /**
         * ACT::GetAccountInfo service function.
         * Inputs:
         *     1 : Account slot
         *     2 : Size
         *     3 : Block ID
         *     4 : Output Buffer Mapping Translation Header ((Size << 4) | 0xC)
         *     5 : Output Buffer Pointer
         * Outputs:
         *     1 : Result of function, 0 on success, otherwise error code
         */
        void GetAccountInfo(Kernel::HLERequestContext& ctx);
    };

private:
    [[maybe_unused]]
    Core::System& system;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int);
    friend class boost::serialization::access;
};

void InstallInterfaces(Core::System& system);

} // namespace Service::ACT

BOOST_CLASS_EXPORT_KEY(Service::ACT::Module)
SERVICE_CONSTRUCT(Service::ACT::Module)