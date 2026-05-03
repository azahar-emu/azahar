// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QFile>
#include "citra_qt/dev_ipc_server.h"
#include "citra_qt/citra_qt.h"
#include "common/logging/log.h"

namespace {
#ifdef AZAHAR_IPC_SOCKET_NAME
constexpr const char* SOCKET_NAME = AZAHAR_IPC_SOCKET_NAME;
#else
constexpr const char* SOCKET_NAME = "azahar-dev-ipc";
#endif
} // namespace

DevIpcServer::DevIpcServer(GMainWindow* main_window, QObject* parent)
    : QObject(parent), main_window_(main_window) {
    server_ = new QLocalServer(this);
    connect(server_, &QLocalServer::newConnection, this, &DevIpcServer::OnNewConnection);
}

DevIpcServer::~DevIpcServer() {
    if (server_ && server_->isListening()) {
        server_->close();
    }
}

bool DevIpcServer::Start() {
    if (server_->isListening()) {
        return true;
    }

    QLocalServer::removeServer(QString::fromLatin1(SOCKET_NAME));

    if (!server_->listen(QString::fromLatin1(SOCKET_NAME))) {
        LOG_ERROR(Frontend, "DevIpcServer: Failed to listen: {}",
                  server_->errorString().toStdString());
        return false;
    }

#ifdef _WIN32
    LOG_INFO(Frontend, "DevIpcServer: Listening on \\\\.\\pipe\\{}", SOCKET_NAME);
#else
    LOG_INFO(Frontend, "DevIpcServer: Listening on {}", SOCKET_NAME);
#endif
    return true;
}

void DevIpcServer::Stop() {
    if (server_->isListening()) {
        server_->close();
        LOG_INFO(Frontend, "DevIpcServer: Stopped");
    }
    pending_reload_socket_ = nullptr;
    reload_in_progress_ = false;
    read_buffers_.clear();
}

void DevIpcServer::OnNewConnection() {
    while (QLocalSocket* socket = server_->nextPendingConnection()) {
        if (read_buffers_.size() >= MAX_CONNECTIONS) {
            LOG_WARNING(Frontend, "DevIpcServer: Rejecting connection (limit {})",
                        MAX_CONNECTIONS);
            socket->disconnectFromServer();
            socket->deleteLater();
            continue;
        }
        read_buffers_[socket] = {};
        connect(socket, &QLocalSocket::readyRead, this, &DevIpcServer::OnReadyRead);
        connect(socket, &QLocalSocket::disconnected, this, &DevIpcServer::OnDisconnected);
    }
}

void DevIpcServer::OnReadyRead() {
    auto* socket = qobject_cast<QLocalSocket*>(sender());
    if (!socket) {
        return;
    }

    QByteArray& buffer = read_buffers_[socket];
    buffer.append(socket->readAll());

    if (buffer.size() > MAX_BUFFER_SIZE) {
        LOG_WARNING(Frontend, "DevIpcServer: Client exceeded buffer limit, disconnecting");
        read_buffers_.remove(socket);
        socket->disconnectFromServer();
        socket->deleteLater();
        return;
    }

    while (buffer.contains('\n')) {
        const int newline_pos = buffer.indexOf('\n');
        const QByteArray line_bytes = buffer.left(newline_pos).trimmed();
        buffer.remove(0, newline_pos + 1);

        if (!line_bytes.isEmpty()) {
            HandleCommand(socket, QString::fromUtf8(line_bytes));
        }
    }
}

void DevIpcServer::OnDisconnected() {
    auto* socket = qobject_cast<QLocalSocket*>(sender());
    if (!socket) {
        return;
    }

    if (socket == pending_reload_socket_) {
        pending_reload_socket_ = nullptr;
        LOG_WARNING(Frontend, "DevIpcServer: Hot-reload client disconnected before completion");
    }

    read_buffers_.remove(socket);
    socket->deleteLater();
}

