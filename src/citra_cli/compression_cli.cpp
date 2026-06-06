// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <filesystem>
#include <iostream>
#undef _UNICODE
#include <getopt.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

#include "citra_cli/citra_cli.h"
#include "common/common_paths.h"
#include "common/logging/log.h"
#include "common/zstd_compression.h"
#include "core/loader/loader.h"

namespace CitraCLI {

static std::string strip_path_filename(std::string path) {
    namespace fs = std::filesystem;
    fs::path path_path = path;
    fs::path stripped_path = path_path.remove_filename();
    return stripped_path.string();
}

static std::string build_output_path(std::string source_path, std::string extension,
                                     std::string output_dir_path) {
    namespace fs = std::filesystem;
    fs::path source_path_path = source_path;
    std::string recommended_filename =
        source_path_path.filename().replace_extension(extension).string();
    return output_dir_path + DIR_SEP + recommended_filename;
}

static bool perform_z3ds_operation(bool is_compressing, const std::string& src_file,
                                   const std::string& dst_file,
                                   const std::array<u8, 4>& underlying_magic, size_t frame_size,
                                   std::function<FileUtil::ProgressCallback>&& update_callback,
                                   std::unordered_map<std::string, std::vector<u8>> metadata) {
    if (is_compressing) {
        return FileUtil::CompressZ3DSFile(src_file, dst_file, underlying_magic, frame_size,
                                          std::move(update_callback), metadata);
    } else { // decompressing
        return FileUtil::DeCompressZ3DSFile(src_file, dst_file, std::move(update_callback));
    }
}

int ParseCompressionCommand(int argc, char* argv[]) {
    Common::Log::Initialize();
    Common::Log::Start();

    const std::string common_error_addendum = "\nCheck log for more details.";

    std::optional<std::string> compress_path;   // The path of a decompressed file to be compressed
    std::optional<std::string> decompress_path; // The path of a compressed file to be decompressed
    std::optional<std::string> output_dir_path; // The directory which will contain processed file

    int option;
    while ((option = getopt(argc, argv, compression_ops_optstring)) != -1) {
        switch (option) {
        case 'c':
            compress_path = optarg;
            break;
        case 'x':
            decompress_path = optarg;
            break;
        case 'o':
            output_dir_path = optarg;
            break;
        }
    }

    bool is_compressing; // True if compressing, false if decompressing
    std::string source_path;
    std::string action_description; // String containing a user-friendly verb
                                    // describing the performed operation
    if (compress_path.has_value()) {
        is_compressing = true;
        source_path = compress_path.value();
        action_description = "Compressing";
    } else if (decompress_path.has_value()) {
        is_compressing = false;
        source_path = decompress_path.value();
        action_description = "Decompressing";
    } else {
        std::cout << "Invalid option combination provided. Quitting." << std::endl;
        return 1;
    }

    std::cout << action_description << " file '" << source_path << "'..." << std::flush;

    if (!output_dir_path.has_value()) {
        output_dir_path = strip_path_filename(source_path);
    }

    auto compress_info = Loader::GetCompressFileInfo(source_path, is_compressing);

    if (!compress_info.has_value()) {
        std::cout << "fail: Failed to get compress info for file." << common_error_addendum
                  << std::endl;
        return 1;
    }

    std::string extension; // The extension that the final processed file should have
    if (is_compressing) {
        extension = compress_info.value().first.recommended_compressed_extension;
    } else {
        extension = compress_info.value().first.recommended_uncompressed_extension;
    }

    std::string output_path = build_output_path(source_path, extension, output_dir_path.value());

    bool success = perform_z3ds_operation(
        is_compressing, source_path, output_path, compress_info.value().first.underlying_magic,
        compress_info.value().second, nullptr, compress_info.value().first.default_metadata);
    if (!success) {
        FileUtil::Delete(output_path);
        std::cout << "fail: Failed to perform Z3DS operation." << common_error_addendum
                  << std::endl;
        return 1;
    }
    std::cout << "success" << std::endl;

    return 0;
}

} // namespace CitraCLI
