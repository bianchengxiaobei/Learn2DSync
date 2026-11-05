#include "shared/config.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <string>

namespace nsync::shared {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

std::string lowercase(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (char ch : text) {
        result.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

} // namespace

bool Config::load_file(const std::filesystem::path& path, std::string& error) {
    values_.clear();

    std::ifstream file(path);
    if (!file) {
        error = "cannot open config: " + path.string();
        return false;
    }

    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        std::string_view view = trim(line);
        if (view.empty() || view.front() == '#' || view.front() == ';') {
            continue;
        }

        const std::size_t equals = view.find('=');
        if (equals == std::string_view::npos) {
            error = "invalid config line " + std::to_string(line_number) +
                " in " + path.string();
            return false;
        }

        const std::string_view key = trim(view.substr(0, equals));
        const std::string_view value = trim(view.substr(equals + 1));
        if (key.empty()) {
            error = "empty config key at line " + std::to_string(line_number) +
                " in " + path.string();
            return false;
        }
        values_.insert_or_assign(std::string(key), std::string(value));
    }

    if (!file.eof()) {
        error = "failed while reading config: " + path.string();
        return false;
    }
    return true;
}

bool Config::save_file(
    const std::filesystem::path& path,
    std::string& error) const {
    std::error_code filesystem_error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(
            path.parent_path(),
            filesystem_error);
        if (filesystem_error) {
            error = "cannot create config directory: " +
                path.parent_path().string() + ": " +
                filesystem_error.message();
            return false;
        }
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "cannot write config: " + path.string();
        return false;
    }

    file << "# NSYNC configuration\n";
    for (const auto& [key, value] : values_) {
        file << key << '=' << value << '\n';
    }
    if (!file) {
        error = "failed while writing config: " + path.string();
        return false;
    }
    return true;
}

void Config::clear() {
    values_.clear();
}

void Config::merge(const Config& other) {
    for (const auto& [key, value] : other.values_) {
        values_.insert_or_assign(key, value);
    }
}

void Config::set(std::string key, std::string value) {
    values_.insert_or_assign(std::move(key), std::move(value));
}

bool Config::contains(std::string_view key) const {
    return values_.find(key) != values_.end();
}

std::string Config::get_string(std::string_view key, std::string fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return fallback;
    }
    return it->second;
}

int Config::get_int(std::string_view key, int fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return fallback;
    }

    int value = 0;
    const char* begin = it->second.data();
    const char* end = begin + it->second.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return fallback;
    }
    return value;
}

int Config::get_int(
    std::string_view key,
    int fallback,
    int minimum,
    int maximum) const {
    return std::clamp(get_int(key, fallback), minimum, maximum);
}

bool Config::get_bool(std::string_view key, bool fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return fallback;
    }

    const std::string value = lowercase(it->second);
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        return false;
    }
    return fallback;
}

} // namespace nsync::shared
