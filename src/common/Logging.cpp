#include "common/Logging.h"

#include "emulator/common/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMessageLogContext>
#include <QtGlobal>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <streambuf>
#include <string>

namespace {

using AIO::Emulator::Common::LogEntry;
using AIO::Emulator::Common::Logger;
using AIO::Emulator::Common::LogLevel;

static std::streambuf *g_origCoutBuf = nullptr;
static std::streambuf *g_origCerrBuf = nullptr;
static std::unique_ptr<std::ostream> g_mirrorCout;
static std::unique_ptr<std::ostream> g_mirrorCerr;

static std::unique_ptr<std::ofstream> g_logFile;
static bool g_mirrorEnabled = false;
static bool g_initialized = false;

static thread_local bool g_inRedirectWrite = false;

static LogLevel parseLevel(const QString &s) {
  const QString v = s.trimmed().toLower();
  if (v == "debug")
    return LogLevel::Debug;
  if (v == "info")
    return LogLevel::Info;
  if (v == "warn" || v == "warning")
    return LogLevel::Warning;
  if (v == "error")
    return LogLevel::Error;
  if (v == "fatal")
    return LogLevel::Fatal;
  return LogLevel::Info;
}

static const char *levelString(LogLevel level) {
  switch (level) {
  case LogLevel::Debug:
    return "DEBUG";
  case LogLevel::Info:
    return "INFO";
  case LogLevel::Warning:
    return "WARN";
  case LogLevel::Error:
    return "ERROR";
  case LogLevel::Fatal:
    return "FATAL";
  }
  return "INFO";
}

static QString isoTimeFromMs(uint64_t msSinceEpoch) {
  const QDateTime dt =
      QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(msSinceEpoch));
  return dt.toString(Qt::ISODateWithMs);
}

static std::string formatEntry(const LogEntry &e) {
  const QString time = isoTimeFromMs(e.timestamp);
  const QString msg = QString::fromStdString(e.message);
  const QString cat = QString::fromStdString(e.category);

  // Single-line, grep-friendly format.
  // Example: 2025-12-18T14:03:12.123 [INFO] [CPU] message
  const QString line = QString("%1 [%2] [%3] %4")
                           .arg(time)
                           .arg(levelString(e.level))
                           .arg(cat)
                           .arg(msg);
  return line.toStdString();
}

static void writeLineToSinks(const std::string &line, LogLevel level) {
  if (g_logFile && g_logFile->is_open()) {
    (*g_logFile) << line << '\n';
    g_logFile->flush();
  }

  if (g_mirrorEnabled) {
    std::ostream &out =
        (level >= LogLevel::Error) ? *g_mirrorCerr : *g_mirrorCout;
    out << line << std::endl;
  }
}

class LoggerLineStreamBuf final : public std::streambuf {
public:
  LoggerLineStreamBuf(std::streambuf *fallback, LogLevel level,
                      std::string category)
      : m_fallback(fallback), m_level(level), m_category(std::move(category)) {}

protected:
  int overflow(int ch) override {
    if (ch == traits_type::eof()) {
      return traits_type::not_eof(ch);
    }

    const char c = static_cast<char>(ch);
    if (c == '\n') {
      flushLine();
      return ch;
    }

    m_line.push_back(c);
    return ch;
  }

  int sync() override {
    flushLine();
    return 0;
  }

private:
  void flushLine() {
    if (m_line.empty())
      return;

    // Prevent deadlocks/recursion if a write happens while the logger itself is
    // logging.
    if (g_inRedirectWrite) {
      if (m_fallback) {
        m_fallback->sputn(m_line.data(),
                          static_cast<std::streamsize>(m_line.size()));
        m_fallback->sputc('\n');
      }
      m_line.clear();
      return;
    }

    g_inRedirectWrite = true;
    Logger::Instance().Log(m_level, m_category, m_line);
    g_inRedirectWrite = false;

    m_line.clear();
  }

  std::streambuf *m_fallback;
  LogLevel m_level;
  std::string m_category;
  std::string m_line;
};

static std::unique_ptr<LoggerLineStreamBuf> g_coutBuf;
static std::unique_ptr<LoggerLineStreamBuf> g_cerrBuf;

