#include "net/server_clock.h"

#include <algorithm>
#include <limits>

namespace nsync::net {

void ServerClock::reset() {
    samples_ = {};
    next_sample_ = 0;
    sample_count_ = 0;
    rtt_ms_ = 0;
    offset_ms_ = 0;
}

void ServerClock::add_sample(
    std::uint32_t client_sent_ms,
    std::uint32_t server_time_ms,
    std::uint32_t received_ms) {
    if (received_ms < client_sent_ms) {
        return;
    }
    const std::uint32_t rtt = received_ms - client_sent_ms;
    if (rtt > 5000) {
        return;
    }
    const std::int64_t midpoint =
        static_cast<std::int64_t>(client_sent_ms) +
        static_cast<std::int64_t>(rtt / 2U);
    const std::int64_t offset =
        static_cast<std::int64_t>(server_time_ms) - midpoint;

    Sample& sample = samples_[static_cast<std::size_t>(next_sample_)];
    sample.rtt_ms = rtt;
    sample.offset_ms = offset;
    next_sample_ = (next_sample_ + 1) %
        static_cast<int>(samples_.size());
    sample_count_ = std::min(sample_count_ + 1, static_cast<int>(samples_.size()));
    recompute();
}

void ServerClock::recompute() {
    if (sample_count_ == 0) {
        rtt_ms_ = 0;
        offset_ms_ = 0;
        return;
    }
    std::uint32_t best_rtt = std::numeric_limits<std::uint32_t>::max();
    for (int index = 0; index < sample_count_; ++index) {
        best_rtt = std::min(
            best_rtt,
            samples_[static_cast<std::size_t>(index)].rtt_ms);
    }
    std::int64_t sum = 0;
    int count = 0;
    for (int index = 0; index < sample_count_; ++index) {
        const Sample& sample = samples_[static_cast<std::size_t>(index)];
        if (sample.rtt_ms <= best_rtt + 8U) {
            sum += sample.offset_ms;
            ++count;
        }
    }
    if (count == 0) {
        return;
    }
    rtt_ms_ = best_rtt;
    offset_ms_ = sum / count;
}

std::uint32_t ServerClock::server_to_local(
    std::uint32_t server_time_ms) const {
    const std::int64_t local =
        static_cast<std::int64_t>(server_time_ms) - offset_ms_;
    const auto maximum = static_cast<std::int64_t>(
        std::numeric_limits<std::uint32_t>::max());
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(local, 0, maximum));
}

std::uint32_t ServerClock::local_to_server(
    std::uint32_t local_time_ms) const {
    const std::int64_t server =
        static_cast<std::int64_t>(local_time_ms) + offset_ms_;
    const auto maximum = static_cast<std::int64_t>(
        std::numeric_limits<std::uint32_t>::max());
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(server, 0, maximum));
}

} // namespace nsync::net
