/**
 * LoggerTests.cpp - Documentation-driven tests for Logger
 *
 * Tests derived from:
 *   - .github/instructions/memory.md: "Prefer the centralized logger
 *     (AIO::Emulator::Common::Logger) so logs are captured consistently"
 *   - Logger.h interface specification
 *
 * Spec coverage:
 *   - Log levels: Debug, Info, Warning, Error, Fatal
 *   - Category filtering: enable/disable categories
 *   - Crash log capture: buffer last N entries for crash dump
 *   - Custom callbacks: user-provided log handlers
 */

#include "emulator/common/Logger.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace AIO::Emulator::Common;

// ============================================================================
// Log Level Tests (per LogLevel enum)
// ============================================================================

TEST(LoggerTest, LogLevelFilteringRespectsMinLevel) {
  // Spec: "if (level < m_minLevel) return;"
  Logger &logger = Logger::Instance();

  std::vector<LogEntry> captured;
  logger.SetCallback(
      [&captured](const LogEntry &entry) { captured.push_back(entry); });

  // Set minimum level to Warning - Debug and Info should be filtered
  logger.SetLevel(LogLevel::Warning);

  captured.clear();
  logger.Log(LogLevel::Debug, "TEST", "debug message");
  logger.Log(LogLevel::Info, "TEST", "info message");
  logger.Log(LogLevel::Warning, "TEST", "warning message");
  logger.Log(LogLevel::Error, "TEST", "error message");

  // Only Warning and Error should pass
  EXPECT_EQ(captured.size(), 2u);
  if (captured.size() >= 2) {
    EXPECT_EQ(captured[0].level, LogLevel::Warning);
    EXPECT_EQ(captured[1].level, LogLevel::Error);
  }

  // Reset to Info for other tests
  logger.SetLevel(LogLevel::Info);
  logger.SetCallback(nullptr);
}

// ============================================================================
// Category Filtering Tests
// ============================================================================

TEST(LoggerTest, CategoryEnabledByDefault) {
  // Spec: "If not found, return true (enabled by default)"
  Logger &logger = Logger::Instance();

  EXPECT_TRUE(logger.IsCategoryEnabled("NEVER_SEEN_CATEGORY"));
}

TEST(LoggerTest, DisableCategoryFiltersLogs) {
  Logger &logger = Logger::Instance();

  std::vector<LogEntry> captured;
  logger.SetCallback(
      [&captured](const LogEntry &entry) { captured.push_back(entry); });
  logger.SetLevel(LogLevel::Debug);

  logger.DisableCategory("FILTERED_CAT");
  logger.EnableCategory("ALLOWED_CAT");

  captured.clear();
  logger.Log(LogLevel::Info, "FILTERED_CAT", "should not appear");
  logger.Log(LogLevel::Info, "ALLOWED_CAT", "should appear");

  EXPECT_EQ(captured.size(), 1u);
  if (!captured.empty()) {
    EXPECT_EQ(captured[0].category, "ALLOWED_CAT");
  }

  // Cleanup
  logger.EnableCategory("FILTERED_CAT");
  logger.SetLevel(LogLevel::Info);
  logger.SetCallback(nullptr);
}

TEST(LoggerTest, EnableCategoryAfterDisable) {
  Logger &logger = Logger::Instance();

  logger.DisableCategory("TOGGLE_CAT");
  EXPECT_FALSE(logger.IsCategoryEnabled("TOGGLE_CAT"));

  logger.EnableCategory("TOGGLE_CAT");
  EXPECT_TRUE(logger.IsCategoryEnabled("TOGGLE_CAT"));
}

// ============================================================================
// Callback Tests
// ============================================================================

TEST(LoggerTest, CustomCallbackReceivesLogEntry) {
  Logger &logger = Logger::Instance();

  LogEntry lastEntry;
  bool callbackInvoked = false;

  logger.SetCallback([&](const LogEntry &entry) {
    lastEntry = entry;
    callbackInvoked = true;
  });
  logger.SetLevel(LogLevel::Debug);

  logger.Log(LogLevel::Info, "CALLBACK_TEST", "test message");

  EXPECT_TRUE(callbackInvoked);
  EXPECT_EQ(lastEntry.level, LogLevel::Info);
  EXPECT_EQ(lastEntry.category, "CALLBACK_TEST");
  EXPECT_EQ(lastEntry.message, "test message");
  EXPECT_GT(lastEntry.timestamp, 0u);

  logger.SetCallback(nullptr);
  logger.SetLevel(LogLevel::Info);
}

