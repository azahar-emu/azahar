// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/sha.h>
#include <zstd.h>
#include <zstd_seekable.h>

#include "common/alignment.h"
#include "common/archives.h"
#include "common/assert.h"
#include "common/file_derived.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"

namespace FileUtil {

SubIOFile::SubIOFile() {
    Child() = std::make_unique<NullIOFile>();
}

SubIOFile::SubIOFile(const std::string& filename, const char openmode[],
                     std::size_t sub_file_offset, std::size_t sub_file_size, int flags)
    : m_sub_file_offset(sub_file_offset), m_sub_file_capacity(sub_file_size),
      m_sub_file_size(sub_file_size) {
    if (!IsReplaceOpenMode(openmode)) {
        // Only "replace" (existing file, read+write, no create/truncate/append) is
        // valid for a subfile, reject anything else.
        Child() = std::make_unique<NullIOFile>();
        m_good = false;
        return;
    }
    Child() = std::make_unique<IOFile>(filename, openmode, flags);
    if (!Open()) {
        Child() = std::make_unique<NullIOFile>();
        m_good = false;
    }
}

SubIOFile::SubIOFile(std::unique_ptr<IOFileBase>&& child_file, std::size_t sub_file_offset,
                     std::size_t sub_file_size)
    : m_sub_file_offset(sub_file_offset), m_sub_file_capacity(sub_file_size),
      m_sub_file_size(sub_file_size) {
    Child() = std::move(child_file);
    if (!Open()) {
        Child() = std::make_unique<NullIOFile>();
        m_good = false;
    }
}

SubIOFile::~SubIOFile() = default;

void SubIOFile::Swap(SubIOFile& other) noexcept {
    std::swap(Child(), other.Child());
    std::swap(m_sub_file_offset, other.m_sub_file_offset);
    std::swap(m_sub_file_capacity, other.m_sub_file_capacity);
    std::swap(m_sub_file_size, other.m_sub_file_size);
    std::swap(m_good, other.m_good);
}

SubIOFile::SubIOFile(SubIOFile&& other) noexcept {
    Swap(other);
}

SubIOFile& SubIOFile::operator=(SubIOFile&& other) noexcept {
    Swap(other);
    return *this;
}

bool SubIOFile::IsReplaceOpenMode(const char* openmode) {
    // Restricts callers to fopen's
    // "r+" or "r" family ("rb+", "r+b", "r", "rb").
    if (openmode == nullptr) {
        return false;
    }
    const std::string_view mode{openmode};
    const bool has_read = mode.find('r') != std::string_view::npos;
    const bool truncate = mode.find('w') != std::string_view::npos;
    const bool append = mode.find('a') != std::string_view::npos;
    return has_read && !truncate && !append;
}

bool SubIOFile::Open() {
    if (!Child()->IsOpen()) {
        m_good = false;
        return false;
    }
    if (Child()->GetSize() < m_sub_file_offset + m_sub_file_capacity) {
        // Child file isn't large enough to contain the requested fragment.
        m_good = false;
        return false;
    }
    if (!Child()->Seek(static_cast<s64>(m_sub_file_offset), SEEK_SET)) {
        m_good = false;
        return false;
    }
    m_good = true;
    return true;
}

bool SubIOFile::IsGood() const {
    return m_good && Child()->IsGood();
}

u64 SubIOFile::Tell() const {
    if (!IsOpen()) {
        return std::numeric_limits<u64>::max();
    }
    const u64 abs_pos = Child()->Tell();
    return (abs_pos > m_sub_file_offset) ? (abs_pos - m_sub_file_offset) : 0;
}

bool SubIOFile::Seek(s64 off, int origin) {
    if (!IsOpen()) {
        m_good = false;
        return false;
    }
    s64 target;
    switch (origin) {
    case SEEK_SET:
        target = off;
        break;
    case SEEK_CUR:
        target = static_cast<s64>(Tell()) + off;
        break;
    case SEEK_END:
        target = static_cast<s64>(m_sub_file_size) + off;
        break;
    default:
        m_good = false;
        return false;
    }
    if (target < 0 || static_cast<u64>(target) > m_sub_file_size) {
        m_good = false;
        return false;
    }
    const bool ok = Child()->Seek(static_cast<s64>(m_sub_file_offset) + target, SEEK_SET);
    if (!ok) {
        m_good = false;
    }
    return ok;
}

u64 SubIOFile::GetSize() const {
    return m_sub_file_size;
}

bool SubIOFile::Resize(u64 size) {
    if (!IsOpen()) {
        m_good = false;
        return false;
    }
    if (size > m_sub_file_capacity) {
        // Growing is not possible as it would go beyond the underying file.
        m_good = false;
        return false;
    }
    m_sub_file_size = size;
    if (Tell() > m_sub_file_size) {
        Seek(0, SEEK_END);
    }
    return true;
}

void SubIOFile::Clear() {
    m_good = true;
    Child()->Clear();
}

std::unique_ptr<IOFileBase> SubIOFile::OpenCopy() const {
    std::unique_ptr<SubIOFile> ret = std::make_unique<SubIOFile>();
    ret->Child() = std::move(Child()->OpenCopy());

    ret->m_sub_file_capacity = m_sub_file_capacity;
    ret->m_sub_file_offset = m_sub_file_offset;
    ret->m_sub_file_size = m_sub_file_size;
    ret->Open();

    return ret;
}

std::size_t SubIOFile::BytesRemaining() const {
    const std::size_t pos = Tell();
    return pos >= m_sub_file_size ? 0 : (m_sub_file_size - pos);
}

std::size_t SubIOFile::ReadImpl(void* data, std::size_t length, std::size_t elem_size) {
    if (!IsOpen() || elem_size == 0) {
        return 0;
    }
    const std::size_t requested_bytes = length * elem_size;
    const std::size_t bytes_to_read = std::min(requested_bytes, BytesRemaining());
    if (bytes_to_read == 0) {
        return 0;
    }
    const std::span<char> byte_span(reinterpret_cast<char*>(data), bytes_to_read);
    const std::size_t bytes_read = Child()->ReadSpan(byte_span);
    return bytes_read / elem_size;
}

std::size_t SubIOFile::ReadAtImpl(void* data, std::size_t byte_count, std::size_t offset) {
    if (!IsOpen() || offset >= m_sub_file_size) {
        return 0;
    }
    const std::size_t available = m_sub_file_size - offset;
    const std::size_t bytes_to_read = std::min(byte_count, available);
    if (bytes_to_read == 0) {
        return 0;
    }
    const std::size_t absolute_offset = m_sub_file_offset + offset;
    return Child()->ReadAtArray(reinterpret_cast<char*>(data), bytes_to_read, absolute_offset);
}

std::size_t SubIOFile::WriteImpl(const void* data, std::size_t length, std::size_t elem_size) {
    if (!IsOpen() || elem_size == 0) {
        return 0;
    }
    const std::size_t requested_bytes = length * elem_size;
    const std::size_t bytes_to_write = std::min(requested_bytes, BytesRemaining());
    if (bytes_to_write == 0) {
        return 0;
    }
    const std::span<const char> byte_span(reinterpret_cast<const char*>(data), bytes_to_write);
    const std::size_t bytes_written = Child()->WriteSpan(byte_span);
    return bytes_written / elem_size;
}

struct CryptoIOFileImpl {

