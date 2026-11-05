#pragma once

#include <array>
#include <cstdint>

namespace nsync::net {

class ServerClock {
public:
    void reset();
    void add_sample(
        std::uint32_t client_sent_ms,
        std::uint32_t server_time_ms,
        std::uint32_t received_ms);

    [[nodiscard]] std::uint32_t rtt_ms() const { return rtt_ms_; }
    [[nodiscard]] std::int64_t offset_ms() const { return offset_ms_; }
    [[nodiscard]] std::uint32_t server_to_local(std::uint32_t server_time_ms) const;
    [[nodiscard]] std::uint32_t local_to_server(std::uint32_t local_time_ms) const;
    [[nodiscard]] int sample_count() const { return sample_count_; }

private:
    struct Sample {
        std::uint32_t rtt_ms = 0;
        std::int64_t offset_ms = 0;
    };

    void recompute();

    std::array<Sample, 16> samples_{};
    int next_sample_ = 0;
    int sample_count_ = 0;
    std::uint32_t rtt_ms_ = 0;
    std::int64_t offset_ms_ = 0;
};

} // namespace nsync::net

