#include "shared/asset_watcher.h"

#include <system_error>
#include <utility>

namespace nsync::shared {

AssetWatcher::Entry AssetWatcher::make_entry(
    std::string key,
    std::filesystem::path path) {
    Entry entry;
    entry.key = std::move(key);
    entry.path = std::move(path);
    refresh(entry);
    return entry;
}

void AssetWatcher::refresh(Entry& entry) {
    std::error_code error;
    const std::filesystem::file_status status =
        std::filesystem::status(entry.path, error);
    if (error || !std::filesystem::is_regular_file(status)) {
        entry.exists = false;
        entry.modified = {};
        entry.size = 0;
        return;
    }

    const std::filesystem::file_time_type modified =
        std::filesystem::last_write_time(entry.path, error);
    if (error) {
        entry.exists = false;
        entry.modified = {};
        entry.size = 0;
        return;
    }

    const std::uintmax_t size = std::filesystem::file_size(entry.path, error);
    if (error) {
        entry.exists = false;
        entry.modified = {};
        entry.size = 0;
        return;
    }

    entry.exists = true;
    entry.modified = modified;
    entry.size = size;
}

void AssetWatcher::watch(std::string key, std::filesystem::path path) {
    for (Entry& entry : entries_) {
        if (entry.key == key) {
            if (entry.path != path) {
                entry.path = std::move(path);
                refresh(entry);
            }
            return;
        }
    }
    entries_.push_back(make_entry(std::move(key), std::move(path)));
}

void AssetWatcher::prime() {
    for (Entry& entry : entries_) {
        refresh(entry);
    }
}

std::vector<std::string> AssetWatcher::poll() {
    std::vector<std::string> changed;
    for (Entry& entry : entries_) {
        Entry current = entry;
        refresh(current);
        if (current.exists != entry.exists ||
            current.modified != entry.modified ||
            current.size != entry.size) {
            changed.push_back(entry.key);
            entry = std::move(current);
        }
    }
    return changed;
}

} // namespace nsync::shared
