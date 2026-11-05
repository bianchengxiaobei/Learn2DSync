#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nsync::net {

struct WanTraceSample {
    std::uint32_t at_ms = 0;
    std::uint32_t latency_ms = 0;
    std::uint32_t loss_percent = 0;
    std::uint32_t duplicate_percent = 0;
};

bool load_wan_trace(
    const std::string& path,
    std::vector<WanTraceSample>& samples,
    std::string* error = nullptr);

} // namespace nsync::net
