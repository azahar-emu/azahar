// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QDialog>
#include <QLabel>
#include "common/param_package.h"
#include "input_common/main.h"

class ControllerSequenceDialog : public QDialog {
    Q_OBJECT

public:
    explicit ControllerSequenceDialog(QWidget* parent = nullptr);
    ~ControllerSequenceDialog();

    QString GetSequence();
    void closeEvent(QCloseEvent*) override;

private:
    void LaunchPollers();
    QLabel* textBox;
    QString key_sequence;
    Common::ParamPackage params1, params2;
    bool focusNextPrevChild(bool next) override;
    std::vector<std::unique_ptr<InputCommon::Polling::DevicePoller>> device_pollers;
    std::unique_ptr<QTimer> poll_timer;
};
