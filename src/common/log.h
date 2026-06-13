// common/log.h - Unified logging interface for Claw
//
// Usage:
//   #include "common/log.h"
//   CLAW_LOG_INFO("message: {}", value);
//   CLAW_LOG_WARN("unexpected state");
//
// Compile-time level can be set with -DCLAW_LOG_LEVEL=CLAW_LOG_LEVEL_WARN
// Runtime default is INFO. Set CLAW_LOG environment variable to change level:
//   CLAW_LOG=debug ./claw ...

#ifndef CLAW_COMMON_LOG_H
#define CLAW_COMMON_LOG_H

#include <string>
#include <utility>

namespace claw {
namespace log {

enum class Level {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
};

// Set/get the active log level. Thread-safe.
void set_level(Level level);
Level get_level();
Level level_from_string(const std::string& s);

// Internal helper for formatted logging.
void log_message(Level level, const char* file, int line, const std::string& message);

// Format-style logging (limited to std::to_string-compatible args).
template <typename... Args>
std::string format_message(const std::string& fmt, Args&&... args);

namespace detail {
inline std::string to_log_string(const std::string& s) { return s; }
inline std::string to_log_string(std::string&& s) { return std::move(s); }
inline std::string to_log_string(const char* s) { return s ? s : ""; }
inline std::string to_log_string(char c) { return std::string(1, c); }

template <typename T>
std::string to_log_string(const T& value) {
    using std::to_string;
    return to_string(value);
}

inline std::string format_one(const std::string& fmt) { return fmt; }

template <typename T, typename... Rest>
std::string format_one(const std::string& fmt, T&& value, Rest&&... rest) {
    std::size_t pos = fmt.find("{}");
    if (pos == std::string::npos) {
        return fmt;
    }
    std::string head = fmt.substr(0, pos);
    std::string tail = fmt.substr(pos + 2);
    return head + to_log_string(std::forward<T>(value)) +
           format_one(tail, std::forward<Rest>(rest)...);
}
} // namespace detail

template <typename... Args>
std::string format_message(const std::string& fmt, Args&&... args) {
    return detail::format_one(fmt, std::forward<Args>(args)...);
}

} // namespace log
} // namespace claw

#ifndef CLAW_LOG_LEVEL
#define CLAW_LOG_LEVEL ::claw::log::Level::Info
#else
// Allow numeric or enum constants at compile time.
#endif

#define CLAW_LOG_ENABLED(level) (static_cast<int>(level) >= static_cast<int>(CLAW_LOG_LEVEL))

#define CLAW_LOG_TRACE(...)                                                    \
    do {                                                                       \
        if (CLAW_LOG_ENABLED(::claw::log::Level::Trace)) {                     \
            ::claw::log::log_message(::claw::log::Level::Trace, __FILE__,      \
                                     __LINE__,                                 \
                                     ::claw::log::format_message(__VA_ARGS__)); \
        }                                                                      \
    } while (0)

#define CLAW_LOG_DEBUG(...)                                                    \
    do {                                                                       \
        if (CLAW_LOG_ENABLED(::claw::log::Level::Debug)) {                     \
            ::claw::log::log_message(::claw::log::Level::Debug, __FILE__,      \
                                     __LINE__,                                 \
                                     ::claw::log::format_message(__VA_ARGS__)); \
        }                                                                      \
    } while (0)

#define CLAW_LOG_INFO(...)                                                     \
    do {                                                                       \
        if (CLAW_LOG_ENABLED(::claw::log::Level::Info)) {                      \
            ::claw::log::log_message(::claw::log::Level::Info, __FILE__,       \
                                     __LINE__,                                 \
                                     ::claw::log::format_message(__VA_ARGS__)); \
        }                                                                      \
    } while (0)

#define CLAW_LOG_WARN(...)                                                     \
    do {                                                                       \
        if (CLAW_LOG_ENABLED(::claw::log::Level::Warn)) {                      \
            ::claw::log::log_message(::claw::log::Level::Warn, __FILE__,       \
                                     __LINE__,                                 \
                                     ::claw::log::format_message(__VA_ARGS__)); \
        }                                                                      \
    } while (0)

#define CLAW_LOG_ERROR(...)                                                    \
    do {                                                                       \
        if (CLAW_LOG_ENABLED(::claw::log::Level::Error)) {                     \
            ::claw::log::log_message(::claw::log::Level::Error, __FILE__,      \
                                     __LINE__,                                 \
                                     ::claw::log::format_message(__VA_ARGS__)); \
        }                                                                      \
    } while (0)

#endif // CLAW_COMMON_LOG_H
