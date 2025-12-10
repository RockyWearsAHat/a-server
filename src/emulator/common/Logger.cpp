#include "emulator/common/Logger.h"
#include <iostream>
#include <chrono>
#include <cstdarg>
#include <cstdio>

namespace AIO::Emulator::Common {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::Log(LogLevel level, const std::string& category, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (level < m_minLevel) return;
    if (!IsCategoryEnabled(category)) return;

    LogEntry entry;
    entry.level = level;
    entry.category = category;
    entry.message = message;
    entry.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();

    if (m_callback) {
        m_callback(entry);
    } else {
        // Default output to stdout/stderr
        std::ostream& out = (level >= LogLevel::Error) ? std::cerr : std::cout;
        const char* levelStr = "INFO";
        switch (level) {
            case LogLevel::Debug: levelStr = "DEBUG"; break;
            case LogLevel::Info: levelStr = "INFO"; break;
            case LogLevel::Warning: levelStr = "WARN"; break;
            case LogLevel::Error: levelStr = "ERROR"; break;
            case LogLevel::Fatal: levelStr = "FATAL"; break;
        }
        out << "[" << levelStr << "] [" << category << "] " << message << std::endl;
    }
}

void Logger::LogFmt(LogLevel level, const std::string& category, const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Log(level, category, std::string(buffer));
}

void Logger::SetCallback(LogCallback callback) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callback = callback;
}

void Logger::EnableCategory(const std::string& category) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_categories[category] = true;
}

void Logger::DisableCategory(const std::string& category) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_categories[category] = false;
}

bool Logger::IsCategoryEnabled(const std::string& category) const {
    // If explicitly disabled, return false
    auto it = m_categories.find(category);
    if (it != m_categories.end()) {
        return it->second;
    }
    // Default behavior: if not found, return true (enabled by default)
    return true; 
}

void Logger::SetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_minLevel = level;
}

} // namespace AIO::Emulator::Common
