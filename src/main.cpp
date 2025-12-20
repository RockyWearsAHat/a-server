#include <iostream>
#include <fstream>
#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include "gui/MainWindow.h"
#include <QRegularExpression>
#include <QTimer>
#include <QDir>
#include <QStandardPaths>
#include <QMetaObject>
#include <signal.h>
#include <atomic>
#include "emulator/common/Logger.h"
#include "emulator/gba/ARM7TDMI.h"
#include "common/Dotenv.h"
#include "common/Logging.h"
#include "nas/NASServer.h"

// Async-signal-safe flag for graceful shutdown
static volatile sig_atomic_t g_quitRequested = 0;

static void HandleSignal(int signum) {
    // Request application quit; actual quit happens on the next timer tick
    g_quitRequested = 1;
}

static void HeadlessCrashQuit(const char* logPath) {
    // Called from emulation thread; schedule quit on the Qt thread.
    AIO::Emulator::Common::Logger::Instance().LogFmt(
        AIO::Emulator::Common::LogLevel::Error,
        "main",
        "Headless crash detected (log: %s), exiting...",
        (logPath ? logPath : "(null)"));

    QCoreApplication* app = QCoreApplication::instance();
    if (!app) {
        return;
    }
    QMetaObject::invokeMethod(app, []() {
        QCoreApplication::exit(1);
    }, Qt::QueuedConnection);
}

