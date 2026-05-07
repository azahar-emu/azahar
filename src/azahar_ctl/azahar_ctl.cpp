// Copyright Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <cstdio>
#include <cstdlib>

#ifdef AZAHAR_IPC_SOCKET_NAME
static constexpr const char* SOCKET_NAME = AZAHAR_IPC_SOCKET_NAME;
#else
static constexpr const char* SOCKET_NAME = "azahar-dev-ipc";
#endif
static constexpr int CONNECT_TIMEOUT_MS = 3000;
static constexpr int DEFAULT_TIMEOUT_MS = 5000;
static constexpr int RELOAD_TIMEOUT_MS = 60000;

// Exit codes
static constexpr int EXIT_OK = 0;
static constexpr int EXIT_USAGE = 1;
static constexpr int EXIT_CONNECT_FAILED = 2;
static constexpr int EXIT_TIMEOUT = 3;
static constexpr int EXIT_SERVER_ERROR = 4;

static void PrintUsage() {
    std::fprintf(stderr, "Usage: azahar-ctl <command> [args...]\n\n");
    std::fprintf(stderr, "Commands:\n");
    std::fprintf(stderr, "  ping                              Check if Azahar is running\n");
    std::fprintf(stderr, "  status                            Show emulation state\n");
    std::fprintf(stderr, "  shutdown                          Stop current game\n");
    std::fprintf(stderr, "  hot-reload <file> [--purge] [--wipe]\n");
    std::fprintf(stderr,
                 "                                    Reload cycle (CIA/3DSX/ELF/3DS/CXI/APP)\n");
    std::fprintf(stderr,
                 "                                    --purge  Uninstall all titles before CIA "
                 "install\n");
    std::fprintf(stderr,
                 "                                    --wipe   Delete save data for the replaced "
                 "title\n");
    std::fprintf(stderr, "  hot-reload --last                 Re-run the previous hot-reload\n");
    std::fprintf(stderr, "  help                              Show this help message\n");
    std::fprintf(stderr, "\nOptions:\n");
    std::fprintf(stderr,
                 "  --timeout <ms>                    Override response timeout (default: "
                 "5000/60000)\n");
    std::fprintf(stderr, "\nExit codes:\n");
    std::fprintf(stderr, "  0  Success\n");
    std::fprintf(stderr, "  1  Usage error\n");
    std::fprintf(stderr, "  2  Cannot connect to Azahar\n");
    std::fprintf(stderr, "  3  Timeout waiting for response\n");
    std::fprintf(stderr, "  4  Server returned an error\n");
}

static bool ReadSingleLine(QLocalSocket& socket, int timeout_ms, QString& out) {
    QByteArray buffer;
    QElapsedTimer timer;
    timer.start();

    while (true) {
        const int remaining = timeout_ms - static_cast<int>(timer.elapsed());
        if (remaining <= 0) {
            break;
        }
        if (!socket.waitForReadyRead(remaining)) {
            break;
        }
        buffer.append(socket.readAll());
        if (buffer.contains('\n')) {
            out = QString::fromUtf8(buffer).trimmed();
            return true;
        }
    }
    if (buffer.contains('\n')) {
        out = QString::fromUtf8(buffer).trimmed();
        return true;
    }
    return false;
}

static int ExtractTimeout(int& argc, char* argv[], int default_timeout) {
    int timeout = default_timeout;
    for (int i = 1; i < argc - 1; i++) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--timeout")) {
            timeout = std::atoi(argv[i + 1]);
            if (timeout <= 0) {
                timeout = default_timeout;
            }
            for (int j = i; j < argc - 2; j++) {
                argv[j] = argv[j + 2];
            }
            argc -= 2;
            break;
        }
    }
    return timeout;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        PrintUsage();
        return EXIT_USAGE;
    }

    const QString verb = QString::fromLocal8Bit(argv[1]);

    if (verb == QStringLiteral("help") || verb == QStringLiteral("--help") ||
        verb == QStringLiteral("-h")) {
        PrintUsage();
        return EXIT_OK;
    }

    int custom_timeout = ExtractTimeout(argc, argv, 0);
    QString command;

    if (verb == QStringLiteral("ping")) {
        command = QStringLiteral("PING");
    } else if (verb == QStringLiteral("status")) {
        command = QStringLiteral("STATUS");
    } else if (verb == QStringLiteral("shutdown")) {
        command = QStringLiteral("SHUTDOWN");
    } else if (verb == QStringLiteral("hot-reload")) {
        if (argc == 3 && QString::fromLocal8Bit(argv[2]) == QStringLiteral("--last")) {
            command = QStringLiteral("HOT_RELOAD_LAST");
        } else {
            if (argc < 3) {
                std::fprintf(stderr, "Error: hot-reload requires a file path or --last\n");
                return EXIT_USAGE;
            }
            QString path = QString::fromLocal8Bit(argv[2]);
            command = QStringLiteral("HOT_RELOAD ") + path;
            for (int i = 3; i < argc; i++) {
                command += QStringLiteral(" ") + QString::fromLocal8Bit(argv[i]);
            }
        }
    } else {
        std::fprintf(stderr, "Unknown command: %s\n", argv[1]);
        PrintUsage();
        return EXIT_USAGE;
    }

    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(SOCKET_NAME));

    if (!socket.waitForConnected(CONNECT_TIMEOUT_MS)) {
        std::fprintf(stderr, "Error: Cannot connect to Azahar.\n");
        std::fprintf(stderr, "Make sure Azahar is running and 'Enable developer IPC server'\n");
        std::fprintf(stderr, "is checked in Emulation > Configuration > Debug.\n");
        return EXIT_CONNECT_FAILED;
    }

    socket.write((command + QStringLiteral("\n")).toUtf8());
    socket.flush();

    int timeout = custom_timeout > 0 ? custom_timeout
                  : verb == QStringLiteral("hot-reload") ? RELOAD_TIMEOUT_MS
                                                         : DEFAULT_TIMEOUT_MS;

    QString response;
    if (!ReadSingleLine(socket, timeout, response)) {
        std::fprintf(stderr, "Error: Timeout waiting for response\n");
        return EXIT_TIMEOUT;
    }

    std::printf("%s\n", response.toLocal8Bit().constData());

    socket.disconnectFromServer();

    return response.startsWith(QStringLiteral("ERR")) ? EXIT_SERVER_ERROR : EXIT_OK;
}
