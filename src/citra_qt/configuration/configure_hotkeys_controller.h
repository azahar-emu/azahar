// Copyright 2026 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QWidget>

namespace Ui {
class ConfigureControllerHotkeys;
}

class HotkeyRegistry;
class QStandardItemModel;

class ConfigureControllerHotkeys : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureControllerHotkeys(QWidget* parent = nullptr);
    ~ConfigureControllerHotkeys() override;

    void ApplyConfiguration(HotkeyRegistry& registry);
    void RetranslateUI();

    /**
     * Populates the hotkey list widget using data from the provided registry.
     * Called everytime the Configure dialog is opened.
     * @param registry The HotkeyRegistry whose data is used to populate the list.
     */
    void Populate(const HotkeyRegistry& registry);

private:
    void Configure(QModelIndex index);
    void ClearAll();
    void PopupContextMenu(const QPoint& menu_location);

    std::unique_ptr<Ui::ConfigureControllerHotkeys> ui;

    QStandardItemModel* model;
};
