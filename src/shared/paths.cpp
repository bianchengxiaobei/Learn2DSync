#include "shared/paths.h"

#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace nsync::shared::paths {
namespace {

std::vector<path> search_roots() {
    std::vector<path> roots;

    std::error_code error;
    const path cwd = std::filesystem::current_path(error);
    if (!error) {
        roots.push_back(cwd);
    }

    const path exe_dir = executable_dir();
    if (!exe_dir.empty()) {
        roots.push_back(exe_dir);
        if (exe_dir.has_parent_path()) {
            roots.push_back(exe_dir.parent_path());
            if (exe_dir.parent_path().has_parent_path()) {
                roots.push_back(exe_dir.parent_path().parent_path());
            }
        }
    }

    const path source = source_dir();
    if (!source.empty()) {
        roots.push_back(source);
        roots.push_back(source / "build" / "vs2022" / "Debug");
        roots.push_back(source / "build" / "vs2022" / "Release");
    }

    return roots;
}

bool is_regular_file_path(const path& value) {
    std::error_code error;
    return std::filesystem::is_regular_file(value, error);
}

bool is_directory_path(const path& value) {
    std::error_code error;
    return std::filesystem::is_directory(value, error);
}

} // namespace

path executable_path() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    if (length == 0 || static_cast<std::size_t>(length) >= buffer.size()) {
        return {};
    }
    buffer.resize(length);
    return std::filesystem::path(buffer);
#else
    char buffer[PATH_MAX]{};
    const ssize_t length = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return {};
    }
    buffer[length] = '\0';
    return std::filesystem::path(buffer);
#endif
}

path executable_dir() {
    const path exe = executable_path();
    return exe.empty() ? path{} : exe.parent_path();
}

path source_dir() {
#ifdef NSYNC_SOURCE_DIR
    return std::filesystem::path(NSYNC_SOURCE_DIR);
#else
    return {};
#endif
}

std::optional<path> find_file(std::string_view relative) {
    const path relative_path{std::string(relative)};
    for (const path& root : search_roots()) {
        const path candidate = root / relative_path;
        if (is_regular_file_path(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::optional<path> find_directory(std::string_view relative) {
    const path relative_path{std::string(relative)};
    for (const path& root : search_roots()) {
        const path candidate = root / relative_path;
        if (is_directory_path(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::optional<path> find_asset_root() {
    for (const path& root : search_roots()) {
        const path candidate = root / "assets";
        if (is_directory_path(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::optional<path> source_asset_root() {
#ifdef NSYNC_SOURCE_DIR
    const path candidate = source_dir() / "assets";
    if (is_directory_path(candidate)) {
        return candidate;
    }
#endif
    return std::nullopt;
}

std::optional<path> find_shader_root() {
    for (const path& root : search_roots()) {
        const path candidate = root / "shaders";
        if (is_regular_file_path(candidate / "vs_dx11.bin")) {
            return candidate;
        }
        if (is_regular_file_path(root / "vs_dx11.bin")) {
            return root;
        }
    }
    return std::nullopt;
}

std::optional<path> find_config(std::string_view name) {
    const std::string relative = "config/" + std::string(name);
    return find_file(relative);
}

std::optional<path> find_system_font() {
    const std::vector<path> candidates = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "C:/Windows/Fonts/msyh.ttc",
    };
    for (const path& candidate : candidates) {
        if (is_regular_file_path(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::optional<path> find_cjk_font() {
    const std::vector<path> candidates = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyhbd.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
        "C:/Windows/Fonts/msjh.ttc",
    };
    for (const path& candidate : candidates) {
        if (is_regular_file_path(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

} // namespace nsync::shared::paths


