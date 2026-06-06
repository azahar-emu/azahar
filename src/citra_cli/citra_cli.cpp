// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#undef _UNICODE
#include <getopt.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

#include "citra_cli/citra_cli.h"
#include "citra_cli/compression_cli.h"

namespace CitraCLI {

bool CheckForOptions(const char* optstring, int argc, char* argv[]) {
    const int original_opterr = opterr;
    opterr = 0; // Temporarily suppress invalid option messages

    bool return_value = false;
    int option;
    while ((option = getopt(argc, argv, optstring)) != -1) {
        for (size_t i = 0; optstring[i] != '\0'; ++i) {
            if (optstring[i] == ':') {
                continue;
            }
            if (option == optstring[i]) {
                return_value = true;
                break;
            }
        }
    }

    opterr = original_opterr;
    optind = 1; // Reset getopt so that it can be used again
    return return_value;
}

int ParseCommand(int argc, char* argv[]) {
    if (CheckForOptions(compression_ops_optstring, argc, argv)) {
        return ParseCompressionCommand(argc, argv);
    }
}

} // namespace CitraCLI
