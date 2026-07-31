// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <memory>
#include <optional>
#include <ranges>
#include <tuple>
#include <unordered_set>
#include <vector>
#include "common/file_derived.h"
#include "common/tar_file.h"

#include "microtar.h"

namespace FileUtil {

struct TarArchiveImpl {
    std::unique_ptr<IOFileBase> file;
    mtar_t tar;
    bool m_good;

    std::vector<TarArchive::TarFileInfo> file_infos;

    TarArchiveImpl() {
        file = std::make_unique<NullIOFile>();
        m_good = false;
    }

    TarArchiveImpl(const std::string& filename) {
        file = std::make_unique<IOFile>(filename, "rb");
        Open();
    }

    TarArchiveImpl(std::unique_ptr<IOFileBase>&& _file) {
        file = std::move(_file);
        Open();
    }

    void Open() {
        file->Seek(0, SEEK_SET);
        memset(&tar, 0, sizeof(tar));

        tar.read = [](mtar_t* tar, void* data, size_t size) {
            size_t res = reinterpret_cast<TarArchiveImpl*>(tar->stream)
                             ->file->ReadBytes(reinterpret_cast<char*>(data), size);
            return static_cast<int>(res == size ? MTAR_ESUCCESS : MTAR_EWRITEFAIL);
        };
        tar.write = [](mtar_t* tar, const void* data, size_t size) {
            size_t res = reinterpret_cast<TarArchiveImpl*>(tar->stream)
                             ->file->WriteBytes(reinterpret_cast<const char*>(data), size);
            return static_cast<int>(res == size ? MTAR_ESUCCESS : MTAR_EREADFAIL);
        };
        tar.seek = [](mtar_t* tar, size_t set_offset) {
            return static_cast<int>(
                reinterpret_cast<TarArchiveImpl*>(tar->stream)->file->Seek(set_offset, SEEK_SET)
                    ? MTAR_ESUCCESS
                    : MTAR_ESEEKFAIL);
        };
        tar.close = [](mtar_t* tar) {
            // NO-OP: This is only used with mtar_close to close the stream, but we have ownership
            // of the file and it will be auto-closed when we are destroyed.
            return static_cast<int>(MTAR_ESUCCESS);
        };
        tar.stream = this;

        mtar_header_t h;
        m_good = mtar_read_header(&tar, &h) == 0;
    }

    const std::vector<TarArchive::TarFileInfo>& GetFileList() {
        if (!m_good) {
            return {};
        }
        if (!file_infos.empty()) {
            return file_infos;
        }
        if (mtar_rewind(&tar) != MTAR_ESUCCESS) {
            m_good = false;
            return {};
        }

        mtar_header_t h;
        int err = MTAR_ESUCCESS;
        constexpr size_t MAX_FILES = 50;

        while ((err = mtar_read_header(&tar, &h)) != MTAR_ENULLRECORD &&
               file_infos.size() < MAX_FILES) {
            if (err != MTAR_ESUCCESS) {
                file_infos.clear();
                m_good = false;
                return {};
            }

            constexpr size_t TAR_HEADER_SIZE = 512;

            file_infos.emplace_back(TarArchive::TarFileInfo{
                .name = h.name,
                // File starts at the current header position plus TAR_HEADER_SIZE
                .position = file->Tell() + TAR_HEADER_SIZE,
                .size = h.size});

            if (mtar_next(&tar) != MTAR_ESUCCESS) {
                file_infos.clear();
                m_good = false;
                return {};
            }
        }

        std::unordered_set<std::string> temp;
        bool has_duplicates =
            std::ranges::any_of(file_infos, [&temp](const TarArchive::TarFileInfo& file) {
                return !temp.insert(file.name).second;
            });
        if (has_duplicates) {
            file_infos.clear();
            m_good = false;
            return {};
        }

        return file_infos;
    }

    std::unique_ptr<FileUtil::IOFileBase> OpenSubFile(const std::string& filename) {
        if (file_infos.empty() || !m_good) {
            return std::make_unique<FileUtil::NullIOFile>();
        }
        auto it = std::find_if(
            file_infos.begin(), file_infos.end(),
            [&filename](const TarArchive::TarFileInfo& info) { return info.name == filename; });
        if (it == file_infos.end()) {
            return std::unique_ptr<FileUtil::NullIOFile>();
        }

        auto ret = std::make_unique<FileUtil::TarIOFile>(file->OpenCopy(), it->position, it->size);
        if (!ret->IsGood()) {
            return std::unique_ptr<FileUtil::NullIOFile>();
        }

        return ret;
    }
};

TarArchive::TarArchive() {
    impl = std::make_unique<TarArchiveImpl>();
}

TarArchive::~TarArchive() {}

TarArchive::TarArchive(const std::string& filename) {
    impl = std::make_unique<TarArchiveImpl>(filename);
}

TarArchive::TarArchive(std::unique_ptr<IOFileBase>&& _file) {
    impl = std::make_unique<TarArchiveImpl>(std::move(_file));
}

bool TarArchive::IsGood() {
    return impl->m_good;
}

const std::vector<TarArchive::TarFileInfo>& TarArchive::GetFileList() {
    return impl->GetFileList();
}

std::unique_ptr<FileUtil::IOFileBase> TarArchive::OpenSubFile(const std::string& filename) {
    return impl->OpenSubFile(filename);
}

} // namespace FileUtil