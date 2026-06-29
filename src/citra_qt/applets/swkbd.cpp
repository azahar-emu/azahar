// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <utility>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include "citra_qt/applets/swkbd.h"
#include "citra_qt/uisettings.h"
#include "common/param_package.h"
#include "common/settings.h"
#ifdef HAVE_SDL2
#include <SDL.h>
#endif

namespace {
constexpr int CONTROLLER_POLL_INTERVAL_MS = 16;
constexpr float CIRCLE_PAD_DIRECTION_THRESHOLD = 0.5f;

void PumpAppletInputEvents() {
#ifdef HAVE_SDL2
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_PumpEvents();
        SDL_GameControllerUpdate();
    }
#endif
}

std::unique_ptr<Input::ButtonDevice> CreateAppletButtonDevice(const std::string& params) {
    const Common::ParamPackage package(params);
    const auto engine = package.Get("engine", "");
    // Keyboard bindings are left to Qt text input so physical typing works even when
    // the active emulator input profile is controller-based.
    if (engine.empty() || engine == "keyboard") {
        return {};
    }
    return Input::CreateDevice<Input::ButtonDevice>(params);
}

std::unique_ptr<Input::AnalogDevice> CreateAppletAnalogDevice(const std::string& params) {
    const Common::ParamPackage package(params);
    const auto engine = package.Get("engine", "");
    if (engine.empty() || engine == "analog_from_button") {
        return {};
    }
    return Input::CreateDevice<Input::AnalogDevice>(params);
}

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

QString GetButtonText(const Frontend::KeyboardConfig& config, std::size_t index,
                      const char* fallback) {
    return index < config.button_text.size() && !config.button_text[index].empty()
               ? QString::fromStdString(config.button_text[index])
               : QObject::tr(fallback);
}

std::array<QString, 4> GetLetterRows(bool uppercase) {
    return {
        QStringLiteral("1234567890"),
        uppercase ? QStringLiteral("QWERTYUIOP") : QStringLiteral("qwertyuiop"),
        uppercase ? QStringLiteral("ASDFGHJKL") : QStringLiteral("asdfghjkl"),
        uppercase ? QStringLiteral("ZXCVBNM") : QStringLiteral("zxcvbnm"),
    };
}

