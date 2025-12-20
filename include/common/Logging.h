#pragma once

#include <QString>

namespace AIO::Common {

/**
 * @brief Initializes application-wide logging.
 *
 * This hooks multiple log sources into a single, consistent sink:
 *
 * - `AIO::Emulator::Common::Logger` entries (emulator/core logging)
 * - Qt message output (qDebug/qWarning/qCritical) via `qInstallMessageHandler`
 * - Optional capture of `std::cout` / `std::cerr` into the logger
 *
 * By default, logs are written to a single file (usually `debug.log`), and can
 * optionally be mirrored to the original console streams.
 *
 * Configuration (environment variables):
 * - `AIO_LOG_MIRROR=1` mirrors logger output to the original stdout/stderr.
 * - `AIO_LOG_APPEND=1` appends to the log file instead of truncating.
 * - `AIO_LOG_LEVEL=debug|info|warn|error|fatal` sets minimum log level.
 */
void InitAppLogging(const QString& logFilePath);

/**
 * @brief Restores stdout/stderr and flushes file sinks.
 *
 * Safe to call multiple times.
 */
void ShutdownAppLogging();

} // namespace AIO::Common
