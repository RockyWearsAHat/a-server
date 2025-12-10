#pragma once

#include <string>
#include <functional>
#include <vector>
#include <mutex>
#include <map>
#include <cstdarg>

namespace AIO::Emulator::Common {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

struct LogEntry {
    LogLevel level;
    std::string category;
    std::string message;
    uint64_t timestamp;
};

class Logger {
public:
    using LogCallback = std::function<void(const LogEntry&)>;

    static Logger& Instance();

    void Log(LogLevel level, const std::string& category, const std::string& message);
    void LogFmt(LogLevel level, const std::string& category, const char* fmt, ...);

    void SetCallback(LogCallback callback);
    void EnableCategory(const std::string& category);
    void DisableCategory(const std::string& category);
    bool IsCategoryEnabled(const std::string& category) const;

    void SetLevel(LogLevel level);

private:
    Logger() = default;
    ~Logger() = default;

    LogCallback m_callback;
    std::map<std::string, bool> m_categories;
    LogLevel m_minLevel = LogLevel::Info;
    std::mutex m_mutex;
    bool m_allCategoriesEnabled = true;
};

} // namespace AIO::Emulator::Common