    std::vector<u8> key;
    std::vector<u8> iv;

    CryptoPP::CTR_Mode<CryptoPP::AES>::Decryption d;
    CryptoPP::CTR_Mode<CryptoPP::AES>::Encryption e;

    std::vector<u8> write_buffer;

    CryptoIOFile::CryptoIOFileHeader header;

    CryptoIOFileImpl() {}

    CryptoIOFileImpl(const std::vector<u8>& aes_key, const std::vector<u8>& aes_iv) {
        key = aes_key;
        iv = aes_iv;
        d.SetKeyWithIV(aes_key.data(), aes_key.size(), aes_iv.data());
        e.SetKeyWithIV(aes_key.data(), aes_key.size(), aes_iv.data());
    }

    std::size_t ReadImpl(std::unique_ptr<IOFileBase>& base, void* data, std::size_t length,
                         std::size_t elem_size) {

        std::size_t res =
            base->ReadBytes(reinterpret_cast<char*>(data), length * elem_size) / elem_size;
        if (res != std::numeric_limits<std::size_t>::max() && res != 0) {
            d.ProcessData(reinterpret_cast<CryptoPP::byte*>(data),
                          reinterpret_cast<CryptoPP::byte*>(data), res * elem_size);
            e.Seek(base->Tell() - header.header_size);
        }
        return res;
    }

