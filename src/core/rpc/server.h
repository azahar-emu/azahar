// Copyright 2019-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <memory>
#include "core/rpc/rpc_server.h"

namespace Core {
class System;
}

namespace Core::RPC {

class UDPServer;
class Packet;

class Server {
public:
    explicit Server(Core::System& system_);
    ~Server();

    void NewRequestCallback(std::unique_ptr<Packet> new_request);

    void ProcessCoreRequests() {
        rpc_server.ProcessCoreRequests();
    }

private:
    RPCServer rpc_server;
    std::unique_ptr<UDPServer> udp_server;
};

} // namespace Core::RPC
