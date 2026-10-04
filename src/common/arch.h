// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the misc/licenses/gplv2.txt file included.

#pragma once

#include <boost/predef.h>

#define CITRA_ARCH(NAME) (CITRA_ARCH_##NAME)

#define CITRA_ARCH_x86_64 BOOST_ARCH_X86_64
#define CITRA_ARCH_arm64                                                                           \
    (BOOST_ARCH_ARM >= BOOST_VERSION_NUMBER(8, 0, 0) && BOOST_ARCH_WORD_BITS == 64)
