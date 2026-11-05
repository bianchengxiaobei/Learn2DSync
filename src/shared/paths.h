#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

namespace nsync::shared::paths {

using std::filesystem::path;

[[nodiscard]] path executable_path();
[[nodiscard]] path executable_dir();
[[nodiscard]] path source_dir();

[[nodiscard]] std::optional<path> find_file(std::string_view relative);
[[nodiscard]] std::optional<path> find_directory(std::string_view relative);
[[nodiscard]] std::optional<path> find_asset_root();
[[nodiscard]] std::optional<path> source_asset_root();
[[nodiscard]] std::optional<path> find_shader_root();
[[nodiscard]] std::optional<path> find_config(std::string_view name);
[[nodiscard]] std::optional<path> find_system_font();
[[nodiscard]] std::optional<path> find_cjk_font();

} // namespace nsync::shared::paths
