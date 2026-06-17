// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QIcon>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrentRun>
#include "citra_qt/configuration/configure_web.h"
#include "citra_qt/uisettings.h"
#include "network/network_settings.h"
#include "ui_configure_web.h"

ConfigureWeb::ConfigureWeb(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureWeb>()) {
    ui->setupUi(this);

#ifndef ENABLE_DISCORD_RPC
    ui->discord_group->setEnabled(false);
#endif
    SetConfiguration();
}

ConfigureWeb::~ConfigureWeb() = default;

void ConfigureWeb::SetConfiguration() {
#ifdef ENABLE_DISCORD_RPC
    ui->toggle_discordrpc->setChecked(UISettings::values.enable_discord_presence.GetValue());
#endif
}

void ConfigureWeb::ApplyConfiguration() {
#ifdef ENABLE_DISCORD_RPC
    UISettings::values.enable_discord_presence = ui->toggle_discordrpc->isChecked();
#endif
}

void ConfigureWeb::RetranslateUI() {
    ui->retranslateUi(this);
}
