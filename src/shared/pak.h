#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nsync::shared::pak {

inline constexpr char kMagic[8] = {'O', 'N', 'R', 'P', 'A', 'K', '1', '\0'};
inline constexpr std::uint32_t kVersion = 1;

struct Entry {
    std::string path;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

struct Index {
    std::vector<Entry> entries;
    std::uint64_t data_offset = 0;
};

bool write_pak(
    const std::filesystem::path& source_dir,
    const std::filesystem::path& pak_path,
    std::string& error);

bool read_pak_index(
    const std::filesystem::path& pak_path,
    Index& index,
    std::string& error);

} // namespace nsync::shared::pak