    std::size_t ReadAtImpl(std::unique_ptr<IOFileBase>& base, void* data, std::size_t byte_count,
                           std::size_t offset) {
        std::size_t res = base->ReadAtBytes(reinterpret_cast<char*>(data), byte_count,
                                            offset + header.header_size);
        if (res != std::numeric_limits<std::size_t>::max() && res != 0) {
            d.Seek(offset);
            d.ProcessData(reinterpret_cast<CryptoPP::byte*>(data),
                          reinterpret_cast<CryptoPP::byte*>(data), res);
            e.Seek(base->Tell() - header.header_size);
        }
        return res;
    }

    std::size_t WriteImpl(std::unique_ptr<IOFileBase>& base, const void* data, std::size_t length,
                          std::size_t elem_size) {
        if (write_buffer.size() < length * elem_size) {
            write_buffer.resize(length * elem_size);
        }
        e.ProcessData(write_buffer.data(), reinterpret_cast<const CryptoPP::byte*>(data),
                      length * elem_size);
        std::size_t res = base->WriteBytes(write_buffer.data(), length * elem_size) / elem_size;
        if (res != std::numeric_limits<std::size_t>::max() && res != 0) {
            d.Seek(base->Tell() - header.header_size);
        }
        return res;
    }

    bool Seek(std::unique_ptr<IOFileBase>& base) {
        u64 pos = base->Tell();
        d.Seek(pos - header.header_size);
        e.Seek(pos - header.header_size);
        return true;
    }
};

bool CryptoIOFile::CryptoIOFileHeader::IsValid(std::span<const u8> key, std::span<const u8> iv,
                                               bool check_hash) {
    bool valid = magic == FileUtil::MakeMagic('A', 'Z', 'C', 'R') && version == 0;
    if (valid && check_hash) {
        std::array<CryptoPP::byte, 0x20> hash_data{};
        memcpy(hash_data.data(), key.data(), std::min(key.size(), size_t(0x10)));
        memcpy(hash_data.data() + 0x10, iv.data(), std::min(iv.size(), size_t(0x10)));

        CryptoPP::SHA256 hash;
        std::array<CryptoPP::byte, CryptoPP::SHA256::DIGESTSIZE> digest{};
        hash.CalculateDigest(digest.data(), hash_data.data(), hash_data.size());

        valid = key_nonce_hash == digest;
    }
    return valid;
}

bool CryptoIOFile::IsCryptoIOFile(IOFileBase* underlying_file, const std::vector<u8>& aes_key,
                                  const std::vector<u8>& aes_iv) {
    CryptoIOFileHeader header{};
    underlying_file->ReadAtBytes(&header, sizeof(header), 0);
    if (header.IsValid(aes_key, aes_iv, false)) {
        return true;
    }

    // Legacy mode. To be removed in a few years, only allow NCCH, NCSD or Z3DS.
    std::vector<u8> data(0x104);
    CryptoPP::CTR_Mode<CryptoPP::AES>::Decryption d;
    d.SetKeyWithIV(aes_key.data(), aes_key.size(), aes_iv.data());
    underlying_file->ReadAtBytes(data.data(), 0x104, 0);
    d.ProcessData(data.data(), data.data(), 0x104);
    u32 magic0x0, magic0x100;
    memcpy(&magic0x0, data.data(), sizeof(u32));
    memcpy(&magic0x100, data.data() + 0x100, sizeof(u32));
    return magic0x0 == FileUtil::MakeMagic('Z', '3', 'D', 'S') ||
           magic0x100 == FileUtil::MakeMagic('N', 'C', 'C', 'H') ||
           magic0x100 == FileUtil::MakeMagic('N', 'C', 'S', 'D');
}

CryptoIOFile::CryptoIOFile() {
    impl = std::make_unique<CryptoIOFileImpl>();
    Child() = std::make_unique<NullIOFile>();
}

CryptoIOFile::CryptoIOFile(const std::string& filename, const char _openmode[],
                           const std::vector<u8>& aes_key, const std::vector<u8>& aes_iv,
                           int flags) {
    openmode = _openmode;
    impl = std::make_unique<CryptoIOFileImpl>(aes_key, aes_iv);
    Child() = std::make_unique<IOFile>(filename, _openmode, flags);
    Open();
}

CryptoIOFile::CryptoIOFile(std::unique_ptr<IOFileBase> underlying_file, const char _openmode[],
                           const std::vector<u8>& aes_key, const std::vector<u8>& aes_iv) {
    impl = std::make_unique<CryptoIOFileImpl>(aes_key, aes_iv);
    Child() = std::move(underlying_file);
    Open();
}

CryptoIOFile::~CryptoIOFile() {}

std::size_t CryptoIOFile::ReadImpl(void* data, std::size_t length, std::size_t elem_size) {
    return impl->ReadImpl(Child(), data, length, elem_size);
}

std::size_t CryptoIOFile::ReadAtImpl(void* data, std::size_t byte_count, std::size_t offset) {
    return impl->ReadAtImpl(Child(), data, byte_count, offset);
}

std::size_t CryptoIOFile::WriteImpl(const void* data, std::size_t length, std::size_t elem_size) {
    return impl->WriteImpl(Child(), data, length, elem_size);
}

bool CryptoIOFile::Seek(s64 off, int origin) {
    s64 target;
    u64 child_size = Child()->GetSize();
    switch (origin) {
    case SEEK_SET:
        target = off + impl->header.header_size;
        break;
    case SEEK_CUR:
        target = static_cast<s64>(Child()->Tell()) + off;
        break;
    case SEEK_END:
        target = static_cast<s64>(child_size) + off;
        break;
    default:
        return false;
    }
    if (target < impl->header.header_size || static_cast<u64>(target) > child_size) {
        return false;
    }
    bool res = Child()->Seek(target, SEEK_SET);
    res &= impl->Seek(Child());
    return res;
}

u64 CryptoIOFile::Tell() const {
    return Child()->Tell() - impl->header.header_size;
}

u64 CryptoIOFile::GetSize() const {
    return Child()->GetSize() - impl->header.header_size;
}

bool CryptoIOFile::Resize(u64 size) {
    UNIMPLEMENTED();
    return false;
}

std::unique_ptr<IOFileBase> CryptoIOFile::OpenCopy() const {
    std::unique_ptr<CryptoIOFile> ret = std::make_unique<CryptoIOFile>();
    ret->Child() = std::move(Child()->OpenCopy());

    ret->impl = std::make_unique<CryptoIOFileImpl>(impl->key, impl->iv);
    ret->impl->header = impl->header;
    ret->Seek(0, SEEK_SET);

    return ret;
}

bool CryptoIOFile::Open() {
    impl = std::make_unique<CryptoIOFileImpl>(impl->key, impl->iv);
    if (openmode.find('w') != openmode.npos) {
        impl->header.magic = FileUtil::MakeMagic('A', 'Z', 'C', 'R');
        impl->header.version = 0;
        impl->header.header_size = sizeof(impl->header);
        impl->header.reserved = 0;

        std::array<CryptoPP::byte, 0x20> hash_data{};
        memcpy(hash_data.data(), impl->key.data(), std::min(impl->key.size(), size_t(0x10)));
        memcpy(hash_data.data() + 0x10, impl->iv.data(), std::min(impl->iv.size(), size_t(0x10)));

        CryptoPP::SHA256 hash;
        std::array<CryptoPP::byte, CryptoPP::SHA256::DIGESTSIZE> digest;
        hash.CalculateDigest(digest.data(), hash_data.data(), hash_data.size());

        memcpy(impl->header.key_nonce_hash.data(), digest.data(), digest.size());

        Child()->WriteObject(impl->header);
    } else {
        if (Child()->ReadAtBytes(&impl->header, sizeof(impl->header), 0) != sizeof(impl->header)) {
            Child()->Close();
            return false;
        }
        bool header_valid = impl->header.IsValid(impl->key, impl->iv, false);
        bool hash_valid = impl->header.IsValid(impl->key, impl->iv, true);
        if (!header_valid) {

            // Legacy mode. To be removed in a few years, only allow NCCH, NCSD or Z3DS.
            impl->header.magic = FileUtil::MakeMagic('A', 'Z', 'C', 'R');
            impl->header.version = 0;
            impl->header.header_size = 0;
            impl->header.reserved = 0;

            u32 magic0x100, magic0x0;
            if (ReadAtBytes(&magic0x100, sizeof(u32), 0x100) != sizeof(u32) ||
                ReadAtBytes(&magic0x0, sizeof(u32), 0x0) != sizeof(u32)) {
                Child()->Close();
                return false;
            }
            if (magic0x100 != FileUtil::MakeMagic('N', 'C', 'C', 'H') &&
                magic0x100 != FileUtil::MakeMagic('N', 'C', 'S', 'D') &&
                magic0x0 != FileUtil::MakeMagic('Z', '3', 'D', 'S')) {
                Child()->Close();
                return false;
            }
        } else {
            if (!hash_valid) {
                // Header valid, but hash invalid.
                Child()->Close();
                return false;
            }
        }
    }
    Seek(0, SEEK_SET);
    return true;
}

template <class Archive>
void CryptoIOFile::serialize(Archive& ar, const unsigned int) {
    ar& boost::serialization::base_object<IOFileBase>(*this);
    ar & impl->header;
    ar & impl->key;
    ar & impl->iv;
    ar & openmode;
    if (Archive::is_loading::value) {
        Open();
    }
}

struct Z3DSWriteIOFile::Z3DSWriteIOFileImpl {
    Z3DSWriteIOFileImpl() {}
    Z3DSWriteIOFileImpl(size_t frame_size) {
        zstd_frame_size = frame_size;
        cstream = ZSTD_seekable_createCStream();
        size_t init_result = ZSTD_seekable_initCStream(cstream, ZSTD_CLEVEL_DEFAULT, 0,
                                                       static_cast<unsigned int>(frame_size));
        if (ZSTD_isError(init_result)) {
            LOG_ERROR(Common_Filesystem, "ZSTD_seekable_initCStream() error : {}",
                      ZSTD_getErrorName(init_result));
        }

        write_header.magic = Z3DSFileHeader::EXPECTED_MAGIC;
        write_header.version = Z3DSFileHeader::EXPECTED_VERSION;
        write_header.header_size = sizeof(Z3DSFileHeader);
        next_input_size_hint = ZSTD_CStreamInSize();
    }

