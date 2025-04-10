// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <QMainWindow>

class UserDataMigrator {
public:
    UserDataMigrator(QMainWindow* main_window);

private:
    enum class LegacyEmu {
        Citra,
        Lime3DS,
    };

    void ShowMigrationPrompt(QMainWindow* main_window);
    void ShowMigrationCancelledMessage(QMainWindow* main_window);
    void MigrateUserData(QMainWindow* main_window, const LegacyEmu selected_legacy_emu);
};