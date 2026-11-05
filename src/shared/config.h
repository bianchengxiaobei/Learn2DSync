#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace nsync::shared {

class Config {
public:
    bool load_file(const std::filesystem::path& path, std::string& error);
    bool save_file(const std::filesystem::path& path, std::string& error) const;
    void clear();
    void merge(const Config& other);

    void set(std::string key, std::string value);
    [[nodiscard]] bool contains(std::string_view key) const;

    [[nodiscard]] std::string get_string(
        std::string_view key,
        std::string fallback = {}) const;
    [[nodiscard]] int get_int(std::string_view key, int fallback) const;
    [[nodiscard]] int get_int(
        std::string_view key,
        int fallback,
        int minimum,
        int maximum) const;
    [[nodiscard]] bool get_bool(std::string_view key, bool fallback) const;

    [[nodiscard]] const std::map<std::string, std::string, std::less<>>& values() const {
        return values_;
    }

private:
    std::map<std::string, std::string, std::less<>> values_;
};

} // namespace nsync::shared