    bool WriteHeader(IOFileBase* file) {
        file->Seek(0, SEEK_SET);
        return file->WriteBytes(&write_header, sizeof(write_header)) == sizeof(write_header);
    }

    bool WriteMetadata(IOFileBase* file, const std::span<u8>& data) {
        std::array<u8, 0x10> tmp_data{};
        size_t total_size = Common::AlignUp(data.size(), 0x10);
        write_header.metadata_size = static_cast<u32>(total_size);
        size_t res_written = file->WriteBytes(data.data(), data.size());
        res_written += file->WriteBytes(tmp_data.data(), total_size - data.size());
        return res_written == total_size;
    }

    size_t Write(IOFileBase* file, const void* data, std::size_t length) {
        size_t ret = length;

        const size_t out_size = ZSTD_CStreamOutSize();
        const size_t in_size = ZSTD_CStreamInSize();

        if (write_buffer.size() < out_size) {
            write_buffer.resize(out_size);
        }

        ZSTD_inBuffer input = {data, length, 0};
        while (input.pos < input.size) {
            ZSTD_outBuffer output = {write_buffer.data(), write_buffer.size(), 0};
            next_input_size_hint = ZSTD_seekable_compressStream(cstream, &output, &input);
            if (ZSTD_isError(next_input_size_hint)) {
                LOG_ERROR(Common_Filesystem, "ZSTD_seekable_compressStream() error : {}",
                          ZSTD_getErrorName(next_input_size_hint));
                ret = 0;
                next_input_size_hint = ZSTD_CStreamInSize();
                break;
            }
            if (next_input_size_hint > in_size) {
                next_input_size_hint = in_size;
            }
            if (file->WriteBytes(static_cast<u8*>(output.dst), output.pos) != output.pos) {
                ret = 0;
                break;
            }
            written_compressed += output.pos;
        }
        return ret;
    }

