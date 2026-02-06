// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QMenu>
#include <QMessageBox>
#include <QStandardItemModel>
#include "citra_qt/configuration/config.h"
#include "citra_qt/configuration/configure_hotkeys_controller.h"
#include "citra_qt/hotkeys.h"
#include "citra_qt/util/sequence_dialog/controller_sequence_dialog.h"
#include "ui_configure_hotkeys_controller.h"

constexpr int name_column = 0;
constexpr int readable_hotkey_column = 1;
constexpr int hotkey_column = 2;

ConfigureControllerHotkeys::ConfigureControllerHotkeys(QWidget* parent)
    : QWidget(parent), ui(std::make_unique<Ui::ConfigureControllerHotkeys>()) {
    ui->setupUi(this);
    setFocusPolicy(Qt::ClickFocus);

    model = new QStandardItemModel(this);
    model->setColumnCount(2);
    model->setHorizontalHeaderLabels({tr("Action"), tr("Controller Hotkey")});
    // TODO: re-enable and get profiles workin
    ui->profileGroup->setEnabled(false);
    connect(ui->hotkey_list, &QTreeView::doubleClicked, this,
            &ConfigureControllerHotkeys::Configure);
    connect(ui->hotkey_list, &QTreeView::customContextMenuRequested, this,
            &ConfigureControllerHotkeys::PopupContextMenu);
    ui->hotkey_list->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->hotkey_list->setModel(model);

    ui->hotkey_list->setColumnWidth(0, 300);
    ui->hotkey_list->resizeColumnToContents(readable_hotkey_column);

    connect(ui->button_clear_all, &QPushButton::clicked, this,
            &ConfigureControllerHotkeys::ClearAll);
}

ConfigureControllerHotkeys::~ConfigureControllerHotkeys() = default;

void ConfigureControllerHotkeys::Populate(const HotkeyRegistry& registry) {
    for (const auto& group : registry.hotkey_groups) {
        QStandardItem* parent_item = new QStandardItem(group.first);
        parent_item->setEditable(false);
        for (const auto& hotkey : group.second) {
            QStandardItem* action = new QStandardItem(hotkey.first);
            QStandardItem* controller_keyseq = new QStandardItem(hotkey.second.controller_keyseq);
            QStandardItem* readable_keyseq =
                new QStandardItem(CleanSequence(hotkey.second.controller_keyseq));
            action->setEditable(false);
            controller_keyseq->setEditable(false);
            parent_item->appendRow({action, readable_keyseq, controller_keyseq});
        }
        model->appendRow(parent_item);
    }

    ui->hotkey_list->expandAll();
}

void ConfigureControllerHotkeys::Configure(QModelIndex index) {
    if (!index.parent().isValid()) {
        return;
    }

    // Swap to the hotkey column
    index = index.sibling(index.row(), hotkey_column);
    QModelIndex readableIndex = index.sibling(index.row(), readable_hotkey_column);

    const auto previous_key = model->data(index);

    ControllerSequenceDialog hotkey_dialog{this};

    const int return_code = hotkey_dialog.exec();
    const auto key_sequence = hotkey_dialog.GetSequence();
    if (return_code == QDialog::Rejected || key_sequence.isEmpty()) {
        return;
    }
    model->setData(index, key_sequence);
    model->setData(readableIndex, CleanSequence(key_sequence));
}

void ConfigureControllerHotkeys::ApplyConfiguration(HotkeyRegistry& registry) {
    for (int key_id = 0; key_id < model->rowCount(); key_id++) {
        QStandardItem* parent = model->item(key_id, 0);
        for (int key_column_id = 0; key_column_id < parent->rowCount(); key_column_id++) {
            const QStandardItem* action = parent->child(key_column_id, name_column);
            const QStandardItem* controller_keyseq = parent->child(key_column_id, hotkey_column);
            for (auto& [group, sub_actions] : registry.hotkey_groups) {
                if (group != parent->text())
                    continue;
                for (auto& [action_name, hotkey] : sub_actions) {
                    if (action_name != action->text())
                        continue;
                    hotkey.controller_keyseq = controller_keyseq->text();
                }
            }
        }
    }

    registry.SaveHotkeys();
}

void ConfigureControllerHotkeys::ClearAll() {
    for (int r = 0; r < model->rowCount(); ++r) {
        const QStandardItem* parent = model->item(r, 0);

        for (int r2 = 0; r2 < parent->rowCount(); ++r2) {
            model->item(r, 0)->child(r2, readable_hotkey_column)->setText(QString{});
            model->item(r, 0)->child(r2, hotkey_column)->setText(QString{});
        }
    }
}

void ConfigureControllerHotkeys::PopupContextMenu(const QPoint& menu_location) {
    const auto index = ui->hotkey_list->indexAt(menu_location);
    if (!index.parent().isValid()) {
        return;
    }

    QMenu context_menu;
    QAction* clear = context_menu.addAction(tr("Clear"));

    const auto readable_hotkey_index = index.sibling(index.row(), readable_hotkey_column);
    const auto hotkey_index = index.sibling(index.row(), hotkey_column);
    connect(clear, &QAction::triggered, this, [this, hotkey_index, readable_hotkey_index] {
        model->setData(hotkey_index, QString{});
        model->setData(readable_hotkey_index, QString{});
    });

    context_menu.exec(ui->hotkey_list->viewport()->mapToGlobal(menu_location));
}

void ConfigureControllerHotkeys::RetranslateUI() {
    ui->retranslateUi(this);
}

QString ConfigureControllerHotkeys::CleanSequence(QString controller_keyseq) {
    if (controller_keyseq.isEmpty())
        return controller_keyseq;
    QStringList keys = controller_keyseq.split(QStringLiteral("||"));

    QString output = QStringLiteral("Button ").append(
        keys.value(0).split(QStringLiteral(",")).value(0).split(QStringLiteral(":")).value(1));

    if (keys.length() > 1) {
        output.append(QStringLiteral(" + Button "))
            .append(keys.value(1)
                        .split(QStringLiteral(","))
                        .value(0)
                        .split(QStringLiteral(":"))
                        .value(1));
    }
    return output;
}
