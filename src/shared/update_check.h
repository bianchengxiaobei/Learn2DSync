#pragma once

#include <string>
#include <string_view>

namespace nsync::shared {

struct UpdateCheckResult {
    std::string current;
    std::string latest;
    std::string url;
    std::string notes;
    bool update_available = false;
};

[[nodiscard]] bool check_for_update(
    std::string_view manifest_source,
    std::string_view current_version,
    UpdateCheckResult& result,
    std::string* error = nullptr);

} // namespace nsync::shared
