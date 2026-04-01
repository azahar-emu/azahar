// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <QHash>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QString>

class GMainWindow;

/// Developer IPC server for hot-reload and remote control via azahar-ctl.
class DevIpcServer : public QObject {
    Q_OBJECT

public:
    explicit DevIpcServer(GMainWindow* main_window, QObject* parent = nullptr);
    ~DevIpcServer() override;

    bool Start();
    void Stop();

    bool IsReloadInProgress() const {
        return reload_in_progress_;
    }

signals:
    void HotReloadRequested(const QString& file_path, bool purge, bool wipe_saves);
    void ShutdownRequested();

public slots:
    void OnHotReloadComplete(bool success, const QString& error);

private slots:
    void OnNewConnection();
    void OnReadyRead();
    void OnDisconnected();

private:
    void HandleCommand(QLocalSocket* socket, const QString& command);
    void SendResponse(QLocalSocket* socket, const QString& response);
    QString GetStatus() const;

    static QString ParseFlags(const QString& args, bool& purge, bool& wipe);

    static constexpr int MAX_BUFFER_SIZE = 65536;
    static constexpr int MAX_CONNECTIONS = 16;

    QLocalServer* server_ = nullptr;
    GMainWindow* main_window_ = nullptr;
    QHash<QLocalSocket*, QByteArray> read_buffers_;
    QPointer<QLocalSocket> pending_reload_socket_;
    bool reload_in_progress_ = false;

    // In-flight reload params, committed to last_reload_* on success.
    QString pending_reload_path_;
    bool pending_reload_purge_ = false;
    bool pending_reload_wipe_ = false;

    // Last successful reload params for HOT_RELOAD_LAST.
    QString last_reload_path_;
    bool last_reload_purge_ = false;
    bool last_reload_wipe_ = false;
};
