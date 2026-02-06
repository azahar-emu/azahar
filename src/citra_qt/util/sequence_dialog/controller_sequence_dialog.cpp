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

    LaunchPollers(); // Fixed: added semicolon
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
            if (params.Has("engine") && params.Has("button")) { // for now, no analog inputs
                if (params.Has("down")) {
                    std::cerr << "button " + params.Get("button", "") + " down" << std::endl;
                    downCount++;
                    if (downCount > 2) {
                        // ignore third and fourth and fifth buttons
                    } else if (downCount == 1) {
                        key_sequence = QStringLiteral("");
                        params1 = params;
                        params2 = Common::ParamPackage();
                        textBox->setText(ConfigureControllerHotkeys::CleanSequence(
                                             QString::fromStdString(params1.Serialize())) +
                                         QStringLiteral("..."));
                    } else if (downCount == 2) {
                        // this is a second button while the first one is held down,
                        // go ahead and set the key_sequence as the chord and clear params
                        params2 = params;
                        key_sequence = QString::fromStdString(params1.Serialize() + "||" +
                                                              params2.Serialize());
                        textBox->setText(ConfigureControllerHotkeys::CleanSequence(key_sequence));
                    }
                } else { // button release
                    downCount--;
                    std::cerr << "button " + params.Get("button", "") + " up" << std::endl;
                    if (downCount == 0) {
                        // all released, clear all saved params
                        params1 = Common::ParamPackage();
                        params2 = Common::ParamPackage();
                    }
                    if (key_sequence.isEmpty()) {
                        key_sequence = QString::fromStdString(params.Serialize());
                        textBox->setText(ConfigureControllerHotkeys::CleanSequence(key_sequence));
                    }
                }
            }
        }
    });
    poll_timer->start(100);
}