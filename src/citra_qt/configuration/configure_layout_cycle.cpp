// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.
#include <QCloseEvent>
#include <QDialog>
#include <QMessageBox>
#include "citra_qt/configuration/configure_layout_cycle.h"
#include "ui_configure_layout_cycle.h"

ConfigureLayoutCycle::ConfigureLayoutCycle(QWidget* parent)
    : QDialog(parent), ui(std::make_unique<Ui::ConfigureLayoutCycle>()) {
    ui->setupUi(this);
    SetConfiguration();
    ConnectEvents();
}

// You MUST define the destructor in the .cpp file
ConfigureLayoutCycle::~ConfigureLayoutCycle() = default;

void ConfigureLayoutCycle::ConnectEvents() {
    disconnect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this,
            &ConfigureLayoutCycle::ApplyConfiguration);
    connect(ui->globalCheck, &QCheckBox::stateChanged, this, &ConfigureLayoutCycle::UpdateGlobal);
}

void ConfigureLayoutCycle::SetConfiguration() {
    if (Settings::IsConfiguringGlobal()) {
        ui->globalCheck->setChecked(true);
        ui->globalCheck->setVisible(false);
    } else {
        ui->globalCheck->setChecked(Settings::values.layouts_to_cycle.UsingGlobal());
        ui->checkGroup->setDisabled(Settings::values.layouts_to_cycle.UsingGlobal());
    }
    for (auto option : Settings::values.layouts_to_cycle.GetValue()) {
        switch (option) {
        case Settings::LayoutOption::Default:
            ui->defaultCheck->setChecked(true);
            break;
        case Settings::LayoutOption::SingleScreen:
            ui->singleCheck->setChecked(true);
            break;
        case Settings::LayoutOption::LargeScreen:
            ui->largeCheck->setChecked(true);
            break;
        case Settings::LayoutOption::SideScreen:
            ui->sidebysideCheck->setChecked(true);
            break;
        case Settings::LayoutOption::SeparateWindows:
            ui->separateCheck->setChecked(true);
            break;
        case Settings::LayoutOption::HybridScreen:
            ui->hybridCheck->setChecked(true);
            break;
        case Settings::LayoutOption::CustomLayout:
            ui->customCheck->setChecked(true);
            break;
        }
    }
}

void ConfigureLayoutCycle::ApplyConfiguration() {
    std::vector<Settings::LayoutOption> newSetting{};
    if (ui->defaultCheck->isChecked())
        newSetting.push_back(Settings::LayoutOption::Default);
    if (ui->singleCheck->isChecked())
        newSetting.push_back(Settings::LayoutOption::SingleScreen);
    if (ui->sidebysideCheck->isChecked())
        newSetting.push_back(Settings::LayoutOption::SideScreen);
    if (ui->largeCheck->isChecked())
        newSetting.push_back(Settings::LayoutOption::LargeScreen);
    if (ui->separateCheck->isChecked())
        newSetting.push_back(Settings::LayoutOption::SeparateWindows);
    if (ui->hybridCheck->isChecked())
        newSetting.push_back(Settings::LayoutOption::HybridScreen);
    if (ui->customCheck->isChecked())
        newSetting.push_back(Settings::LayoutOption::CustomLayout);
    if (newSetting.empty()) {
        QMessageBox::warning(this, tr("No Layout Selected"),
                             tr("Please select at least one layout option to cycle through."));
        return;
    } else {
        Settings::values.layouts_to_cycle = newSetting;
        accept();
    }
}

void ConfigureLayoutCycle::UpdateGlobal() {
    Settings::values.layouts_to_cycle.SetGlobal(ui->globalCheck->isChecked());
    ui->checkGroup->setDisabled(ui->globalCheck->isChecked());
    ui->checkGroup->repaint(); // Force visual update
}
