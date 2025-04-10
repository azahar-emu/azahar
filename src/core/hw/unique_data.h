// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <cstddef>
#include <vector>
#include "common/common_types.h"
#include "common/file_util.h"

namespace FileSys {
class Certificate;
class OTP;
} // namespace FileSys

namespace HW::UniqueData {

struct SecureInfoA {
    std::array<u8, 0x100> signature;
    struct {
        u8 region;
        u8 unknown;
        std::array<u8, 0xF> serial_number;
    } body;

    bool IsValid() const {
        for (auto c : body.serial_number) {
            if (c != 0) {
                return true;
            }
        }

        return false;
    }

    void Invalidate() {
        memset(body.serial_number.data(), 0, body.serial_number.size());
    }

    bool VerifySignature() const;
};
static_assert(sizeof(SecureInfoA) == 0x111);

struct LocalFriendCodeSeedB {
    std::array<u8, 0x100> signature;
    struct {
        u64 unknown;
        u64 friend_code_seed;
    } body;

    bool IsValid() const {
        return body.friend_code_seed != 0;
    }

    void Invalidate() {
        body.friend_code_seed = 0;
    }

    bool VerifySignature() const;
};
static_assert(sizeof(LocalFriendCodeSeedB) == 0x110);

struct MovableSed {
    static constexpr std::array<u8, 0x4> seed_magic{0x53, 0x45, 0x45, 0x44};

    std::array<u8, 0x4> magic;
    u8 unk0;
    u8 is_full;
    u8 unk1;
    u8 unk2;
    LocalFriendCodeSeedB lfcs;
    std::array<u8, 0x8> key_y;

    bool IsValid() const {
        return magic == seed_magic;
    }

    void Invalidate() {
        magic = {0x0, 0x0, 0x0, 0x0};
    }

    bool VerifySignature() const;

    bool IsFull() {
        return is_full != 0;
    }
};
static_assert(sizeof(MovableSed) == 0x120);

struct MovableSedFull {
    struct {
        MovableSed sed;
        std::array<u8, 0x10> unknown;
    } body;
    std::array<u8, 0x10> aes_mac;

    bool IsValid() const {
        return body.sed.magic == MovableSed::seed_magic;
    }

    void Invalidate() {
        body.sed.magic = {0x0, 0x0, 0x0, 0x0};
    }

    bool VerifySignature() const {
        // TODO(PabloMK7): Implement AES MAC verification
        return body.sed.VerifySignature();
    }

    bool IsFull() {
        return body.sed.IsFull();
    }

    size_t GetRealSize() {
        return IsFull() ? sizeof(MovableSedFull) : sizeof(MovableSed);
    }
};
static_assert(sizeof(MovableSedFull) == 0x140);

enum class SecureDataLoadStatus {
    Loaded = 0,
    InvalidSignature = 1,

    NotFound = -1,
    Invalid = -2,
    IOError = -3,
};

SecureDataLoadStatus LoadSecureInfoA();
SecureDataLoadStatus LoadLocalFriendCodeSeedB();
SecureDataLoadStatus LoadOTP();
SecureDataLoadStatus LoadMovable();

std::string GetSecureInfoAPath();
std::string GetLocalFriendCodeSeedBPath();
std::string GetOTPPath();

std::string GetMovablePath();

SecureInfoA& GetSecureInfoA();
LocalFriendCodeSeedB& GetLocalFriendCodeSeedB();
FileSys::Certificate& GetCTCert();
FileSys::OTP& GetOTP();
MovableSedFull& GetMovableSed();

enum class UniqueCryptoFileID {
    NCCH = 0,
};

void InvalidateSecureData();

std::unique_ptr<FileUtil::IOFile> OpenUniqueCryptoFile(const std::string& filename,
                                                       const char openmode[], UniqueCryptoFileID id,
                                                       int flags = 0);

bool IsFullConsoleLinked();
void UnlinkConsole();
} // namespace HW::UniqueData