int main(int argc, char *argv[]) {
    // NOTE: Do not enable std::unitbuf here.
    // We redirect std::cout/std::cerr through LoggerLineStreamBuf (see src/common/Logging.cpp),
    // and unitbuf forces a sync() on every insertion which fragments single logical log lines
    // into many entries in debug.log.
    QApplication app(argc, argv);
    app.setApplicationName("AIOServer");
    app.setApplicationVersion("1.0");

    // Install signal handlers to allow Ctrl+C and SIGTERM to gracefully quit
    struct sigaction sa{};
    sa.sa_handler = HandleSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    // Parse command line arguments
    QCommandLineParser parser;
    parser.setApplicationDescription("AIO Entertainment System - Multi-console emulator");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption romOption(QStringList() << "r" << "rom",
        "ROM file to load directly on startup",
        "rom-path");
    parser.addOption(romOption);

    QCommandLineOption logOption(QStringList() << "l" << "log-file",
        "Log file path (default: debug.log)",
        "log-path",
        "debug.log");
    parser.addOption(logOption);

    QCommandLineOption exitOnCrashOption(QStringList() << "e" << "exit-on-crash",
        "Exit immediately on crash (for automated testing)");
    parser.addOption(exitOnCrashOption);

    QCommandLineOption headlessOption(QStringList() << "headless",
        "Run without GUI (requires --rom)");
    parser.addOption(headlessOption);

    QCommandLineOption headlessMaxMsOption(QStringList() << "headless-max-ms",
        "In --headless mode, automatically quit after N milliseconds (useful for deterministic log capture)",
        "ms",
        "0");
    parser.addOption(headlessMaxMsOption);

    QCommandLineOption nasRootOption(QStringList() << "nas-root",
        "Root directory to serve via NAS (default: ~/AIO_NAS)",
        "path");
    parser.addOption(nasRootOption);

    QCommandLineOption nasPortOption(QStringList() << "nas-port",
        "NAS server port (default: 8080)",
        "port",
        "8080");
    parser.addOption(nasPortOption);

    QCommandLineOption nasTokenOption(QStringList() << "nas-token",
        "Optional bearer token to require for all NAS requests",
        "token");
    parser.addOption(nasTokenOption);

    // Debugger options
    QCommandLineOption debugOption(QStringList() << "d" << "debug",
        "Enable interactive CPU debugger (terminal controls)");
    parser.addOption(debugOption);
    QCommandLineOption bpOption(QStringList() << "b" << "br" << "breakpoint",
        "Add breakpoint address (hex, can repeat)",
        "address");
    parser.addOption(bpOption);
    QCommandLineOption bpsOption(QStringList() << "bs" << "brs" << "breakpoints",
        "Add multiple breakpoint addresses (comma or JSON list)",
        "addresses");
    parser.addOption(bpsOption);

    parser.process(app);

    // Configure logger
    std::string logPath = parser.value(logOption).toStdString();
    bool exitOnCrash = parser.isSet(exitOnCrashOption);
    bool headless = parser.isSet(headlessOption);
    const bool nasEnabled = true;

    AIO::Emulator::Common::Logger::Instance().SetLogFile(logPath);
    AIO::Emulator::Common::Logger::Instance().SetExitOnCrash(exitOnCrash);

    // Central logging:
    // - Routes Qt logs + stdio into a single file (default: debug.log)
    // - Keeps emulator crash logging pointed at the same path
    AIO::Common::InitAppLogging(QString::fromStdString(logPath));

    AIO::Emulator::Common::Logger::Instance().Log(AIO::Emulator::Common::LogLevel::Info, "main", "AIO Server Initializing...");
    AIO::Emulator::Common::Logger::Instance().LogFmt(AIO::Emulator::Common::LogLevel::Info, "main", "Log file: %s", logPath.c_str());

    // Load .env into process environment (optional)
    {
        const auto vars = AIO::Common::Dotenv::LoadFile(".env");
        AIO::Common::Dotenv::ApplyToEnvironment(vars);
        if (!vars.empty()) {
            AIO::Emulator::Common::Logger::Instance().LogFmt(AIO::Emulator::Common::LogLevel::Info, "main", "Loaded .env with %zu keys", vars.size());
        }
    }
    if (exitOnCrash) {
        AIO::Emulator::Common::Logger::Instance().Log(AIO::Emulator::Common::LogLevel::Info, "main", "Exit-on-crash: ENABLED");
    }

    // Scope runtime objects so they are destroyed (and stop their threads) before logging shuts down.
    int rc = 0;
    {
        // Start NAS server early so it works in headless mode.
        std::unique_ptr<AIO::NAS::NASServer> nasServer;
        if (nasEnabled) {
            QString root = parser.value(nasRootOption);
            if (root.isEmpty()) {
                root = qEnvironmentVariable("AIO_NAS_ROOT");
            }
            if (root.isEmpty()) {
                root = QDir(QDir::homePath()).absoluteFilePath("AIO_NAS");
            }
            // Ensure default root exists.
            QDir().mkpath(root);

            int portInt = 8080;
            {
                const QString portStr = parser.isSet(nasPortOption) ? parser.value(nasPortOption) : qEnvironmentVariable("AIO_NAS_PORT");
                if (!portStr.isEmpty()) {
                    bool okPort = false;
                    const int parsed = portStr.toInt(&okPort);
                    if (okPort && parsed > 0 && parsed <= 65535) {
                        portInt = parsed;
                    }
                }
            }

            QString token = parser.value(nasTokenOption);
            if (token.isEmpty()) {
                token = qEnvironmentVariable("AIO_NAS_TOKEN");
            }

            AIO::NAS::NASServer::Options opt;
            opt.rootPath = root;
            opt.port = static_cast<quint16>(portInt);
            opt.bearerToken = token;

            nasServer = std::make_unique<AIO::NAS::NASServer>(opt);
            if (!nasServer->Start()) {
                AIO::Emulator::Common::Logger::Instance().Log(AIO::Emulator::Common::LogLevel::Warning, "main", "Failed to start NAS server (continuing without NAS)");
            } else {
                // Expose to GUI for embedded NAS viewer.
                const QString url = QString("http://127.0.0.1:%1/").arg(nasServer->Port());
                qputenv("AIO_NAS_URL", url.toUtf8());
            }
        }

        AIO::GUI::MainWindow window;

        // In headless mode, override crash handler to avoid GUI dialogs.
        if (headless) {
            AIO::Emulator::GBA::CrashPopupCallback = &HeadlessCrashQuit;
        }

        // In headless mode, optionally quit after a bounded duration.
        if (headless) {
            bool okMs = false;
            const int maxMs = parser.value(headlessMaxMsOption).toInt(&okMs);
            if (okMs && maxMs > 0) {
                QTimer::singleShot(maxMs, [&]() {
                    AIO::Emulator::Common::Logger::Instance().LogFmt(AIO::Emulator::Common::LogLevel::Info, "main", "Headless max time reached (%d ms), exiting...", maxMs);
                    QCoreApplication::quit();
                });
            }
        }

        // Periodic check for quit request set by signal handler
        QTimer quitPoll;
        quitPoll.setInterval(100);
        QObject::connect(&quitPoll, &QTimer::timeout, [&]() {
            if (g_quitRequested) {
                AIO::Emulator::Common::Logger::Instance().Log(AIO::Emulator::Common::LogLevel::Info, "main", "Quit requested via signal, exiting...");
                QCoreApplication::quit();
            }
        });
        quitPoll.start();

        // If ROM specified, load it directly
        if (parser.isSet(romOption)) {
            QString romPath = parser.value(romOption);
            AIO::Emulator::Common::Logger::Instance().LogFmt(AIO::Emulator::Common::LogLevel::Info, "main", "ROM option set: %s", romPath.toStdString().c_str());
            AIO::Emulator::Common::Logger::Instance().LogFmt(AIO::Emulator::Common::LogLevel::Info, "main", "Auto-loading ROM: %s", romPath.toStdString().c_str());

            // Detect emulator type from ROM extension (default to GBA)
            std::string romStr = romPath.toStdString();
            if (romStr.find(".nro") != std::string::npos || romStr.find(".nso") != std::string::npos) {
                window.SetEmulatorType(1); // Switch
            } else {
                window.SetEmulatorType(0); // GBA (default)
            }

            if (!headless) {
                window.show();
            }

            AIO::Emulator::Common::Logger::Instance().Log(AIO::Emulator::Common::LogLevel::Info, "main", "Calling window.LoadROM()");
            window.LoadROM(romPath.toStdString());
            AIO::Emulator::Common::Logger::Instance().Log(AIO::Emulator::Common::LogLevel::Info, "main", "window.LoadROM() returned");

            // Configure debugger on GBA emulator via window API
            if (parser.isSet(debugOption)) {
                window.EnableDebugger(true);
                const auto bpValues = parser.values(bpOption);
                for (const auto& bpStr : bpValues) {
                    bool ok = false;
                    uint32_t addr = bpStr.toUInt(&ok, 16);
                    if (ok) window.AddBreakpoint(addr);
                }
                if (parser.isSet(bpsOption)) {
                    QString raw = parser.value(bpsOption);
                    // Normalize: remove brackets and quotes, split on comma/space
                    QString norm = raw;
                    norm.replace('[', ' ').replace(']', ' ').replace('"', ' ').replace('\'', ' ');
                    const auto parts = norm.split(QRegularExpression("[ ,]+"), Qt::SkipEmptyParts);
                    for (const auto &p : parts) {
                        bool ok=false; uint32_t addr = p.toUInt(&ok, 16);
                        if (ok) window.AddBreakpoint(addr);
                    }
                }
                AIO::Emulator::Common::Logger::Instance().Log(AIO::Emulator::Common::LogLevel::Info, "main", "Debugger enabled. Controls: Down/Enter=step, Up=step back, c=continue");
            }

            if (headless) {
                AIO::Emulator::Common::Logger::Instance().Log(AIO::Emulator::Common::LogLevel::Info, "main", "Running in headless mode...");
            }
        } else {
            AIO::Emulator::Common::Logger::Instance().Log(AIO::Emulator::Common::LogLevel::Info, "main", "No ROM option set");
            if (headless) {
                // NAS-only headless mode is valid.
            }
            if (!headless) {
                window.show();
            }
        }

        rc = app.exec();
    }
    AIO::Common::ShutdownAppLogging();
    return rc;
}
