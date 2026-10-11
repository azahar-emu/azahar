// Copyright 2024 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/sha.h>
#include "common/file_util.h"
#include "common/logging/log.h"
#include "core/file_sys/otp.h"
#include "core/loader/loader.h"

namespace FileSys {
Loader::ResultStatus OTP::Load(const std::string& file_path, std::span<const u8> key,
                               std::span<const u8> iv) {
    FileUtil::IOFile file(file_path, "rb");
    if (!file.IsOpen())
        return Loader::ResultStatus::ErrorNotFound;

    if (file.GetSize() != sizeof(OTPBin)) {
        LOG_ERROR(HW_AES, "Invalid OTP size");
        return Loader::ResultStatus::Error;
    }

    OTPBin temp_otp;
    if (file.ReadBytes(&temp_otp, sizeof(OTPBin)) != sizeof(OTPBin)) {
        return Loader::ResultStatus::Error;
    }

    // OTP is probably encrypted, decrypt it.
    if (temp_otp.body.magic != otp_magic) {
        CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption d;
        d.SetKeyWithIV(key.data(), key.size(), iv.data());
        d.ProcessData(reinterpret_cast<u8*>(&temp_otp), reinterpret_cast<u8*>(&temp_otp),
                      sizeof(temp_otp));

        if (temp_otp.body.magic != otp_magic) {
            LOG_ERROR(HW_AES, "OTP failed to decrypt (or uses dev keys)");
            return Loader::ResultStatus::Error;
        }
    }

    // Verify OTP hash
    CryptoPP::SHA256 hash;
    std::array<u8, CryptoPP::SHA256::DIGESTSIZE> digest;
    hash.CalculateDigest(digest.data(), reinterpret_cast<u8*>(&temp_otp.body),
                         sizeof(temp_otp.body));
    if (temp_otp.hash != digest) {
        LOG_ERROR(HW_AES, "OTP is corrupted");
        return Loader::ResultStatus::Error;
    }

    otp = temp_otp;
    return Loader::ResultStatus::Success;
}
} // namespace FileSys
