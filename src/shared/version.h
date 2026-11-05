#pragma once

#include <string>
#include <string_view>

#ifndef NSYNC_VERSION
#define NSYNC_VERSION "0.0.0"
#endif
#ifndef NSYNC_BUILD_CONFIG
#define NSYNC_BUILD_CONFIG "unknown"
#endif

namespace nsync::shared {

inline constexpr std::string_view kVersion = NSYNC_VERSION;
inline constexpr std::string_view kBuildConfig = NSYNC_BUILD_CONFIG;

[[nodiscard]] std::string version_string();
[[nodiscard]] bool valid_version(std::string_view version);
[[nodiscard]] int compare_versions(
    std::string_view left,
    std::string_view right);

} // namespace nsync::shared
