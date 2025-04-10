// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include "common/string_util.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/fs/archive.h"
#include "core/loader/loader.h"
#include "core/loader/smdh.h"
#include "jni/android_common/android_common.h"
#include "jni/id_cache.h"

namespace {

std::vector<u8> GetSMDHData(const std::string& path, bool& is_encrypted) {
    std::unique_ptr<Loader::AppLoader> loader = Loader::GetLoader(path);
    if (!loader) {
        return {};
    }

    u64 program_id = 0;
    loader->ReadProgramId(program_id);

    std::vector<u8> smdh = [program_id, &loader, &is_encrypted]() -> std::vector<u8> {
        std::vector<u8> original_smdh;
        auto result = loader->ReadIcon(original_smdh);
        if (result == Loader::ResultStatus::ErrorEncrypted) {
            is_encrypted = true;
            return original_smdh;
        }

        if (program_id < 0x00040000'00000000 || program_id > 0x00040000'FFFFFFFF)
            return original_smdh;

        std::string update_path = Service::AM::GetTitleContentPath(
            Service::FS::MediaType::SDMC, program_id + 0x0000000E'00000000);

        if (!FileUtil::Exists(update_path))
            return original_smdh;

        std::unique_ptr<Loader::AppLoader> update_loader = Loader::GetLoader(update_path);

        if (!update_loader)
            return original_smdh;

        std::vector<u8> update_smdh;
        update_loader->ReadIcon(update_smdh);
        return update_smdh;
    }();

    return smdh;
}

} // namespace

extern "C" {

static Loader::SMDH* GetPointer(JNIEnv* env, jobject obj) {
    return reinterpret_cast<Loader::SMDH*>(env->GetLongField(obj, IDCache::GetGameInfoPointer()));
}

JNIEXPORT jlong JNICALL Java_org_citra_citra_1emu_model_GameInfo_initialize(JNIEnv* env, jclass,
                                                                            jstring j_path) {
    bool is_encrypted = false;
    std::vector<u8> smdh_data = GetSMDHData(GetJString(env, j_path), is_encrypted);

    Loader::SMDH* smdh = nullptr;
    if (is_encrypted) {
        smdh = new Loader::SMDH;
        smdh->magic = 0xDEADDEAD;
    } else if (Loader::IsValidSMDH(smdh_data)) {
        smdh = new Loader::SMDH;
        std::memcpy(smdh, smdh_data.data(), sizeof(Loader::SMDH));
    }
    return reinterpret_cast<jlong>(smdh);
}

JNIEXPORT jboolean JNICALL Java_org_citra_citra_1emu_model_GameInfo_isEncrypted(JNIEnv* env,
                                                                                jobject obj) {
    Loader::SMDH* smdh = GetPointer(env, obj);
    return smdh->magic == 0xDEADDEAD;
}

JNIEXPORT void JNICALL Java_org_citra_citra_1emu_model_GameInfo_finalize(JNIEnv* env, jobject obj) {
    delete GetPointer(env, obj);
}

jstring Java_org_citra_citra_1emu_model_GameInfo_getTitle(JNIEnv* env, jobject obj) {
    Loader::SMDH* smdh = GetPointer(env, obj);
    Loader::SMDH::TitleLanguage language = Loader::SMDH::TitleLanguage::English;

    // Get the title from SMDH in UTF-16 format
    std::u16string title{reinterpret_cast<char16_t*>(
        smdh->titles[static_cast<std::size_t>(language)].long_title.data())};

    return ToJString(env, Common::UTF16ToUTF8(title).data());
}

jstring Java_org_citra_citra_1emu_model_GameInfo_getCompany(JNIEnv* env, jobject obj) {
    Loader::SMDH* smdh = GetPointer(env, obj);
    Loader::SMDH::TitleLanguage language = Loader::SMDH::TitleLanguage::English;

    // Get the Publisher's name from SMDH in UTF-16 format
    char16_t* publisher;
    publisher = reinterpret_cast<char16_t*>(
        smdh->titles[static_cast<std::size_t>(language)].publisher.data());

    return ToJString(env, Common::UTF16ToUTF8(publisher).data());
}

jstring Java_org_citra_citra_1emu_model_GameInfo_getRegions(JNIEnv* env, jobject obj) {
    Loader::SMDH* smdh = GetPointer(env, obj);

    using GameRegion = Loader::SMDH::GameRegion;
    static const std::map<GameRegion, const char*> regions_map = {
        {GameRegion::Japan, "Japan"},   {GameRegion::NorthAmerica, "North America"},
        {GameRegion::Europe, "Europe"}, {GameRegion::Australia, "Australia"},
        {GameRegion::China, "China"},   {GameRegion::Korea, "Korea"},
        {GameRegion::Taiwan, "Taiwan"}};
    std::vector<GameRegion> regions = smdh->GetRegions();

    if (regions.empty()) {
        return ToJString(env, "Invalid region");
    }

    const bool region_free =
        std::all_of(regions_map.begin(), regions_map.end(), [&regions](const auto& it) {
            return std::find(regions.begin(), regions.end(), it.first) != regions.end();
        });

    if (region_free) {
        return ToJString(env, "Region free");
    }

    const std::string separator = ", ";
    std::string result = regions_map.at(regions.front());
    for (auto region = ++regions.begin(); region != regions.end(); ++region) {
        result += separator + regions_map.at(*region);
    }

    return ToJString(env, result);
}

jintArray Java_org_citra_citra_1emu_model_GameInfo_getIcon(JNIEnv* env, jobject obj) {
    Loader::SMDH* smdh = GetPointer(env, obj);

    // Always get a 48x48(large) icon
    std::vector<u16> icon_data = smdh->GetIcon(true);
    if (icon_data.empty()) {
        return nullptr;
    }

    jintArray icon = env->NewIntArray(static_cast<jsize>(icon_data.size() / 2));
    env->SetIntArrayRegion(icon, 0, env->GetArrayLength(icon),
                           reinterpret_cast<jint*>(icon_data.data()));

    return icon;
}

jboolean Java_org_citra_citra_1emu_model_GameInfo_getIsVisibleSystemTitle(JNIEnv* env,
                                                                          jobject obj) {
    Loader::SMDH* smdh = GetPointer(env, obj);
    if (smdh == nullptr) {
        return false;
    }
    return smdh->flags & Loader::SMDH::Flags::Visible;
}
}
