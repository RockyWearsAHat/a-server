#pragma once

#include <cstdarg>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// Coverage exclusion macros - these lines are excluded from lcov coverage
// reports Use LCOV_EXCL_LINE at the end of a line to exclude just that line Use
// LCOV_EXCL_START / LCOV_EXCL_STOP to exclude blocks The .lcovrc config also
// auto-excludes Logger:: calls

// Convenience macro for logging that won't count against coverage
#define AIO_LOG(level, category, ...)                                          \
  AIO::Emulator::Common::Logger::Instance().LogFmt(                            \
      level, category, __VA_ARGS__) // LCOV_EXCL_LINE

#define AIO_LOG_INFO(category, ...)                                            \
  AIO::Emulator::Common::Logger::Instance().LogFmt(                            \
      AIO::Emulator::Common::LogLevel::Info, category,                         \
      __VA_ARGS__) // LCOV_EXCL_LINE

#define AIO_LOG_DEBUG(category, ...)                                           \
  AIO::Emulator::Common::Logger::Instance().LogFmt(                            \
      AIO::Emulator::Common::LogLevel::Debug, category,                        \
      __VA_ARGS__) // LCOV_EXCL_LINE

#define AIO_LOG_WARN(category, ...)                                            \
  AIO::Emulator::Common::Logger::Instance().LogFmt(                            \
      AIO::Emulator::Common::LogLevel::Warning, category,                      \
      __VA_ARGS__) // LCOV_EXCL_LINE

#define AIO_LOG_ERROR(category, ...)                                           \
  AIO::Emulator::Common::Logger::Instance().LogFmt(                            \
      AIO::Emulator::Common::LogLevel::Error, category,                        \
      __VA_ARGS__) // LCOV_EXCL_LINE

#define AIO_LOG_FATAL(category, ...)                                           \
  AIO::Emulator::Common::Logger::Instance().LogFmt(                            \
      AIO::Emulator::Common::LogLevel::Fatal, category,                        \
      __VA_ARGS__) // LCOV_EXCL_LINE

namespace AIO::Emulator::Common {

enum class LogLevel { Debug, Info, Warning, Error, Fatal };

struct LogEntry {
  LogLevel level;
  std::string category;
  std::string message;
  uint64_t timestamp;
};

class Logger {
public:
  using LogCallback = std::function<void(const LogEntry &)>;

  static Logger &Instance();

  void Log(LogLevel level, const std::string &category,
           const std::string &message);
  void LogFmt(LogLevel level, const std::string &category, const char *fmt,
              ...);

  void SetCallback(LogCallback callback);
  void EnableCategory(const std::string &category);
  void DisableCategory(const std::string &category);
  bool IsCategoryEnabled(const std::string &category) const;

  void SetLevel(LogLevel level);

  // Crash handling and configuration
  void SetLogFile(const std::string &path);
  void SetExitOnCrash(bool exit);
  void WriteCrashLog(const std::string &message);
  void FlushLogs();

private:
  Logger() = default;
  ~Logger() = default;

  LogCallback m_callback;
  std::map<std::string, bool> m_categories;
  LogLevel m_minLevel = LogLevel::Info;
  std::mutex m_mutex;
  bool m_allCategoriesEnabled = true;
  std::string m_logFilePath = "crash_log.txt";
  bool m_exitOnCrash = false;
  std::vector<LogEntry> m_logBuffer;
};

} // namespace AIO::Emulator::Common
