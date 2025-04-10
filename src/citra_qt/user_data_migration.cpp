// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QTranslator>
#include "common/file_util.h"
#include "user_data_migration.h"

// Needs to be included at the end due to https://bugreports.qt.io/browse/QTBUG-73263
#include <filesystem>

namespace fs = std::filesystem;

UserDataMigrator::UserDataMigrator(QMainWindow* main_window) {
    // NOTE: Logging is not initialized yet, do not produce logs here.

    // Check migration if user directory does not exist
    if (!fs::is_directory(FileUtil::GetUserPath(FileUtil::UserPath::UserDir))) {
        ShowMigrationPrompt(main_window);
    }
}

void UserDataMigrator::ShowMigrationPrompt(QMainWindow* main_window) {
    namespace fs = std::filesystem;

    const QString migration_prompt_message =
        main_window->tr("Would you like to migrate your data for use in Azahar?\n"
                        "(This may take a while; The old data will not be deleted)");
    const bool citra_dir_found =
        fs::is_directory(FileUtil::GetUserPath(FileUtil::UserPath::LegacyCitraUserDir));
    const bool lime3ds_dir_found =
        fs::is_directory(FileUtil::GetUserPath(FileUtil::UserPath::LegacyLime3DSUserDir));

    if (citra_dir_found && lime3ds_dir_found) {
        QMessageBox migration_prompt;
        migration_prompt.setWindowTitle(main_window->tr("Migration"));
        migration_prompt.setText(
            main_window->tr("Azahar has detected user data for Citra and Lime3DS.\n\n") +
            migration_prompt_message);
        migration_prompt.setIcon(QMessageBox::Information);

        const QAbstractButton* lime3dsButton = migration_prompt.addButton(
            main_window->tr("Migrate from Lime3DS"), QMessageBox::YesRole);
        const QAbstractButton* citraButton =
            migration_prompt.addButton(main_window->tr("Migrate from Citra"), QMessageBox::YesRole);
        migration_prompt.addButton(QMessageBox::No);

        migration_prompt.exec();

        if (citraButton == migration_prompt.clickedButton()) {
            MigrateUserData(main_window, LegacyEmu::Citra);
        }
        if (lime3dsButton == migration_prompt.clickedButton()) {
            MigrateUserData(main_window, LegacyEmu::Lime3DS);
        }

        return;
    }

    else if (citra_dir_found) {
        if (QMessageBox::information(
                main_window, main_window->tr("Migration"),
                main_window->tr("Azahar has detected user data for Citra.\n\n") +
                    migration_prompt_message,
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes) {
            MigrateUserData(main_window, LegacyEmu::Citra);
            return;
        }
    }

    else if (lime3ds_dir_found) {
        if (QMessageBox::information(
                main_window, main_window->tr("Migration"),
                main_window->tr("Azahar has detected user data for Lime3DS.\n\n") +
                    migration_prompt_message,
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes) {
            MigrateUserData(main_window, LegacyEmu::Lime3DS);
            return;
        }
    }

    else { // Neither Citra or Lime3DS data was found; Do nothing
        return;
    }

    // If we're here, the user chose not to migrate
    ShowMigrationCancelledMessage(main_window);
}

void UserDataMigrator::ShowMigrationCancelledMessage(QMainWindow* main_window) {
    QMessageBox::information(
        main_window, main_window->tr("Migration"),
        main_window
            ->tr("You can manually re-trigger this prompt by deleting the "
                 "new user data directory:\n"
                 "%1")
            .arg(QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::UserDir))),
        QMessageBox::Ok);
}

void UserDataMigrator::MigrateUserData(QMainWindow* main_window,
                                       const LegacyEmu selected_legacy_emu) {
    namespace fs = std::filesystem;
    const auto copy_options = fs::copy_options::update_existing | fs::copy_options::recursive;

    std::string legacy_user_dir;
    std::string legacy_config_dir;
    std::string legacy_cache_dir;
    if (LegacyEmu::Citra == selected_legacy_emu) {
        legacy_user_dir = FileUtil::GetUserPath(FileUtil::UserPath::LegacyCitraUserDir);
        legacy_config_dir = FileUtil::GetUserPath(FileUtil::UserPath::LegacyCitraConfigDir);
        legacy_cache_dir = FileUtil::GetUserPath(FileUtil::UserPath::LegacyCitraCacheDir);
    } else if (LegacyEmu::Lime3DS == selected_legacy_emu) {
        legacy_user_dir = FileUtil::GetUserPath(FileUtil::UserPath::LegacyLime3DSUserDir);
        legacy_config_dir = FileUtil::GetUserPath(FileUtil::UserPath::LegacyLime3DSConfigDir);
        legacy_cache_dir = FileUtil::GetUserPath(FileUtil::UserPath::LegacyLime3DSCacheDir);
    }

    fs::copy(legacy_user_dir, FileUtil::GetUserPath(FileUtil::UserPath::UserDir), copy_options);

    if (fs::is_directory(legacy_config_dir)) {
        fs::copy(legacy_config_dir, FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir),
                 copy_options);
    }
    if (fs::is_directory(legacy_cache_dir)) {
        fs::copy(legacy_cache_dir, FileUtil::GetUserPath(FileUtil::UserPath::CacheDir),
                 copy_options);
    }

    QMessageBox::information(
        main_window, main_window->tr("Migration"),
        main_window
            ->tr("Data was migrated successfully.\n\n"
                 "If you wish to clean up the files which were left in the old "
                 "data location, you can do so by deleting the following directory:\n"
                 "%1")
            .arg(QString::fromStdString(legacy_user_dir)),
        QMessageBox::Ok);
}
