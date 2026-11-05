#pragma once

#include "core/fix32.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace nsync::core {

inline constexpr std::uint32_t kAtlasVersion = 1;
inline constexpr std::size_t kMaxAtlasFrames = 4096;

struct AtlasFrame {
    std::string name;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    Fix32 pivot_x = Fix32::from_ratio(1, 2);
    Fix32 pivot_y = Fix32::one();
};

struct Atlas {
    std::string image;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::vector<AtlasFrame> frames;

    bool load_json(std::string_view json, std::string* error = nullptr);
    bool load_file(
        const std::filesystem::path& path,
        std::string* error = nullptr);
    [[nodiscard]] bool valid(std::string* error = nullptr) const;
    [[nodiscard]] const AtlasFrame* find(std::string_view name) const;
};

} // namespace nsync::core
