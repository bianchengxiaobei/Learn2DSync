#pragma once

#include "net/transport.h"
#include "net/wan_trace.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nsync::net {

struct TransportMessage {
    std::uint64_t peer = 0;
    std::vector<std::uint8_t> data;
};

class KcpTransport final : public ITransport {
public:
    KcpTransport();
    ~KcpTransport() override;

    KcpTransport(const KcpTransport&) = delete;
    KcpTransport& operator=(const KcpTransport&) = delete;

    bool open(
        const std::string& host,
        std::uint16_t port,
        std::uint32_t conv,
        std::string* error = nullptr);

    void set_latency_ms(std::uint32_t latency_ms);
    [[nodiscard]] std::uint32_t latency_ms() const;
    void set_network_simulation(
        std::uint32_t jitter_ms,
        std::uint32_t loss_percent,
        std::uint32_t duplicate_percent,
        std::uint32_t burst_length = 1);
    void set_latency_spikes(
        std::uint32_t spike_ms,
        std::uint32_t interval_ms,
        std::uint32_t duration_ms);
    void set_network_trace(std::vector<WanTraceSample> samples);

    bool send(const std::vector<std::uint8_t>& data) override;
    bool poll(std::vector<std::vector<std::uint8_t>>& messages) override;
    void update(std::uint32_t now_ms) override;
    void close() override;
    [[nodiscard]] bool is_open() const override;
    [[nodiscard]] TransportStats stats() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class KcpServerTransport {
public:
    KcpServerTransport();
    ~KcpServerTransport();

    KcpServerTransport(const KcpServerTransport&) = delete;
    KcpServerTransport& operator=(const KcpServerTransport&) = delete;

    bool start(std::uint16_t port, std::string* error = nullptr);
    void close();

    bool poll(
        std::vector<TransportMessage>& messages,
        std::string* error = nullptr);
    bool send(
        std::uint64_t peer,
        const std::vector<std::uint8_t>& data,
        std::string* error = nullptr);
    void update(std::uint32_t now_ms);
    void remove_peer(std::uint64_t peer);

    [[nodiscard]] std::uint16_t port() const;
    [[nodiscard]] bool is_open() const;
    [[nodiscard]] TransportStats stats() const;
    [[nodiscard]] int session_count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nsync::net
