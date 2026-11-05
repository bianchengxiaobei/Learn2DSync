#pragma once

#include "shared/pak.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nsync::shared {

[[nodiscard]] std::optional<std::string> normalize_virtual_path(
    std::string_view path);

class ResourceManager {
public:
    bool mount_directory(
        std::string name,
        const std::filesystem::path& root,
        std::string& error);
    bool mount_pak(
        std::string name,
        const std::filesystem::path& pak_path,
        std::string& error);

    [[nodiscard]] bool exists(std::string_view virtual_path) const;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> read(
        std::string_view virtual_path) const;
    [[nodiscard]] std::optional<std::string> read_text(
        std::string_view virtual_path) const;

    void clear();
    [[nodiscard]] std::size_t mount_count() const { return mounts_.size(); }
    [[nodiscard]] bool empty() const { return mounts_.empty(); }

private:
    enum class MountType {
        Directory,
        Pak,
    };

    struct Mount {
        MountType type = MountType::Directory;
        std::string name;
        std::filesystem::path path;
        pak::Index index;
    };

    [[nodiscard]] const pak::Entry* find_pak_entry(
        const Mount& mount,
        std::string_view normalized) const;

    std::vector<Mount> mounts_;
};

} // namespace nsync::shared
