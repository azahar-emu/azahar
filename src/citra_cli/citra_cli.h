// Copyright 2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv3 or any later version
// Refer to the LICENSE.txt file included.

#include <getopt.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

namespace CitraCLI {

constexpr char compression_ops_optstring[] = "c:x:o:";
constexpr char cli_capture_optstring[] = "c:x:o:";

const struct option no_long_opts[] = {{nullptr, 0, nullptr, 0}};

bool CheckForOptions(const char* optstring, int argc, char* argv[]);
int ParseCommand(int argc, char* argv[]);

} // namespace CitraCLI
