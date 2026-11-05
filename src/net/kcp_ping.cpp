#include "net/kcp_ping.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <ikcp.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <random>
#include <thread>
#include <vector>

namespace nsync::net {
namespace {

constexpr IUINT32 kConv = 0x4F4E5244; // "ONRD"
constexpr int kPingIntervalMs = 250;
constexpr int kReceiveBuffer = 2048;

std::uint32_t now_ms() {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return static_cast<std::uint32_t>(duration_cast<milliseconds>(steady_clock::now() - start).count());
}

class WsaGuard {
public:
    WsaGuard() {
        WSADATA data{};
        ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WsaGuard() {
        if (ok_) {
            WSACleanup();
        }
    }
    bool ok() const { return ok_; }

private:
    bool ok_ = false;
};

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket() { close(); }

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool open(std::uint16_t bind_port) {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd_ == INVALID_SOCKET) {
            std::fprintf(stderr, "socket() failed: %d\n", WSAGetLastError());
            return false;
        }

        int reuse = 1;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(bind_port);
        if (bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
            std::fprintf(stderr, "bind(%u) failed: %d\n", static_cast<unsigned>(bind_port), WSAGetLastError());
            close();
            return false;
        }

        u_long non_blocking = 1;
        ioctlsocket(fd_, FIONBIO, &non_blocking);
        return true;
    }

    void close() {
        if (fd_ != INVALID_SOCKET) {
            closesocket(fd_);
            fd_ = INVALID_SOCKET;
        }
    }

    bool send_to(const char* data, int size, const sockaddr_in& peer) {
        const int sent = sendto(
            fd_, data, size, 0,
            reinterpret_cast<const sockaddr*>(&peer), sizeof(peer));
        return sent == size;
    }

    int receive_from(char* data, int capacity, sockaddr_in& peer) {
        int peer_size = sizeof(peer);
        const int result = recvfrom(
            fd_, data, capacity, 0,
            reinterpret_cast<sockaddr*>(&peer), &peer_size);
        if (result == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                return 0;
            }
            return -1;
        }
        return result;
    }

private:
    SOCKET fd_ = INVALID_SOCKET;
};

bool resolve_ipv4(const char* host, std::uint16_t port, sockaddr_in& address) {
    address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    return inet_pton(AF_INET, host, &address.sin_addr) == 1;
}

struct QueuedPacket {
    std::vector<char> data;
    sockaddr_in peer{};
    std::uint32_t due_ms = 0;
};

class SimulatedLink {
public:
    SimulatedLink(UdpSocket& socket, int delay_ms, int loss_percent)
        : socket_(socket),
          delay_ms_(std::max(0, delay_ms)),
          loss_percent_(std::clamp(loss_percent, 0, 100)),
          rng_(0x4F4E5244) {}

    void send(const char* data, int size, const sockaddr_in& peer) {
        ++sent_packets_;
        if (loss_percent_ > 0 && distribution_(rng_) < loss_percent_) {
            ++dropped_packets_;
            return;
        }

        if (delay_ms_ == 0) {
            if (!socket_.send_to(data, size, peer)) {
                ++send_errors_;
            }
            return;
        }

        QueuedPacket packet;
        packet.data.assign(data, data + size);
        packet.peer = peer;
        packet.due_ms = now_ms() + static_cast<std::uint32_t>(delay_ms_);
        queue_.push_back(std::move(packet));
    }

    void pump() {
        const std::uint32_t now = now_ms();
        while (!queue_.empty() && queue_.front().due_ms <= now) {
            QueuedPacket packet = std::move(queue_.front());
            queue_.pop_front();
            if (!socket_.send_to(packet.data.data(), static_cast<int>(packet.data.size()), packet.peer)) {
                ++send_errors_;
            }
        }
    }

