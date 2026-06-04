// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <vector>
#include <QDialog>
#include <QValidator>
#include "core/frontend/applets/swkbd.h"

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;
class QtKeyboard;

class QtKeyboardValidator final : public QValidator {
public:
    explicit QtKeyboardValidator(QtKeyboard* keyboard);
    State validate(QString& input, int& pos) const override;

private:
    QtKeyboard* keyboard;
};

class QtKeyboardDialog final : public QDialog {
    Q_OBJECT

public:
    QtKeyboardDialog(QWidget* parent, QtKeyboard* keyboard);
    void Submit();

private:
    QDialogButtonBox* buttons;
    QLabel* label;
    QLineEdit* line_edit;
    QVBoxLayout* layout;
    QtKeyboard* keyboard;
    QString text;
    u8 button;

    friend class QtKeyboard;
};

class QtSoftwareKeyboardDialog final : public QDialog {
    Q_OBJECT

public:
    QtSoftwareKeyboardDialog(QWidget* parent, QtKeyboard* keyboard);

private:
    void AppendText(const QString& value);
    void Backspace();
    void Cancel();
    void Submit();
    void ToggleCase();
    void UpdateLengthLabel();
    void ShowInlineValidationError(Frontend::ValidationError error);
    void ClearValidationError();

    QLineEdit* line_edit;
    QLabel* length_label;
    QLabel* validation_label;
    QPushButton* shift_button;
    QtKeyboard* keyboard;
    QString text;
    u8 button;
    std::vector<QPushButton*> letter_buttons;
    bool uppercase = true;

    friend class QtKeyboard;
};

class QtKeyboard final : public QObject, public Frontend::SoftwareKeyboard {
    Q_OBJECT

public:
    explicit QtKeyboard(QWidget& parent);
    void Execute(const Frontend::KeyboardConfig& config) override;
    void ShowError(const std::string& error) override;

private:
    Q_INVOKABLE void OpenInputDialog();
    Q_INVOKABLE void OpenSoftwareKeyboardDialog();
    Q_INVOKABLE void ShowErrorDialog(QString message);

    /// Index of the buttons
    u8 ok_id;
    static constexpr u8 forgot_id = 1;
    static constexpr u8 cancel_id = 0;

    QWidget& parent;

    std::string result_text;
    int result_button;

    friend class QtKeyboardDialog;
    friend class QtSoftwareKeyboardDialog;
    friend class QtKeyboardValidator;
};