    bool Close(IOFileBase* file, size_t written_uncompressed) {
        const size_t out_size = ZSTD_CStreamOutSize();

        if (write_buffer.size() < out_size) {
            write_buffer.resize(out_size);
        }

        size_t remaining;
        do {
            ZSTD_outBuffer output = {write_buffer.data(), write_buffer.size(), 0};
            remaining = ZSTD_seekable_endStream(cstream, &output); /* close stream */
            if (ZSTD_isError(remaining)) {
                LOG_ERROR(Common_Filesystem, "ZSTD_seekable_endStream() error : {}",
                          ZSTD_getErrorName(remaining));
                return false;
            }

            if (file->WriteBytes(static_cast<u8*>(output.dst), output.pos) != output.pos) {
                return false;
            }
            written_compressed += output.pos;
        } while (remaining);

        write_header.compressed_size = written_compressed;
        write_header.uncompressed_size = written_uncompressed;

        ZSTD_seekable_freeCStream(cstream);

        return WriteHeader(file);
    }

    std::vector<u8> write_buffer;
    size_t next_input_size_hint = 0;
    size_t zstd_frame_size = 0;
    u64 written_compressed = 0;

    ZSTD_seekable_CStream* cstream{};
    Z3DSFileHeader write_header{};
};

Z3DSWriteIOFile::Z3DSWriteIOFile() : impl{std::make_unique<Z3DSWriteIOFileImpl>()} {
    Child() = std::make_unique<NullIOFile>();
}

Z3DSWriteIOFile::Z3DSWriteIOFile(std::unique_ptr<IOFileBase>&& underlying_file,
                                 const std::array<u8, 4>& underlying_magic, size_t frame_size)
    : impl{std::make_unique<Z3DSWriteIOFileImpl>(frame_size)} {
    Child() = std::move(underlying_file);
    ASSERT_MSG(!Child()->GetType().HasCompressedType(), "Underlying file is already compressed!");
    impl->write_header.underlying_magic = underlying_magic;
    impl->WriteHeader(Child().get());

    Metadata().Add("compressor", std::string("Azahar ") + Common::g_build_fullname);

    std::time_t tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buf[0x20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    Metadata().Add("date", buf);

    Metadata().Add(
        "maxframesize",
        std::to_string(frame_size ? frame_size : ZSTD_SEEKABLE_MAX_FRAME_DECOMPRESSED_SIZE));
}

Z3DSWriteIOFile::~Z3DSWriteIOFile() {
    this->Close();
}

bool Z3DSWriteIOFile::Close() {
    impl->Close(Child().get(), written_uncompressed);
    return Child()->Close();
}

u64 Z3DSWriteIOFile::GetSize() const {
    return written_uncompressed;
}

bool Z3DSWriteIOFile::Resize(u64 size) {
    // Stubbed
    UNIMPLEMENTED();
    return false;
}

bool Z3DSWriteIOFile::Open() {
    if (is_serializing) {
        return true;
    }
    // Stubbed
    UNIMPLEMENTED();
    return false;
}

std::size_t Z3DSWriteIOFile::ReadImpl(void* data, std::size_t length, std::size_t data_size) {
    // Stubbed
    UNIMPLEMENTED();
    return 0;
}

std::size_t Z3DSWriteIOFile::ReadAtImpl(void* data, std::size_t byte_count, std::size_t offset) {
    // Stubbed
    UNIMPLEMENTED();
    return 0;
}

std::size_t Z3DSWriteIOFile::WriteImpl(const void* data, std::size_t length,
                                       std::size_t data_size) {
    if (!metadata_written) {
        metadata_written = true;
        auto metadata_binary = metadata.AsBinary();
        if (!metadata_binary.empty()) {
            impl->WriteMetadata(Child().get(), metadata_binary);
        }
    }

    size_t ret = impl->Write(Child().get(), data, length * data_size);
    written_uncompressed += ret;
    return ret;
}

bool Z3DSWriteIOFile::Seek(s64 off, int origin) {
    if (is_serializing) {
        return true;
    }
    // Stubbed
    UNIMPLEMENTED();
    return false;
}

u64 Z3DSWriteIOFile::Tell() const {
    return written_uncompressed;
}

std::unique_ptr<IOFileBase> Z3DSWriteIOFile::OpenCopy() const {
    UNIMPLEMENTED();
    return nullptr;
}

size_t Z3DSWriteIOFile::GetNextWriteHint() {
    return impl->next_input_size_hint;
}

template <class Archive>
void Z3DSWriteIOFile::serialize(Archive& ar, const unsigned int) {
    is_serializing = true;
    ar& boost::serialization::base_object<IOFileBase>(*this);

    ar & written_uncompressed;
    ar & metadata_written;
    ar & metadata;

    Z3DSFileHeader hd;
    size_t frame_size;
    u64 written_compressed;
    if (Archive::is_loading::value) {
        ar & hd;
        ar & frame_size;
        ar & written_compressed;
        impl = std::make_unique<Z3DSWriteIOFileImpl>(frame_size);
        impl->write_header = hd;
        impl->written_compressed = written_compressed;
    } else {
        ar & impl->write_header;
        ar & impl->zstd_frame_size;
        ar & impl->written_compressed;
    }
    is_serializing = false;
}

struct Z3DSReadIOFile::Z3DSReadIOFileImpl {
    Z3DSReadIOFileImpl() {}
    Z3DSReadIOFileImpl(IOFileBase* file, bool load_metadata = true) {
        curr_file = file;
        m_good = file->ReadAtBytes(&header, sizeof(header), 0) == sizeof(header);
        m_good &= header.magic == Z3DSFileHeader::EXPECTED_MAGIC &&
                  header.version == Z3DSFileHeader::EXPECTED_VERSION;

        if (!m_good) {
            return;
        }

        if (header.metadata_size && load_metadata) {
            std::vector<u8> buff(header.metadata_size);
            file->ReadAtBytes(buff.data(), buff.size(), header.header_size);
            metadata = Z3DSMetadata(buff);
        }

        seekable = ZSTD_seekable_create();

        ZSTD_seekable_customFile custom_file{
            .opaque = this,
            .read = [](void* opaque, void* buffer, size_t n) -> int {
                return reinterpret_cast<Z3DSReadIOFileImpl*>(opaque)->OnZSTDRead(buffer, n);
            },
            .seek = [](void* opaque, long long offset, int origin) -> int {
                return reinterpret_cast<Z3DSReadIOFileImpl*>(opaque)->OnZSTDSeek(offset, origin);
            },
        };
        size_t init_result = ZSTD_seekable_initAdvanced(seekable, custom_file);
        if (ZSTD_isError(init_result)) {
            LOG_ERROR(Common_Filesystem, "ZSTD_seekable_initCStream() error : {}",
                      ZSTD_getErrorName(init_result));
            m_good = false;
        }
    }

    int OnZSTDRead(void* buffer, size_t n) {
        const size_t read = curr_file->ReadBytes(reinterpret_cast<uint8_t*>(buffer), n);
        if (read != n) {
            return -1;
        }
        return 0;
    }

    int OnZSTDSeek(long long offset, int origin) {
        if (origin == SEEK_SET) {
            offset += static_cast<long long>(header.metadata_size) + header.header_size;
        }
        const bool res = curr_file->Seek(offset, origin);
        return res ? 0 : -1;
    }

    size_t Read(void* data, std::size_t length) {
        if (!m_good)
            return 0;
        size_t result = ZSTD_seekable_decompress(seekable, data, length, uncompressed_pos);
        if (ZSTD_isError(result)) {
            LOG_ERROR(Common_Filesystem, "ZSTD_seekable_decompress() error : {}",
                      ZSTD_getErrorName(result));
            return 0;
        }
        uncompressed_pos += result;
        return result;
    }

    size_t ReadAt(void* data, std::size_t length, size_t pos) {
        if (!m_good)
            return 0;
        // ReadAt should be thread safe, but seekable compression is not,
        // so we are forced to use a lock.
        std::scoped_lock lock(read_mutex);

        size_t result = ZSTD_seekable_decompress(seekable, data, length, pos);
        if (ZSTD_isError(result)) {
            LOG_ERROR(Common_Filesystem, "ZSTD_seekable_decompress() error : {}",
                      ZSTD_getErrorName(result));
            return 0;
        }
        return result;
    }

    bool Seek(s64 off, int origin) {
        s64 start = 0;
        switch (origin) {
        case SEEK_SET:
            start = 0;
            break;
        case SEEK_CUR:
            start = static_cast<s64>(uncompressed_pos);
            break;
        case SEEK_END:
            start = static_cast<s64>(header.uncompressed_size);
            break;
        default:
            return false;
        }
        s64 new_pos = start + off;
        if (new_pos < 0)
            return false;
        uncompressed_pos = static_cast<u64>(new_pos);
        return true;
    }

    void Close() {
        ZSTD_seekable_free(seekable);
    }

    Z3DSFileHeader header{};
    ZSTD_seekable* seekable = nullptr;
    bool m_good = true;
    IOFileBase* curr_file = nullptr;
    std::mutex read_mutex;
    u64 uncompressed_pos = 0;
    Z3DSMetadata metadata;
};

bool Z3DSReadIOFile::IsZ3DSIOFile(IOFileBase* underlying_file) {
    Z3DSFileHeader header{};
    if (underlying_file->ReadAtBytes(&header, sizeof(header), 0) != sizeof(header)) {
        return false;
    }
    if (header.magic != Z3DSFileHeader::EXPECTED_MAGIC ||
        header.version != Z3DSFileHeader::EXPECTED_VERSION) {
        return false;
    }
    return true;
}

std::optional<u32> Z3DSReadIOFile::GetUnderlyingFileMagic(IOFileBase* underlying_file) {
    Z3DSFileHeader header{};
    if (underlying_file->ReadAtBytes(&header, sizeof(header), 0) != sizeof(header)) {
        return std::nullopt;
    }
    if (header.magic != Z3DSFileHeader::EXPECTED_MAGIC ||
        header.version != Z3DSFileHeader::EXPECTED_VERSION) {
        return std::nullopt;
    }

    return MakeMagic(header.underlying_magic[0], header.underlying_magic[1],
                     header.underlying_magic[2], header.underlying_magic[3]);
}

Z3DSReadIOFile::Z3DSReadIOFile() : impl{std::make_unique<Z3DSReadIOFileImpl>()} {
    Child() = std::make_unique<NullIOFile>();
}

Z3DSReadIOFile::Z3DSReadIOFile(std::unique_ptr<IOFileBase>&& underlying_file)
    : impl{std::make_unique<Z3DSReadIOFileImpl>(underlying_file.get())} {
    Child() = std::move(underlying_file);
    ASSERT_MSG(!Child()->GetType().HasCompressedType(), "Underlying file is already compressed!");
}

Z3DSReadIOFile::~Z3DSReadIOFile() {
    this->Close();
}

bool Z3DSReadIOFile::Close() {
    impl->Close();
    return Child()->Close();
}

u64 Z3DSReadIOFile::GetSize() const {
    return impl->header.uncompressed_size;
}

bool Z3DSReadIOFile::Resize(u64 size) {
    // Stubbed
    UNIMPLEMENTED();
    return false;
}

bool Z3DSReadIOFile::IsGood() const {
    return Child()->IsGood() && impl->m_good;
}

bool Z3DSReadIOFile::Open() {
    if (is_serializing) {
        return true;
    }
    // Stubbed
    UNIMPLEMENTED();
    return false;
}

std::size_t Z3DSReadIOFile::ReadImpl(void* data, std::size_t length, std::size_t data_size) {
    size_t res = impl->Read(data, length * data_size);
    return res == std::numeric_limits<size_t>::max() ? res : (res / data_size);
}

std::size_t Z3DSReadIOFile::ReadAtImpl(void* data, std::size_t byte_count, std::size_t offset) {
    return impl->ReadAt(data, byte_count, offset);
}

std::size_t Z3DSReadIOFile::WriteImpl(const void* data, std::size_t length, std::size_t data_size) {
    // Stubbed
    UNIMPLEMENTED();
    return 0;
}

bool Z3DSReadIOFile::Seek(s64 off, int origin) {
    if (is_serializing) {
        return true;
    }
    return impl->Seek(off, origin);
}

u64 Z3DSReadIOFile::Tell() const {
    return impl->uncompressed_pos;
}

std::unique_ptr<IOFileBase> Z3DSReadIOFile::OpenCopy() const {
    std::unique_ptr<Z3DSReadIOFile> ret = std::make_unique<Z3DSReadIOFile>();
    ret->Child() = std::move(Child()->OpenCopy());

    ret->impl = std::make_unique<Z3DSReadIOFileImpl>(ret->Child().get());

    return ret;
}

std::array<u8, 4> Z3DSReadIOFile::GetFileMagic() {
    return impl->header.underlying_magic;
}

const Z3DSMetadata& Z3DSReadIOFile::Metadata() {
    return impl->metadata;
}

template <class Archive>
void Z3DSReadIOFile::serialize(Archive& ar, const unsigned int) {
    is_serializing = true;
    ar& boost::serialization::base_object<IOFileBase>(*this);

    if (Archive::is_loading::value) {
        impl = std::make_unique<Z3DSReadIOFileImpl>(Child().get(), false);
    }
    ar & impl->uncompressed_pos;
    ar & impl->metadata;
    is_serializing = false;
}

} // namespace FileUtil

SERIALIZE_EXPORT_IMPL(FileUtil::NullIOFile);
SERIALIZE_EXPORT_IMPL(FileUtil::SubIOFile);
SERIALIZE_EXPORT_IMPL(FileUtil::CryptoIOFile);
SERIALIZE_EXPORT_IMPL(FileUtil::Z3DSReadIOFile);
SERIALIZE_EXPORT_IMPL(FileUtil::Z3DSWriteIOFile);