#pragma once

#include <string>
#include <unordered_map>

namespace AIO {
namespace Common {

class Dotenv {
public:
    // Loads KEY=VALUE pairs from a file (default: ".env").
    // - Ignores blank lines and lines starting with '#'
    // - Supports optional quotes: KEY="value" or KEY='value'
    // - Trims surrounding whitespace
    static std::unordered_map<std::string, std::string> LoadFile(const std::string& path);

    // Applies loaded variables to the process environment (best-effort).
    // Existing environment variables are not overwritten.
    static void ApplyToEnvironment(const std::unordered_map<std::string, std::string>& vars);

private:
    static std::string Trim(std::string s);
    static bool ParseLine(const std::string& line, std::string& keyOut, std::string& valueOut);
};

} // namespace Common
} // namespace AIO
