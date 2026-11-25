// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <atomic>
#include <mutex>
#include <vector>
#include "common/file_preload.h"

namespace FileUtil::Preload {

namespace {
std::mutex g_mutex;
std::vector<std::shared_ptr<const Image>> g_images;
std::atomic<bool> g_any{false};

std::shared_ptr<const Image> Find(Image::Kind kind, const std::string& filename, u64 source_size,
                                  u64 size) {
    if (!g_any.load(std::memory_order_relaxed)) {
        return nullptr;
    }
    std::scoped_lock lock{g_mutex};
    for (const auto& image : g_images) {
        if (image->kind == kind && image->filename == filename &&
            image->source_size == source_size && image->size == size) {
            return image;
        }
    }
    return nullptr;
}
} // Anonymous namespace

void Add(std::shared_ptr<const Image> image) {
    std::scoped_lock lock{g_mutex};
    std::erase_if(g_images, [&](const auto& other) {
        return other->kind == image->kind && other->filename == image->filename;
    });
    g_images.push_back(std::move(image));
    g_any.store(true, std::memory_order_relaxed);
}

void Clear() {
    std::scoped_lock lock{g_mutex};
    g_images.clear();
    g_any.store(false, std::memory_order_relaxed);
}

bool Any() {
    return g_any.load(std::memory_order_relaxed);
}

std::shared_ptr<const Image> FindRaw(const std::string& filename, u64 file_size) {
    return Find(Image::Kind::Raw, filename, file_size, file_size);
}

std::shared_ptr<const Image> FindDecompressed(const std::string& filename, u64 compressed_size,
                                              u64 uncompressed_size) {
    return Find(Image::Kind::Decompressed, filename, compressed_size, uncompressed_size);
}

} // namespace FileUtil::Preload
