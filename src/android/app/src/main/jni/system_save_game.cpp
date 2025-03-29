// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <common/string_util.h>
#include <core/core.h>
#include <core/hle/service/cfg/cfg.h>
#include <core/hle/service/ptm/ptm.h>
#include <core/hw/unique_data.h>
#include "android_common/android_common.h"

static bool changes_pending = false;
std::shared_ptr<Service::CFG::Module> cfg;

extern "C" {

void Java_org_citra_citra_1emu_utils_SystemSaveGame_save([[maybe_unused]] JNIEnv* env,
                                                         [[maybe_unused]] jobject obj) {
    if (changes_pending) {
        changes_pending = false;
        cfg->UpdateConfigNANDSavegame();
    }
}

void Java_org_citra_citra_1emu_utils_SystemSaveGame_load([[maybe_unused]] JNIEnv* env,
                                                         [[maybe_unused]] jobject obj) {
    cfg.reset();
    cfg = Service::CFG::GetModule(Core::System::GetInstance());
}

jboolean Java_org_citra_citra_1emu_utils_SystemSaveGame_getIsSystemSetupNeeded(
    [[maybe_unused]] JNIEnv* env, [[maybe_unused]] jobject obj) {
    return cfg->IsSystemSetupNeeded();
}

void Java_org_citra_citra_1emu_utils_SystemSaveGame_setSystemSetupNeeded(
    [[maybe_unused]] JNIEnv* env, [[maybe_unused]] jobject obj, jboolean needed) {
    cfg->SetSystemSetupNeeded(needed);
    changes_pending = true;
}

jstring Java_org_citra_citra_1emu_utils_SystemSaveGame_getUsername([[maybe_unused]] JNIEnv* env,
                                                                   [[maybe_unused]] jobject obj) {
    return ToJString(env, Common::UTF16ToUTF8(cfg->GetUsername()));
}

void Java_org_citra_citra_1emu_utils_SystemSaveGame_setUsername([[maybe_unused]] JNIEnv* env,
                                                                [[maybe_unused]] jobject obj,
                                                                jstring username) {
    cfg->SetUsername(Common::UTF8ToUTF16(GetJString(env, username)));
    changes_pending = true;
}

jshortArray Java_org_citra_citra_1emu_utils_SystemSaveGame_getBirthday(
    [[maybe_unused]] JNIEnv* env, [[maybe_unused]] jobject obj) {
    jshortArray jbirthdayArray = env->NewShortArray(2);
    auto birthday = cfg->GetBirthday();
    jshort birthdayArray[2]{static_cast<jshort>(get<0>(birthday)),
                            static_cast<jshort>(get<1>(birthday))};
    env->SetShortArrayRegion(jbirthdayArray, 0, 2, birthdayArray);
    return jbirthdayArray;
}

void Java_org_citra_citra_1emu_utils_SystemSaveGame_setBirthday([[maybe_unused]] JNIEnv* env,
                                                                [[maybe_unused]] jobject obj,
                                                                jshort jmonth, jshort jday) {
    cfg->SetBirthday(static_cast<u8>(jmonth), static_cast<u8>(jday));
    changes_pending = true;
}

jint Java_org_citra_citra_1emu_utils_SystemSaveGame_getSystemLanguage(
    [[maybe_unused]] JNIEnv* env, [[maybe_unused]] jobject obj) {
    return cfg->GetSystemLanguage();
}

void Java_org_citra_citra_1emu_utils_SystemSaveGame_setSystemLanguage([[maybe_unused]] JNIEnv* env,
                                                                      [[maybe_unused]] jobject obj,
                                                                      jint jsystemLanguage) {
    cfg->SetSystemLanguage(static_cast<Service::CFG::SystemLanguage>(jsystemLanguage));
    changes_pending = true;
}

jint Java_org_citra_citra_1emu_utils_SystemSaveGame_getSoundOutputMode(
    [[maybe_unused]] JNIEnv* env, [[maybe_unused]] jobject obj) {
    return cfg->GetSoundOutputMode();
}

void Java_org_citra_citra_1emu_utils_SystemSaveGame_setSoundOutputMode([[maybe_unused]] JNIEnv* env,
                                                                       [[maybe_unused]] jobject obj,
                                                                       jint jmode) {
    cfg->SetSoundOutputMode(static_cast<Service::CFG::SoundOutputMode>(jmode));
    changes_pending = true;
}

jshort Java_org_citra_citra_1emu_utils_SystemSaveGame_getCountryCode([[maybe_unused]] JNIEnv* env,
                                                                     [[maybe_unused]] jobject obj) {
    return cfg->GetCountryCode();
}

void Java_org_citra_citra_1emu_utils_SystemSaveGame_setCountryCode([[maybe_unused]] JNIEnv* env,
                                                                   [[maybe_unused]] jobject obj,
                                                                   jshort jmode) {
    cfg->SetCountryCode(static_cast<u8>(jmode));
    changes_pending = true;
}

jint Java_org_citra_citra_1emu_utils_SystemSaveGame_getPlayCoins([[maybe_unused]] JNIEnv* env,
                                                                 [[maybe_unused]] jobject obj) {
    return Service::PTM::Module::GetPlayCoins();
}

void Java_org_citra_citra_1emu_utils_SystemSaveGame_setPlayCoins([[maybe_unused]] JNIEnv* env,
                                                                 [[maybe_unused]] jobject obj,
                                                                 jint jcoins) {
    Service::PTM::Module::SetPlayCoins(static_cast<u16>(jcoins));
    changes_pending = true;
}

jlong Java_org_citra_citra_1emu_utils_SystemSaveGame_getConsoleId([[maybe_unused]] JNIEnv* env,
                                                                  [[maybe_unused]] jobject obj) {
    return cfg->GetConsoleUniqueId();
}

void Java_org_citra_citra_1emu_utils_SystemSaveGame_regenerateConsoleId(
    [[maybe_unused]] JNIEnv* env, [[maybe_unused]] jobject obj) {
    const auto [random_number, console_id] = cfg->GenerateConsoleUniqueId();
    cfg->SetConsoleUniqueId(random_number, console_id);
    changes_pending = true;
}

jstring Java_org_citra_citra_1emu_utils_SystemSaveGame_getMac(JNIEnv* env,
                                                              [[maybe_unused]] jobject obj) {
    return ToJString(env, cfg->GetMacAddress());
}

void Java_org_citra_citra_1emu_utils_SystemSaveGame_regenerateMac(JNIEnv* env,
                                                                  [[maybe_unused]] jobject obj) {
    cfg->GetMacAddress() = Service::CFG::GenerateRandomMAC();
    cfg->SaveMacAddress();
}

jint Java_org_citra_citra_1emu_utils_SystemSaveGame_getCountryCompatibility(
    JNIEnv* env, [[maybe_unused]] jobject obj, jint region) {
    int res = 0;
    u8 country = cfg->GetCountryCode();
    if (region != Settings::REGION_VALUE_AUTO_SELECT &&
        !Service::CFG::Module::IsValidRegionCountry(static_cast<u32>(region), country)) {
        res |= 1;
    }
    if (HW::UniqueData::GetSecureInfoA().IsValid()) {
        region = static_cast<jint>(cfg->GetRegionValue(true));
        if (!Service::CFG::Module::IsValidRegionCountry(static_cast<u32>(region), country)) {
            res |= 2;
        }
    }
    return res;
}

} // extern "C"
