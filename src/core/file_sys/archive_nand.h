// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>
#include <boost/serialization/base_object.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/string.hpp>
#include "core/file_sys/archive_backend.h"
#include "core/hle/result.h"

namespace FileSys {

enum class NANDArchiveType : u32 {
    RW,   ///< Access to Read Write (rw) directory
    RO,   ///< Access to Read Only (ro) directory
    RO_W, ///< Access to Read Only (ro) directory with write permissions
};

/// Archive backend for SDMC archive
class NANDArchive : public ArchiveBackend {
public:
    explicit NANDArchive(const std::string& mount_point_, NANDArchiveType archive_type)
        : mount_point(mount_point_) {}

    std::string GetName() const override {
        return "NANDArchive: " + mount_point;
    }

    ResultVal<std::unique_ptr<FileBackend>> OpenFile(const Path& path, const Mode& mode,
                                                     u32 attributes) override;
    Result DeleteFile(const Path& path) const override;
    Result RenameFile(const Path& src_path, const Path& dest_path) const override;
    Result DeleteDirectory(const Path& path) const override;
    Result DeleteDirectoryRecursively(const Path& path) const override;
    Result CreateFile(const Path& path, u64 size, u32 attributes) const override;
    Result CreateDirectory(const Path& path, u32 attributes) const override;
    Result RenameDirectory(const Path& src_path, const Path& dest_path) const override;
    ResultVal<std::unique_ptr<DirectoryBackend>> OpenDirectory(const Path& path) override;
    u64 GetFreeBytes() const override;

protected:
    std::string mount_point{};
    NANDArchiveType archive_type{};

    NANDArchive() = default;
    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar& boost::serialization::base_object<ArchiveBackend>(*this);
        ar & mount_point;
        ar & archive_type;
    }
    friend class boost::serialization::access;

private:
    bool AllowsWrite() const {
        return archive_type != NANDArchiveType::RO;
    }
};

/// File system interface to the NAND archive
class ArchiveFactory_NAND final : public ArchiveFactory {
public:
    explicit ArchiveFactory_NAND(const std::string& mount_point, NANDArchiveType type);

    /**
     * Initialize the archive.
     * @return true if it initialized successfully
     */
    bool Initialize();

    std::string GetPath();

    std::string GetName() const override {
        switch (archive_type) {
        case NANDArchiveType::RW:
            return "NAND RW";
        case NANDArchiveType::RO:
            return "NAND RO";
        case NANDArchiveType::RO_W:
            return "NAND RO W";
        default:
            break;
        }

        UNIMPLEMENTED();
        return "";
    }

    ResultVal<std::unique_ptr<ArchiveBackend>> Open(const Path& path, u64 program_id) override;
    Result Format(const Path& path, const FileSys::ArchiveFormatInfo& format_info, u64 program_id,
                  u32 directory_buckets, u32 file_buckets) override;
    ResultVal<ArchiveFormatInfo> GetFormatInfo(const Path& path, u64 program_id) const override;

private:
    std::string nand_directory;
    NANDArchiveType archive_type;

    ArchiveFactory_NAND() = default;
    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar& boost::serialization::base_object<ArchiveFactory>(*this);
        ar & nand_directory;
        ar & archive_type;
    }
    friend class boost::serialization::access;
};

} // namespace FileSys

BOOST_CLASS_EXPORT_KEY(FileSys::NANDArchive)
BOOST_CLASS_EXPORT_KEY(FileSys::ArchiveFactory_NAND)
