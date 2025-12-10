#include <emulator/gba/Logger.h>
#include <cstdarg>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace AIO::Emulator::GBA {

LoggerConfig Logger::sConfig;
std::deque<std::string> Logger::sBacktrace;
std::mutex Logger::sMutex;
int Logger::sOccurrenceCount = 0;
bool Logger::sBreakpointHit = false;

void Logger::Init(const LoggerConfig& config) {
    std::lock_guard<std::mutex> lock(sMutex);
    sConfig = config;
    sBacktrace.clear();
    sOccurrenceCount = 0;
    sBreakpointHit = false;
}

bool Logger::IsCategoryEnabled(LogCategory category) {
    return (sConfig.enabledCategories & category);
}

void Logger::Log(LogCategory category, const std::string& message) {
    if (!IsCategoryEnabled(category)) return;
    
    std::lock_guard<std::mutex> lock(sMutex);
    std::cout << message << std::endl;
}

void Logger::Log(LogCategory category, const char* fmt, ...) {
    if (!IsCategoryEnabled(category)) return;
    
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    std::lock_guard<std::mutex> lock(sMutex);
    std::cout << buffer << std::endl;
}

void Logger::LogCPU(uint32_t pc, uint32_t opcode, const std::string& disassembly, const std::string& regs) {
    // Format the log line
    std::stringstream ss;
    ss << "PC:0x" << std::hex << std::setw(8) << std::setfill('0') << pc 
       << " Op:0x" << std::setw(8) << opcode 
       << " " << disassembly 
       << " | " << regs;
    
    std::string line = ss.str();

    std::lock_guard<std::mutex> lock(sMutex);

    // Add to backtrace if enabled
    if (sConfig.backtraceSize > 0) {
        sBacktrace.push_back(line);
        if (sBacktrace.size() > (size_t)sConfig.backtraceSize) {
            sBacktrace.pop_front();
        }
    }

    // Print if CPU logging is enabled
    if (IsCategoryEnabled(LogCategory::CPU)) {
        std::cout << line << std::endl;
    }
}

void Logger::CheckBreakpoint(uint32_t address) {
    if (!sConfig.breakOnAddress) return;
    
    // Simple address match (could be expanded to range)
    if (address == sConfig.breakAddress) {
        std::lock_guard<std::mutex> lock(sMutex);
        sOccurrenceCount++;
        
        bool trigger = false;
        if (sConfig.occurrenceMode == "first" && sOccurrenceCount == 1) trigger = true;
        else if (sConfig.occurrenceMode == "all") trigger = true;
        else if (sConfig.occurrenceMode == "nth" && sOccurrenceCount == 1) trigger = true; // Simplified for now
        
        if (trigger) {
            std::cout << "\n[LOGGER] Breakpoint hit at 0x" << std::hex << address 
                      << " (Occurrence " << std::dec << sOccurrenceCount << ")" << std::endl;
            
            if (sConfig.backtraceSize > 0) {
                std::cout << "[LOGGER] Dumping Backtrace (" << sBacktrace.size() << " lines):" << std::endl;
                for (const auto& line : sBacktrace) {
                    std::cout << line << std::endl;
                }
                std::cout << "[LOGGER] End Backtrace" << std::endl;
            }
            
            // For now, we just log. In a real debugger we might pause.
            // If the user wants to stop, they can use the backtrace to see what happened.
            // We could exit(0) here if it's a "run until" scenario.
            if (sConfig.occurrenceMode == "first") {
                std::cout << "[LOGGER] Exiting after first occurrence." << std::endl;
                exit(0);
            }
        }
    }
}

void Logger::DumpBacktrace() {
    std::lock_guard<std::mutex> lock(sMutex);
    std::cout << "[LOGGER] Dumping Backtrace (" << sBacktrace.size() << " lines):" << std::endl;
    for (const auto& line : sBacktrace) {
        std::cout << line << std::endl;
    }
    std::cout << "[LOGGER] End Backtrace" << std::endl;
}

} // namespace AIO::Emulator::GBA
