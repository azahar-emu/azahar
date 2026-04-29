// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <iterator>
#include <memory>
#include "common/archives.h"
#include "common/common_types.h"
#include "common/file_util.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "core/file_sys/disk_archive.h"
#include "core/file_sys/errors.h"

SERIALIZE_EXPORT_IMPL(FileSys::DiskFile)
SERIALIZE_EXPORT_IMPL(FileSys::DiskDirectory)

namespace FileSys {

ResultVal<std::size_t> DiskFile::Read(const u64 offset, const std::size_t length,
                                      u8* buffer) const {
    if (!mode.read_flag)
        return ResultInvalidOpenFlags;

    file->Seek(offset, SEEK_SET);
    return file->ReadBytes(buffer, length);
}

ResultVal<std::size_t> DiskFile::Write(const u64 offset, const std::size_t length, const bool flush,
                                       const bool update_timestamp, const u8* buffer) {
    if (!mode.write_flag)
        return ResultInvalidOpenFlags;

    file->Seek(offset, SEEK_SET);
    std::size_t written = file->WriteBytes(buffer, length);
    if (flush)
        file->Flush();
    return written;
}

u64 DiskFile::GetSize() const {
    return file->GetSize();
}

bool DiskFile::SetSize(const u64 size) const {
    file->Resize(size);
    file->Flush();
    return true;
}

bool DiskFile::Close() {
    return file->Close();
}

DiskDirectory::DiskDirectory(const std::string& path) {
    directory.size = FileUtil::ScanDirectoryTree(path, directory);
    directory.isDirectory = true;
    children_iterator = directory.children.begin();
}

u32 DiskDirectory::Read(const u32 count, Entry* entries) {
    u32 entries_read = 0;

    while (entries_read < count && children_iterator != directory.children.cend()) {
        const FileUtil::FSTEntry& file = *children_iterator;
        // Directory entries are exposed to the guest as UTF-16. Normalize host UTF-8 names first
        // so host Unicode normalization differences do not leak into guest-visible SDMC paths.
#if defined(__APPLE__)
        const std::string filename = Common::NormalizeNFDToNFC(file.virtualName);
#else
        const std::string& filename = file.virtualName;
#endif
        const std::u16string filename_utf16 = Common::UTF8ToUTF16(filename);
        Entry& entry = entries[entries_read];

        LOG_TRACE(Service_FS, "File {}: size={} dir={}", filename, file.size, file.isDirectory);

        std::fill(std::begin(entry.filename), std::end(entry.filename), u'\0');

        const std::size_t copy_length = std::min(filename_utf16.size(), FILENAME_LENGTH - 1);
        for (std::size_t j = 0; j < copy_length; ++j) {
            entry.filename[j] = filename_utf16[j];
        }

        FileUtil::SplitFilename83(filename, entry.short_name, entry.extension);

        entry.is_directory = file.isDirectory;
        entry.is_hidden = (!filename.empty() && filename[0] == '.');
        entry.is_read_only = 0;
        entry.file_size = file.size;

        // We emulate a SD card where the archive bit has never been cleared, as it would be on
        // most user SD cards.
        // Some homebrews (blargSNES for instance) are known to mistakenly use the archive bit as a
        // file bit.
        entry.is_archive = !file.isDirectory;

        ++entries_read;
        ++children_iterator;
    }
    return entries_read;
}
} // namespace FileSys
