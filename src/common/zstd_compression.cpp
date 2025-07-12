// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <mutex>
#include <sstream>
#include <zstd.h>
#include <zstd/contrib/seekable_format/zstd_seekable.h>

#include <boost/serialization/base_object.hpp>
#include <boost/serialization/unique_ptr.hpp>
#include "common/alignment.h"
#include "common/archives.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/scm_rev.h"
#include "common/zstd_compression.h"

namespace Common::Compression {
std::vector<u8> CompressDataZSTD(std::span<const u8> source, s32 compression_level) {
    compression_level = std::clamp(compression_level, ZSTD_minCLevel(), ZSTD_maxCLevel());
    const std::size_t max_compressed_size = ZSTD_compressBound(source.size());

    if (ZSTD_isError(max_compressed_size)) {
        LOG_ERROR(Common, "Error determining ZSTD maximum compressed size: {} ({})",
                  ZSTD_getErrorName(max_compressed_size), max_compressed_size);
        return {};
    }

    std::vector<u8> compressed(max_compressed_size);
    const std::size_t compressed_size = ZSTD_compress(
        compressed.data(), compressed.size(), source.data(), source.size(), compression_level);

    if (ZSTD_isError(compressed_size)) {
        LOG_ERROR(Common, "Error compressing ZSTD data: {} ({})",
                  ZSTD_getErrorName(compressed_size), compressed_size);
        return {};
    }

    compressed.resize(compressed_size);
    return compressed;
}

std::vector<u8> CompressDataZSTDDefault(std::span<const u8> source) {
    return CompressDataZSTD(source, ZSTD_CLEVEL_DEFAULT);
}

std::vector<u8> DecompressDataZSTD(std::span<const u8> compressed) {
    const std::size_t decompressed_size =
        ZSTD_getFrameContentSize(compressed.data(), compressed.size());

    if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        LOG_ERROR(Common, "ZSTD decompressed size could not be determined.");
        return {};
    }
    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR || ZSTD_isError(decompressed_size)) {
        LOG_ERROR(Common, "Error determining ZSTD decompressed size: {} ({})",
                  ZSTD_getErrorName(decompressed_size), decompressed_size);
        return {};
    }

    std::vector<u8> decompressed(decompressed_size);
    const std::size_t uncompressed_result_size = ZSTD_decompress(
        decompressed.data(), decompressed.size(), compressed.data(), compressed.size());

    if (decompressed_size != uncompressed_result_size) {
        LOG_ERROR(Common, "ZSTD decompression expected {} bytes, got {}", decompressed_size,
                  uncompressed_result_size);
        return {};
    }
    if (ZSTD_isError(uncompressed_result_size)) {
        LOG_ERROR(Common, "Error decompressing ZSTD data: {} ({})",
                  ZSTD_getErrorName(uncompressed_result_size), uncompressed_result_size);
        return {};
    }

    return decompressed;
}

} // namespace Common::Compression

