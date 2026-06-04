// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <iterator>
#include <unordered_map>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include "citra_qt/applets/swkbd.h"
#include "citra_qt/uisettings.h"

namespace {
QString GetValidationErrorMessage(Frontend::ValidationError error, int max_text_length) {
    using namespace Frontend;
    const std::unordered_map<ValidationError, QString> validation_error_messages = {
        {ValidationError::FixedLengthRequired,
         QObject::tr("Text length is not correct (should be %1 characters)").arg(max_text_length)},
        {ValidationError::MaxLengthExceeded,
         QObject::tr("Text is too long (should be no more than %1 characters)")
             .arg(max_text_length)},
        {ValidationError::BlankInputNotAllowed, QObject::tr("Blank input is not allowed")},
        {ValidationError::EmptyInputNotAllowed, QObject::tr("Empty input is not allowed")},
    };
    const auto message = validation_error_messages.find(error);
    return message == validation_error_messages.end()
               ? QObject::tr("Input does not match the requested software keyboard format")
               : message->second;
}

void ShowValidationError(QWidget* parent, Frontend::ValidationError error, int max_text_length) {
    QMessageBox::critical(parent, QObject::tr("Validation error"),
                          GetValidationErrorMessage(error, max_text_length));
}

} // Anonymous namespace

QtKeyboardValidator::QtKeyboardValidator(QtKeyboard* keyboard_) : keyboard(keyboard_) {}

QtKeyboardValidator::State QtKeyboardValidator::validate(QString& input, int& pos) const {
    if (keyboard->ValidateFilters(input.toStdString()) == Frontend::ValidationError::None) {
        if (input.size() > keyboard->config.max_text_length)
            return State::Invalid;
        return State::Acceptable;
    } else {
        return State::Invalid;
    }
}

QtKeyboardDialog::QtKeyboardDialog(QWidget* parent, QtKeyboard* keyboard_)
    : QDialog(parent), keyboard(keyboard_) {
    using namespace Frontend;
    const auto config = keyboard->config;
    layout = new QVBoxLayout;
    label = new QLabel(QString::fromStdString(config.hint_text));
    line_edit = new QLineEdit;
    line_edit->setValidator(new QtKeyboardValidator(keyboard));
    buttons = new QDialogButtonBox;
    // Initialize buttons
    switch (config.button_config) {
    case ButtonConfig::Triple:
        buttons->addButton(config.button_text[1].empty()
                               ? tr(SWKBD_BUTTON_FORGOT)
                               : QString::fromStdString(config.button_text[1]),
                           QDialogButtonBox::ButtonRole::HelpRole);
        [[fallthrough]];
    case ButtonConfig::Dual:
        buttons->addButton(config.button_text[0].empty()
                               ? tr(SWKBD_BUTTON_CANCEL)
                               : QString::fromStdString(config.button_text[0]),
                           QDialogButtonBox::ButtonRole::RejectRole);
        [[fallthrough]];
    case ButtonConfig::Single:
        buttons->addButton(config.button_text[2].empty()
                               ? tr(SWKBD_BUTTON_OKAY)
                               : QString::fromStdString(config.button_text[2]),
                           QDialogButtonBox::ButtonRole::AcceptRole);
        break;
    case ButtonConfig::None:
        break;
    }
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { Submit(); });
    connect(buttons, &QDialogButtonBox::rejected, this, [this] {
        button = QtKeyboard::cancel_id;
        accept();
    });
    connect(buttons, &QDialogButtonBox::helpRequested, this, [this] {
        button = QtKeyboard::forgot_id;
        accept();
    });
    layout->addWidget(label);
    layout->addWidget(line_edit);
    layout->addWidget(buttons);
    setLayout(layout);
}

void QtKeyboardDialog::Submit() {
    auto error = keyboard->ValidateInput(line_edit->text().toStdString());
    if (error != Frontend::ValidationError::None) {
        ShowValidationError(this, error, keyboard->config.max_text_length);
    } else {
        button = keyboard->ok_id;
        text = line_edit->text();
        accept();
    }
}

QtSoftwareKeyboardDialog::QtSoftwareKeyboardDialog(QWidget* parent, QtKeyboard* keyboard_)
    : QDialog(parent), keyboard(keyboard_) {
    setWindowTitle(tr("Software Keyboard"));
    setMinimumWidth(560);

    auto* const layout = new QVBoxLayout;
    auto* const label = new QLabel(QString::fromStdString(keyboard->config.hint_text));
    line_edit = new QLineEdit;
    line_edit->setMinimumHeight(34);
    line_edit->setValidator(new QtKeyboardValidator(keyboard));
    length_label = new QLabel;
    validation_label = new QLabel;
    validation_label->setStyleSheet(QStringLiteral("color: palette(highlight);"));
    validation_label->setVisible(false);

    auto* const keys = new QGridLayout;
    const QString rows[] = {
        QStringLiteral("1234567890"),
        QStringLiteral("QWERTYUIOP"),
        QStringLiteral("ASDFGHJKL"),
        QStringLiteral("ZXCVBNM"),
    };

    for (int row = 0; row < std::size(rows); ++row) {
        for (int column = 0; column < rows[row].size(); ++column) {
            const QString value = rows[row].mid(column, 1);
            auto* const button = new QPushButton(value);
            button->setMinimumHeight(42);
            button->setFocusPolicy(Qt::NoFocus);
            if (value.front().isLetter()) {
                letter_buttons.push_back(button);
            }
            connect(button, &QPushButton::clicked, this, [this, button] {
                AppendText(button->text());
            });
            keys->addWidget(button, row, column);
        }
    }

    auto* const controls = new QHBoxLayout;
    shift_button = new QPushButton(tr("Shift"));
    shift_button->setCheckable(true);
    shift_button->setChecked(uppercase);
    auto* const space = new QPushButton(tr("Space"));
    auto* const backspace = new QPushButton(tr("Backspace"));
    auto* const ok = new QPushButton(tr(Frontend::SWKBD_BUTTON_OKAY));
    auto* const cancel = new QPushButton(tr(Frontend::SWKBD_BUTTON_CANCEL));

    for (auto* const button : {shift_button, space, backspace, ok, cancel}) {
        button->setMinimumHeight(42);
        button->setFocusPolicy(Qt::NoFocus);
    }

    connect(shift_button, &QPushButton::clicked, this, [this] { ToggleCase(); });
    connect(space, &QPushButton::clicked, this, [this] { AppendText(QStringLiteral(" ")); });
    connect(backspace, &QPushButton::clicked, this, [this] { Backspace(); });
    connect(ok, &QPushButton::clicked, this, [this] { Submit(); });
    connect(cancel, &QPushButton::clicked, this, [this] { Cancel(); });

    controls->addWidget(shift_button);
    controls->addWidget(space);
    controls->addWidget(backspace);
    controls->addWidget(ok);
    controls->addWidget(cancel);

    layout->addWidget(label);
    layout->addWidget(line_edit);
    layout->addWidget(length_label);
    layout->addWidget(validation_label);
    layout->addLayout(keys);
    layout->addLayout(controls);
    setLayout(layout);
    UpdateLengthLabel();
    line_edit->setFocus();
}

