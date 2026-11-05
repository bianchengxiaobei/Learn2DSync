#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace nsync::shared {

void install_crash_handler(
    std::string_view app_name,
    const std::filesystem::path& crash_dir);
void shutdown_crash_handler();
void set_last_snapshot(std::uint32_t tick, std::uint64_t hash);
[[nodiscard]] std::filesystem::path crash_report_dir();

bool write_crash_report(
    const std::filesystem::path& path,
    std::string_view app_name,
    std::uint32_t exception_code,
    std::uint64_t exception_address,
    std::string* error = nullptr);

} // namespace nsync::shared
