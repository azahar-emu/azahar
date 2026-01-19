// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "citra_room/citra_room.h"
#include "common/detached_tasks.h"
#include "common/scope_exit.h"

int main(int argc, char* argv[]) {
    Common::DetachedTasks detached_tasks;
    SCOPE_EXIT({ detached_tasks.WaitForAllTasks(); });

    LaunchRoom(argc, argv, false);
}