void QtSoftwareKeyboardDialog::AppendText(const QString& value) {
    QString next = line_edit->text();
    const int cursor_position = line_edit->cursorPosition();
    next.insert(cursor_position, value);
    if (next.size() > keyboard->config.max_text_length) {
        return;
    }
    line_edit->setText(next);
    line_edit->setCursorPosition(cursor_position + value.size());
    UpdateLengthLabel();
    ClearValidationError();
}

void QtSoftwareKeyboardDialog::Backspace() {
    line_edit->backspace();
    UpdateLengthLabel();
    ClearValidationError();
}

void QtSoftwareKeyboardDialog::Cancel() {
    button = QtKeyboard::cancel_id;
    accept();
}

void QtSoftwareKeyboardDialog::ToggleCase() {
    uppercase = !uppercase;
    for (auto* const button : letter_buttons) {
        button->setText(uppercase ? button->text().toUpper() : button->text().toLower());
    }
    shift_button->setChecked(uppercase);
}

void QtSoftwareKeyboardDialog::UpdateLengthLabel() {
    length_label->setText(
        tr("%1 / %2").arg(line_edit->text().size()).arg(keyboard->config.max_text_length));
}

void QtSoftwareKeyboardDialog::Submit() {
    auto error = keyboard->ValidateInput(line_edit->text().toStdString());
    if (error != Frontend::ValidationError::None) {
        ShowInlineValidationError(error);
    } else {
        button = keyboard->ok_id;
        text = line_edit->text();
        accept();
    }
}

void QtSoftwareKeyboardDialog::ShowInlineValidationError(Frontend::ValidationError error) {
    validation_label->setText(GetValidationErrorMessage(error, keyboard->config.max_text_length));
    validation_label->setVisible(true);
}

void QtSoftwareKeyboardDialog::ClearValidationError() {
    validation_label->clear();
    validation_label->setVisible(false);
}
QtKeyboard::QtKeyboard(QWidget& parent_) : parent(parent_) {}

void QtKeyboard::Execute(const Frontend::KeyboardConfig& config) {
    SoftwareKeyboard::Execute(config);
    if (this->config.button_config != Frontend::ButtonConfig::None) {
        ok_id = static_cast<u8>(this->config.button_config);
    }
    if (UISettings::values.use_on_screen_software_keyboard.GetValue()) {
        QMetaObject::invokeMethod(this, "OpenSoftwareKeyboardDialog", Qt::BlockingQueuedConnection);
    } else {
        QMetaObject::invokeMethod(this, "OpenInputDialog", Qt::BlockingQueuedConnection);
    }
    Finalize(result_text, result_button);
}

void QtKeyboard::ShowError(const std::string& error) {
    QString message = QString::fromStdString(error);
    QMetaObject::invokeMethod(this, "ShowErrorDialog", Qt::BlockingQueuedConnection,
                              Q_ARG(QString, message));
}

void QtKeyboard::OpenInputDialog() {
    QtKeyboardDialog dialog(&parent, this);
    dialog.setWindowFlags(dialog.windowFlags() &
                          ~(Qt::WindowCloseButtonHint | Qt::WindowContextHelpButtonHint));
    dialog.setWindowModality(Qt::WindowModal);
    dialog.exec();

    result_text = dialog.text.toStdString();
    result_button = dialog.button;
    LOG_INFO(Frontend, "SWKBD input dialog finished, text={}, button={}", result_text,
             result_button);
}

void QtKeyboard::OpenSoftwareKeyboardDialog() {
    QtSoftwareKeyboardDialog dialog(&parent, this);
    dialog.setWindowFlags(dialog.windowFlags() &
                          ~(Qt::WindowCloseButtonHint | Qt::WindowContextHelpButtonHint));
    dialog.setWindowModality(Qt::WindowModal);
    dialog.exec();

    result_text = dialog.text.toStdString();
    result_button = dialog.button;
    LOG_INFO(Frontend, "SWKBD software keyboard dialog finished, text={}, button={}", result_text,
             result_button);
}

void QtKeyboard::ShowErrorDialog(QString message) {
    QMessageBox::critical(&parent, tr("Software Keyboard"), message);
}