namespace FileUtil {

template <typename T>
void ReadFromIStream(std::istringstream& s, T* out, size_t out_size) {
    s.read(reinterpret_cast<char*>(out), out_size);
}

template <typename T>
void WriteToOStream(std::ostringstream& s, const T* out, size_t out_size) {
    s.write(reinterpret_cast<const char*>(out), out_size);
}

Z3DSMetadata::Z3DSMetadata(const std::span<u8>& source_data) {
    if (source_data.empty())
        return;
    std::string buf(reinterpret_cast<const char*>(source_data.data()), source_data.size());
    std::istringstream in(buf, std::ios::binary);

    u8 version;
    ReadFromIStream(in, &version, sizeof(version));

    if (version != METADATA_VERSION) {
        return;
    }

    while (!in.eof()) {
        Item item;
        ReadFromIStream(in, &item, sizeof(Item));
        // If end item is reached, stop processing
        if (item.type == Item::TYPE_END) {
            break;
        }
        // Only binary type supported for now
        if (item.type != Item::TYPE_BINARY) {
            continue;
        }
        std::string name(item.name_len, '\0');
        std::vector<u8> data(item.data_len);
        ReadFromIStream(in, name.data(), name.size());
        ReadFromIStream(in, data.data(), data.size());
        items.insert({std::move(name), std::move(data)});
    }
}

std::vector<u8> Z3DSMetadata::AsBinary() {
    if (items.empty())
        return {};
    std::ostringstream out;
    u8 version = METADATA_VERSION;
    WriteToOStream(out, &version, sizeof(u8));

    for (const auto& it : items) {
        Item item{
            .type = Item::TYPE_BINARY,
            .name_len = static_cast<u8>(std::min<size_t>(0xFF, it.first.size())),
            .data_len = static_cast<u16>(std::min<size_t>(0xFFFF, it.second.size())),
        };
        WriteToOStream(out, &item, sizeof(item));
        WriteToOStream(out, it.first.data(), item.name_len);
        WriteToOStream(out, it.second.data(), item.data_len);
    }

    // Write end item
    Item end{};
    WriteToOStream(out, &end, sizeof(end));

    std::string out_str = out.str();
    return std::vector<u8>(out_str.begin(), out_str.end());
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

    bool WriteHeader(IOFile* file) {
        file->Seek(0, SEEK_SET);
        return file->WriteBytes(&write_header, sizeof(write_header)) == sizeof(write_header);
    }

    bool WriteMetadata(IOFile* file, const std::span<u8>& data) {
        std::array<u8, 0x10> tmp_data{};
        size_t total_size = Common::AlignUp(data.size(), 0x10);
        write_header.metadata_size = static_cast<u32>(total_size);
        size_t res_written = file->WriteBytes(data.data(), data.size());
        res_written += file->WriteBytes(tmp_data.data(), total_size - data.size());
        return res_written == total_size;
    }

    size_t Write(IOFile* file, const void* data, std::size_t length) {
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

    bool Close(IOFile* file, size_t written_uncompressed) {
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

Z3DSWriteIOFile::Z3DSWriteIOFile()
    : IOFile(), file{std::make_unique<IOFile>()}, impl{std::make_unique<Z3DSWriteIOFileImpl>()} {}

Z3DSWriteIOFile::Z3DSWriteIOFile(std::unique_ptr<IOFile>&& underlying_file,
                                 const std::array<u8, 4>& underlying_magic, size_t frame_size)
    : IOFile(), file{std::move(underlying_file)},
      impl{std::make_unique<Z3DSWriteIOFileImpl>(frame_size)} {
    ASSERT_MSG(!file->IsCompressed(), "Underlying file is already compressed!");
    impl->write_header.underlying_magic = underlying_magic;
    impl->WriteHeader(file.get());

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
    impl->Close(file.get(), written_uncompressed);
    return file->Close();
}

u64 Z3DSWriteIOFile::GetSize() const {
    return written_uncompressed;
}

bool Z3DSWriteIOFile::Resize(u64 size) {
    // Stubbed
    UNIMPLEMENTED();
    return false;
}

bool Z3DSWriteIOFile::Flush() {
    return file->Flush();
}

void Z3DSWriteIOFile::Clear() {
    return file->Clear();
}

bool Z3DSWriteIOFile::IsCrypto() {
    return file->IsCrypto();
}

const std::string& Z3DSWriteIOFile::Filename() const {
    return file->Filename();
}

bool Z3DSWriteIOFile::IsOpen() const {
    return file->IsOpen();
}

bool Z3DSWriteIOFile::IsGood() const {
    return file->IsGood();
}

int Z3DSWriteIOFile::GetFd() const {
    return file->GetFd();
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

std::size_t Z3DSWriteIOFile::ReadAtImpl(void* data, std::size_t length, std::size_t data_size,
                                        std::size_t offset) {
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
            impl->WriteMetadata(file.get(), metadata_binary);
        }
    }

    size_t ret = impl->Write(file.get(), data, length * data_size);
    written_uncompressed += ret;
    return ret;
}

bool Z3DSWriteIOFile::SeekImpl(s64 off, int origin) {
    if (is_serializing) {
        return true;
    }
    // Stubbed
    UNIMPLEMENTED();
    return false;
}

u64 Z3DSWriteIOFile::TellImpl() const {
    return written_uncompressed;
}

size_t Z3DSWriteIOFile::GetNextWriteHint() {
    return impl->next_input_size_hint;
}

template <class Archive>
void Z3DSWriteIOFile::serialize(Archive& ar, const unsigned int) {
    is_serializing = true;
    ar& boost::serialization::base_object<IOFile>(*this);

    ar & file;
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
    Z3DSReadIOFileImpl(IOFile* file, bool load_metadata = true) {
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
    IOFile* curr_file = nullptr;
    std::mutex read_mutex;
    u64 uncompressed_pos = 0;
    Z3DSMetadata metadata;
};

std::optional<u32> Z3DSReadIOFile::GetUnderlyingFileMagic(IOFile* underlying_file) {
    Z3DSFileHeader header{};
    underlying_file->ReadAtBytes(&header, sizeof(header), 0);
    if (header.magic != Z3DSFileHeader::EXPECTED_MAGIC ||
        header.version != Z3DSFileHeader::EXPECTED_VERSION) {
        return std::nullopt;
    }

    return MakeMagic(header.underlying_magic[0], header.underlying_magic[1],
                     header.underlying_magic[2], header.underlying_magic[3]);
}

Z3DSReadIOFile::Z3DSReadIOFile()
    : IOFile(), file{std::make_unique<IOFile>()}, impl{std::make_unique<Z3DSReadIOFileImpl>()} {}

Z3DSReadIOFile::Z3DSReadIOFile(std::unique_ptr<IOFile>&& underlying_file)
    : IOFile(), file{std::move(underlying_file)},
      impl{std::make_unique<Z3DSReadIOFileImpl>(file.get())} {
    ASSERT_MSG(!file->IsCompressed(), "Underlying file is already compressed!");
}

Z3DSReadIOFile::~Z3DSReadIOFile() {
    this->Close();
}

bool Z3DSReadIOFile::Close() {
    impl->Close();
    return file->Close();
}

u64 Z3DSReadIOFile::GetSize() const {
    return impl->header.uncompressed_size;
}

bool Z3DSReadIOFile::Resize(u64 size) {
    // Stubbed
    UNIMPLEMENTED();
    return false;
}

bool Z3DSReadIOFile::Flush() {
    return file->Flush();
}

void Z3DSReadIOFile::Clear() {
    return file->Clear();
}

bool Z3DSReadIOFile::IsCrypto() {
    return file->IsCrypto();
}

const std::string& Z3DSReadIOFile::Filename() const {
    return file->Filename();
}

bool Z3DSReadIOFile::IsOpen() const {
    return file->IsOpen();
}

bool Z3DSReadIOFile::IsGood() const {
    return file->IsGood() && impl->m_good;
}

int Z3DSReadIOFile::GetFd() const {
    return file->GetFd();
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
    return impl->Read(data, length * data_size);
}

std::size_t Z3DSReadIOFile::ReadAtImpl(void* data, std::size_t length, std::size_t data_size,
                                       std::size_t offset) {
    return impl->ReadAt(data, length * data_size, offset);
}

std::size_t Z3DSReadIOFile::WriteImpl(const void* data, std::size_t length, std::size_t data_size) {
    // Stubbed
    UNIMPLEMENTED();
    return 0;
}

bool Z3DSReadIOFile::SeekImpl(s64 off, int origin) {
    if (is_serializing) {
        return true;
    }
    return impl->Seek(off, origin);
}

u64 Z3DSReadIOFile::TellImpl() const {
    return impl->uncompressed_pos;
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
    ar& boost::serialization::base_object<IOFile>(*this);

    ar & file;

    if (Archive::is_loading::value) {
        impl = std::make_unique<Z3DSReadIOFileImpl>(file.get(), false);
    }
    ar & impl->uncompressed_pos;
    ar & impl->metadata;
    is_serializing = false;
}

bool CompressZ3DSFile(const std::string& src_file_name, const std::string& dst_file_name,
                      const std::array<u8, 4>& underlying_magic, size_t frame_size,
                      std::function<ProgressCallback>&& update_callback) {

    IOFile in_file(src_file_name, "rb");
    if (!in_file.IsOpen()) {
        LOG_ERROR(Common_Filesystem, "Failed to open source file: {}", src_file_name);
        return false;
    }

    std::unique_ptr<IOFile> out_file = std::make_unique<IOFile>(dst_file_name, "wb");
    if (!out_file->IsOpen()) {
        LOG_ERROR(Common_Filesystem, "Failed to open destination file: {}", dst_file_name);
        return false;
    }

    if (Z3DSReadIOFile::GetUnderlyingFileMagic(&in_file) != std::nullopt) {
        LOG_ERROR(Common_Filesystem, "Source file is already compressed, nothing to do: {}",
                  src_file_name);
        return false;
    }

    Z3DSWriteIOFile out_compress_file(std::move(out_file), underlying_magic, frame_size);

    size_t next_chunk = out_compress_file.GetNextWriteHint();
    std::vector<u8> buffer(next_chunk);
    size_t in_size = in_file.GetSize();
    size_t written = 0;

    while (written != in_size) {
        size_t to_read = ((in_size - written) > next_chunk) ? next_chunk : (in_size - written);
        if (buffer.size() < to_read) {
            buffer.resize(to_read);
        }
        if (in_file.ReadBytes(buffer.data(), to_read) != to_read) {
            LOG_ERROR(Common_Filesystem, "Failed to read from source file");
            return false;
        }
        if (out_compress_file.WriteBytes(buffer.data(), to_read) != to_read) {
            LOG_ERROR(Common_Filesystem, "Failed to write to destination file");
        }
        written += to_read;
        next_chunk = out_compress_file.GetNextWriteHint();
        if (update_callback) {
            update_callback(written, in_size);
        }
    }
    LOG_INFO(Common_Filesystem, "File {} compressed successfully to {}", src_file_name,
             dst_file_name);
    return true;
}

bool DeCompressZ3DSFile(const std::string& src_file_name, const std::string& dst_file_name,
                        std::function<ProgressCallback>&& update_callback) {

    std::unique_ptr<IOFile> in_file = std::make_unique<IOFile>(src_file_name, "rb");
    if (!in_file->IsOpen()) {
        LOG_ERROR(Common_Filesystem, "Failed to open source file: {}", src_file_name);
        return false;
    }

    IOFile out_file(dst_file_name, "wb");
    if (!out_file.IsOpen()) {
        LOG_ERROR(Common_Filesystem, "Failed to open destination file: {}", dst_file_name);
        return false;
    }

    if (Z3DSReadIOFile::GetUnderlyingFileMagic(in_file.get()) == std::nullopt) {
        LOG_ERROR(Common_Filesystem,
                  "Source file is not compressed or is invalid, nothing to do: {}", src_file_name);
        return false;
    }

    Z3DSReadIOFile in_compress_file(std::move(in_file));
    size_t next_chunk = 64 * 1024 * 1024;
    std::vector<u8> buffer(next_chunk);
    size_t in_size = in_compress_file.GetSize();
    size_t written = 0;

    while (written != in_size) {
        size_t to_read = (in_size - written) > next_chunk ? next_chunk : (in_size - written);
        if (buffer.size() < to_read) {
            buffer.resize(to_read);
        }
        if (in_compress_file.ReadBytes(buffer.data(), to_read) != to_read) {
            LOG_ERROR(Common_Filesystem, "Failed to read from source file");
            return false;
        }
        if (out_file.WriteBytes(buffer.data(), to_read) != to_read) {
            LOG_ERROR(Common_Filesystem, "Failed to write to destination file");
        }
        written += to_read;
        if (update_callback) {
            update_callback(written, in_size);
        }
    }
    LOG_INFO(Common_Filesystem, "File {} decompressed successfully to {}", src_file_name,
             dst_file_name);
    return true;
}
} // namespace FileUtil

SERIALIZE_EXPORT_IMPL(FileUtil::Z3DSReadIOFile);
SERIALIZE_EXPORT_IMPL(FileUtil::Z3DSWriteIOFile);
