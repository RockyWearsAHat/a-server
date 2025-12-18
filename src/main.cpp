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
#include <signal.h>
#include <atomic>
#include "emulator/common/Logger.h"
#include "common/Dotenv.h"
#include "nas/NASServer.h"

// Async-signal-safe flag for graceful shutdown
static volatile sig_atomic_t g_quitRequested = 0;

static void HandleSignal(int signum) {
    // Request application quit; actual quit happens on the next timer tick
    g_quitRequested = 1;
}

int main(int argc, char *argv[]) {
    std::cout << std::unitbuf; // Flush stdout immediately
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
        "Custom log file path (default: crash_log.txt)",
        "log-path",
        "crash_log.txt");
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

    // NAS options (NAS auto-starts by default)
    QCommandLineOption nasOption(QStringList() << "nas",
        "(Deprecated) NAS now auto-starts by default");
    parser.addOption(nasOption);

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

    // Redirect stderr and stdout to debug log file for visibility
    static std::ofstream debug_log("debug.log", std::ios::trunc);
    std::cerr.rdbuf(debug_log.rdbuf());
    std::cout.rdbuf(debug_log.rdbuf());

    std::cout << "AIO Server Initializing..." << std::endl;
    std::cout << "Log file: " << logPath << std::endl;
    std::cout << "Debug output: debug.log" << std::endl;

    // Load .env into process environment (optional)
    {
        const auto vars = AIO::Common::Dotenv::LoadFile(".env");
        AIO::Common::Dotenv::ApplyToEnvironment(vars);
        if (!vars.empty()) {
            std::cout << "[main] Loaded .env with " << vars.size() << " keys" << std::endl;
        }
    }
    if (exitOnCrash) {
        std::cout << "Exit-on-crash: ENABLED" << std::endl;
    }

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
            std::cerr << "[main] Warning: failed to start NAS server (continuing without NAS)" << std::endl;
        } else {
            // Expose to GUI for embedded NAS viewer.
            const QString url = QString("http://127.0.0.1:%1/").arg(nasServer->Port());
            qputenv("AIO_NAS_URL", url.toUtf8());
        }
    }

    AIO::GUI::MainWindow window;

    // In headless mode, optionally quit after a bounded duration.
    if (headless) {
        bool okMs = false;
        const int maxMs = parser.value(headlessMaxMsOption).toInt(&okMs);
        if (okMs && maxMs > 0) {
            QTimer::singleShot(maxMs, [&]() {
                std::cout << "[main] Headless max time reached (" << maxMs << "ms), exiting..." << std::endl;
                QCoreApplication::quit();
            });
        }
    }
    
    // Periodic check for quit request set by signal handler
    QTimer quitPoll;
    quitPoll.setInterval(100);
    QObject::connect(&quitPoll, &QTimer::timeout, [&]() {
        if (g_quitRequested) {
            std::cout << "[main] Quit requested via signal, exiting..." << std::endl;
            QCoreApplication::quit();
        }
    });
    quitPoll.start();
    
    // If ROM specified, load it directly
    if (parser.isSet(romOption)) {
        QString romPath = parser.value(romOption);
        std::cout << "[main] ROM option set: " << romPath.toStdString() << std::endl;
        std::cout << "[main] Auto-loading ROM: " << romPath.toStdString() << std::endl;
        
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
        
        std::cout << "[main] Calling window.LoadROM()" << std::endl;
        window.LoadROM(romPath.toStdString());
        std::cout << "[main] window.LoadROM() returned" << std::endl;

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
            std::cout << "[main] Debugger enabled. Controls: Down/Enter=step, Up=step back, c=continue" << std::endl;
        }
        
        if (headless) {
            std::cout << "Running in headless mode..." << std::endl;
        }
    } else {
        std::cout << "[main] No ROM option set" << std::endl;
        if (headless) {
            // NAS-only headless mode is valid.
        }
        if (!headless) {
            window.show();
        }
    }

    return app.exec();
}
