#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nsync::client {

struct RgbaImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;
};

bool load_rgba_image(
    const std::uint8_t* data,
    std::size_t size,
    RgbaImage& image,
    std::string& error);

} // namespace nsync::client
