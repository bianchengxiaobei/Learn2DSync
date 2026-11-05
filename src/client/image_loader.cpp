#include "client/image_loader.h"

#if defined(_MSC_VER)
#pragma optimize("", on)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("O2")
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#if defined(__GNUC__) && !defined(_MSC_VER)
#pragma GCC pop_options
#endif

namespace nsync::client {

bool load_rgba_image(
    const std::uint8_t* data,
    std::size_t size,
    RgbaImage& image,
    std::string& error) {
    if (data == nullptr || size == 0) {
        error = "empty image";
        return false;
    }
    if (size > static_cast<std::size_t>(0x7FFFFFFF)) {
        error = "image is too large";
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(
        data,
        static_cast<int>(size),
        &width,
        &height,
        &channels,
        4);
    if (pixels == nullptr) {
        error = stbi_failure_reason();
        return false;
    }
    if (width <= 0 || height <= 0 ||
        width > 0xFFFF || height > 0xFFFF) {
        stbi_image_free(pixels);
        error = "invalid image dimensions";
        return false;
    }

    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    image.pixels.assign(
        pixels,
        pixels + static_cast<std::size_t>(width) * height * 4U);
    stbi_image_free(pixels);
    return true;
}

} // namespace nsync::client
