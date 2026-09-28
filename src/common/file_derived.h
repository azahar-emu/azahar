// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <boost/serialization/base_object.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/unique_ptr.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/wrapper.hpp>
#include "common/common_types.h"
#include "common/file_util.h"
#include "common/zstd_compression.h"

namespace FileUtil {

struct CryptoIOFileImpl;

class NullIOFile : public IOFileBase {
public:
    NullIOFile() = default;
    ~NullIOFile() = default;
    bool Open() override {
        return false;
    }
    std::unique_ptr<IOFileBase> OpenCopy() const {
        return std::make_unique<NullIOFile>();
    }
    bool Close() override {
        return false;
    }
    bool IsOpen() const override {
        return false;
    }
    bool IsGood() const override {
        return false;
    }
    int GetFd() const override {
        return 0;
    }
    u64 GetSize() const override {
        return 0;
    }
    bool Resize(u64 size) override {
        return false;
    }
    bool Flush() override {
        return false;
    }
    void Clear() override {}
    const std::string& Filename() const override {
        return empty;
    }
    bool Seek(s64 off, int origin) override {
        return false;
    }
    u64 Tell() const override {
        return 0;
    }

protected:
    std::size_t ReadImpl(void* data, std::size_t length, std::size_t elem_size) override {
        return 0;
    }
    std::size_t ReadAtImpl(void* data, std::size_t byte_count, std::size_t offset) override {
        return 0;
    }
    std::size_t WriteImpl(const void* data, std::size_t length, std::size_t elem_size) override {
        return 0;
    }
    IOType::Type MyType() const override {
        return IOType::Type::NullIOFile;
    }

private:
    static inline std::string empty{};
    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar& boost::serialization::base_object<IOFileBase>(*this);
    }
    friend class boost::serialization::access;
};

// File that exposes a fragment [sub_file_offset, sub_file_offset + sub_file_size) of
// the underlying file. The file can only be opened in replace or read modes, without
// create, truncate or append. Resizing is only possible if new size is smaller.
class SubIOFile final : public IOFileBase {
public:
    SubIOFile();

    // Opens filename as an IOFile child and exposes the fragment
    // [sub_file_offset, sub_file_offset + sub_file_size) of it.
    SubIOFile(const std::string& filename, const char openmode[], std::size_t sub_file_offset,
              std::size_t sub_file_size, int flags = 0);

    // Uses an already opened child file and exposes the fragment
    // [sub_file_offset, sub_file_offset + sub_file_size) of it.
    SubIOFile(std::unique_ptr<IOFileBase>&& child_file, std::size_t sub_file_offset,
              std::size_t sub_file_size);

    ~SubIOFile() override;

    SubIOFile(SubIOFile&& other) noexcept;
    SubIOFile& operator=(SubIOFile&& other) noexcept;

    [[nodiscard]] bool IsGood() const override;
    bool Seek(s64 off, int origin) override;
    u64 Tell() const override;
    u64 GetSize() const override;
    bool Resize(u64 size) override;
    void Clear() override;

    std::unique_ptr<IOFileBase> OpenCopy() const override;

protected:
    bool Open() override;
    std::size_t ReadImpl(void* data, std::size_t length, std::size_t elem_size) override;
    std::size_t ReadAtImpl(void* data, std::size_t byte_count, std::size_t offset) override;
    std::size_t WriteImpl(const void* data, std::size_t length, std::size_t elem_size) override;
    IOType::Type MyType() const override {
        return IOType::Type::SubIOFile;
    }

private:
    static bool IsReplaceOpenMode(const char* openmode);
    [[nodiscard]] std::size_t BytesRemaining() const;
    void Swap(SubIOFile& other) noexcept;

    std::size_t m_sub_file_offset = 0;   // Offset of the fragment inside the child file.
    std::size_t m_sub_file_capacity = 0; // Original (maximum) size given at construction.
    std::size_t m_sub_file_size = 0;     // Current logical size (<= capacity).
    bool m_good = true;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar& boost::serialization::base_object<IOFileBase>(*this);
        ar & m_sub_file_offset;
        ar & m_sub_file_capacity;
        ar & m_sub_file_size;
    }
    friend class boost::serialization::access;
};

class CryptoIOFile : public IOFileBase {
public:
    static bool IsCryptoIOFile(IOFileBase* underlying_file, const std::vector<u8>& aes_key,
                               const std::vector<u8>& aes_iv);
    CryptoIOFile();

    // flags is used for windows specific file open mode flags, which
    // allows citra to open the logs in shared write mode, so that the file
    // isn't considered "locked" while citra is open and people can open the log file and view it
    CryptoIOFile(const std::string& filename, const char openmode[], const std::vector<u8>& aes_key,
                 const std::vector<u8>& aes_iv, int flags = 0);

    CryptoIOFile(std::unique_ptr<IOFileBase> underlying_file, const char _openmode[],
                 const std::vector<u8>& aes_key, const std::vector<u8>& aes_iv);

    ~CryptoIOFile() override;

    IOType::Type MyType() const override {
        return IOType::Type::CryptoFile;
    }

    bool Seek(s64 off, int origin) override;
    u64 Tell() const override;
    u64 GetSize() const override;
    bool Resize(u64 size) override;

    std::unique_ptr<IOFileBase> OpenCopy() const override;

private:
    bool Open() override;

