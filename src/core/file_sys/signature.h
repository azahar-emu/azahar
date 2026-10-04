// Copyright 2024 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include "common/common_types.h"
#include "common/logging/log.h"

namespace FileSys {

enum SignatureType : u32 {
    Rsa4096Sha1 = 0x10000,
    Rsa2048Sha1 = 0x10001,
    EllipticSha1 = 0x10002,
    Rsa4096Sha256 = 0x10003,
    Rsa2048Sha256 = 0x10004,
    EcdsaSha256 = 0x10005
};

inline u32 GetSignatureSize(u32 signature_type) {
    switch (signature_type) {
    case Rsa4096Sha1:
    case Rsa4096Sha256:
        return 0x200;

    case Rsa2048Sha1:
    case Rsa2048Sha256:
        return 0x100;

    case EllipticSha1:
    case EcdsaSha256:
        return 0x3C;
    }

    LOG_ERROR(Common_Filesystem, "Bad signature {}", signature_type);
    return 0;
}
} // namespace FileSys