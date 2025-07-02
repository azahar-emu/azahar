// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Copyright 2019 yuzu Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <span>
#include <unordered_map>
#include <vector>

#include <boost/serialization/array.hpp>
#include <boost/serialization/unordered_map.hpp>
#include "common/archives.h"
#include "common/common_types.h"
#include "common/file_util.h"

namespace Common::Compression {

/**
 * Compresses a source memory region with Zstandard and returns the compressed data in a vector.
 *
 * @param source the uncompressed source memory region.
 * @param compression_level the used compression level. Should be between 1 and 22.
 *
 * @return the compressed data.
 */
[[nodiscard]] std::vector<u8> CompressDataZSTD(std::span<const u8> source, s32 compression_level);

/**
 * Compresses a source memory region with Zstandard with the default compression level and returns
 * the compressed data in a vector.
 *
 * @param source the uncompressed source memory region.
 *
 * @return the compressed data.
 */
[[nodiscard]] std::vector<u8> CompressDataZSTDDefault(std::span<const u8> source);

/**
 * Decompresses a source memory region with Zstandard and returns the uncompressed data in a vector.
 *
 * @param compressed the compressed source memory region.
 *
 * @return the decompressed data.
 */
[[nodiscard]] std::vector<u8> DecompressDataZSTD(std::span<const u8> compressed);

} // namespace Common::Compression

namespace FileUtil {

struct Z3DSFileHeader {
    static constexpr std::array<u8, 4> EXPECTED_MAGIC = {'Z', '3', 'D', 'S'};
    static constexpr u16 EXPECTED_VERSION = 1;

    std::array<u8, 4> magic = EXPECTED_MAGIC;
    std::array<u8, 4> underlying_magic{};
    u16 version = EXPECTED_VERSION;
    u16 header_size = 0;
    u32 metadata_size = 0;
    u64 compressed_size = 0;
    u64 uncompressed_size = 0;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar & magic;
        ar & underlying_magic;
        ar & version;
        ar & header_size;
        ar & metadata_size;
        ar & compressed_size;
        ar & uncompressed_size;
    }
};
static_assert(sizeof(Z3DSFileHeader) == 0x20, "Invalid Z3DSFileHeader size");

class Z3DSMetadata {
public:
    static constexpr u8 METADATA_VERSION = 1;
    Z3DSMetadata() {}

    Z3DSMetadata(const std::span<u8>& source_data);

    void Add(const std::string& name, const std::span<u8>& data) {
        items.insert({name, std::vector<u8>(data.begin(), data.end())});
    }

    void Add(const std::string& name, const std::string& data) {
        items.insert({name, std::vector<u8>(data.begin(), data.end())});
    }

    std::optional<std::vector<u8>> Get(const std::string& name) const {
        auto it = items.find(name);
        if (it == items.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    std::vector<u8> AsBinary();

private:
    struct Item {
        enum Type : u8 {
            TYPE_END = 0,
            TYPE_BINARY = 1,
        };
        Type type{};
        u8 name_len{};
        u16 data_len{};
    };
    static_assert(sizeof(Item) == 4);

    std::unordered_map<std::string, std::vector<u8>> items;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar & items;
    }
    friend class boost::serialization::access;
};

class Z3DSWriteIOFile : public IOFile {
public:
    static constexpr size_t DEFAULT_FRAME_SIZE = 256 * 1024; // 256KiB
    static constexpr size_t MAX_FRAME_SIZE = 0;              // Let the lib decide, usually 1GiB

    Z3DSWriteIOFile();

    Z3DSWriteIOFile(std::unique_ptr<IOFile>&& underlying_file,
                    const std::array<u8, 4>& underlying_magic, size_t frame_size);

    ~Z3DSWriteIOFile();

    bool Close() override;

    u64 GetSize() const override;

    bool Resize(u64 size) override;

    bool Flush() override;

    void Clear() override;

    bool IsCrypto() override;

    bool IsCompressed() override {
        return true;
    }

    const std::string& Filename() const override;

    bool IsOpen() const override;

    bool IsGood() const override;

    int GetFd() const override;

    Z3DSMetadata& Metadata() {
        return metadata;
    }

    size_t GetNextWriteHint();

private:
    struct Z3DSWriteIOFileImpl;
    bool Open() override;

    std::size_t ReadImpl(void* data, std::size_t length, std::size_t data_size) override;
    std::size_t ReadAtImpl(void* data, std::size_t length, std::size_t data_size,
                           std::size_t offset) override;
    std::size_t WriteImpl(const void* data, std::size_t length, std::size_t data_size) override;

    bool SeekImpl(s64 off, int origin) override;
    u64 TellImpl() const override;

    std::unique_ptr<IOFile> file;
    std::unique_ptr<Z3DSWriteIOFileImpl> impl;
    u64 written_uncompressed = 0;
    bool metadata_written = false;
    Z3DSMetadata metadata;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int);
    friend class boost::serialization::access;
    bool is_serializing = false;
};

class Z3DSReadIOFile : public IOFile {
public:
    static std::optional<u32> GetUnderlyingFileMagic(IOFile* underlying_file);

    Z3DSReadIOFile();

    Z3DSReadIOFile(std::unique_ptr<IOFile>&& underlying_file);

    ~Z3DSReadIOFile();

    bool Close() override;

    u64 GetSize() const override;

    bool Resize(u64 size) override;

    bool Flush() override;

    void Clear() override;

    bool IsCrypto() override;

    bool IsCompressed() override {
        return true;
    }

    const std::string& Filename() const override;

    bool IsOpen() const override;

    bool IsGood() const override;

    int GetFd() const override;

    std::array<u8, 4> GetFileMagic();

    const Z3DSMetadata& Metadata();

private:
    struct Z3DSReadIOFileImpl;

    static constexpr u32 MakeMagic(char a, char b, char c, char d) {
        return a | b << 8 | c << 16 | d << 24;
    }

    bool Open() override;

    std::size_t ReadImpl(void* data, std::size_t length, std::size_t data_size) override;
    std::size_t ReadAtImpl(void* data, std::size_t length, std::size_t data_size,
                           std::size_t offset) override;
    std::size_t WriteImpl(const void* data, std::size_t length, std::size_t data_size) override;

    bool SeekImpl(s64 off, int origin) override;
    u64 TellImpl() const override;

    std::unique_ptr<IOFile> file;
    std::unique_ptr<Z3DSReadIOFileImpl> impl;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int);
    friend class boost::serialization::access;
    bool is_serializing = false;
};

using ProgressCallback = void(std::size_t, std::size_t);

bool CompressZ3DSFile(const std::string& src_file, const std::string& dst_file,
                      const std::array<u8, 4>& underlying_magic, size_t frame_size,
                      std::function<ProgressCallback>&& update_callback = nullptr);

bool DeCompressZ3DSFile(const std::string& src_file, const std::string& dst_file,
                        std::function<ProgressCallback>&& update_callback = nullptr);

} // namespace FileUtil

BOOST_CLASS_EXPORT_KEY(FileUtil::Z3DSWriteIOFile)
BOOST_CLASS_EXPORT_KEY(FileUtil::Z3DSReadIOFile)