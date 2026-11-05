#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nsync::shared {

class AssetWatcher {
public:
    void watch(std::string key, std::filesystem::path path);
    void prime();
    [[nodiscard]] std::vector<std::string> poll();

    [[nodiscard]] bool empty() const { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        std::string key;
        std::filesystem::path path;
        bool exists = false;
        std::filesystem::file_time_type modified{};
        std::uintmax_t size = 0;
    };

    static Entry make_entry(std::string key, std::filesystem::path path);
    static void refresh(Entry& entry);

    std::vector<Entry> entries_;
};

} // namespace nsync::shared
