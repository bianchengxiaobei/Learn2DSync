#include "shared/crash_handler.h"

#include "shared/log.h"
#include "shared/version.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace nsync::shared {
namespace {

std::atomic<bool> g_installed{false};
std::atomic<bool> g_handling{false};
std::atomic<bool> g_has_snapshot{false};
std::atomic<std::uint32_t> g_last_snapshot_tick{0};
std::atomic<std::uint64_t> g_last_snapshot_hash{0};
std::string g_app_name = "nsync";
std::filesystem::path g_crash_dir;

#ifdef _WIN32
LPTOP_LEVEL_EXCEPTION_FILTER g_previous_filter = nullptr;
#endif

std::string local_timestamp(bool filename_safe) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    char buffer[64]{};
    std::strftime(
        buffer,
        sizeof(buffer),
        filename_safe ? "%Y%m%d_%H%M%S" : "%Y-%m-%d %H:%M:%S",
        &local);
    return buffer;
}

std::filesystem::path unique_report_base(
    const std::filesystem::path& directory,
    const std::string& suffix) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const std::string base =
        "crash_" + local_timestamp(true) + "_" +
        std::to_string(
#ifdef _WIN32
            static_cast<unsigned long>(GetCurrentProcessId())
#else
            static_cast<unsigned long>(getpid())
#endif
        );
    std::filesystem::path candidate = directory / (base + suffix);
    int index = 1;
    while (std::filesystem::exists(candidate, error) && !error) {
        candidate = directory /
            (base + "_" + std::to_string(index++) + suffix);
    }
    return candidate;
}

std::string hex_value(std::uint64_t value, int width) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(width)
           << std::setfill('0') << value;
    return stream.str();
}

#ifdef _WIN32
bool write_minidump(
    const std::filesystem::path& path,
    EXCEPTION_POINTERS* exception) {
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = exception;
    info.ClientPointers = FALSE;
    const BOOL written = MiniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        file,
        MiniDumpNormal,
        exception != nullptr ? &info : nullptr,
        nullptr,
        nullptr);
    CloseHandle(file);
    return written != FALSE;
}

LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS* exception) {
    if (g_handling.exchange(true)) {
        return EXCEPTION_EXECUTE_HANDLER;
    }
    const EXCEPTION_RECORD* record =
        exception != nullptr ? exception->ExceptionRecord : nullptr;
    const std::uint32_t code =
        record != nullptr ? record->ExceptionCode : 0U;
    const std::uint64_t address = record != nullptr
        ? reinterpret_cast<std::uint64_t>(record->ExceptionAddress)
        : 0U;
    const std::filesystem::path report =
        unique_report_base(g_crash_dir.empty()
                ? std::filesystem::current_path()
                : g_crash_dir,
            ".txt");
    write_crash_report(report, g_app_name, code, address, nullptr);
    const std::filesystem::path dump = report.parent_path() /
        (report.stem().string() + ".dmp");
    write_minidump(dump, exception);
    return EXCEPTION_EXECUTE_HANDLER;
}
#else
void posix_signal_handler(int signal) {
    if (g_handling.exchange(true)) {
        std::_Exit(128 + signal);
    }
    const std::filesystem::path report =
        unique_report_base(g_crash_dir.empty()
                ? std::filesystem::current_path()
                : g_crash_dir,
            ".txt");
    write_crash_report(
        report,
        g_app_name,
        static_cast<std::uint32_t>(signal),
        0,
        nullptr);
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}
#endif

} // namespace

void set_last_snapshot(std::uint32_t tick, std::uint64_t hash) {
    g_last_snapshot_tick.store(tick, std::memory_order_relaxed);
    g_last_snapshot_hash.store(hash, std::memory_order_relaxed);
    g_has_snapshot.store(true, std::memory_order_relaxed);
}

std::filesystem::path crash_report_dir() {
    return g_crash_dir;
}

bool write_crash_report(
    const std::filesystem::path& path,
    std::string_view app_name,
    std::uint32_t exception_code,
    std::uint64_t exception_address,
    std::string* error) {
    std::ostringstream report;
    report << "NSYNC CRASH REPORT\n";
    report << "time=" << local_timestamp(false) << "\n";
    report << "app=" << app_name << "\n";
    report << "version=" << kVersion << "\n";
    report << "build_config=" << kBuildConfig << "\n";
    report << "pid="
#ifdef _WIN32
           << static_cast<unsigned long>(GetCurrentProcessId())
#else
           << static_cast<unsigned long>(getpid())
#endif
           << "\n";
    report << "exception_code=" << hex_value(exception_code, 8) << "\n";
    report << "exception_address=" << hex_value(exception_address, 16)
           << "\n";
    if (g_has_snapshot.load(std::memory_order_relaxed)) {
        report << "last_snapshot_tick="
               << g_last_snapshot_tick.load(std::memory_order_relaxed)
               << "\n";
        report << "last_snapshot_hash="
               << hex_value(
                      g_last_snapshot_hash.load(std::memory_order_relaxed),
                      16)
               << "\n";
    } else {
        report << "last_snapshot=none\n";
    }
    report << "recent_logs:\n";
    const std::vector<std::string> logs = recent_log_lines(256);
    if (logs.empty()) {
        report << "(no recent logs)\n";
    } else {
        for (const std::string& line : logs) {
            report << line << "\n";
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error != nullptr) {
            *error = "cannot open crash report: " + path.string();
        }
        return false;
    }
    output << report.str();
    output.flush();
    if (!output) {
        if (error != nullptr) {
            *error = "cannot write crash report: " + path.string();
        }
        return false;
    }
    return true;
}

void install_crash_handler(
    std::string_view app_name,
    const std::filesystem::path& crash_dir) {
    g_app_name = app_name.empty() ? "nsync" : std::string(app_name);
    g_crash_dir = crash_dir;
    std::error_code error;
    if (!g_crash_dir.empty()) {
        std::filesystem::create_directories(g_crash_dir, error);
        if (error) {
            NSYNC_LOG_WARN(
                "crash",
                "cannot create crash directory ",
                g_crash_dir.string(),
                ": ",
                error.message());
            g_crash_dir = std::filesystem::temp_directory_path(error) /
                "nsync_crash";
            if (!error) {
                std::filesystem::create_directories(g_crash_dir, error);
            }
        }
    }
    if (g_crash_dir.empty()) {
        g_crash_dir = std::filesystem::current_path(error);
    }

#ifdef _WIN32
    g_previous_filter = SetUnhandledExceptionFilter(
        unhandled_exception_filter);
#else
    std::signal(SIGSEGV, posix_signal_handler);
    std::signal(SIGABRT, posix_signal_handler);
    std::signal(SIGFPE, posix_signal_handler);
    std::signal(SIGILL, posix_signal_handler);
#endif
    g_installed.store(true, std::memory_order_relaxed);
    NSYNC_LOG_INFO(
        "crash",
        "crash reports directory: ",
        g_crash_dir.string());
}

void shutdown_crash_handler() {
    if (!g_installed.exchange(false)) {
        return;
    }
#ifdef _WIN32
    SetUnhandledExceptionFilter(g_previous_filter);
    g_previous_filter = nullptr;
#else
    std::signal(SIGSEGV, SIG_DFL);
    std::signal(SIGABRT, SIG_DFL);
    std::signal(SIGFPE, SIG_DFL);
    std::signal(SIGILL, SIG_DFL);
#endif
}

} // namespace nsync::shared
