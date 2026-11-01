// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

// winsock2.h must be included before anything else in any translation unit that might
// transitively pull in <windows.h> (e.g. via Qt headers) - otherwise windows.h drags in the
// old winsock.h first and every winsock2/ws2tcpip symbol below conflicts. Same idiom as
// core/gdbstub/gdbstub.cpp.
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "common/common_types.h"

namespace NetworkStreaming {

#if defined(_WIN32)

// SOCKET is UINT_PTR (64-bit on x64 Windows) - storing it in a plain `int` truncates it.
// Mirrors the existing SocketHolder idiom in src/network/artic_base/artic_base_client.h.
using SocketHandle = unsigned long long;
using SockLenT = int;

constexpr int kShutdownBoth = SD_BOTH;
// Windows send()/recv() never raise SIGPIPE, so there's no MSG_NOSIGNAL equivalent needed.
constexpr int kNoSignal = 0;

inline SOCKET NativeSocket(SocketHandle fd) {
    return static_cast<SOCKET>(fd);
}

inline void CloseSocketHandle(SocketHandle fd) {
    closesocket(NativeSocket(fd));
}

#else

using SocketHandle = int;
using SockLenT = socklen_t;

constexpr int kShutdownBoth = SHUT_RDWR;
constexpr int kNoSignal = MSG_NOSIGNAL;

inline int NativeSocket(SocketHandle fd) {
    return fd;
}

inline void CloseSocketHandle(SocketHandle fd) {
    close(fd);
}

#endif

constexpr SocketHandle InvalidSocketHandle = static_cast<SocketHandle>(-1);

} // namespace NetworkStreaming
