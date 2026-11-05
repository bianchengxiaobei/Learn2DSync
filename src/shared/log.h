#pragma once

#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nsync::shared {

enum class LogLevel {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Off,
};

[[nodiscard]] LogLevel log_level();
void set_log_level(LogLevel level);

[[nodiscard]] bool parse_log_level(std::string_view text, LogLevel& level);
[[nodiscard]] std::string_view log_level_name(LogLevel level);
[[nodiscard]] bool is_log_enabled(LogLevel level);

void write_log(LogLevel level, std::string_view category, std::string_view message);
[[nodiscard]] std::vector<std::string> recent_log_lines(
    std::size_t max_lines = 256);

template <typename... Args>
void log(LogLevel level, std::string_view category, Args&&... args) {
    if (!is_log_enabled(level)) {
        return;
    }
    std::ostringstream stream;
    if constexpr (sizeof...(Args) > 0) {
        (stream << ... << args);
    }
    write_log(level, category, stream.str());
}

} // namespace nsync::shared

#define NSYNC_LOG_TRACE(category, ...) \
    ::nsync::shared::log(::nsync::shared::LogLevel::Trace, category, __VA_ARGS__)
#define NSYNC_LOG_DEBUG(category, ...) \
    ::nsync::shared::log(::nsync::shared::LogLevel::Debug, category, __VA_ARGS__)
#define NSYNC_LOG_INFO(category, ...) \
    ::nsync::shared::log(::nsync::shared::LogLevel::Info, category, __VA_ARGS__)
#define NSYNC_LOG_WARN(category, ...) \
    ::nsync::shared::log(::nsync::shared::LogLevel::Warn, category, __VA_ARGS__)
#define NSYNC_LOG_ERROR(category, ...) \
    ::nsync::shared::log(::nsync::shared::LogLevel::Error, category, __VA_ARGS__)
