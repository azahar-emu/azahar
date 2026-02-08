// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <iostream>
#include <QShortcut>
#include <QTimer>
#include <QWidget>
#include "core/frontend/input.h"
#include "hotkey_monitor.h"
#include "hotkeys.h"

struct ControllerHotkeyMonitor::ButtonState {
    Hotkey* hk;
    bool lastStatus = false;
    bool lastStatus2 = false;
};

ControllerHotkeyMonitor::ControllerHotkeyMonitor() {
    m_buttons = std::make_unique<std::map<QString, ButtonState>>();
    m_timer = new QTimer();
    QObject::connect(m_timer, &QTimer::timeout, [this]() { checkAllButtons(); });
}

void ControllerHotkeyMonitor::start(const int msec) {
    m_timer->start(msec);
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
        bool trigger = false;
        bool currentStatus = it.hk->button_device->GetStatus();
        if (it.hk->button_device2) {
            // two buttons, need both pressed and one *just now* pressed
            bool currentStatus2 = it.hk->button_device2->GetStatus();
            if (currentStatus)
                std::cerr << "button one pressed" << std::endl;
            if (currentStatus2)
                std::cerr << "button two pressed" << std::endl;
            trigger = currentStatus && currentStatus2 && (!it.lastStatus || !it.lastStatus2);
            it.lastStatus = currentStatus;
            it.lastStatus2 = currentStatus2;
        } else {
            // if only one button, trigger as soon as pressed
            trigger = currentStatus && !it.lastStatus;
            it.lastStatus = currentStatus;
        }
        if (trigger) {
            if (it.hk->action) {
                it.hk->action->trigger();
            }
            for (auto const& [name, hotkey_shortcut] : it.hk->shortcuts) {
                if (hotkey_shortcut && hotkey_shortcut->isEnabled()) {
                    QWidget* parent = qobject_cast<QWidget*>(hotkey_shortcut->parent());
                    if (parent && parent->isActiveWindow()) {
                        hotkey_shortcut->activated();
                        break;
                    }
                }
            }
        }
    }
}