// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <algorithm>
#include <memory>
#include <string>
#include "common/file_derived.h"
#include "common/logging/log.h"
#include "common/string_util.h"
#include "common/tar_file.h"
#include "common/zstd_compression.h"
#include "core/core.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/am/am.h"
#include "core/loader/3dsx.h"
#include "core/loader/artic.h"
#include "core/loader/elf.h"
#include "core/loader/ncch.h"

namespace Loader {

FileType IdentifyFile(FileUtil::IOFile& file) {
    FileType type;

#define CHECK_TYPE(loader)                                                                         \
    type = AppLoader_##loader::IdentifyType(&file);                                                \
    if (FileType::Error != type)                                                                   \
        return type;

    CHECK_TYPE(THREEDSX)
    CHECK_TYPE(ELF)
    CHECK_TYPE(NCCH)

#undef CHECK_TYPE

    return FileType::Unknown;
}

FileType IdentifyFile(const std::string& file_name) {
    FileUtil::IOFile file(file_name, "rb");
    if (!file.IsOpen()) {
        LOG_ERROR(Loader, "Failed to load file {}", file_name);
        return FileType::Unknown;
    }

    return IdentifyFile(file);
}

FileType GuessFromExtension(const std::string& extension_) {
    std::string extension = Common::ToLower(extension_);

    if (extension == ".elf" || extension == ".axf")
        return FileType::ELF;

    if (extension == ".cci" || extension == ".zcci" || extension == ".bcci" || extension == ".3ds")
        return FileType::CCI;

    if (extension == ".cxi" || extension == ".app" || extension == ".zcxi" || extension == ".bcxi")
        return FileType::CXI;

    if (extension == ".3dsx" || extension == ".z3dsx")
        return FileType::THREEDSX;

    if (extension == ".cia" || extension == ".zcia" || extension == ".bcia")
        return FileType::CIA;

    return FileType::Unknown;
}

const char* GetFileTypeString(FileType type, bool is_compressed, bool is_bundle) {
    switch (type) {
    case FileType::CCI:
        return is_bundle ? "NCSD (B)" : (is_compressed ? "NCSD (Z)" : "NCSD");
    case FileType::CXI:
        return is_bundle ? "NCCH (B)" : (is_compressed ? "NCCH (Z)" : "NCCH");
    case FileType::CIA:
        return is_bundle ? "CIA (B)" : (is_compressed ? "CIA (Z)" : "CIA");
    case FileType::ELF:
        return "ELF";
    case FileType::THREEDSX:
        return is_compressed ? "3DSX (Z)" : "3DSX";
    case FileType::ARTIC:
        return "ARTIC";
    case FileType::Error:
    case FileType::Unknown:
        break;
    }

    return "unknown";
}

/**
 * Get a loader for a file with a specific type
 * @param file The file to load
 * @param type The type of the file
 * @param filename the file name (without path)
 * @param filepath the file full path (with name)
 * @return std::unique_ptr<AppLoader> a pointer to a loader object;  nullptr for unsupported type
 */
static std::unique_ptr<AppLoader> GetFileLoader(Core::System& system, FileUtil::IOFile&& file,
                                                FileType type, const std::string& filename,
                                                const std::string& filepath) {
    switch (type) {

    // 3DSX file format.
    case FileType::THREEDSX:
        return std::make_unique<AppLoader_THREEDSX>(system, std::move(file), filename, filepath);

    // Standard ELF file format.
    case FileType::ELF:
        return std::make_unique<AppLoader_ELF>(system, std::move(file), filename);

    // NCCH/NCSD container formats.
    case FileType::CXI:
    case FileType::CCI:
        return std::make_unique<AppLoader_NCCH>(system, std::move(file), filepath);

    case FileType::ARTIC: {
        Apploader_Artic::ArticInitMode mode = Apploader_Artic::ArticInitMode::NONE;
        if (filename.starts_with("articinio://")) {
            mode = Apploader_Artic::ArticInitMode::O3DS;
        } else if (filename.starts_with("articinin://")) {
            mode = Apploader_Artic::ArticInitMode::N3DS;
        }
        auto strToUInt = [](const std::string& str) -> int {
            char* pEnd = NULL;
            unsigned long ul = ::strtoul(str.c_str(), &pEnd, 10);
            if (*pEnd)
                return -1;
            return static_cast<int>(ul);
        };

        u16 port = 5543;
        std::string server_addr = filename.substr(12);
        auto pos = server_addr.find(":");
        if (pos != server_addr.npos) {
            int newVal = strToUInt(server_addr.substr(pos + 1));
            if (newVal >= 0 && newVal <= 0xFFFF) {
                port = static_cast<u16>(newVal);
                server_addr = server_addr.substr(0, pos);
            }
        }
        return std::make_unique<Apploader_Artic>(system, server_addr, port, mode);
    }

    default:
        return nullptr;
    }
}

std::unique_ptr<AppLoader> GetLoader(const std::string& filename) {
    if (filename.starts_with("articbase://") || filename.starts_with("articinio://") ||
        filename.starts_with("articinin://")) {
        return GetFileLoader(Core::System::GetInstance(), FileUtil::IOFile(), FileType::ARTIC,
                             filename, "");
    }

    FileUtil::IOFile file(filename, "rb");
    if (!file.IsOpen()) {
        LOG_ERROR(Loader, "Failed to load file {}", filename);
        return nullptr;
    }

    std::string filename_filename, filename_extension;
    Common::SplitPath(filename, nullptr, &filename_filename, &filename_extension);

    FileType type = IdentifyFile(file);
    FileType filename_type = GuessFromExtension(filename_extension);

    if (type != filename_type) {
        // Do not show the error for CIA files, as their type cannot be determined.
        if (!(type == FileType::Unknown && filename_type == FileType::CIA)) {
            LOG_WARNING(Loader, "File {} has a different type than its extension.", filename);
        }

        if (FileType::Unknown == type)
            type = filename_type;
    }

    LOG_DEBUG(Loader, "Loading file {} as {}...", filename, GetFileTypeString(type));

    auto& system = Core::System::GetInstance();
    return GetFileLoader(system, std::move(file), type, filename_filename, filename);
}

Loader::AppLoader::LoaderFileInfo GetLoaderFileInfo(const std::string& filepath) {
    Loader::AppLoader::LoaderFileInfo loader_info{};
    auto loader = Loader::GetLoader(filepath);
    if (loader) {
        loader_info = loader->GetLoaderFileInfo();
        loader_info.default_compress_frame_size = FileUtil::Z3DSWriteIOFile::DEFAULT_FRAME_SIZE;
    } else {
        bool is_compressed = false;
        bool is_bundled = false;
        auto check_status = Service::AM::CheckCIAToInstall(filepath, is_compressed, is_bundled);
        if (check_status == Service::AM::InstallStatus::Success ||
            check_status == Service::AM::InstallStatus::ErrorCannotCheckBundle) {
            loader_info.is_valid = true;
            loader_info.is_compress_supported = !is_bundled;
            loader_info.is_bundle_supported = true;
            loader_info.is_compressed = is_compressed;
            loader_info.is_bundled = is_bundled;
            loader_info.recommended_compressed_extension = "zcia";
            loader_info.recommended_bundle_extension = "bcia";
            loader_info.recommended_plain_extension = "cia";
            loader_info.underlying_magic = std::array<u8, 4>({'C', 'I', 'A', '\0'});
            loader_info.default_compress_frame_size =
                FileUtil::Z3DSWriteIOFile::DEFAULT_CIA_FRAME_SIZE;
            if (!is_bundled) {
                auto meta_info = Service::AM::GetCIAInfos(filepath);
                if (meta_info.Succeeded()) {
                    const auto& meta_info_val = meta_info.Unwrap();
                    std::vector<u8> value(sizeof(Service::AM::TitleInfo));
                    memcpy(value.data(), &meta_info_val.first, sizeof(Service::AM::TitleInfo));
                    loader_info.default_metadata.emplace("titleinfo", value);
                    if (meta_info_val.second) {
                        value.resize(sizeof(Loader::SMDH));
                        memcpy(value.data(), meta_info_val.second.get(), sizeof(Loader::SMDH));
                        loader_info.default_metadata.emplace("smdh", value);
                    }
                }
            }
        }
    }

    return loader_info;
}

ResultStatus CreateBundleFile(const std::string& out_path, const std::string& main_file,
                              const std::vector<std::string>& dependencies,
                              std::function<FileUtil::ProgressCallback>&& progress_callback) {

    auto main_file_info = GetLoaderFileInfo(main_file);

    if (!main_file_info.is_valid || !main_file_info.is_bundle_supported ||
        main_file_info.is_bundled) {
        // Should never happen in well formed frontends.
        return ResultStatus::ErrorInvalidFormat;
    }

    FileUtil::Z3DSMetadata metadata;
    auto default_metadata = main_file_info.default_metadata;
    default_metadata.erase("smdh"); // Exclude SMDH as its not needed
    for (auto it : default_metadata) {
        metadata.Add(it.first, it.second);
    }

    std::vector<u8> smdh_buffer(sizeof(Loader::SMDH));
    Loader::SMDH* smdh = reinterpret_cast<Loader::SMDH*>(smdh_buffer.data());

    std::vector<std::pair<std::string, std::string>> in_files;

    auto process_file = [&smdh_buffer, &smdh, &in_files](const std::string& filename) {
        auto file_info = GetLoaderFileInfo(filename);

        if (!file_info.is_valid || !file_info.is_bundle_supported || file_info.is_bundled) {
            // Should never happen in well formed frontends.
            return ResultStatus::ErrorInvalidFormat;
        }

        memset(smdh_buffer.data(), 0, sizeof(Loader::SMDH));

        bool good = false;
        auto it = file_info.default_metadata.find("smdh");
        if (it != file_info.default_metadata.end()) {
            memcpy(smdh_buffer.data(), it->second.data(),
                   std::min(sizeof(Loader::SMDH), it->second.size()));
            good = smdh->IsValid();
        } else {
            auto loader = Loader::GetLoader(filename);
            if (loader && loader->ReadIcon(smdh_buffer, false) == ResultStatus::Success) {
                good = smdh->IsValid();
            }
        }

        Service::AM::TitleInfo title_info{};
        it = file_info.default_metadata.find("titleinfo");
        if (it != file_info.default_metadata.end()) {
            memcpy(&title_info, it->second.data(),
                   std::min(sizeof(Service::AM::TitleInfo), it->second.size()));
        }

        const std::string extension =
            "." + ((file_info.is_compress_supported && file_info.is_compressed)
                       ? file_info.recommended_compressed_extension
                       : file_info.recommended_plain_extension);

        std::string name = fmt::format("{:016X}", title_info.tid);
        if (good) {
            const auto& short_title = smdh->GetShortTitle(SMDH::TitleLanguage::English);
            auto title_end = std::find(short_title.begin(), short_title.end(), u'\0');
            std::string title = Common::UTF16ToUTF8(std::u16string{short_title.begin(), title_end});

            // Escape any special characters from the SMDH title.
            for (char& c : title) {
                const u8 uc = static_cast<u8>(c);
                if (uc < 0x20 || uc == 0x7F ||
                    std::string_view("/\\:*?\"<>|").find(c) != std::string_view::npos) {
                    c = '_';
                }
            }

            // Limit the name of the file so that it doesn't cut the extension or duplicate
            // file markers. Maximum length is 100 bytes including the terminator.
            constexpr std::size_t MAX_TAR_NAME = 99;
            constexpr std::size_t UNIQUE_SUFFIX_SIZE = 5;
            const std::size_t fixed_size = name.size() + 3 + extension.size() + UNIQUE_SUFFIX_SIZE;
            std::size_t title_size = std::min(title.size(), MAX_TAR_NAME - fixed_size);
            if (title_size < title.size()) {
                while (title_size > 0 && (static_cast<u8>(title[title_size]) & 0xC0) == 0x80) {
                    title_size--;
                }
                title.resize(title_size);
            }
            if (!title.empty()) {
                name += " - " + title;
            }
        }

        const auto name_taken = [&in_files](const std::string& candidate) {
            return std::ranges::any_of(
                in_files, [&candidate](const auto& f) { return f.second == candidate; });
        };
        std::string unique_name = name + extension;
        for (int i = 1; name_taken(unique_name); i++) {
            unique_name = fmt::format("{} ({}){}", name, i, extension);
        }

        in_files.push_back(std::make_pair(filename, unique_name));
        return ResultStatus::Success;
    };

    ResultStatus res = ResultStatus::Success;

    if ((res = process_file(main_file)) != ResultStatus::Success) {
        return res;
    }

    metadata.Add("mainfile", in_files[0].second);

    for (const auto& dep : dependencies) {
        if ((res = process_file(dep)) != ResultStatus::Success) {
            return res;
        }
    }

    return FileUtil::TarArchive::CreateTarFile(out_path, in_files, std::move(metadata),
                                               std::move(progress_callback))
               ? ResultStatus::Success
               : ResultStatus::Error;
}

} // namespace Loader
