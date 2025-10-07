// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.
#pragma once

#include <memory>
#include <QDialog>
#include "common/settings.h"

namespace Ui {
class ConfigureLayoutCycle;
}

class ConfigureLayoutCycle : public QDialog {
    Q_OBJECT

public:
    explicit ConfigureLayoutCycle(QWidget* parent = nullptr);
    ~ConfigureLayoutCycle() override;

public slots:
    void ApplyConfiguration();

private slots:

private:
    void SetConfiguration();
    void ConnectEvents();
    void UpdateGlobal();

    std::unique_ptr<Ui::ConfigureLayoutCycle> ui;
};