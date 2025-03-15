// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QIcon>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrentRun>
#include "citra_qt/configuration/configure_web.h"
#include "citra_qt/uisettings.h"
#include "ui_configure_web.h"

ConfigureWeb::ConfigureWeb(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureWeb>()) {
    ui->setupUi(this);

#ifndef USE_DISCORD_PRESENCE
    ui->discord_group->setEnabled(false);
#endif
    SetConfiguration();
}

ConfigureWeb::~ConfigureWeb() = default;

void ConfigureWeb::SetConfiguration() {

    ui->toggle_discordrpc->setChecked(UISettings::values.enable_discord_presence.GetValue()),

        ui->username_lineedit->setText(
            QString::fromStdString(Settings::values.citra_username.GetValue()));

    ui->web_api_url_lineedit->setText(
        QString::fromStdString(Settings::values.web_api_url.GetValue()));

    ui->token_lineedit->setEnabled(ENABLE_WEB_SERVICE);
    ui->token_lineedit->setText(QString::fromStdString(Settings::values.citra_token.GetValue()));
}

void ConfigureWeb::ApplyConfiguration() {
    UISettings::values.enable_discord_presence = ui->toggle_discordrpc->isChecked();

    Settings::values.citra_username = ui->username_lineedit->text().toStdString();

    Settings::values.web_api_url = ui->web_api_url_lineedit->text().toStdString();

    Settings::values.citra_token = ui->token_lineedit->text().toStdString();
}

void ConfigureWeb::RetranslateUI() {
    ui->retranslateUi(this);
}
