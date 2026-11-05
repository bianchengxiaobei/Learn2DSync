#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nsync::net {

struct TransportStats {
    std::uint64_t packets_sent = 0;
    std::uint64_t packets_received = 0;
    std::uint64_t bytes_sent = 0;
    std::uint64_t bytes_received = 0;
    std::uint32_t rtt_ms = 0;
    std::uint32_t waitsnd = 0;
    std::uint32_t retransmissions = 0;
    std::uint64_t packets_dropped = 0;
    std::uint64_t packets_duplicated = 0;
    std::uint64_t packets_delayed_spike = 0;
    std::uint64_t packets_traced = 0;
    std::uint32_t sessions = 0;
    std::uint32_t conv = 0;
};

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual bool send(const std::vector<std::uint8_t>& data) = 0;
    virtual bool poll(std::vector<std::vector<std::uint8_t>>& messages) = 0;
    virtual void update(std::uint32_t now_ms) = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool is_open() const = 0;
    [[nodiscard]] virtual TransportStats stats() const = 0;
};

} // namespace nsync::net