static void qtMessageHandler(QtMsgType type, const QMessageLogContext &ctx,
                             const QString &msg) {
  // Keep Qt noise segregated so it can be grepped/filtered.
  LogLevel level = LogLevel::Info;
  switch (type) {
  case QtDebugMsg:
    level = LogLevel::Debug;
    break;
  case QtInfoMsg:
    level = LogLevel::Info;
    break;
  case QtWarningMsg:
    level = LogLevel::Warning;
    break;
  case QtCriticalMsg:
    level = LogLevel::Error;
    break;
  case QtFatalMsg:
    level = LogLevel::Fatal;
    break;
  }

  QString decorated = msg;
  if (ctx.file && *ctx.file) {
    decorated = QString("%1 (%2:%3)").arg(msg).arg(ctx.file).arg(ctx.line);
  }

  Logger::Instance().Log(level, "Qt", decorated.toStdString());
}

} // namespace

namespace AIO::Common {

void InitAppLogging(const QString &logFilePath) {
  if (g_initialized)
    return;
  g_initialized = true;

  const bool append = (qEnvironmentVariableIntValue("AIO_LOG_APPEND") != 0);
  g_mirrorEnabled = (qEnvironmentVariableIntValue("AIO_LOG_MIRROR") != 0) ||
                    (qEnvironmentVariableIntValue("AIO_TRACE_IE_WRITES") != 0);

  // Ensure the directory exists if a directory is provided.
  if (!logFilePath.isEmpty()) {
    const QFileInfo fi(logFilePath);
    if (fi.dir().exists() == false && !fi.path().isEmpty() &&
        fi.path() != ".") {
      fi.dir().mkpath(".");
    }
  }

  g_logFile = std::make_unique<std::ofstream>(
      logFilePath.toStdString(), append ? (std::ios::out | std::ios::app)
                                        : (std::ios::out | std::ios::trunc));

  // Configure core logger.
  Logger::Instance().SetLogFile(logFilePath.toStdString());
  Logger::Instance().SetLevel(
      parseLevel(qEnvironmentVariable("AIO_LOG_LEVEL")));

  // Capture original cout/cerr for optional mirroring and recursion fallback.
  g_origCoutBuf = std::cout.rdbuf();
  g_origCerrBuf = std::cerr.rdbuf();
  g_mirrorCout = std::make_unique<std::ostream>(g_origCoutBuf);
  g_mirrorCerr = std::make_unique<std::ostream>(g_origCerrBuf);

  // Route Logger entries into our file sink.
  Logger::Instance().SetCallback([](const LogEntry &e) {
    // Avoid re-entrancy through stdout/stderr while handling logger output.
    g_inRedirectWrite = true;
    const std::string line = formatEntry(e);
    writeLineToSinks(line, e.level);
    g_inRedirectWrite = false;
  });

  // Route Qt logging into Logger.
  qInstallMessageHandler(qtMessageHandler);

  // Capture stdio into Logger for consistent formatting.
  g_coutBuf = std::make_unique<LoggerLineStreamBuf>(g_origCoutBuf,
                                                    LogLevel::Info, "STDOUT");
  g_cerrBuf = std::make_unique<LoggerLineStreamBuf>(g_origCerrBuf,
                                                    LogLevel::Error, "STDERR");
  std::cout.rdbuf(g_coutBuf.get());
  std::cerr.rdbuf(g_cerrBuf.get());

  Logger::Instance().Log(LogLevel::Info, "main",
                         "Logging initialized: " + logFilePath.toStdString());
}

void ShutdownAppLogging() {
  if (!g_initialized)
    return;

  // Restore std streams first.
  if (g_origCoutBuf)
    std::cout.rdbuf(g_origCoutBuf);
  if (g_origCerrBuf)
    std::cerr.rdbuf(g_origCerrBuf);

  g_coutBuf.reset();
  g_cerrBuf.reset();

  if (g_logFile && g_logFile->is_open()) {
    g_logFile->flush();
    g_logFile->close();
  }
  g_logFile.reset();

  // Leave Qt message handler installed for process lifetime.
}

} // namespace AIO::Common
