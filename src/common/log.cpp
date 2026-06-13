// common/log.cpp - Default logging implementation

#include "common/log.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <shared_mutex>

namespace claw {
namespace log {

namespace {

const char* level_name(Level level) {
    switch (level) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
    }
    return "UNKNOWN";
}

Level env_level() {
    const char* env = std::getenv("CLAW_LOG");
    if (!env || !*env) {
        return Level::Info;
    }
    return level_from_string(env);
}

std::shared_mutex g_mutex;
Level g_level = env_level();

} // namespace

void set_level(Level level) {
    std::unique_lock<std::shared_mutex> lock(g_mutex);
    g_level = level;
}

Level get_level() {
    std::shared_lock<std::shared_mutex> lock(g_mutex);
    return g_level;
}

Level level_from_string(const std::string& s) {
    if (s.empty()) return Level::Info;
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lower == "trace") return Level::Trace;
    if (lower == "debug") return Level::Debug;
    if (lower == "info")  return Level::Info;
    if (lower == "warn" || lower == "warning") return Level::Warn;
    if (lower == "error" || lower == "fatal") return Level::Error;
    return Level::Info;
}

void log_message(Level level, const char* file, int line, const std::string& message) {
    {
        std::shared_lock<std::shared_mutex> lock(g_mutex);
        if (static_cast<int>(level) < static_cast<int>(g_level)) {
            return;
        }
    }

    // Keep only the filename for brevity.
    const char* slash = std::strrchr(file, '/');
    const char* basename = slash ? slash + 1 : file;

    std::ostream& out = (level == Level::Error || level == Level::Warn)
                            ? std::cerr
                            : std::clog;

    out << "[" << level_name(level) << "] " << basename << ":" << line
        << " " << message << "\n";
}

} // namespace log
} // namespace claw
