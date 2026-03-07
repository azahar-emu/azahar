// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <map>
#include <QAction>
#include <QKeySequence>
#include <QString>
#include "core/frontend/input.h"
#include "hotkey_monitor.h"

class QDialog;
class QSettings;
class QShortcut;
class QWidget;

struct Hotkey {
    QKeySequence keyseq;
    QString controller_keyseq;
    std::map<QString, QShortcut*> shortcuts;
    Qt::ShortcutContext context = Qt::WindowShortcut;
    std::unique_ptr<Input::ButtonDevice> button_device = nullptr;
    std::unique_ptr<Input::ButtonDevice> button_device2 = nullptr;
    QAction* action = nullptr;
};

class HotkeyRegistry final {
public:
    friend class ConfigureHotkeys;
    friend class ConfigureControllerHotkeys;

    explicit HotkeyRegistry();
    ~HotkeyRegistry();

    ControllerHotkeyMonitor buttonMonitor;

    /**
     * Loads hotkeys from the settings file.
     *
     * @note Yet unregistered hotkeys which are present in the settings will automatically be
     *       registered.
     */
    void LoadHotkeys();

    /**
     * Saves all registered hotkeys to the settings file.
     *
     * @note Each hotkey group will be stored a settings group; For each hotkey inside that group, a
     *       settings group will be created to store the key sequence and the hotkey context.
     */
    void SaveHotkeys();

    /**
     * Updates the button devices for a hotkey based on the controller_keyseq value
     */
    void UpdateControllerHotkey(QString name, Hotkey& hk);

    /**
     * Returns a QShortcut object whose activated() signal can be connected to other QObjects'
     * slots.
     *
     * @param group  General group this hotkey belongs to (e.g. "Main Window", "Debugger").
     * @param action Name of the action (e.g. "Start Emulation", "Load Image").
     * @param widget Parent widget of the returned QShortcut.
     * @warning If multiple QWidgets' call this function for the same action, the returned
     * QShortcut will be the same. Thus, you shouldn't rely on the caller really being the
     *          QShortcut's parent.
     */
    QShortcut* GetHotkey(const QString& group, const QString& action, QObject* widget);

    /**
     * Returns a QKeySequence object whose signal can be connected to QAction::setShortcut.
     *
     * @param group  General group this hotkey belongs to (e.g. "Main Window", "Debugger").
     * @param action Name of the action (e.g. "Start Emulation", "Load Image").
     */
    QKeySequence GetKeySequence(const QString& group, const QString& action);

    /**
     * Returns a Qt::ShortcutContext object who can be connected to other
     * QAction::setShortcutContext.
     *
     * @param group  General group this shortcut context belongs to (e.g. "Main Window",
     * "Debugger").
     * @param action Name of the action (e.g. "Start Emulation", "Load Image").
     */
    Qt::ShortcutContext GetShortcutContext(const QString& group, const QString& action);

    /**
     * Stores a QAction into the appropriate hotkey, for triggering by controller
     *
     * @param group General group this shortcut context belongs to
     * @param action_name Name of the action
     * @param action The QAction to store
     */
    void SetAction(const QString& group, const QString& action_name, QAction* action);

    /**
     * Takes a controller keysequene for a hotkey and returns a readable string
     *
     *
     */
    static QString SequenceToString(QString controller_keyseq);

private:
    using HotkeyMap = std::map<QString, Hotkey>;
    using HotkeyGroupMap = std::map<QString, HotkeyMap>;

    HotkeyGroupMap hotkey_groups;
};
