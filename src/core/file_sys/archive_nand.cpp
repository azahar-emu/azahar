// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <memory>
#include "common/archives.h"
#include "common/common_paths.h"
#include "common/error.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/file_sys/archive_nand.h"
#include "core/file_sys/disk_archive.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/path_parser.h"

SERIALIZE_EXPORT_IMPL(FileSys::NANDArchive)
SERIALIZE_EXPORT_IMPL(FileSys::ArchiveFactory_NAND)

namespace FileSys {

// TODO(PabloMK7): This code is very similar to the SMDC archive code. Maybe we should look
// into unifying everything in a FAT-like archive, as both the SMDC and NAND archives
// seem to behave the same way.

ResultVal<std::unique_ptr<FileBackend>> NANDArchive::OpenFile(const Path& path, const Mode& mode,
                                                              u32 attributes) {
    LOG_DEBUG(Service_FS, "called path={} mode={:01X}", path.DebugStr(), mode.hex);

    if (!AllowsWrite() && mode != Mode::ReadOnly()) {
        return ResultInvalidOpenFlags;
    }

    const PathParser path_parser(path);

    if (!path_parser.IsValid()) {
        LOG_ERROR(Service_FS, "Invalid path {}", path.DebugStr());
        return ResultInvalidPath;
    }

    if (mode.hex == 0) {
        LOG_ERROR(Service_FS, "Empty open mode");
        return ResultInvalidOpenFlags;
    }

    if (mode.create_flag && !mode.write_flag) {
        LOG_ERROR(Service_FS, "Create flag set but write flag not set");
        return ResultInvalidOpenFlags;
    }

    const auto full_path = path_parser.BuildHostPath(mount_point);

    switch (path_parser.GetHostStatus(mount_point)) {
    case PathParser::InvalidMountPoint:
        LOG_CRITICAL(Service_FS, "(unreachable) Invalid mount point {}", mount_point);
        return ResultNotFound;
    case PathParser::PathNotFound:
    case PathParser::FileInPath:
        LOG_DEBUG(Service_FS, "Path not found {}", full_path);
        return ResultNotFound;
    case PathParser::DirectoryFound:
        LOG_DEBUG(Service_FS, "{} is not a file", full_path);
        return ResultUnexpectedFileOrDirectorySdmc;
    case PathParser::NotFound:
        if (!mode.create_flag) {
            LOG_DEBUG(Service_FS, "Non-existing file {} can't be open without mode create.",
                      full_path);
            return ResultNotFound;
        } else {
            // Create the file
            FileUtil::CreateEmptyFile(full_path);
        }
        break;
    case PathParser::FileFound:
        break; // Expected 'success' case
    }

    FileUtil::IOFile file(full_path, mode.write_flag ? "r+b" : "rb");
    if (!file.IsOpen()) {
        LOG_CRITICAL(Service_FS, "Error opening {}: {}", full_path, Common::GetLastErrorMsg());
        return ResultNotFound;
    }

    return std::make_unique<DiskFile>(std::move(file), mode, nullptr);
}

Result NANDArchive::DeleteFile(const Path& path) const {

    if (!AllowsWrite()) {
        return ResultInvalidOpenFlags;
    }

    const PathParser path_parser(path);

    if (!path_parser.IsValid()) {
        LOG_ERROR(Service_FS, "Invalid path {}", path.DebugStr());
        return ResultInvalidPath;
    }

    const auto full_path = path_parser.BuildHostPath(mount_point);

    switch (path_parser.GetHostStatus(mount_point)) {
    case PathParser::InvalidMountPoint:
        LOG_CRITICAL(Service_FS, "(unreachable) Invalid mount point {}", mount_point);
        return ResultNotFound;
    case PathParser::PathNotFound:
    case PathParser::FileInPath:
    case PathParser::NotFound:
        LOG_DEBUG(Service_FS, "{} not found", full_path);
        return ResultNotFound;
    case PathParser::DirectoryFound:
        LOG_ERROR(Service_FS, "{} is not a file", full_path);
        return ResultUnexpectedFileOrDirectorySdmc;
    case PathParser::FileFound:
        break; // Expected 'success' case
    }

    if (FileUtil::Delete(full_path)) {
        return ResultSuccess;
    }

    LOG_CRITICAL(Service_FS, "(unreachable) Unknown error deleting {}", full_path);
    return ResultNotFound;
}

Result NANDArchive::RenameFile(const Path& src_path, const Path& dest_path) const {

    if (!AllowsWrite()) {
        return ResultInvalidOpenFlags;
    }

    const PathParser path_parser_src(src_path);

    // TODO: Verify these return codes with HW
    if (!path_parser_src.IsValid()) {
        LOG_ERROR(Service_FS, "Invalid src path {}", src_path.DebugStr());
        return ResultInvalidPath;
    }

    const PathParser path_parser_dest(dest_path);

    if (!path_parser_dest.IsValid()) {
        LOG_ERROR(Service_FS, "Invalid dest path {}", dest_path.DebugStr());
        return ResultInvalidPath;
    }

    const auto src_path_full = path_parser_src.BuildHostPath(mount_point);
    const auto dest_path_full = path_parser_dest.BuildHostPath(mount_point);

    if (FileUtil::Rename(src_path_full, dest_path_full)) {
        return ResultSuccess;
    }

    // TODO(yuriks): This code probably isn't right, it'll return a Status even if the file didn't
    // exist or similar. Verify.
    return Result(ErrorDescription::NoData, ErrorModule::FS, // TODO: verify description
                  ErrorSummary::NothingHappened, ErrorLevel::Status);
}

template <typename T>
static Result DeleteDirectoryHelper(const Path& path, const std::string& mount_point, T deleter) {
    const PathParser path_parser(path);

    if (!path_parser.IsValid()) {
        LOG_ERROR(Service_FS, "Invalid path {}", path.DebugStr());
        return ResultInvalidPath;
    }

    if (path_parser.IsRootDirectory())
        return ResultInvalidOpenFlags;

    const auto full_path = path_parser.BuildHostPath(mount_point);

    switch (path_parser.GetHostStatus(mount_point)) {
    case PathParser::InvalidMountPoint:
        LOG_CRITICAL(Service_FS, "(unreachable) Invalid mount point {}", mount_point);
        return ResultNotFound;
    case PathParser::PathNotFound:
    case PathParser::NotFound:
        LOG_ERROR(Service_FS, "Path not found {}", full_path);
        return ResultNotFound;
    case PathParser::FileInPath:
    case PathParser::FileFound:
        LOG_ERROR(Service_FS, "Unexpected file in path {}", full_path);
        return ResultUnexpectedFileOrDirectorySdmc;
    case PathParser::DirectoryFound:
        break; // Expected 'success' case
    }

    if (deleter(full_path)) {
        return ResultSuccess;
    }

    LOG_ERROR(Service_FS, "Directory not empty {}", full_path);
    return ResultUnexpectedFileOrDirectorySdmc;
}

Result NANDArchive::DeleteDirectory(const Path& path) const {
    if (!AllowsWrite()) {
        return ResultInvalidOpenFlags;
    }

    return DeleteDirectoryHelper(path, mount_point, FileUtil::DeleteDir);
}

Result NANDArchive::DeleteDirectoryRecursively(const Path& path) const {
    if (!AllowsWrite()) {
        return ResultInvalidOpenFlags;
    }

    return DeleteDirectoryHelper(
        path, mount_point, [](const std::string& p) { return FileUtil::DeleteDirRecursively(p); });
}

Result NANDArchive::CreateFile(const FileSys::Path& path, u64 size, u32 attributes) const {
    if (!AllowsWrite()) {
        return ResultInvalidOpenFlags;
    }

    const PathParser path_parser(path);

    if (!path_parser.IsValid()) {
        LOG_ERROR(Service_FS, "Invalid path {}", path.DebugStr());
        return ResultInvalidPath;
    }

    const auto full_path = path_parser.BuildHostPath(mount_point);

    switch (path_parser.GetHostStatus(mount_point)) {
    case PathParser::InvalidMountPoint:
        LOG_CRITICAL(Service_FS, "(unreachable) Invalid mount point {}", mount_point);
        return ResultNotFound;
    case PathParser::PathNotFound:
    case PathParser::FileInPath:
        LOG_ERROR(Service_FS, "Path not found {}", full_path);
        return ResultNotFound;
    case PathParser::DirectoryFound:
        LOG_ERROR(Service_FS, "{} already exists", full_path);
        return ResultUnexpectedFileOrDirectorySdmc;
    case PathParser::FileFound:
        LOG_ERROR(Service_FS, "{} already exists", full_path);
        return ResultAlreadyExists;
    case PathParser::NotFound:
        break; // Expected 'success' case
    }

    if (size == 0) {
        FileUtil::CreateEmptyFile(full_path);
        return ResultSuccess;
    }

    FileUtil::IOFile file(full_path, "wb");
    // Creates a sparse file (or a normal file on filesystems without the concept of sparse files)
    // We do this by seeking to the right size, then writing a single null byte.
    if (file.Seek(size - 1, SEEK_SET) && file.WriteBytes("", 1) == 1) {
        return ResultSuccess;
    }

    LOG_ERROR(Service_FS, "Too large file");
    return Result(ErrorDescription::TooLarge, ErrorModule::FS, ErrorSummary::OutOfResource,
                  ErrorLevel::Info);
}

Result NANDArchive::CreateDirectory(const Path& path, u32 attributes) const {
    if (!AllowsWrite()) {
        return ResultInvalidOpenFlags;
    }

    const PathParser path_parser(path);

    if (!path_parser.IsValid()) {
        LOG_ERROR(Service_FS, "Invalid path {}", path.DebugStr());
        return ResultInvalidPath;
    }

    const auto full_path = path_parser.BuildHostPath(mount_point);

    switch (path_parser.GetHostStatus(mount_point)) {
    case PathParser::InvalidMountPoint:
        LOG_CRITICAL(Service_FS, "(unreachable) Invalid mount point {}", mount_point);
        return ResultNotFound;
    case PathParser::PathNotFound:
    case PathParser::FileInPath:
        LOG_ERROR(Service_FS, "Path not found {}", full_path);
        return ResultNotFound;
    case PathParser::DirectoryFound:
    case PathParser::FileFound:
        LOG_DEBUG(Service_FS, "{} already exists", full_path);
        return ResultAlreadyExists;
    case PathParser::NotFound:
        break; // Expected 'success' case
    }

    if (FileUtil::CreateDir(mount_point + path.AsString())) {
        return ResultSuccess;
    }

    LOG_CRITICAL(Service_FS, "(unreachable) Unknown error creating {}", mount_point);
    return Result(ErrorDescription::NoData, ErrorModule::FS, ErrorSummary::Canceled,
                  ErrorLevel::Status);
}

Result NANDArchive::RenameDirectory(const Path& src_path, const Path& dest_path) const {
    if (!AllowsWrite()) {
        return ResultInvalidOpenFlags;
    }

    const PathParser path_parser_src(src_path);

    // TODO: Verify these return codes with HW
    if (!path_parser_src.IsValid()) {
        LOG_ERROR(Service_FS, "Invalid src path {}", src_path.DebugStr());
        return ResultInvalidPath;
    }

    const PathParser path_parser_dest(dest_path);

    if (!path_parser_dest.IsValid()) {
        LOG_ERROR(Service_FS, "Invalid dest path {}", dest_path.DebugStr());
        return ResultInvalidPath;
    }

    const auto src_path_full = path_parser_src.BuildHostPath(mount_point);
    const auto dest_path_full = path_parser_dest.BuildHostPath(mount_point);

    if (FileUtil::Rename(src_path_full, dest_path_full)) {
        return ResultSuccess;
    }

    // TODO(yuriks): This code probably isn't right, it'll return a Status even if the file didn't
    // exist or similar. Verify.
    return Result(ErrorDescription::NoData, ErrorModule::FS, // TODO: verify description
                  ErrorSummary::NothingHappened, ErrorLevel::Status);
}

ResultVal<std::unique_ptr<DirectoryBackend>> NANDArchive::OpenDirectory(const Path& path) {
    const PathParser path_parser(path);

    if (!path_parser.IsValid()) {
        LOG_ERROR(Service_FS, "Invalid path {}", path.DebugStr());
        return ResultInvalidPath;
    }

    const auto full_path = path_parser.BuildHostPath(mount_point);

    switch (path_parser.GetHostStatus(mount_point)) {
    case PathParser::InvalidMountPoint:
        LOG_CRITICAL(Service_FS, "(unreachable) Invalid mount point {}", mount_point);
        return ResultNotFound;
    case PathParser::PathNotFound:
    case PathParser::NotFound:
    case PathParser::FileFound:
        LOG_DEBUG(Service_FS, "{} not found", full_path);
        return ResultNotFound;
    case PathParser::FileInPath:
        LOG_DEBUG(Service_FS, "Unexpected file in path {}", full_path);
        return ResultUnexpectedFileOrDirectorySdmc;
    case PathParser::DirectoryFound:
        break; // Expected 'success' case
    }

    return std::make_unique<DiskDirectory>(full_path);
}

u64 NANDArchive::GetFreeBytes() const {
    // TODO: Stubbed to return 1GiB
    return 1024 * 1024 * 1024;
}

ArchiveFactory_NAND::ArchiveFactory_NAND(const std::string& nand_directory, NANDArchiveType type)
    : nand_directory(nand_directory), archive_type(type) {

    LOG_DEBUG(Service_FS, "Directory {} set as NAND.", nand_directory);
}

bool ArchiveFactory_NAND::Initialize() {
    if (!FileUtil::CreateFullPath(GetPath())) {
        LOG_ERROR(Service_FS, "Unable to create NAND path.");
        return false;
    }

    return true;
}

std::string ArchiveFactory_NAND::GetPath() {
    switch (archive_type) {
    case NANDArchiveType::RW:
        return PathParser("/rw").BuildHostPath(nand_directory) + DIR_SEP;
    case NANDArchiveType::RO:
    case NANDArchiveType::RO_W:
        return PathParser("/ro").BuildHostPath(nand_directory) + DIR_SEP;
    default:
        break;
    }

    UNREACHABLE();
    return "";
}

ResultVal<std::unique_ptr<ArchiveBackend>> ArchiveFactory_NAND::Open(const Path& path,
                                                                     u64 program_id) {
    return std::make_unique<NANDArchive>(GetPath(), archive_type);
}

Result ArchiveFactory_NAND::Format(const Path& path, const FileSys::ArchiveFormatInfo& format_info,
                                   u64 program_id, u32 directory_buckets, u32 file_buckets) {
    // TODO(PabloMK7): Find proper error code
    LOG_ERROR(Service_FS, "Unimplemented Format archive {}", GetName());
    return UnimplementedFunction(ErrorModule::FS);
}

ResultVal<ArchiveFormatInfo> ArchiveFactory_NAND::GetFormatInfo(const Path& path,
                                                                u64 program_id) const {
    // TODO(PabloMK7): Implement
    LOG_ERROR(Service_FS, "Unimplemented GetFormatInfo archive {}", GetName());
    return UnimplementedFunction(ErrorModule::FS);
}
} // namespace FileSys
