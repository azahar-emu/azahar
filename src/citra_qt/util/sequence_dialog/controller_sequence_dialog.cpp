// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <iostream>
#include <QDialogButtonBox>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include "common/param_package.h"
#include "configuration/configure_hotkeys_controller.h"
#include "controller_sequence_dialog.h"
#include "util/sequence_dialog/controller_sequence_dialog.h"

ControllerSequenceDialog::ControllerSequenceDialog(QWidget* parent)
    : QDialog(parent), poll_timer(std::make_unique<QTimer>()) {
    setWindowTitle(tr("Press then release one or two controller buttons"));

    auto* const buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->setCenterButtons(true);

    textBox = new QLabel(QStringLiteral("Waiting..."), this);
    auto* const layout = new QVBoxLayout(this);
    layout->addWidget(textBox);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    LaunchPollers();
}

ControllerSequenceDialog::~ControllerSequenceDialog() = default;

QString ControllerSequenceDialog::GetSequence() {
    return key_sequence;
}

void ControllerSequenceDialog::closeEvent(QCloseEvent*) {
    reject();
}

bool ControllerSequenceDialog::focusNextPrevChild(bool next) {
    return false;
}

void ControllerSequenceDialog::LaunchPollers() {
    device_pollers = InputCommon::Polling::GetPollers(InputCommon::Polling::DeviceType::Button);

    for (auto& poller : device_pollers) {
        poller->Start();
    }

    connect(poll_timer.get(), &QTimer::timeout, this, [this, downCount = 0]() mutable {
        Common::ParamPackage params;
        for (auto& poller : device_pollers) {
            params = poller->GetNextInput();
            if (params.Has("engine")) {
                std::cerr << "controller hotkey event detected: " + params.Serialize() << std::endl;
                if (params.Has("down")) {
                    downCount++;
                    if (downCount == 1) {
                        // either the first press, or the first new press
                        key_sequence = QStringLiteral("");
                        params1 = params;
                        params2 = Common::ParamPackage();
                        key_sequence = QString::fromStdString(params1.Serialize());
                        textBox->setText(ConfigureControllerHotkeys::CleanSequence(key_sequence) +
                                         QStringLiteral("..."));
                    } else if (downCount == 2 && !params2.Has("engine")) {
                        // this is a second button, currently only one button saved, so save it
                        params2 = params;
                        key_sequence = QString::fromStdString(params1.Serialize() + "||" +
                                                              params2.Serialize());
                        textBox->setText(ConfigureControllerHotkeys::CleanSequence(key_sequence));
                    }
                    // if downCount == 3 or more, just ignore them - we have saved the first two
                    // presses
                } else { // button release
                    downCount--;
                    if (downCount <= 0) {
                        // once all buttons are released, clear the params so the user can try again
                        // if need be
                        params1 = Common::ParamPackage();
                        params2 = Common::ParamPackage();
                    }
                }
            }
        }
    });
    poll_timer->start(100);
}