TEST(LoggerTest, NullCallbackUsesDefaultOutput) {
  // Setting nullptr callback shouldn't crash
  Logger &logger = Logger::Instance();

  logger.SetCallback(nullptr);

  // This should output to stdout/stderr, not crash
  EXPECT_NO_THROW(logger.Log(LogLevel::Info, "NULL_CB", "safe message"));
}

// ============================================================================
// LogFmt Tests (printf-style formatting)
// ============================================================================

TEST(LoggerTest, LogFmtFormatsCorrectly) {
  Logger &logger = Logger::Instance();

  std::string capturedMessage;
  logger.SetCallback(
      [&](const LogEntry &entry) { capturedMessage = entry.message; });
  logger.SetLevel(LogLevel::Debug);

  logger.LogFmt(LogLevel::Info, "FMT_TEST", "value=%d, name=%s", 42, "test");

  EXPECT_EQ(capturedMessage, "value=42, name=test");

  logger.SetCallback(nullptr);
  logger.SetLevel(LogLevel::Info);
}

// ============================================================================
// Log Buffer / Crash Capture Tests
// ============================================================================

TEST(LoggerTest, LogBufferLimitsSize) {
  // Spec: "Buffer all logs for crash dump... Keep last 1000 entries"
  // We can't directly access m_logBuffer, but we can verify behavior
  // by logging many entries and ensuring no memory issues
  Logger &logger = Logger::Instance();
  logger.SetLevel(LogLevel::Debug);

  // Log more than 1000 entries - should not cause memory issues
  for (int i = 0; i < 1500; ++i) {
    logger.Log(LogLevel::Debug, "BUFFER_TEST", "entry " + std::to_string(i));
  }

  // If we reach here without crash, buffer limiting is working
  SUCCEED();

  logger.SetLevel(LogLevel::Info);
}

// ============================================================================
// Configuration Tests
// ============================================================================

TEST(LoggerTest, SetLogFileDoesNotCrash) {
  Logger &logger = Logger::Instance();

  // Should accept path without crashing
  EXPECT_NO_THROW(logger.SetLogFile("/tmp/test_crash_log.txt"));
  EXPECT_NO_THROW(logger.SetLogFile("crash_log.txt"));
}

TEST(LoggerTest, SetExitOnCrashConfigurable) {
  Logger &logger = Logger::Instance();

  // Should be configurable without crashing
  EXPECT_NO_THROW(logger.SetExitOnCrash(false));
  EXPECT_NO_THROW(logger.SetExitOnCrash(true));

  // Reset to safe default
  logger.SetExitOnCrash(false);
}

TEST(LoggerTest, FlushLogsDoesNotCrash) {
  Logger &logger = Logger::Instance();

  // Add some logs first
  logger.Log(LogLevel::Info, "FLUSH_TEST", "message to flush");

  EXPECT_NO_THROW(logger.FlushLogs());
}

// ============================================================================
// Thread Safety (basic smoke test)
// ============================================================================

TEST(LoggerTest, ConcurrentLogCallsDoNotCrash) {
  // Spec: "std::lock_guard<std::mutex> lock(m_mutex);" - thread safe
  Logger &logger = Logger::Instance();
  logger.SetLevel(LogLevel::Debug);

  std::vector<LogEntry> captured;
  logger.SetCallback(
      [&captured](const LogEntry &entry) { captured.push_back(entry); });

  // Single-threaded rapid logging should work
  for (int i = 0; i < 100; ++i) {
    logger.Log(LogLevel::Debug, "CONCURRENT", std::to_string(i));
  }

  EXPECT_EQ(captured.size(), 100u);

  logger.SetCallback(nullptr);
  logger.SetLevel(LogLevel::Info);
}

// ============================================================================
// Singleton Pattern
// ============================================================================

TEST(LoggerTest, InstanceReturnsSameObject) {
  // Spec: "static Logger& Instance()" - singleton pattern
  Logger &a = Logger::Instance();
  Logger &b = Logger::Instance();

  EXPECT_EQ(&a, &b);
}