    std::uint64_t sent_packets() const { return sent_packets_; }
    std::uint64_t dropped_packets() const { return dropped_packets_; }
    std::uint64_t send_errors() const { return send_errors_; }

private:
    UdpSocket& socket_;
    int delay_ms_ = 0;
    int loss_percent_ = 0;
    std::mt19937 rng_;
    std::uniform_int_distribution<int> distribution_{0, 99};
    std::deque<QueuedPacket> queue_;
    std::uint64_t sent_packets_ = 0;
    std::uint64_t dropped_packets_ = 0;
    std::uint64_t send_errors_ = 0;
};

struct KcpEndpoint {
    ikcpcb* kcp = nullptr;
    SimulatedLink* link = nullptr;
    sockaddr_in peer{};
    bool has_peer = false;
};

int kcp_output(const char* data, int size, ikcpcb*, void* user) {
    auto* endpoint = static_cast<KcpEndpoint*>(user);
    if (endpoint == nullptr || endpoint->link == nullptr || !endpoint->has_peer) {
        return 0;
    }
    endpoint->link->send(data, size, endpoint->peer);
    return 0;
}

ikcpcb* make_kcp(KcpEndpoint& endpoint) {
    ikcpcb* kcp = ikcp_create(kConv, &endpoint);
    if (kcp == nullptr) {
        return nullptr;
    }
    ikcp_setoutput(kcp, kcp_output);
    ikcp_nodelay(kcp, 1, 10, 2, 1);
    ikcp_wndsize(kcp, 256, 256);
    ikcp_setmtu(kcp, 1200);
    endpoint.kcp = kcp;
    return kcp;
}

struct PingMessage {
    std::uint32_t magic = 0x50494E47; // "PING"
    std::uint32_t sequence = 0;
    std::uint32_t sent_ms = 0;
};

} // namespace

