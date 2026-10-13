// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <chrono>
#include <memory>
#include <optional>
#include <ranges>
#include <tuple>
#include <unordered_set>
#include <vector>
#include "common/file_derived.h"
#include "common/scm_rev.h"
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
            return static_cast<int>(res == size ? MTAR_ESUCCESS : MTAR_EREADFAIL);
        };
        tar.write = [](mtar_t* tar, const void* data, size_t size) {
            size_t res = reinterpret_cast<TarArchiveImpl*>(tar->stream)
                             ->file->WriteBytes(reinterpret_cast<const char*>(data), size);
            return static_cast<int>(res == size ? MTAR_ESUCCESS : MTAR_EWRITEFAIL);
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

    const std::vector<TarArchive::TarFileInfo>& Fail() {
        file_infos.clear();
        m_good = false;
        return file_infos;
    }

    const std::vector<TarArchive::TarFileInfo>& GetFileList() {
        if (!m_good) {
            return Fail();
        }
        if (!file_infos.empty()) {
            return file_infos;
        }
        if (mtar_rewind(&tar) != MTAR_ESUCCESS) {
            return Fail();
        }

        mtar_header_t h;
        int err = MTAR_ESUCCESS;
        constexpr size_t MAX_FILES = 50;
        const u64 archive_size = file->GetSize();

        while ((err = mtar_read_header(&tar, &h)) != MTAR_ENULLRECORD &&
               file_infos.size() < MAX_FILES) {
            if (err != MTAR_ESUCCESS) {
                return Fail();
            }

            constexpr size_t TAR_HEADER_SIZE = 512;

            std::string_view name(h.name);
            // Ignore hidden files and Pax entries
            if (!name.starts_with(".") && h.type == MTAR_TREG) {
                // File starts at the current header position plus TAR_HEADER_SIZE
                const u64 position = file->Tell() + TAR_HEADER_SIZE;
                // The file data must be fully inside the archive.
                if (position > archive_size || h.size > archive_size - position) {
                    return Fail();
                }
                file_infos.emplace_back(TarArchive::TarFileInfo{
                    .name = std::string(name), .position = position, .size = h.size});
            }

            if (mtar_next(&tar) != MTAR_ESUCCESS) {
                return Fail();
            }
        }

        std::unordered_set<std::string> temp;
        bool has_duplicates =
            std::ranges::any_of(file_infos, [&temp](const TarArchive::TarFileInfo& file) {
                return !temp.insert(file.name).second;
            });
        if (has_duplicates) {
            return Fail();
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
            return std::make_unique<FileUtil::NullIOFile>();
        }

        auto ret = std::make_unique<FileUtil::TarIOFile>(file->OpenCopy(), it->position, it->size);
        if (!ret->IsGood()) {
            return std::make_unique<FileUtil::NullIOFile>();
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

bool TarArchive::CreateTarFile(const std::string& out_file_name,
                               const std::vector<std::pair<std::string, std::string>>& in_files,
                               Z3DSMetadata&& metadata,
                               std::function<FileUtil::ProgressCallback>&& progress_callback) {

    IOFile out_file(out_file_name, "wb");
    if (!out_file.IsOpen()) {
        return false;
    }

    mtar_t tar;
    memset(&tar, 0, sizeof(tar));

    tar.read = [](mtar_t* tar, void* data, size_t size) {
        size_t res =
            reinterpret_cast<IOFile*>(tar->stream)->ReadBytes(reinterpret_cast<char*>(data), size);
        return static_cast<int>(res == size ? MTAR_ESUCCESS : MTAR_EREADFAIL);
    };
    tar.write = [](mtar_t* tar, const void* data, size_t size) {
        size_t res = reinterpret_cast<IOFile*>(tar->stream)
                         ->WriteBytes(reinterpret_cast<const char*>(data), size);
        return static_cast<int>(res == size ? MTAR_ESUCCESS : MTAR_EWRITEFAIL);
    };
    tar.seek = [](mtar_t* tar, size_t set_offset) {
        return static_cast<int>(reinterpret_cast<IOFile*>(tar->stream)->Seek(set_offset, SEEK_SET)
                                    ? MTAR_ESUCCESS
                                    : MTAR_ESEEKFAIL);
    };
    tar.close = [](mtar_t* tar) {
        // NO-OP: This is only used with mtar_close to close the stream, but we have ownership
        // of the file and it will be auto-closed when we are destroyed.
        return static_cast<int>(MTAR_ESUCCESS);
    };
    tar.stream = &out_file;

    auto error_exit = [&]() {
        out_file.Close();
        FileUtil::Delete(out_file.Filename());
    };

    metadata.Add("bundler", std::string("Azahar ") + Common::g_build_fullname);

    std::time_t tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &tt);
#else
    gmtime_r(&tt, &tm);
#endif
    char buf[0x20];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    metadata.Add("date", buf);

    auto metadata_vec = metadata.AsBinary();

    if (mtar_write_file_header(&tar, "metadata", metadata_vec.size()) != MTAR_ESUCCESS) {
        error_exit();
        return false;
    }

    if (mtar_write_data(&tar, metadata_vec.data(), metadata_vec.size()) != MTAR_ESUCCESS) {
        error_exit();
        return false;
    }

    std::size_t tot_files = in_files.size();
    std::size_t curr_file = 0;

    float single_file_prog = 1 / (float)tot_files;

    std::vector<u8> work_buf(0x10000);
    for (const auto& in_path : in_files) {
        const std::string& path = in_path.first;
        const std::string& name = in_path.second;

        IOFile in_file(path, "rb");
        if (!in_file.IsOpen()) {
            error_exit();
            return false;
        }

        size_t file_size = in_file.GetSize();

        if (mtar_write_file_header(&tar, name.c_str(), file_size) != MTAR_ESUCCESS) {
            error_exit();
            return false;
        }

        size_t remaining = file_size;
        while (remaining) {
            size_t read = in_file.ReadBytes(
                work_buf.data(), (remaining > work_buf.size()) ? work_buf.size() : remaining);

            if (!in_file.IsGood()) {
                error_exit();
                return false;
            }

            if (mtar_write_data(&tar, work_buf.data(), read) != MTAR_ESUCCESS) {
                error_exit();
                return false;
            }
            remaining -= read;

            float curr_prog = (single_file_prog * curr_file) +
                              single_file_prog * (1.f - ((float)remaining / (float)file_size));

            progress_callback((size_t)(curr_prog * 1000), 1000);
        }

        curr_file++;
    }

    mtar_finalize(&tar);
    mtar_close(&tar);
    return true;
}

} // namespace FileUtil