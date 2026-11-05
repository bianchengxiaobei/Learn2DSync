#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct SourceFrame {
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};

struct Options {
    fs::path input;
    fs::path output;
    fs::path metadata;
    std::string image = "sprites/heroes.png";
    std::uint32_t max_width = 512;
    bool check_only = false;
};

std::vector<std::uint8_t> read_file(const fs::path& path, std::string& error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "cannot open " + path.string();
        return {};
    }
    const std::streampos size = input.tellg();
    if (size <= 0) {
        error = "empty file " + path.string();
        return {};
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    if (!input.read(
            reinterpret_cast<char*>(data.data()),
            static_cast<std::streamsize>(data.size()))) {
        error = "short read from " + path.string();
        return {};
    }
    return data;
}

std::string escape_json(std::string_view text) {
    std::string result;
    result.reserve(text.size() + 8);
    for (char character : text) {
        switch (character) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result.push_back(character); break;
        }
    }
    return result;
}

bool load_sources(
    const fs::path& directory,
    std::vector<SourceFrame>& frames,
    std::string& error) {
    std::error_code fs_error;
    if (!fs::is_directory(directory, fs_error)) {
        error = "source directory does not exist: " + directory.string();
        return false;
    }

    std::vector<fs::path> paths;
    for (const fs::directory_entry& entry :
         fs::directory_iterator(directory, fs_error)) {
        if (fs_error) {
            error = "failed to scan source directory: " + fs_error.message();
            return false;
        }
        if (!entry.is_regular_file(fs_error)) {
            continue;
        }
        std::string extension = entry.path().extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
        if (extension == ".png") {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());

    if (paths.empty()) {
        error = "no PNG source frames in " + directory.string();
        return false;
    }
    if (paths.size() > 4096) {
        error = "too many source frames";
        return false;
    }

    frames.reserve(paths.size());
    for (const fs::path& path : paths) {
        std::vector<std::uint8_t> bytes = read_file(path, error);
        if (bytes.empty()) {
            return false;
        }
        if (bytes.size() > static_cast<std::size_t>(0x7FFFFFFF)) {
            error = "source image is too large: " + path.string();
            return false;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(
            bytes.data(),
            static_cast<int>(bytes.size()),
            &width,
            &height,
            &channels,
            4);
        if (pixels == nullptr) {
            error = "cannot decode " + path.string() + ": " +
                    stbi_failure_reason();
            return false;
        }
        if (width <= 0 || height <= 0 ||
            width > 0xFFFF || height > 0xFFFF) {
            stbi_image_free(pixels);
            error = "invalid image dimensions: " + path.string();
            return false;
        }

        SourceFrame frame;
        frame.name = path.stem().string();
        frame.width = static_cast<std::uint32_t>(width);
        frame.height = static_cast<std::uint32_t>(height);
        frame.pixels.assign(
            pixels,
            pixels +
                static_cast<std::size_t>(frame.width) * frame.height * 4U);
        stbi_image_free(pixels);
        frames.push_back(std::move(frame));
    }
    return true;
}

bool layout_frames(
    std::vector<SourceFrame>& frames,
    std::uint32_t max_width,
    std::uint32_t& atlas_width,
    std::uint32_t& atlas_height,
    std::string& error) {
    constexpr std::uint32_t padding = 1;
    atlas_width = max_width;
    std::uint32_t cursor_x = padding;
    std::uint32_t cursor_y = padding;
    std::uint32_t row_height = 0;

    for (SourceFrame& frame : frames) {
        if (frame.width + padding * 2U > max_width) {
            error = "source frame is wider than the atlas: " + frame.name;
            return false;
        }
        if (cursor_x + frame.width + padding > max_width) {
            cursor_x = padding;
            cursor_y += row_height + padding;
            row_height = 0;
        }
        frame.x = cursor_x;
        frame.y = cursor_y;
        cursor_x += frame.width + padding;
        row_height = std::max(row_height, frame.height);
    }

    atlas_height = cursor_y + row_height + padding;
    if (atlas_width == 0 || atlas_height == 0) {
        error = "empty atlas layout";
        return false;
    }
    if (atlas_width > 0xFFFFU || atlas_height > 0xFFFFU) {
        error = "atlas dimensions exceed metadata limits";
        return false;
    }
    return true;
}

bool write_png(
    const fs::path& path,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<std::uint8_t>& pixels,
    std::string& error) {
    std::error_code fs_error;
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path(), fs_error);
        if (fs_error) {
            error = "cannot create output directory: " + path.string();
            return false;
        }
    }

    if (stbi_write_png(
            path.string().c_str(),
            static_cast<int>(width),
            static_cast<int>(height),
            4,
            pixels.data(),
            static_cast<int>(width * 4U)) == 0) {
        error = "failed to write PNG: " + path.string();
        return false;
    }
    return true;
}

