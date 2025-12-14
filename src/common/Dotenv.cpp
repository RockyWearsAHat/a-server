#include "common/Dotenv.h"

#include <fstream>
#include <cctype>

#if defined(_WIN32)
  #include <cstdlib>
#else
  #include <stdlib.h>
#endif

namespace AIO {
namespace Common {

std::string Dotenv::Trim(std::string s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) end--;
    return s.substr(start, end - start);
}

bool Dotenv::ParseLine(const std::string& rawLine, std::string& keyOut, std::string& valueOut) {
    std::string line = Trim(rawLine);
    if (line.empty()) return false;
    if (line[0] == '#') return false;

    const auto eq = line.find('=');
    if (eq == std::string::npos) return false;

    std::string key = Trim(line.substr(0, eq));
    std::string value = Trim(line.substr(eq + 1));

    if (key.empty()) return false;

    // Strip surrounding quotes
    if (value.size() >= 2) {
        const char first = value.front();
        const char last = value.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            value = value.substr(1, value.size() - 2);
        }
    }

    keyOut = key;
    valueOut = value;
    return true;
}

std::unordered_map<std::string, std::string> Dotenv::LoadFile(const std::string& path) {
    std::unordered_map<std::string, std::string> vars;

    std::ifstream in(path);
    if (!in.good()) {
        return vars;
    }

    std::string line;
    while (std::getline(in, line)) {
        std::string key;
        std::string value;
        if (ParseLine(line, key, value)) {
            vars[key] = value;
        }
    }

    return vars;
}

void Dotenv::ApplyToEnvironment(const std::unordered_map<std::string, std::string>& vars) {
    for (const auto& kv : vars) {
        const auto& key = kv.first;
        const auto& value = kv.second;

        // Don't overwrite if already defined.
        if (std::getenv(key.c_str()) != nullptr) {
            continue;
        }

#if defined(_WIN32)
        _putenv_s(key.c_str(), value.c_str());
#else
        setenv(key.c_str(), value.c_str(), 0);
#endif
    }
}

} // namespace Common
} // namespace AIO
