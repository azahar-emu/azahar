// Copyright 2014-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <vector>
#include <QAction>
#include <QCollator>
#include <QShortcut>
#include <QtGlobal>
#include "citra_qt/hotkeys.h"
#include "citra_qt/uisettings.h"
#include "input_common/main.h"

HotkeyRegistry::HotkeyRegistry() = default;

HotkeyRegistry::~HotkeyRegistry() = default;

void HotkeyRegistry::SaveHotkeys() {
    UISettings::values.shortcuts.clear();
    for (const auto& group : hotkey_groups) {
        for (const auto& hotkey : group.second) {
            UISettings::values.shortcuts.push_back(
                {hotkey.first, group.first,
                 UISettings::ContextualShortcut({hotkey.second.keyseq.toString(),
                                                 hotkey.second.controller_keyseq,
                                                 hotkey.second.context})});
        }
    }
}
void HotkeyRegistry::UpdateControllerHotkey(QString name, Hotkey& hk) {
    if (hk.controller_keyseq.isEmpty()) {
        buttonMonitor.removeButton(name);
    } else {
        QStringList paramList = hk.controller_keyseq.split(QStringLiteral("||"));
        if (paramList.length() > 0) {
            hk.button_device =
                Input::CreateDevice<Input::ButtonDevice>(paramList.value(0).toStdString());
            if (paramList.length() > 1) {
                hk.button_device2 =
                    Input::CreateDevice<Input::ButtonDevice>(paramList.value(1).toStdString());
            }
            buttonMonitor.addButton(name, &hk);
        }
    }
}
void HotkeyRegistry::LoadHotkeys() {
    // Make sure NOT to use a reference here because it would become invalid once we call
    // beginGroup()
    for (auto shortcut : UISettings::values.shortcuts) {
        Hotkey& hk = hotkey_groups[shortcut.group][shortcut.name];
        if (!shortcut.shortcut.keyseq.isEmpty() || !shortcut.shortcut.controller_keyseq.isEmpty()) {
            hk.keyseq =
                QKeySequence::fromString(shortcut.shortcut.keyseq, QKeySequence::NativeText);
            hk.context = static_cast<Qt::ShortcutContext>(shortcut.shortcut.context);
            hk.controller_keyseq = shortcut.shortcut.controller_keyseq;
        }
        UpdateControllerHotkey(shortcut.name, hk);

        for (auto const& [_, hotkey_shortcut] : hk.shortcuts) {
            if (hotkey_shortcut) {
                hotkey_shortcut->disconnect();
                hotkey_shortcut->setKey(hk.keyseq);
            }
        }
    }
}

QShortcut* HotkeyRegistry::GetHotkey(const QString& group, const QString& action, QObject* widget) {
    Hotkey& hk = hotkey_groups[group][action];
    const auto widget_name = widget->objectName();

    if (!hk.shortcuts[widget_name]) {
        hk.shortcuts[widget_name] = new QShortcut(hk.keyseq, widget, nullptr, nullptr, hk.context);
    }

    return hk.shortcuts[widget_name];
}

QKeySequence HotkeyRegistry::GetKeySequence(const QString& group, const QString& action) {
    Hotkey& hk = hotkey_groups[group][action];
    return hk.keyseq;
}

Qt::ShortcutContext HotkeyRegistry::GetShortcutContext(const QString& group,
                                                       const QString& action) {
    Hotkey& hk = hotkey_groups[group][action];
    return hk.context;
}

void HotkeyRegistry::SetAction(const QString& group, const QString& action_name, QAction* action) {
    Hotkey& hk = hotkey_groups[group][action_name];
    hk.action = action;
}

QString HotkeyRegistry::SequenceToString(QString controller_keyseq) {
    if (controller_keyseq.isEmpty())
        return controller_keyseq;
    QStringList keys = controller_keyseq.split(QStringLiteral("||"));
    Common::ParamPackage p1 = Common::ParamPackage(keys.value(0).toStdString());
    QString output = QString::fromStdString(InputCommon::ButtonToText(p1));

    if (keys.length() > 1) {
        output.append(QStringLiteral(" + "));
        p1 = Common::ParamPackage(keys.value(1).toStdString());
        output.append(QString::fromStdString(InputCommon::ButtonToText(p1)));
    }
    return output;
}

static int HotkeyDisplayRank(const QString& group, const QString& action_name) {
    // Unlisted actions sort last, so a hotkey added later still appears.
    if (group != QStringLiteral("Savestates")) {
        return 1000;
    }

    static const QStringList ordered = {
        QStringLiteral("Quick Save"),
        QStringLiteral("Quick Load"),
        QStringLiteral("Next Save Slot"),
        QStringLiteral("Previous Save Slot"),
        QStringLiteral("Save to Current Slot"),
        QStringLiteral("Load from Current Slot"),
        QStringLiteral("Save to Oldest Non-Quicksave Slot"),
        QStringLiteral("Load from Newest Non-Quicksave Slot"),
    };

    const int index = ordered.indexOf(action_name);
    if (index >= 0) {
        return index;
    }
    // Two blocks of ten. Equal ranks are broken numerically by the caller, giving 1..10.
    if (action_name.startsWith(QStringLiteral("Save to Slot "))) {
        return 100;
    }
    if (action_name.startsWith(QStringLiteral("Load from Slot "))) {
        return 200;
    }
    return 1000;
}

std::vector<QString> HotkeyDisplayOrder(const QString& group,
                                        const std::map<QString, Hotkey>& hotkeys) {
    std::vector<QString> names;
    names.reserve(hotkeys.size());
    for (const auto& [name, hotkey] : hotkeys) {
        names.push_back(name);
    }

    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);

    std::sort(names.begin(), names.end(), [&](const QString& lhs, const QString& rhs) {
        const int lrank = HotkeyDisplayRank(group, lhs);
        const int rrank = HotkeyDisplayRank(group, rhs);
        if (lrank != rrank) {
            return lrank < rrank;
        }
        return collator.compare(lhs, rhs) < 0;
    });
    return names;
}