    friend struct CryptoIOFileImpl;

    struct CryptoIOFileHeader {
        u32 magic{};
        u16 version{};
        u16 header_size{};
        u64 reserved{};
        std::array<u8, 0x20> key_nonce_hash{};

        bool IsValid(std::span<const u8> key, std::span<const u8> iv, bool check_hash);

        template <class Archive>
        void serialize(Archive& ar, const unsigned int) {
            ar & magic;
            ar & version;
            ar & header_size;
            ar & reserved;
        }
        friend class boost::serialization::access;
    };
    static_assert(sizeof(CryptoIOFileHeader) == 0x30, "Invalid CryptoIOFileHeader size");

    std::unique_ptr<CryptoIOFileImpl> impl;
    std::string openmode;

protected:
    std::size_t ReadImpl(void* data, std::size_t length, std::size_t elem_size) override;
    std::size_t ReadAtImpl(void* data, std::size_t byte_count, std::size_t offset) override;
    std::size_t WriteImpl(const void* data, std::size_t length, std::size_t elem_size) override;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int);
    friend class boost::serialization::access;
};

struct Z3DSFileHeader {
    static constexpr std::array<u8, 4> EXPECTED_MAGIC = {'Z', '3', 'D', 'S'};
    static constexpr u8 EXPECTED_VERSION = 1;

    std::array<u8, 4> magic = EXPECTED_MAGIC;
    std::array<u8, 4> underlying_magic{};
    u8 version = EXPECTED_VERSION;
    u8 reserved = 0;
    u16 header_size = 0;
    u32 metadata_size = 0;
    u64 compressed_size = 0;
    u64 uncompressed_size = 0;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar & magic;
        ar & underlying_magic;
        ar & version;
        ar & reserved;
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

class Z3DSWriteIOFile : public IOFileBase {
public:
    static constexpr size_t DEFAULT_FRAME_SIZE = 256 * 1024;           // 256KiB
    static constexpr size_t DEFAULT_CIA_FRAME_SIZE = 32 * 1024 * 1024; // 32MiB
    static constexpr size_t MAX_FRAME_SIZE = 0; // Let the lib decide, usually 1GiB

    Z3DSWriteIOFile();

    Z3DSWriteIOFile(std::unique_ptr<IOFileBase>&& underlying_file,
                    const std::array<u8, 4>& underlying_magic, size_t frame_size);

    ~Z3DSWriteIOFile();

    bool Close() override;

    u64 GetSize() const override;

    bool Resize(u64 size) override;

    Z3DSMetadata& Metadata() {
        return metadata;
    }

    size_t GetNextWriteHint();

    bool Seek(s64 off, int origin) override;
    u64 Tell() const override;

    std::unique_ptr<IOFileBase> OpenCopy() const override;

private:
    struct Z3DSWriteIOFileImpl;
    bool Open() override;

    std::size_t ReadImpl(void* data, std::size_t length, std::size_t data_size) override;
    std::size_t ReadAtImpl(void* data, std::size_t byte_count, std::size_t offset) override;
    std::size_t WriteImpl(const void* data, std::size_t length, std::size_t data_size) override;

    IOType::Type MyType() const override {
        return IOType::Type::Z3DSWriteIOFile;
    }

    std::unique_ptr<Z3DSWriteIOFileImpl> impl;
    u64 written_uncompressed = 0;
    bool metadata_written = false;
    Z3DSMetadata metadata;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int);
    friend class boost::serialization::access;
    bool is_serializing = false;
};

class Z3DSReadIOFile : public IOFileBase {
public:
    static bool IsZ3DSIOFile(IOFileBase* underlying_file);
    static std::optional<u32> GetUnderlyingFileMagic(IOFileBase* underlying_file);

    Z3DSReadIOFile();

    Z3DSReadIOFile(std::unique_ptr<IOFileBase>&& underlying_file);

    ~Z3DSReadIOFile();

    bool Close() override;

    u64 GetSize() const override;

    bool Resize(u64 size) override;

    bool IsGood() const override;

    std::array<u8, 4> GetFileMagic();

    const Z3DSMetadata& Metadata();

    bool Seek(s64 off, int origin) override;
    u64 Tell() const override;

    std::unique_ptr<IOFileBase> OpenCopy() const override;

private:
    struct Z3DSReadIOFileImpl;

    bool Open() override;

    std::size_t ReadImpl(void* data, std::size_t length, std::size_t data_size) override;
    std::size_t ReadAtImpl(void* data, std::size_t byte_count, std::size_t offset) override;
    std::size_t WriteImpl(const void* data, std::size_t length, std::size_t data_size) override;

    IOType::Type MyType() const override {
        return IOType::Type::Z3DSReadIOFile;
    }

    std::unique_ptr<Z3DSReadIOFileImpl> impl;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int);
    friend class boost::serialization::access;
    bool is_serializing = false;
};
} // namespace FileUtil

BOOST_CLASS_EXPORT_KEY(FileUtil::NullIOFile)
BOOST_CLASS_EXPORT_KEY(FileUtil::SubIOFile)
BOOST_CLASS_EXPORT_KEY(FileUtil::CryptoIOFile)
BOOST_CLASS_EXPORT_KEY(FileUtil::Z3DSWriteIOFile)
BOOST_CLASS_EXPORT_KEY(FileUtil::Z3DSReadIOFile)