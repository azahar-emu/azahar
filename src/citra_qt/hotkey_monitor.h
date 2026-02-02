// Copyright 2025 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <memory>
#include <QString>

class QTimer;
struct Hotkey;

class ControllerHotkeyMonitor {
public:
    explicit ControllerHotkeyMonitor();
    ~ControllerHotkeyMonitor();
    void addButton(const QString& name, Hotkey* hk);
    void removeButton(const QString& name);

private:
    void checkAllButtons();
    struct ButtonState;

    std::unique_ptr<std::map<QString, ButtonState>> m_buttons;
    QTimer* m_timer;
};