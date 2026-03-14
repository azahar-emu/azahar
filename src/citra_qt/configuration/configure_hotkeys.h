// Copyright 2017 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QWidget>
#include "citra_qt/configuration/configure_input.h"

namespace Ui {
class ConfigureHotkeys;
}

class HotkeyRegistry;
class QStandardItemModel;

class ConfigureHotkeys : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureHotkeys(QWidget* parent = nullptr);
    ~ConfigureHotkeys() override;

    void ApplyConfiguration(HotkeyRegistry& registry);
    void RetranslateUI();

    void EmitHotkeysChanged();

    /**
     * Populates the hotkey list widget using data from the provided registry.
     * Called everytime the Configure dialog is opened.
     * @param registry The HotkeyRegistry whose data is used to populate the list.
     */
    void Populate(const HotkeyRegistry& registry);

public slots:
    void OnInputKeysChanged(QMap<QKeySequence, ConfigureInput::InputBinding> new_key_list);
    void OnClearBinding(ConfigureInput::InputBinding hotkey_to_clear);
signals:
    void HotkeysChanged(QMap<QKeySequence, ConfigureInput::InputBinding> new_key_list);
    void ClearInputBinding(ConfigureInput::InputBinding binding);

private:
    void Configure(QModelIndex index);
    std::pair<bool, ConfigureInput::InputBinding> IsUsedKey(QKeySequence key_sequence) const;
    QMap<QKeySequence, ConfigureInput::InputBinding> GetUsedKeyList() const;

    void RestoreDefaults();
    void ClearAll();
    void PopupContextMenu(const QPoint& menu_location);
    void RestoreHotkey(QModelIndex index);

    /**
     * List of keyboard keys currently registered to any of the 3DS inputs.
     * These can't be bound to any hotkey.
     * Synchronised with ConfigureInput via signal-slot.
     */
    QMap<QKeySequence, ConfigureInput::InputBinding> input_keys_list;

    std::unique_ptr<Ui::ConfigureHotkeys> ui;

    QStandardItemModel* model;
};
