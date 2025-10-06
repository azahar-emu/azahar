#include <QCloseEvent>
#include <QDialog>
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
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this,
            &ConfigureLayoutCycle::ApplyConfiguration);
}

void ConfigureLayoutCycle::SetConfiguration() {
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
    Settings::values.layouts_to_cycle = newSetting;
    accept();
}
