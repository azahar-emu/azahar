// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>

#include <boost/serialization/base_object.hpp>
#include <boost/serialization/export.hpp>
#include <common/file_derived.h>
#include <common/file_util.h>

namespace FileUtil {

struct TarArchiveImpl;

// Derived from SubIOFile so that callers can detect if a file comes
// from a TarArchive. Should not override anything other than MyType().
class TarIOFile : public SubIOFile {
public:
    using SubIOFile::SubIOFile;

protected:
    IOType::Type MyType() const override {
        return IOType::Type::TarIOFile;
    }

private:
    template <class Archive>
    void serialize(Archive& ar, const unsigned int) {
        ar& boost::serialization::base_object<IOFileBase>(*this);
    }
    friend class boost::serialization::access;
};

class TarArchive {
public:
    struct TarFileInfo {
        std::string name;
        std::size_t position;
        std::size_t size;
    };

    static std::unique_ptr<TarArchive> OpenTarArchive(const std::string& filename) {
        std::unique_ptr<TarArchive> ret = std::make_unique<TarArchive>(filename);
        return ret->IsGood() ? std::move(ret) : nullptr;
    }

    static std::unique_ptr<TarArchive> OpenTarArchive(std::unique_ptr<IOFileBase>&& file) {
        std::unique_ptr<TarArchive> ret = std::make_unique<TarArchive>(std::move(file));
        return ret->IsGood() ? std::move(ret) : nullptr;
    }

    TarArchive();

    ~TarArchive();

    TarArchive(const std::string& filename);

    TarArchive(std::unique_ptr<IOFileBase>&& file);

    bool IsGood();

    const std::vector<TarArchive::TarFileInfo>& GetFileList();

    std::unique_ptr<FileUtil::IOFileBase> OpenSubFile(const std::string& filename);

private:
    std::unique_ptr<TarArchiveImpl> impl;
};

} // namespace FileUtil

BOOST_CLASS_EXPORT_KEY(FileUtil::TarIOFile)