std::array<QString, 4> GetNumberPadRows() {
    return {
        QStringLiteral("123"),
        QStringLiteral("456"),
        QStringLiteral("789"),
        QStringLiteral("0"),
    };
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

SoftwareKeyboardInputInterpreter::SoftwareKeyboardInputInterpreter() {
    using namespace Settings::NativeButton;

    const auto& profile_buttons = Settings::values.current_input_profile.buttons;
    buttons[ButtonA] = CreateAppletButtonDevice(profile_buttons[A]);
    buttons[ButtonB] = CreateAppletButtonDevice(profile_buttons[B]);
    buttons[ButtonUp] = CreateAppletButtonDevice(profile_buttons[Up]);
    buttons[ButtonDown] = CreateAppletButtonDevice(profile_buttons[Down]);
    buttons[ButtonLeft] = CreateAppletButtonDevice(profile_buttons[Left]);
    buttons[ButtonRight] = CreateAppletButtonDevice(profile_buttons[Right]);

    circle_pad = CreateAppletAnalogDevice(
        Settings::values.current_input_profile.analogs[Settings::NativeAnalog::CirclePad]);
}

std::vector<SoftwareKeyboardInputInterpreter::Action> SoftwareKeyboardInputInterpreter::Poll() {
    PumpAppletInputEvents();

    std::vector<Action> actions;
    const std::array<Action, NumButtons> button_actions{{
        Action::Accept,
        Action::CancelOrBackspace,
        Action::MoveUp,
        Action::MoveDown,
        Action::MoveLeft,
        Action::MoveRight,
    }};

    for (std::size_t index = 0; index < buttons.size(); ++index) {
        if (!buttons[index]) {
            continue;
        }
        const bool is_pressed = buttons[index]->GetStatus();
        if (is_pressed && !previous_button_state[index]) {
            actions.push_back(button_actions[index]);
        }
        previous_button_state[index] = is_pressed;
    }

    if (circle_pad) {
        const auto [x, y] = circle_pad->GetStatus();
        const std::array<std::pair<bool, Action>, NumDirections> direction_actions{{
            {y > CIRCLE_PAD_DIRECTION_THRESHOLD, Action::MoveUp},
            {y < -CIRCLE_PAD_DIRECTION_THRESHOLD, Action::MoveDown},
            {x < -CIRCLE_PAD_DIRECTION_THRESHOLD, Action::MoveLeft},
            {x > CIRCLE_PAD_DIRECTION_THRESHOLD, Action::MoveRight},
        }};

        for (std::size_t index = 0; index < direction_actions.size(); ++index) {
            const auto [is_pressed, action] = direction_actions[index];
            if (is_pressed && !previous_direction_state[index]) {
                actions.push_back(action);
            }
            previous_direction_state[index] = is_pressed;
        }
    }

    return actions;
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
    connect(line_edit, &QLineEdit::textChanged, this, [this] {
        UpdateLengthLabel();
        ClearValidationError();
    });
    length_label = new QLabel;
    validation_label = new QLabel;
    validation_label->setStyleSheet(QStringLiteral("color: palette(highlight);"));
    validation_label->setVisible(false);

    auto* const keys = new QGridLayout;
    const auto rows = IsNumberPad() ? GetNumberPadRows() : GetLetterRows(uppercase);

    for (int row = 0; row < std::size(rows); ++row) {
        std::vector<QPushButton*> key_button_row;
        std::vector<QPushButton*> button_row;
        for (int column = 0; column < rows[row].size(); ++column) {
            const QString value = rows[row].mid(column, 1);
            auto* const button = new QPushButton(value);
            button->setMinimumHeight(42);
            button->setFocusPolicy(Qt::NoFocus);
            connect(button, &QPushButton::clicked, this,
                    [this, button] { AppendText(button->text()); });
            keys->addWidget(button, row, column);
            key_button_row.push_back(button);
            button_row.push_back(button);
        }
        key_buttons.push_back(std::move(key_button_row));
        button_rows.push_back(std::move(button_row));
    }

    auto* const controls = new QHBoxLayout;
    shift_button = new QPushButton(tr("Shift"));
    shift_button->setCheckable(true);
    shift_button->setChecked(uppercase);
    symbols_button = new QPushButton(tr("Symbols"));
    shift_button->setEnabled(!IsNumberPad());
    symbols_button->setEnabled(!IsNumberPad());
    auto* const space = new QPushButton(tr("Space"));
    auto* const backspace = new QPushButton(tr("Backspace"));
    std::vector<QPushButton*> control_buttons{shift_button, symbols_button, space, backspace};

    QPushButton* ok = nullptr;
    QPushButton* cancel = nullptr;
    QPushButton* forgot = nullptr;

    switch (keyboard->config.button_config) {
    case Frontend::ButtonConfig::Triple:
        forgot = new QPushButton(GetButtonText(keyboard->config, 1, Frontend::SWKBD_BUTTON_FORGOT));
        [[fallthrough]];
    case Frontend::ButtonConfig::Dual:
        cancel = new QPushButton(GetButtonText(keyboard->config, 0, Frontend::SWKBD_BUTTON_CANCEL));
        [[fallthrough]];
    case Frontend::ButtonConfig::Single:
        ok = new QPushButton(GetButtonText(keyboard->config, 2, Frontend::SWKBD_BUTTON_OKAY));
        break;
    case Frontend::ButtonConfig::None:
        break;
    }
    if (cancel) {
        control_buttons.push_back(cancel);
    }
    if (forgot) {
        control_buttons.push_back(forgot);
    }
    if (ok) {
        control_buttons.push_back(ok);
    }

    for (auto* const button : control_buttons) {
        button->setMinimumHeight(42);
        button->setFocusPolicy(Qt::NoFocus);
    }

    default_button_palette = control_buttons.front()->palette();
    selected_button_palette = default_button_palette;
    selected_button_palette.setColor(QPalette::Button, palette().color(QPalette::Highlight));
    selected_button_palette.setColor(QPalette::ButtonText,
                                     palette().color(QPalette::HighlightedText));

    connect(shift_button, &QPushButton::clicked, this, [this] { ToggleCase(); });
    connect(symbols_button, &QPushButton::clicked, this, [this] { ToggleSymbols(); });
    connect(space, &QPushButton::clicked, this, [this] { AppendText(QStringLiteral(" ")); });
    connect(backspace, &QPushButton::clicked, this, [this] { Backspace(); });
    if (forgot) {
        connect(forgot, &QPushButton::clicked, this, [this] {
            button = QtKeyboard::forgot_id;
            accept();
        });
    }
    if (cancel) {
        connect(cancel, &QPushButton::clicked, this, [this] { Cancel(); });
    }
    if (ok) {
        connect(ok, &QPushButton::clicked, this, [this] { Submit(); });
    }

    button_rows.push_back(control_buttons);

    for (auto* const button : control_buttons) {
        controls->addWidget(button);
    }

    layout->addWidget(label);
    layout->addWidget(line_edit);
    layout->addWidget(length_label);
    layout->addWidget(validation_label);
    layout->addLayout(keys);
    layout->addLayout(controls);
    setLayout(layout);
    UpdateLengthLabel();
    SetSelectedButton(0, 0);
    line_edit->setFocus();

    controller_poll_timer = new QTimer(this);
    connect(controller_poll_timer, &QTimer::timeout, this,
            &QtSoftwareKeyboardDialog::PollControllerInput);
    controller_poll_timer->start(CONTROLLER_POLL_INTERVAL_MS);
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
}

void QtSoftwareKeyboardDialog::Backspace() {
    line_edit->backspace();
}

void QtSoftwareKeyboardDialog::Cancel() {
    button = QtKeyboard::cancel_id;
    accept();
}

void QtSoftwareKeyboardDialog::ToggleCase() {
    if (IsNumberPad()) {
        return;
    }

    uppercase = !uppercase;
    shift_button->setChecked(uppercase);
    if (!symbols) {
        UpdateKeyLabels();
    }
}

void QtSoftwareKeyboardDialog::ToggleSymbols() {
    if (IsNumberPad()) {
        return;
    }

    if (!symbols) {
        symbols = true;
        symbol_page = 0;
    } else if (symbol_page == 0) {
        symbol_page = 1;
    } else {
        symbols = false;
        symbol_page = 0;
    }

    UpdateKeyLabels();
    if (symbols && button_rows[selected_row][selected_column] == shift_button) {
        SetSelectedButton(static_cast<int>(button_rows.size()) - 1, 1);
    }
}

void QtSoftwareKeyboardDialog::UpdateKeyLabels() {
    const auto letter_rows = IsNumberPad() ? GetNumberPadRows() : GetLetterRows(uppercase);
    const std::array<std::array<QString, 4>, 2> symbol_rows{{
        {
            QStringLiteral("1234567890"),
            QStringLiteral("-_.,!?@#&/"),
            QStringLiteral("'\":;()[]\\"),
            QStringLiteral("+*=<>%~"),
        },
        {
            QStringLiteral("{}[]()<>|\\"),
            QStringLiteral("$%^`~+-=*/"),
            QStringLiteral("@#&.,?!:;"),
            QStringLiteral("'\"_`~/\\"),
        },
    }};
    const std::array<QString, 4>& rows = symbols ? symbol_rows[symbol_page] : letter_rows;

    for (int row = 0; row < static_cast<int>(key_buttons.size()); ++row) {
        for (int column = 0; column < static_cast<int>(key_buttons[row].size()); ++column) {
            key_buttons[row][column]->setText(rows[row].mid(column, 1));
        }
    }

    shift_button->setEnabled(!symbols);
    symbols_button->setText(symbols ? (symbol_page == 0 ? tr("More") : tr("ABC")) : tr("Symbols"));
}

bool QtSoftwareKeyboardDialog::IsNumberPad() const {
    return keyboard->config.type == Frontend::KeyboardType::NumPad;
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

void QtSoftwareKeyboardDialog::MoveSelection(int row_delta, int column_delta) {
    int next_row = selected_row + row_delta;
    if (next_row < 0) {
        next_row = static_cast<int>(button_rows.size()) - 1;
    } else if (next_row >= static_cast<int>(button_rows.size())) {
        next_row = 0;
    }

    const int row_size = static_cast<int>(button_rows[next_row].size());
    int next_column = selected_column;
    if (row_delta != 0) {
        const auto* const selected_button = button_rows[selected_row][selected_column];
        next_column = FindClosestColumnInRow(next_row, selected_button->geometry().center().x());
    } else {
        next_column += column_delta;
        if (next_column < 0) {
            next_column = row_size - 1;
        } else if (next_column >= row_size) {
            next_column = 0;
        }
    }

    SetSelectedButton(next_row, next_column);
}

int QtSoftwareKeyboardDialog::FindClosestColumnInRow(int row, int source_x) const {
    int closest_column = 0;
    int closest_distance = std::numeric_limits<int>::max();

    for (int column = 0; column < static_cast<int>(button_rows[row].size()); ++column) {
        const int distance = std::abs(button_rows[row][column]->geometry().center().x() - source_x);
        if (distance < closest_distance) {
            closest_column = column;
            closest_distance = distance;
        }
    }

    return closest_column;
}

void QtSoftwareKeyboardDialog::SetButtonControllerSelected(QPushButton* button, bool selected) {
    button->setAutoFillBackground(selected);
    button->setPalette(selected ? selected_button_palette : default_button_palette);
    button->update();
}

void QtSoftwareKeyboardDialog::SetSelectedButton(int row, int column) {
    if (!button_rows.empty()) {
        auto* const previous_button = button_rows[selected_row][selected_column];
        SetButtonControllerSelected(previous_button, false);
    }

    selected_row = row;
    selected_column = std::min(column, static_cast<int>(button_rows[selected_row].size()) - 1);

    auto* const selected_button = button_rows[selected_row][selected_column];
    SetButtonControllerSelected(selected_button, true);
}

void QtSoftwareKeyboardDialog::ActivateSelectedButton() {
    button_rows[selected_row][selected_column]->click();
}

void QtSoftwareKeyboardDialog::HandleInputAction(SoftwareKeyboardInputInterpreter::Action action) {
    using Action = SoftwareKeyboardInputInterpreter::Action;

    switch (action) {
    case Action::MoveUp:
        MoveSelection(-1, 0);
        break;
    case Action::MoveDown:
        MoveSelection(1, 0);
        break;
    case Action::MoveLeft:
        MoveSelection(0, -1);
        break;
    case Action::MoveRight:
        MoveSelection(0, 1);
        break;
    case Action::Accept:
        ActivateSelectedButton();
        break;
    case Action::CancelOrBackspace:
        if (!line_edit->text().isEmpty()) {
            Backspace();
        } else if (keyboard->config.button_config == Frontend::ButtonConfig::Dual ||
                   keyboard->config.button_config == Frontend::ButtonConfig::Triple) {
            Cancel();
        }
        break;
    }
}

void QtSoftwareKeyboardDialog::PollControllerInput() {
    for (const auto action : input_interpreter.Poll()) {
        HandleInputAction(action);
    }
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