int run_kcp_server(std::uint16_t port, int duration_ms, int delay_ms, int loss_percent) {
    WsaGuard wsa;
    if (!wsa.ok()) {
        std::fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    UdpSocket socket;
    if (!socket.open(port)) {
        return 1;
    }

    SimulatedLink link(socket, delay_ms, loss_percent);
    KcpEndpoint endpoint;
    endpoint.link = &link;

    std::printf(
        "kcp-server: port=%u duration=%dms delay=%dms loss=%d%% conv=0x%08X\n",
        static_cast<unsigned>(port), duration_ms, delay_ms, loss_percent,
        static_cast<unsigned>(kConv));

    const std::uint32_t start = now_ms();
    while (static_cast<int>(now_ms() - start) < duration_ms) {
        link.pump();

        char buffer[kReceiveBuffer];
        for (;;) {
            sockaddr_in from{};
            const int size = socket.receive_from(buffer, sizeof(buffer), from);
            if (size <= 0) {
                break;
            }

            if (!endpoint.has_peer) {
                endpoint.peer = from;
                endpoint.has_peer = true;
                if (make_kcp(endpoint) == nullptr) {
                    std::fprintf(stderr, "ikcp_create failed\n");
                    return 1;
                }
            }

            if (endpoint.kcp != nullptr) {
                ikcp_input(endpoint.kcp, buffer, size);
            }
        }

        if (endpoint.kcp != nullptr) {
            char message[kReceiveBuffer];
            for (;;) {
                const int size = ikcp_recv(endpoint.kcp, message, sizeof(message));
                if (size <= 0) {
                    break;
                }
                ikcp_send(endpoint.kcp, message, size);
            }
            ikcp_update(endpoint.kcp, now_ms());
            ikcp_flush(endpoint.kcp);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (endpoint.kcp != nullptr) {
        ikcp_release(endpoint.kcp);
    }

    const std::uint64_t total = link.sent_packets() + link.dropped_packets();
    std::printf(
        "kcp-server: packets sent=%llu dropped=%llu errors=%llu (%.1f%% simulated loss)\n",
        static_cast<unsigned long long>(link.sent_packets()),
        static_cast<unsigned long long>(link.dropped_packets()),
        static_cast<unsigned long long>(link.send_errors()),
        total == 0 ? 0.0 : 100.0 * static_cast<double>(link.dropped_packets()) / static_cast<double>(total));
    return 0;
}

int run_kcp_client(const char* host, std::uint16_t port, int duration_ms, int delay_ms, int loss_percent) {
    WsaGuard wsa;
    if (!wsa.ok()) {
        std::fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    UdpSocket socket;
    if (!socket.open(0)) {
        return 1;
    }

    SimulatedLink link(socket, delay_ms, loss_percent);
    KcpEndpoint endpoint;
    endpoint.link = &link;
    endpoint.has_peer = resolve_ipv4(host, port, endpoint.peer);
    if (!endpoint.has_peer) {
        std::fprintf(stderr, "invalid IPv4 address: %s\n", host);
        return 1;
    }
    if (make_kcp(endpoint) == nullptr) {
        std::fprintf(stderr, "ikcp_create failed\n");
        return 1;
    }

    std::printf(
        "kcp-client: %s:%u duration=%dms delay=%dms loss=%d%% conv=0x%08X\n",
        host, static_cast<unsigned>(port), duration_ms, delay_ms, loss_percent,
        static_cast<unsigned>(kConv));

    std::vector<std::uint32_t> rtt_samples;
    std::uint32_t next_ping_ms = 0;
    std::uint32_t sequence = 0;
    const std::uint32_t start = now_ms();

    while (static_cast<int>(now_ms() - start) < duration_ms) {
        link.pump();
        const std::uint32_t now = now_ms();

        if (now >= next_ping_ms) {
            PingMessage ping;
            ping.sequence = sequence++;
            ping.sent_ms = now;
            ikcp_send(endpoint.kcp, reinterpret_cast<const char*>(&ping), sizeof(ping));
            ikcp_flush(endpoint.kcp);
            next_ping_ms = now + kPingIntervalMs;
        }

        char buffer[kReceiveBuffer];
        for (;;) {
            sockaddr_in from{};
            const int size = socket.receive_from(buffer, sizeof(buffer), from);
            if (size <= 0) {
                break;
            }
            ikcp_input(endpoint.kcp, buffer, size);
        }

        for (;;) {
            PingMessage pong;
            const int size = ikcp_recv(endpoint.kcp, reinterpret_cast<char*>(&pong), sizeof(pong));
            if (size <= 0) {
                break;
            }
            if (size == sizeof(PingMessage) && pong.magic == 0x50494E47) {
                const std::uint32_t rtt = now_ms() - pong.sent_ms;
                rtt_samples.push_back(rtt);
                std::printf("ping %u: rtt=%ums\n", pong.sequence, rtt);
            }
        }

        ikcp_update(endpoint.kcp, now_ms());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::uint32_t min_rtt = 0;
    std::uint32_t max_rtt = 0;
    std::uint64_t sum_rtt = 0;
    if (!rtt_samples.empty()) {
        min_rtt = *std::min_element(rtt_samples.begin(), rtt_samples.end());
        max_rtt = *std::max_element(rtt_samples.begin(), rtt_samples.end());
        for (std::uint32_t sample : rtt_samples) {
            sum_rtt += sample;
        }
    }

    const double average_rtt = rtt_samples.empty()
        ? 0.0
        : static_cast<double>(sum_rtt) / static_cast<double>(rtt_samples.size());
    const std::uint64_t total = link.sent_packets() + link.dropped_packets();

    std::printf(
        "kcp-client: sent=%u received=%zu min=%ums avg=%.1fms max=%ums packets=%llu dropped=%llu errors=%llu (%.1f%% simulated loss)\n",
        sequence,
        rtt_samples.size(),
        min_rtt,
        average_rtt,
        max_rtt,
        static_cast<unsigned long long>(link.sent_packets()),
        static_cast<unsigned long long>(link.dropped_packets()),
        static_cast<unsigned long long>(link.send_errors()),
        total == 0 ? 0.0 : 100.0 * static_cast<double>(link.dropped_packets()) / static_cast<double>(total));

    ikcp_release(endpoint.kcp);
    return rtt_samples.empty() ? 2 : 0;
}

} // namespace nsync::net
