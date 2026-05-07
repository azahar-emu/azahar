// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <boost/serialization/base_object.hpp>
#include <boost/serialization/unique_ptr.hpp>
#include "common/archives.h"
#include "common/logging/log.h"
#include "core/file_sys/directory_backend.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/service/fs/directory.h"

SERIALIZE_EXPORT_IMPL(Service::FS::Directory)

namespace Service::FS {

template <class Archive>
void Directory::serialize(Archive& ar, const unsigned int) {
    ar& boost::serialization::base_object<Kernel::SessionRequestHandler>(*this);
    ar & path;
    ar & backend;
}

Directory::Directory(std::unique_ptr<FileSys::DirectoryBackend>&& backend,
                     const FileSys::Path& path)
    : Directory() {
    this->backend = std::move(backend);
    this->path = path;
}

Directory::Directory() : ServiceFramework("", 1), path(""), backend(nullptr) {
    static const FunctionInfo functions[] = {
        // clang-format off
        {0x0801, &Directory::Read, "Read"},
        {0x0802, &Directory::Close, "Close"},
        // clang-format on
    };
    RegisterHandlers(functions);
}

Directory::~Directory() {}

void Directory::Read(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);

    struct AsyncData {
        // Input
        u32 count;

        // Output
        Result ret{0};
        u32 read;
        Kernel::MappedBuffer* buffer;
    };

    auto async_data = std::make_shared<AsyncData>();
    async_data->count = rp.Pop<u32>();
    async_data->buffer = &rp.PopMappedBuffer();

    ctx.RunAsync(
        [this, async_data](Kernel::HLERequestContext& ctx) {
            std::vector<FileSys::Entry> entries(async_data->count);
            LOG_TRACE(Service_FS, "Read {}: count={}", GetName(), count);
            // Number of entries actually read
            async_data->read = backend->Read(static_cast<u32>(entries.size()), entries.data());
            async_data->buffer->Write(entries.data(), 0, async_data->read * sizeof(FileSys::Entry));
            return 0;
        },
        [async_data](Kernel::HLERequestContext& ctx) {
            IPC::RequestBuilder rb(ctx, 2, 2);

            rb.Push(ResultSuccess);
            rb.Push(async_data->read);
            rb.PushMappedBuffer(*async_data->buffer);
        },
        Settings::values.async_fs_operations.GetValue());
}

void Directory::Close(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx);
    LOG_TRACE(Service_FS, "Close {}", GetName());

    ctx.RunAsync(
        [this](Kernel::HLERequestContext& ctx) {
            backend->Close();
            return 0;
        },
        [](Kernel::HLERequestContext& ctx) {
            IPC::RequestBuilder rb(ctx, 1, 0);
            rb.Push(ResultSuccess);
        },
        Settings::values.async_fs_operations.GetValue());
}

} // namespace Service::FS
