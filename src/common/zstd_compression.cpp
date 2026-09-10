// Copyright 2020-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <chrono>
#include <ctime>
#include <mutex>
#include <sstream>
#include <zstd.h>
#include <zstd_seekable.h>

#include <boost/serialization/base_object.hpp>
#include <boost/serialization/unique_ptr.hpp>
#include "common/alignment.h"
#include "common/archives.h"
#include "common/assert.h"
#include "common/file_derived.h"
#include "common/logging/log.h"
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

std::size_t GetDecompressedSize(std::span<const u8> compressed) {
    return ZSTD_getFrameContentSize(compressed.data(), compressed.size());
}

std::vector<u8> DecompressDataZSTD(std::span<const u8> compressed) {
    const std::size_t decompressed_size = GetDecompressedSize(compressed);

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
            in.ignore(static_cast<std::streamsize>(item.name_len) + item.data_len);
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

bool CompressZ3DSFile(const std::string& src_file_name, const std::string& dst_file_name,
                      const std::array<u8, 4>& underlying_magic, size_t frame_size,
                      std::function<ProgressCallback>&& update_callback,
                      std::unordered_map<std::string, std::vector<u8>> metadata) {

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

    for (auto& it : metadata) {
        std::string val_str(it.second.size(), '\0');
        memcpy(val_str.data(), it.second.data(), val_str.size());
        out_compress_file.Metadata().Add(it.first, val_str);
    }

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
