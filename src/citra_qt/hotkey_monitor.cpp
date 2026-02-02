// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QShortcut>
#include <QTimer>
#include "hotkey_monitor.h"
#include "hotkeys.h"

struct ControllerHotkeyMonitor::ButtonState {
    Hotkey* hk;
    bool lastStatus;
};

ControllerHotkeyMonitor::ControllerHotkeyMonitor() {
    m_buttons = std::make_unique<std::map<QString, ButtonState>>();
    m_timer = new QTimer();
    QObject::connect(m_timer, &QTimer::timeout, [this]() { checkAllButtons(); });
    m_timer->start(16);
}

ControllerHotkeyMonitor::~ControllerHotkeyMonitor() {
    delete m_timer;
}

void ControllerHotkeyMonitor::addButton(const QString& name, Hotkey* hk) {
    (*m_buttons)[name] = {hk, false};
}

void ControllerHotkeyMonitor::removeButton(const QString& name) {
    m_buttons->erase(name);
}

void ControllerHotkeyMonitor::checkAllButtons() {
    for (auto& [name, it] : *m_buttons) {
        bool currentStatus = it.hk->button_device.GetStatus();
        if (currentStatus != it.lastStatus) {
            if (it.lastStatus && !currentStatus) {
                for (auto const& [_, hotkey_shortcut] : it.hk->shortcuts) {
                    if (hotkey_shortcut) {
                        hotkey_shortcut->activated();
                    }
                }
            }
            it.lastStatus = currentStatus;
        }
    }
}