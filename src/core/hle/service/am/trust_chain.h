// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <span>
#include <string>
#include "common/common_types.h"
#include "core/hle/result.h"

struct NCCH_Header;

namespace FileSys {
class Ticket;
class TitleMetadata;
} // namespace FileSys

namespace Service::AM {

// The checks Process9 makes on the certificates, tickets, TMDs and contents it installs.
// A signed object names its signer in its issuer string ("Root-CA00000003-XS0000000c"),
// whose certificate is in certs.db. Certificates chain up to the Root key.

// Process9 returns its errors as 0xD8E08065 + (n - 1).
constexpr Result ResultESInvalidArgument{0xD8E08066};    // -2
constexpr Result ResultVerificationFailed{0xD8E0806A};   // -6
constexpr Result ResultConsoleIDMismatch{0xD8E0806B};    // -7
constexpr Result ResultInvalidSignatureType{0xD8E0806D}; // -9
constexpr Result ResultInvalidTicketVersion{0xD8E0806F}; // -11
constexpr Result ResultInvalidTMDVersion{0xD8E08070};    // -12
constexpr Result ResultInvalidNCCH{0xD8E08025};

/// The certificate store, nand/dbs/certs.db/. Each certificate is a file named after the issuer
/// string of the objects it signs ("Root-CA00000003-XS0000000c.cert").
std::string GetCertsDirectory();

/// Writes the retail CA00000003, XS0000000c and CP0000000b certificates to certs.db when they
/// are missing. On hardware certs.db comes with them from the factory.
void EnsureDefaultCertificates();

/// Imports concatenated certificates into certs.db, as Process9 does for AM:ImportCertificates
/// and for the certificate chain of a CIA. All of them must verify up to Root (they can issue
/// each other), or none is stored. Certificates already in certs.db are left as they are.
Result ImportCertificates(std::span<const u8> data);

/// Checks the signature of a ticket (by an XS certificate) and its format. The signature of a
/// personalized ticket covers its title key unwrapped, so DoTitlekeyFixup must be done first.
Result VerifyTicket(const FileSys::Ticket& ticket);

/// Checks the signature of a TMD (by a CP or SP certificate), its format and the hashes of its
/// content records.
Result VerifyTitleMetadata(const FileSys::TitleMetadata& tmd);

/// Checks the signature of an NCCH header. header is the header as received, exheader the
/// decrypted extended header (needed for CXIs, whose header key is in the access descriptor).
Result VerifyNCCHSignature(const NCCH_Header& header, std::span<const u8> exheader);

} // namespace Service::AM
