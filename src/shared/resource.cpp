#include "shared/resource.h"

#include <fstream>
#include <limits>
#include <system_error>

namespace nsync::shared {
namespace {

bool read_whole_file(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>& data) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return false;
    }
    const std::streampos end = input.tellg();
    if (end < 0) {
        return false;
    }
    const auto size = static_cast<std::uint64_t>(end);
    if (size > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    data.resize(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    if (size > 0 &&
        !input.read(
            reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(size))) {
        return false;
    }
    return true;
}

bool read_pak_range(
    const std::filesystem::path& path,
    const pak::Entry& entry,
    std::vector<std::uint8_t>& data) {
    if (entry.size > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    data.resize(static_cast<std::size_t>(entry.size));
    if (entry.size == 0) {
        return true;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    input.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
    if (!input) {
        return false;
    }
    input.read(
        reinterpret_cast<char*>(data.data()),
        static_cast<std::streamsize>(entry.size));
    return input.gcount() == static_cast<std::streamsize>(entry.size);
}

} // namespace

std::optional<std::string> normalize_virtual_path(std::string_view path) {
    std::string normalized;
    normalized.reserve(path.size());

    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find_first_of("/\\", start);
        const std::size_t length =
            (end == std::string_view::npos ? path.size() : end) - start;
        const std::string_view component = path.substr(start, length);

        if (!component.empty() && component != ".") {
            if (component == ".." || component.find(':') != std::string_view::npos) {
                return std::nullopt;
            }
            if (!normalized.empty()) {
                normalized.push_back('/');
            }
            normalized.append(component);
        }

        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }

    if (normalized.empty()) {
        return std::nullopt;
    }
    return normalized;
}

bool ResourceManager::mount_directory(
    std::string name,
    const std::filesystem::path& root,
    std::string& error) {
    std::error_code fs_error;
    if (!std::filesystem::is_directory(root, fs_error)) {
        error = "resource directory does not exist: " + root.string();
        return false;
    }

    Mount mount;
    mount.type = MountType::Directory;
    mount.name = std::move(name);
    mount.path = root;
    mounts_.push_back(std::move(mount));
    return true;
}

bool ResourceManager::mount_pak(
    std::string name,
    const std::filesystem::path& pak_path,
    std::string& error) {
    pak::Index index;
    if (!pak::read_pak_index(pak_path, index, error)) {
        return false;
    }

    Mount mount;
    mount.type = MountType::Pak;
    mount.name = std::move(name);
    mount.path = pak_path;
    mount.index = std::move(index);
    mounts_.push_back(std::move(mount));
    return true;
}

const pak::Entry* ResourceManager::find_pak_entry(
    const Mount& mount,
    std::string_view normalized) const {
    for (const pak::Entry& entry : mount.index.entries) {
        if (entry.path == normalized) {
            return &entry;
        }
    }
    return nullptr;
}

bool ResourceManager::exists(std::string_view virtual_path) const {
    const auto normalized = normalize_virtual_path(virtual_path);
    if (!normalized) {
        return false;
    }

    for (auto iterator = mounts_.rbegin(); iterator != mounts_.rend(); ++iterator) {
        const Mount& mount = *iterator;
        if (mount.type == MountType::Directory) {
            std::error_code fs_error;
            if (std::filesystem::is_regular_file(
                    mount.path / std::filesystem::path(*normalized),
                    fs_error)) {
                return true;
            }
        } else if (find_pak_entry(mount, *normalized) != nullptr) {
            return true;
        }
    }
    return false;
}

std::optional<std::vector<std::uint8_t>> ResourceManager::read(
    std::string_view virtual_path) const {
    const auto normalized = normalize_virtual_path(virtual_path);
    if (!normalized) {
        return std::nullopt;
    }

    for (auto iterator = mounts_.rbegin(); iterator != mounts_.rend(); ++iterator) {
        const Mount& mount = *iterator;
        std::vector<std::uint8_t> data;

        if (mount.type == MountType::Directory) {
            const std::filesystem::path file =
                mount.path / std::filesystem::path(*normalized);
            std::error_code fs_error;
            if (!std::filesystem::is_regular_file(file, fs_error)) {
                continue;
            }
            if (read_whole_file(file, data)) {
                return data;
            }
            continue;
        }

        const pak::Entry* entry = find_pak_entry(mount, *normalized);
        if (entry != nullptr && read_pak_range(mount.path, *entry, data)) {
            return data;
        }
    }
    return std::nullopt;
}

std::optional<std::string> ResourceManager::read_text(
    std::string_view virtual_path) const {
    const auto data = read(virtual_path);
    if (!data) {
        return std::nullopt;
    }
    if (data->empty()) {
        return std::string{};
    }
    return std::string(
        reinterpret_cast<const char*>(data->data()),
        data->size());
}

void ResourceManager::clear() {
    mounts_.clear();
}

} // namespace nsync::shared
