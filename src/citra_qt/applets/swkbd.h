// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <array>
#include <memory>
#include <vector>
#include <QDialog>
#include <QValidator>
#include "core/frontend/applets/swkbd.h"
#include "core/frontend/input.h"

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QVBoxLayout;
class QtKeyboard;

class QtKeyboardValidator final : public QValidator {
public:
    explicit QtKeyboardValidator(QtKeyboard* keyboard);
    State validate(QString& input, int& pos) const override;

private:
    QtKeyboard* keyboard;
};

class SoftwareKeyboardInputInterpreter {
public:
    enum class Action {
        MoveUp,
        MoveDown,
        MoveLeft,
        MoveRight,
        Accept,
        CancelOrBackspace,
    };

    SoftwareKeyboardInputInterpreter();
    std::vector<Action> Poll();

private:
    enum Button {
        ButtonA,
        ButtonB,
        ButtonUp,
        ButtonDown,
        ButtonLeft,
        ButtonRight,
        NumButtons,
    };

    enum Direction {
        DirectionUp,
        DirectionDown,
        DirectionLeft,
        DirectionRight,
        NumDirections,
    };

    std::array<std::unique_ptr<Input::ButtonDevice>, NumButtons> buttons;
    std::unique_ptr<Input::AnalogDevice> circle_pad;
    std::array<bool, NumButtons> previous_button_state{};
    std::array<bool, NumDirections> previous_direction_state{};
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
    void MoveSelection(int row_delta, int column_delta);
    void SetSelectedButton(int row, int column);
    void ActivateSelectedButton();
    void HandleInputAction(SoftwareKeyboardInputInterpreter::Action action);
    void PollControllerInput();

    QLineEdit* line_edit;
    QLabel* length_label;
    QLabel* validation_label;
    QPushButton* shift_button;
    QTimer* controller_poll_timer;
    QtKeyboard* keyboard;
    QString text;
    u8 button;
    std::vector<std::vector<QPushButton*>> button_rows;
    std::vector<QPushButton*> letter_buttons;
    SoftwareKeyboardInputInterpreter input_interpreter;
    int selected_row = 0;
    int selected_column = 0;
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