bool write_metadata(
    const fs::path& path,
    const std::string& image,
    std::uint32_t width,
    std::uint32_t height,
    const std::vector<SourceFrame>& frames,
    std::string& error) {
    std::error_code fs_error;
    if (!path.parent_path().empty()) {
        fs::create_directories(path.parent_path(), fs_error);
        if (fs_error) {
            error = "cannot create metadata directory: " + path.string();
            return false;
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "cannot open metadata output: " + path.string();
        return false;
    }
    output << "{\n"
           << "  \"version\": 1,\n"
           << "  \"image\": \"" << escape_json(image) << "\",\n"
           << "  \"width\": " << width << ",\n"
           << "  \"height\": " << height << ",\n"
           << "  \"frames\": [\n";
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const SourceFrame& frame = frames[i];
        output << "    {\"name\": \"" << escape_json(frame.name)
               << "\", \"rect\": [" << frame.x << ", " << frame.y
               << ", " << frame.width << ", " << frame.height
               << "], \"pivot\": [0.5, 1.0]}"
               << (i + 1 == frames.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
    if (!output.good()) {
        error = "failed to write metadata: " + path.string();
        return false;
    }
    return true;
}

bool parse_args(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        const auto require_value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << name << '\n';
                return nullptr;
            }
            return argv[++i];
        };

        if (argument == "--in") {
            const char* value = require_value("--in");
            if (value == nullptr) {
                return false;
            }
            options.input = value;
        } else if (argument == "--out") {
            const char* value = require_value("--out");
            if (value == nullptr) {
                return false;
            }
            options.output = value;
        } else if (argument == "--meta") {
            const char* value = require_value("--meta");
            if (value == nullptr) {
                return false;
            }
            options.metadata = value;
        } else if (argument == "--image") {
            const char* value = require_value("--image");
            if (value == nullptr) {
                return false;
            }
            options.image = value;
        } else if (argument == "--max-width") {
            const char* value = require_value("--max-width");
            if (value == nullptr) {
                return false;
            }
            try {
                options.max_width = static_cast<std::uint32_t>(
                    std::stoul(value));
            } catch (...) {
                std::cerr << "invalid --max-width\n";
                return false;
            }
        } else if (argument == "--check") {
            options.check_only = true;
        } else if (argument == "--help" || argument == "-h") {
            return false;
        } else {
            std::cerr << "unknown option: " << argument << '\n';
            return false;
        }
    }

    if (options.input.empty()) {
        std::cerr << "missing --in\n";
        return false;
    }
    if (!options.check_only &&
        (options.output.empty() || options.metadata.empty())) {
        std::cerr << "packing requires --out and --meta\n";
        return false;
    }
    if (options.max_width < 64 || options.max_width > 8192) {
        std::cerr << "--max-width must be in 64..8192\n";
        return false;
    }
    return true;
}

void print_usage() {
    std::cout
        << "usage: nsync_atlas --in <source-dir> --out <atlas.png> "
           "--meta <atlas.json> [--image <virtual-path>] "
           "[--max-width 512] [--check]\n";
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_args(argc, argv, options)) {
        print_usage();
        return 1;
    }

    std::string error;
    std::vector<SourceFrame> frames;
    if (!load_sources(options.input, frames, error)) {
        std::cerr << "atlas: " << error << '\n';
        return 1;
    }

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    if (!layout_frames(frames, options.max_width, width, height, error)) {
        std::cerr << "atlas: " << error << '\n';
        return 1;
    }

    if (options.check_only) {
        std::cout << "atlas check: frames=" << frames.size()
                  << " size=" << width << 'x' << height << '\n';
        return 0;
    }

    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(width) * height * 4U,
        0);
    for (const SourceFrame& frame : frames) {
        for (std::uint32_t row = 0; row < frame.height; ++row) {
            const std::size_t source_offset =
                static_cast<std::size_t>(row) * frame.width * 4U;
            const std::size_t destination_offset =
                (static_cast<std::size_t>(frame.y + row) * width +
                 frame.x) * 4U;
            std::copy(
                frame.pixels.begin() +
                    static_cast<std::ptrdiff_t>(source_offset),
                frame.pixels.begin() +
                    static_cast<std::ptrdiff_t>(source_offset + frame.width * 4U),
                pixels.begin() +
                    static_cast<std::ptrdiff_t>(destination_offset));
        }
    }

    if (!write_png(options.output, width, height, pixels, error)) {
        std::cerr << "atlas: " << error << '\n';
        return 1;
    }
    if (!write_metadata(
            options.metadata,
            options.image,
            width,
            height,
            frames,
            error)) {
        std::cerr << "atlas: " << error << '\n';
        return 1;
    }

    std::cout << "atlas: frames=" << frames.size()
              << " size=" << width << 'x' << height
              << " image=" << options.output.string()
              << " metadata=" << options.metadata.string() << '\n';
    return 0;
}
