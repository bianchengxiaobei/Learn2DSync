#pragma once

#include <cstdint>

namespace nsync::net {

class RateLimiter {
public:
    RateLimiter() = default;
    explicit RateLimiter(
        std::uint32_t limit,
        std::uint32_t window_ms = 1000)
        : limit_(limit),
          window_ms_(window_ms == 0 ? 1000 : window_ms) {}

    [[nodiscard]] bool allow(std::uint32_t now_ms) {
        if (limit_ == 0) {
            return true;
        }
        if (!started_ ||
            static_cast<std::uint32_t>(now_ms - window_start_ms_) >=
                window_ms_) {
            started_ = true;
            window_start_ms_ = now_ms;
            count_ = 0;
        }
        if (count_ >= limit_) {
            return false;
        }
        ++count_;
        return true;
    }

    void reset() {
        started_ = false;
        window_start_ms_ = 0;
        count_ = 0;
    }

    [[nodiscard]] std::uint32_t limit() const { return limit_; }
    [[nodiscard]] std::uint32_t window_ms() const { return window_ms_; }

private:
    std::uint32_t limit_ = 0;
    std::uint32_t window_ms_ = 1000;
    std::uint32_t window_start_ms_ = 0;
    std::uint32_t count_ = 0;
    bool started_ = false;
};

} // namespace nsync::net
