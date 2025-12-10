#pragma once

#include "emulator/common/Logger.h"
#include <string>

namespace AIO::Emulator::Common {

class Loggable {
public:
    Loggable(const std::string& category) : m_category(category) {}
    virtual ~Loggable() = default;

protected:
    void LogDebug(const char* fmt, ...) const {
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        Logger::Instance().Log(LogLevel::Debug, m_category, buffer);
    }

    void LogInfo(const char* fmt, ...) const {
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        Logger::Instance().Log(LogLevel::Info, m_category, buffer);
    }

    void LogWarn(const char* fmt, ...) const {
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        Logger::Instance().Log(LogLevel::Warning, m_category, buffer);
    }

    void LogError(const char* fmt, ...) const {
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        Logger::Instance().Log(LogLevel::Error, m_category, buffer);
    }

    std::string m_category;
};

} // namespace AIO::Emulator::Common
