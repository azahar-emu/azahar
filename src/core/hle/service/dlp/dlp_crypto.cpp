// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include "common/archives.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/ssl/ssl_c.h"
#include "core/hw/aes/arithmetic128.h"
#include "core/hw/aes/key.h"
#include "core/hle/service/dlp/dlp_base.h"

namespace Service::DLP {

void DLP_Base::DLPEncryptCTR(void *_out, size_t size, const u8 *iv_ctr) {
    auto out = reinterpret_cast<u8*>(_out);
    memset(out, 0, size);

    HW::AES::SelectDlpNfcKeyYIndex(HW::AES::DlpNfcKeyY::Dlp);
    HW::AES::AESKey key = HW::AES::GetNormalKey(HW::AES::DLPNFCDataKey);

    // AlgorithmType::CTR_Encrypt
    CryptoPP::CTR_Mode<CryptoPP::AES>::Encryption aes;
    aes.SetKeyWithIV(key.data(), CryptoPP::AES::BLOCKSIZE, iv_ctr);
    aes.ProcessData(out, out, size);
}

}
