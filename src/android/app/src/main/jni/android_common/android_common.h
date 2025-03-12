// Copyright 2022 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <string>

#include <jni.h>

std::string GetJString(JNIEnv* env, jstring jstr);
jstring ToJString(JNIEnv* env, std::string_view str);
jobjectArray ToJStringArray(JNIEnv* env, const std::vector<std::string>& strs);
