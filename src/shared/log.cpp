#include "shared/log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace nsync::shared {
namespace {

std::atomic<LogLevel> g_level{LogLevel::Info};
std::mutex g_output_mutex;
constexpr std::size_t kRecentLogCapacity = 256;
std::deque<std::string> g_recent_logs;

std::string lowercase(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (char ch : text) {
        result.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t value = std::chrono::system_clock::to_time_t(now);

    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif

    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);

    char result[40]{};
    std::snprintf(
        result,
        sizeof(result),
        "%s.%03lld",
        buffer,
        static_cast<long long>(millis.count()));
    return result;
}

} // namespace

LogLevel log_level() {
    return g_level.load(std::memory_order_relaxed);
}

void set_log_level(LogLevel level) {
    g_level.store(level, std::memory_order_relaxed);
}

bool parse_log_level(std::string_view text, LogLevel& level) {
    const std::string value = lowercase(text);
    if (value == "trace") {
        level = LogLevel::Trace;
    } else if (value == "debug") {
        level = LogLevel::Debug;
    } else if (value == "info") {
        level = LogLevel::Info;
    } else if (value == "warn" || value == "warning") {
        level = LogLevel::Warn;
    } else if (value == "error") {
        level = LogLevel::Error;
    } else if (value == "off" || value == "none") {
        level = LogLevel::Off;
    } else {
        return false;
    }
    return true;
}

std::string_view log_level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Off: return "OFF";
    }
    return "INFO";
}

bool is_log_enabled(LogLevel level) {
    const LogLevel current = g_level.load(std::memory_order_relaxed);
    return current != LogLevel::Off &&
        static_cast<int>(level) >= static_cast<int>(current);
}

void write_log(LogLevel level, std::string_view category, std::string_view message) {
    if (!is_log_enabled(level)) {
        return;
    }

    std::ostringstream line;
    line << timestamp()
         << " [" << log_level_name(level) << "]"
         << " [" << category << "] "
         << message;

    std::lock_guard<std::mutex> lock(g_output_mutex);
    std::string text = line.str();
    g_recent_logs.push_back(text);
    while (g_recent_logs.size() > kRecentLogCapacity) {
        g_recent_logs.pop_front();
    }
    std::ostream& output = (level >= LogLevel::Warn) ? std::cerr : std::cout;
    output << text << '\n';
    if (level >= LogLevel::Warn) {
        output.flush();
    }
}

std::vector<std::string> recent_log_lines(std::size_t max_lines) {
    std::vector<std::string> result;
    std::unique_lock<std::mutex> lock(g_output_mutex, std::try_to_lock);
    if (!lock.owns_lock() || max_lines == 0) {
        return result;
    }
    const std::size_t count =
        std::min(max_lines, g_recent_logs.size());
    result.reserve(count);
    const std::size_t start = g_recent_logs.size() - count;
    for (std::size_t index = start; index < g_recent_logs.size(); ++index) {
        result.push_back(g_recent_logs[index]);
    }
    return result;
}

} // namespace nsync::shared
