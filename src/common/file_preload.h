// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#pragma once

#include <memory>
#include <string>
#include "common/common_types.h"

/**
 * Games held in memory (the iOS app's Preload Game into Memory). A frontend reads a game file into
 * memory before the game is loaded, decompressed when it is a compressed (Z3DS) file, and adds it
 * here. The file classes that read games then serve their reads from memory: IOFile for a plain
 * file (looked up by name and size) and Z3DSReadIOFile for a compressed one (by the name and size
 * of the compressed file and the uncompressed size), so nothing is read from storage or
 * decompressed while playing. The classes and their save state data stay as they are; a file
 * restored from a save state finds its image again on its first read.
 */
namespace FileUtil::Preload {

struct Image {
    enum class Kind {
        Raw,          ///< The bytes of a file as they are on storage
        Decompressed, ///< The uncompressed contents of a Z3DS file
    };
    Kind kind = Kind::Raw;
    std::string filename;
    /// The size of the file on storage (Raw), or of the compressed stream (Decompressed).
    u64 source_size = 0;
    /// The size of `data`: the file size (Raw) or the uncompressed size (Decompressed).
    u64 size = 0;
    std::unique_ptr<u8[]> data;
};

/// Adds an image, replacing one for the same file.
void Add(std::shared_ptr<const Image> image);

/// Removes every image (the game ended), the memory is released once no file uses it.
void Clear();

/// Whether any image is held, a cheap check before a lookup.
bool Any();

/// The image of a plain file of this name and size, or null.
std::shared_ptr<const Image> FindRaw(const std::string& filename, u64 file_size);

/// The decompressed image of a compressed file of this name, with a compressed stream of
/// `compressed_size` bytes and `uncompressed_size` bytes of contents, or null.
std::shared_ptr<const Image> FindDecompressed(const std::string& filename, u64 compressed_size,
                                              u64 uncompressed_size);

} // namespace FileUtil::Preload
