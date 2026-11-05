#include "shared/version.h"

#include <array>
#include <cstddef>

namespace nsync::shared {
namespace {

bool parse_version(
    std::string_view text,
    std::array<int, 4>& parts,
    std::size_t& count) {
    parts = {};
    count = 0;
    if (text.empty()) {
        return false;
    }
    std::size_t position = 0;
    while (position < text.size()) {
        if (count >= parts.size()) {
            return false;
        }
        const std::size_t start = position;
        int value = 0;
        while (position < text.size() && text[position] >= '0' &&
               text[position] <= '9') {
            value = value * 10 + (text[position] - '0');
            ++position;
        }
        if (position == start) {
            return false;
        }
        parts[count++] = value;
        if (position == text.size()) {
            break;
        }
        if (text[position] != '.') {
            return false;
        }
        ++position;
        if (position == text.size()) {
            return false;
        }
    }
    return count > 0;
}

} // namespace

std::string version_string() {
    std::string result(kVersion);
    result += " (";
    result += kBuildConfig;
    result += ")";
    return result;
}

bool valid_version(std::string_view version) {
    std::array<int, 4> parts{};
    std::size_t count = 0;
    return parse_version(version, parts, count);
}

int compare_versions(std::string_view left, std::string_view right) {
    std::array<int, 4> left_parts{};
    std::array<int, 4> right_parts{};
    std::size_t left_count = 0;
    std::size_t right_count = 0;
    const bool left_valid = parse_version(left, left_parts, left_count);
    const bool right_valid = parse_version(right, right_parts, right_count);
    if (!left_valid || !right_valid) {
        if (left == right) {
            return 0;
        }
        return left < right ? -1 : 1;
    }
    for (std::size_t index = 0; index < left_parts.size(); ++index) {
        const int left_value =
            index < left_count ? left_parts[index] : 0;
        const int right_value =
            index < right_count ? right_parts[index] : 0;
        if (left_value < right_value) {
            return -1;
        }
        if (left_value > right_value) {
            return 1;
        }
    }
    return 0;
}

} // namespace nsync::shared
