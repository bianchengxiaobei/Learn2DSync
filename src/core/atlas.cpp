#include "core/atlas.h"

#include "core/json.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <utility>

namespace nsync::core {
namespace {

bool read_text_file(
    const std::filesystem::path& path,
    std::string& text) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    text.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    return true;
}

bool read_u32(const JsonValue* value, std::uint32_t& result) {
    if (value == nullptr || value->type != JsonValue::Type::Number) {
        return false;
    }
    const double number = value->number_value;
    if (std::floor(number) != number ||
        number < 0.0 ||
        number > static_cast<double>(0xFFFFFFFFU)) {
        return false;
    }
    result = static_cast<std::uint32_t>(number);
    return true;
}

bool read_u16(const JsonValue* value, std::uint16_t& result) {
    std::uint32_t number = 0;
    if (!read_u32(value, number) || number > 0xFFFFU) {
        return false;
    }
    result = static_cast<std::uint16_t>(number);
    return true;
}

bool read_fix32(const JsonValue* value, Fix32& result) {
    if (value == nullptr || value->type != JsonValue::Type::Number) {
        return false;
    }
    const double number = value->number_value;
    if (!std::isfinite(number)) {
        return false;
    }
    result = Fix32::from_raw(static_cast<std::int32_t>(std::llround(
        number * static_cast<double>(Fix32::kOne))));
    return true;
}

bool parse_rect(
    const JsonValue* value,
    std::uint16_t& x,
    std::uint16_t& y,
    std::uint16_t& width,
    std::uint16_t& height) {
    if (value == nullptr ||
        value->type != JsonValue::Type::Array ||
        value->array.size() != 4) {
        return false;
    }
    return read_u16(&value->array[0], x) &&
        read_u16(&value->array[1], y) &&
        read_u16(&value->array[2], width) &&
        read_u16(&value->array[3], height);
}

bool parse_pivot(const JsonValue* value, AtlasFrame& frame) {
    if (value == nullptr) {
        return true;
    }
    if (value->type != JsonValue::Type::Array ||
        value->array.size() != 2) {
        return false;
    }
    Fix32 x;
    Fix32 y;
    if (!read_fix32(&value->array[0], x) ||
        !read_fix32(&value->array[1], y)) {
        return false;
    }
    if (x < Fix32::zero() || x > Fix32::one() ||
        y < Fix32::zero() || y > Fix32::one()) {
        return false;
    }
    frame.pivot_x = x;
    frame.pivot_y = y;
    return true;
}

} // namespace

bool Atlas::load_json(std::string_view json, std::string* error) {
    JsonValue root;
    std::string parse_error;
    if (!parse_json(json, root, &parse_error)) {
        if (error) {
            *error = "atlas json: " + parse_error;
        }
        return false;
    }
    if (root.type != JsonValue::Type::Object) {
        if (error) {
            *error = "atlas must be a json object";
        }
        return false;
    }

    Atlas next;
    std::uint32_t version = 0;
    const JsonValue* version_value = root.find("version");
    if (!read_u32(version_value, version)) {
        if (error) {
            *error = "atlas requires an integer version";
        }
        return false;
    }
    if (version != kAtlasVersion) {
        if (error) {
            *error = "unsupported atlas version";
        }
        return false;
    }

    const JsonValue* image_value = root.find("image");
    if (image_value == nullptr ||
        image_value->type != JsonValue::Type::String ||
        image_value->string_value.empty()) {
        if (error) {
            *error = "atlas requires a non-empty image path";
        }
        return false;
    }
    next.image = image_value->string_value;

    if (!read_u16(root.find("width"), next.width) ||
        !read_u16(root.find("height"), next.height) ||
        next.width == 0 ||
        next.height == 0) {
        if (error) {
            *error = "atlas requires positive width and height";
        }
        return false;
    }

    const JsonValue* frames_value = root.find("frames");
    if (frames_value == nullptr ||
        frames_value->type != JsonValue::Type::Array ||
        frames_value->array.empty() ||
        frames_value->array.size() > kMaxAtlasFrames) {
        if (error) {
            *error = "atlas requires a non-empty frames array";
        }
        return false;
    }

    next.frames.reserve(frames_value->array.size());
    for (const JsonValue& frame_value : frames_value->array) {
        if (frame_value.type != JsonValue::Type::Object) {
            if (error) {
                *error = "atlas frame must be an object";
            }
            return false;
        }
        const JsonValue* name_value = frame_value.find("name");
        if (name_value == nullptr ||
            name_value->type != JsonValue::Type::String ||
            name_value->string_value.empty() ||
            name_value->string_value.size() > 128) {
            if (error) {
                *error = "atlas frame requires a valid name";
            }
            return false;
        }

        AtlasFrame frame;
        frame.name = name_value->string_value;
        if (!parse_rect(
                frame_value.find("rect"),
                frame.x,
                frame.y,
                frame.width,
                frame.height)) {
            if (error) {
                *error = "atlas frame " + frame.name + " has an invalid rect";
            }
            return false;
        }
        if (!parse_pivot(frame_value.find("pivot"), frame)) {
            if (error) {
                *error = "atlas frame " + frame.name + " has an invalid pivot";
            }
            return false;
        }
        if (next.find(frame.name) != nullptr) {
            if (error) {
                *error = "duplicate atlas frame: " + frame.name;
            }
            return false;
        }
        next.frames.push_back(std::move(frame));
    }

    std::string validation_error;
    if (!next.valid(&validation_error)) {
        if (error) {
            *error = validation_error;
        }
        return false;
    }

    *this = std::move(next);
    if (error) {
        error->clear();
    }
    return true;
}

bool Atlas::load_file(
    const std::filesystem::path& path,
    std::string* error) {
    std::string text;
    if (!read_text_file(path, text)) {
        if (error) {
            *error = "failed to read atlas: " + path.string();
        }
        return false;
    }
    return load_json(text, error);
}

bool Atlas::valid(std::string* error) const {
    if (image.empty() || width == 0 || height == 0 ||
        frames.empty() || frames.size() > kMaxAtlasFrames) {
        if (error) {
            *error = "invalid atlas header";
        }
        return false;
    }

    for (std::size_t i = 0; i < frames.size(); ++i) {
        const AtlasFrame& frame = frames[i];
        if (frame.name.empty() ||
            frame.width == 0 ||
            frame.height == 0 ||
            frame.x + frame.width > width ||
            frame.y + frame.height > height) {
            if (error) {
                *error = "atlas frame out of bounds: " + frame.name;
            }
            return false;
        }
        if (frame.pivot_x < Fix32::zero() || frame.pivot_x > Fix32::one() ||
            frame.pivot_y < Fix32::zero() || frame.pivot_y > Fix32::one()) {
            if (error) {
                *error = "atlas frame pivot out of range: " + frame.name;
            }
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (frames[j].name == frame.name) {
                if (error) {
                    *error = "duplicate atlas frame: " + frame.name;
                }
                return false;
            }
        }
    }

    if (error) {
        error->clear();
    }
    return true;
}

const AtlasFrame* Atlas::find(std::string_view name) const {
    for (const AtlasFrame& frame : frames) {
        if (frame.name == name) {
            return &frame;
        }
    }
    return nullptr;
}

} // namespace nsync::core