QString DevIpcServer::ParseFlags(const QString& args, bool& purge, bool& wipe) {
    const QStringList tokens = args.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList path_parts;
    purge = false;
    wipe = false;

    for (const auto& token : tokens) {
        if (token == QStringLiteral("--purge")) {
            purge = true;
        } else if (token == QStringLiteral("--wipe")) {
            wipe = true;
        } else {
            path_parts.append(token);
        }
    }

    return path_parts.join(QLatin1Char(' '));
}

void DevIpcServer::HandleCommand(QLocalSocket* socket, const QString& command) {
    LOG_DEBUG(Frontend, "DevIpcServer: Received command: {}", command.toStdString());

    if (command == QStringLiteral("PING")) {
        SendResponse(socket, QStringLiteral("OK"));
    } else if (command == QStringLiteral("STATUS")) {
        SendResponse(socket, GetStatus());
    } else if (command == QStringLiteral("SHUTDOWN")) {
        if (main_window_->IsEmulationRunning()) {
            emit ShutdownRequested();
            SendResponse(socket, QStringLiteral("OK"));
        } else {
            SendResponse(socket, QStringLiteral("ERR:not-running"));
        }
    } else if (command.startsWith(QStringLiteral("HOT_RELOAD "))) {
        if (reload_in_progress_) {
            SendResponse(socket, QStringLiteral("ERR:reload-in-progress"));
            return;
        }

        bool purge, wipe;
        const QString file_path = ParseFlags(command.mid(11), purge, wipe);

        if (file_path.isEmpty()) {
            SendResponse(socket, QStringLiteral("ERR:missing-file-path"));
            return;
        }

        if (!QFile::exists(file_path)) {
            SendResponse(socket, QStringLiteral("ERR:file-not-found"));
            return;
        }

        pending_reload_path_ = file_path;
        pending_reload_purge_ = purge;
        pending_reload_wipe_ = wipe;

        reload_in_progress_ = true;
        pending_reload_socket_ = socket;
        emit HotReloadRequested(file_path, purge, wipe);
    } else if (command == QStringLiteral("HOT_RELOAD_LAST")) {
        if (reload_in_progress_) {
            SendResponse(socket, QStringLiteral("ERR:reload-in-progress"));
            return;
        }
        if (last_reload_path_.isEmpty()) {
            SendResponse(socket, QStringLiteral("ERR:no-previous-reload"));
            return;
        }
        if (!QFile::exists(last_reload_path_)) {
            SendResponse(socket, QStringLiteral("ERR:file-not-found"));
            return;
        }

        pending_reload_path_ = last_reload_path_;
        pending_reload_purge_ = last_reload_purge_;
        pending_reload_wipe_ = last_reload_wipe_;

        reload_in_progress_ = true;
        pending_reload_socket_ = socket;
        emit HotReloadRequested(last_reload_path_, last_reload_purge_, last_reload_wipe_);
    } else {
        SendResponse(socket, QStringLiteral("ERR:unknown-command"));
    }
}

void DevIpcServer::OnHotReloadComplete(bool success, const QString& error) {
    reload_in_progress_ = false;

    if (success) {
        last_reload_path_ = pending_reload_path_;
        last_reload_purge_ = pending_reload_purge_;
        last_reload_wipe_ = pending_reload_wipe_;
    }

    if (pending_reload_socket_ &&
        pending_reload_socket_->state() == QLocalSocket::ConnectedState) {
        if (success) {
            SendResponse(pending_reload_socket_, QStringLiteral("OK"));
        } else {
            SendResponse(pending_reload_socket_, QStringLiteral("ERR:") + error);
        }
    }
    pending_reload_socket_ = nullptr;
}

void DevIpcServer::SendResponse(QLocalSocket* socket, const QString& response) {
    if (!socket || socket->state() != QLocalSocket::ConnectedState) {
        return;
    }
    socket->write((response + QStringLiteral("\n")).toUtf8());
    socket->flush();
}

QString DevIpcServer::GetStatus() const {
    if (main_window_->IsEmulationRunning()) {
        return QStringLiteral("RUNNING %1")
            .arg(main_window_->GetGameTitleId(), 16, 16, QLatin1Char('0'));
    }
    return QStringLiteral("IDLE");
}
