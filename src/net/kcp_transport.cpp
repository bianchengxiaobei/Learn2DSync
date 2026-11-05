#include "net/kcp_transport.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <ikcp.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <random>
#include <unordered_map>
#include <utility>

namespace nsync::net {
namespace {

constexpr std::size_t kUdpBufferSize = 2048;
constexpr std::size_t kMaxMessageSize = 128U * 1024U;
constexpr int kReceiveWindow = 256;
constexpr int kMtu = 1200;
constexpr std::size_t kMaxServerSessions = 32;
constexpr std::size_t kMaxDelayedDatagrams = 4096;

bool ensure_wsa(std::string* error) {
    static const bool initialized = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!initialized && error) {
        *error = "WSAStartup failed";
    }
    return initialized;
}

std::string socket_error(const char* action) {
    return std::string(action) + " failed: " +
        std::to_string(WSAGetLastError());
}

bool set_non_blocking(SOCKET socket, std::string* error) {
    u_long enabled = 1;
    if (ioctlsocket(socket, FIONBIO, &enabled) == SOCKET_ERROR) {
        if (error) {
            *error = socket_error("ioctlsocket");
        }
        return false;
    }
    return true;
}

bool resolve_ipv4(
    const std::string& host,
    std::uint16_t port,
    sockaddr_in& endpoint,
    std::string* error) {
    endpoint = {};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &endpoint.sin_addr) == 1) {
        return true;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* result = nullptr;
    const int code = getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (code != 0 || result == nullptr) {
        if (error) {
            *error = "cannot resolve host: " + host;
        }
        if (result != nullptr) {
            freeaddrinfo(result);
        }
        return false;
    }
    const auto* address = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
    endpoint.sin_addr = address->sin_addr;
    freeaddrinfo(result);
    return true;
}

bool same_peer(const sockaddr_in& a, const sockaddr_in& b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr &&
        a.sin_port == b.sin_port;
}

bool configure_kcp(ikcpcb* kcp) {
    if (kcp == nullptr) {
        return false;
    }
    ikcp_nodelay(kcp, 1, 10, 2, 1);
    ikcp_wndsize(kcp, kReceiveWindow, kReceiveWindow);
    ikcp_setmtu(kcp, kMtu);
    return true;
}

void update_kcp_stats(TransportStats& stats, const ikcpcb* kcp) {
    if (kcp == nullptr) {
        return;
    }
    if (kcp->rx_srtt > 0) {
        stats.rtt_ms = static_cast<std::uint32_t>(kcp->rx_srtt);
    }
    stats.waitsnd = static_cast<std::uint32_t>(ikcp_waitsnd(kcp));
    stats.retransmissions = kcp->xmit;
}

} // namespace

struct KcpTransport::Impl {
    struct DelayedDatagram {
        std::uint32_t due_ms = 0;
        std::vector<std::uint8_t> bytes;
    };

    SOCKET socket = INVALID_SOCKET;
    sockaddr_in peer{};
    ikcpcb* kcp = nullptr;
    std::uint32_t conv = 0;
    std::uint32_t clock_ms = 0;
    std::uint32_t latency_ms = 0;
    std::uint32_t jitter_ms = 0;
    std::uint32_t loss_percent = 0;
    std::uint32_t duplicate_percent = 0;
    std::uint32_t burst_length = 1;
    std::uint32_t spike_ms = 0;
    std::uint32_t spike_interval_ms = 0;
    std::uint32_t spike_duration_ms = 0;
    std::uint32_t outbound_burst_remaining = 0;
    std::uint32_t inbound_burst_remaining = 0;
    std::vector<WanTraceSample> trace;
    std::size_t trace_index = 0;
    std::uint32_t trace_epoch_ms = 0;
    bool trace_started = false;
    std::mt19937 rng{0x9E3779B9U};
    TransportStats stats{};
    std::deque<DelayedDatagram> outbound;
    std::deque<DelayedDatagram> inbound;
    bool open = false;

    bool roll_percent(std::uint32_t percent) {
        return percent != 0U &&
            (rng() % 100U) < static_cast<std::uint32_t>(percent);
    }

    bool should_drop(std::uint32_t& burst_remaining) {
        if (burst_remaining > 0U) {
            --burst_remaining;
            return true;
        }
        if (!roll_percent(loss_percent)) {
            return false;
        }
        if (burst_length > 1U) {
            burst_remaining = burst_length - 1U;
        }
        return true;
    }

    const WanTraceSample* current_trace() {
        if (trace.empty()) {
            return nullptr;
        }
        if (!trace_started) {
            trace_started = true;
            trace_epoch_ms = clock_ms;
        }
        const std::uint32_t elapsed_ms = clock_ms - trace_epoch_ms;
        while (trace_index + 1U < trace.size() &&
               trace[trace_index + 1U].at_ms <= elapsed_ms) {
            ++trace_index;
        }
        return &trace[trace_index];
    }

    bool spike_active() const {
        return spike_ms != 0U && spike_interval_ms != 0U &&
            spike_duration_ms != 0U &&
            (clock_ms % spike_interval_ms) < spike_duration_ms;
    }

    bool has_delay() const {
        return latency_ms != 0U || jitter_ms != 0U || spike_ms != 0U;
    }

    std::uint32_t jitter_due_ms() {
        std::uint32_t due = latency_ms;
        if (jitter_ms != 0U) {
            due += static_cast<std::uint32_t>(rng() % (jitter_ms + 1U));
        }
        if (spike_active()) {
            due += spike_ms;
            ++stats.packets_delayed_spike;
        }
        return due;
    }

    void enqueue(
        std::deque<DelayedDatagram>& queue,
        std::vector<std::uint8_t> bytes,
        std::uint32_t due_ms) {
        if (queue.size() >= kMaxDelayedDatagrams) {
            ++stats.packets_dropped;
            return;
        }
        DelayedDatagram datagram;
        datagram.due_ms = due_ms;
        datagram.bytes = std::move(bytes);
        auto position = std::upper_bound(
            queue.begin(),
            queue.end(),
            due_ms,
            [](std::uint32_t due, const DelayedDatagram& item) {
                return due < item.due_ms;
            });
        queue.insert(position, std::move(datagram));
    }

    bool schedule_outbound(std::vector<std::uint8_t> bytes) {
        const WanTraceSample* sample = current_trace();
        if (sample != nullptr) {
            ++stats.packets_traced;
        }
        if (sample != nullptr
                ? roll_percent(sample->loss_percent)
                : should_drop(outbound_burst_remaining)) {
            ++stats.packets_dropped;
            return true;
        }
        const std::uint32_t duplicate_percent_for_packet =
            sample != nullptr ? sample->duplicate_percent : duplicate_percent;
        const bool duplicate = roll_percent(duplicate_percent_for_packet);
        if (duplicate) {
            ++stats.packets_duplicated;
        }
        if (sample == nullptr && !has_delay()) {
            if (!send_now(bytes.data(), static_cast<int>(bytes.size()))) {
                return false;
            }
            if (duplicate &&
                !send_now(bytes.data(), static_cast<int>(bytes.size()))) {
                return false;
            }
            return true;
        }
        const std::uint32_t delay_ms = sample != nullptr
            ? sample->latency_ms
            : jitter_due_ms();
        const std::uint32_t due_ms = clock_ms + delay_ms;
        enqueue(outbound, bytes, due_ms);
        if (duplicate) {
            enqueue(outbound, bytes, due_ms);
        }
        return true;
    }

    bool schedule_inbound(std::vector<std::uint8_t> bytes) {
        const WanTraceSample* sample = current_trace();
        if (sample != nullptr) {
            ++stats.packets_traced;
        }
        if (sample != nullptr
                ? roll_percent(sample->loss_percent)
                : should_drop(inbound_burst_remaining)) {
            ++stats.packets_dropped;
            return true;
        }
        const std::uint32_t duplicate_percent_for_packet =
            sample != nullptr ? sample->duplicate_percent : duplicate_percent;
        const bool duplicate = roll_percent(duplicate_percent_for_packet);
        if (duplicate) {
            ++stats.packets_duplicated;
        }
        if (sample == nullptr && !has_delay()) {
            ikcp_input(kcp, reinterpret_cast<const char*>(bytes.data()),
                static_cast<long>(bytes.size()));
            if (duplicate) {
                ikcp_input(kcp, reinterpret_cast<const char*>(bytes.data()),
                    static_cast<long>(bytes.size()));
            }
            return true;
        }
        const std::uint32_t delay_ms = sample != nullptr
            ? sample->latency_ms
            : jitter_due_ms();
        const std::uint32_t due_ms = clock_ms + delay_ms;
        enqueue(inbound, bytes, due_ms);
        if (duplicate) {
            enqueue(inbound, bytes, due_ms);
        }
        return true;
    }

    bool send_now(const std::uint8_t* data, int size) {
        const int sent = sendto(
            socket,
            reinterpret_cast<const char*>(data),
            size,
            0,
            reinterpret_cast<const sockaddr*>(&peer),
            sizeof(peer));
        if (sent != size) {
            return false;
        }
        ++stats.packets_sent;
        stats.bytes_sent += static_cast<std::uint64_t>(size);
        return true;
    }

    static int output(
        const char* data,
        int size,
        ikcpcb*,
        void* user) {
        auto* impl = static_cast<Impl*>(user);
        if (impl == nullptr || !impl->open || impl->socket == INVALID_SOCKET ||
            data == nullptr || size <= 0) {
            return -1;
        }
        std::vector<std::uint8_t> bytes(
            reinterpret_cast<const std::uint8_t*>(data),
            reinterpret_cast<const std::uint8_t*>(data) + size);
        return impl->schedule_outbound(std::move(bytes)) ? 0 : -1;
    }

    void flush_outbound(std::uint32_t now_ms) {
        while (!outbound.empty() && outbound.front().due_ms <= now_ms) {
            DelayedDatagram& datagram = outbound.front();
            send_now(datagram.bytes.data(), static_cast<int>(datagram.bytes.size()));
            outbound.pop_front();
        }
    }

    void deliver_inbound(std::uint32_t now_ms) {
        while (!inbound.empty() && inbound.front().due_ms <= now_ms) {
            DelayedDatagram& datagram = inbound.front();
            ikcp_input(
                kcp,
                reinterpret_cast<const char*>(datagram.bytes.data()),
                static_cast<long>(datagram.bytes.size()));
            inbound.pop_front();
        }
    }

    void close() {
        open = false;
        outbound.clear();
        inbound.clear();
        if (kcp != nullptr) {
            ikcp_release(kcp);
            kcp = nullptr;
        }
        if (socket != INVALID_SOCKET) {
            closesocket(socket);
            socket = INVALID_SOCKET;
        }
    }
};

KcpTransport::KcpTransport() : impl_(std::make_unique<Impl>()) {}

KcpTransport::~KcpTransport() {
    close();
}

bool KcpTransport::open(
    const std::string& host,
    std::uint16_t port,
    std::uint32_t conv,
    std::string* error) {
    close();
    impl_->clock_ms = 0;
    if (!ensure_wsa(error)) {
        return false;
    }
    if (port == 0) {
        if (error) {
            *error = "invalid server port";
        }
        return false;
    }
    if (!resolve_ipv4(host, port, impl_->peer, error)) {
        return false;
    }

    impl_->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->socket == INVALID_SOCKET) {
        if (error) {
            *error = socket_error("socket");
        }
        return false;
    }
    if (!set_non_blocking(impl_->socket, error)) {
        close();
        return false;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = 0;
    if (bind(
            impl_->socket,
            reinterpret_cast<const sockaddr*>(&local),
            sizeof(local)) == SOCKET_ERROR) {
        if (error) {
            *error = socket_error("bind client");
        }
        close();
        return false;
    }

    if (conv == 0) {
        std::random_device device;
        conv = (static_cast<std::uint32_t>(device()) ^
                static_cast<std::uint32_t>(device() << 1U)) |
            1U;
    }
    impl_->conv = conv;
    impl_->kcp = ikcp_create(conv, impl_.get());
    if (impl_->kcp == nullptr || !configure_kcp(impl_->kcp)) {
        if (error) {
            *error = "ikcp_create failed";
        }
        close();
        return false;
    }
    ikcp_setoutput(impl_->kcp, &Impl::output);
    impl_->open = true;
    impl_->stats = {};
    impl_->stats.conv = conv;
    return true;
}

void KcpTransport::set_latency_ms(std::uint32_t latency_ms) {
    if (impl_) {
        impl_->latency_ms = std::min<std::uint32_t>(latency_ms, 2000U);
    }
}

std::uint32_t KcpTransport::latency_ms() const {
    return impl_ ? impl_->latency_ms : 0;
}

void KcpTransport::set_network_simulation(
    std::uint32_t jitter_ms,
    std::uint32_t loss_percent,
    std::uint32_t duplicate_percent,
    std::uint32_t burst_length) {
    if (!impl_) {
        return;
    }
    impl_->jitter_ms = std::min<std::uint32_t>(jitter_ms, 2000U);
    impl_->loss_percent = std::min<std::uint32_t>(loss_percent, 100U);
    impl_->duplicate_percent =
        std::min<std::uint32_t>(duplicate_percent, 100U);
    impl_->burst_length = std::max<std::uint32_t>(1U, burst_length);
    impl_->outbound_burst_remaining = 0;
    impl_->inbound_burst_remaining = 0;
}

void KcpTransport::set_latency_spikes(
    std::uint32_t spike_ms,
    std::uint32_t interval_ms,
    std::uint32_t duration_ms) {
    if (!impl_) {
        return;
    }
    impl_->spike_ms = std::min<std::uint32_t>(spike_ms, 5000U);
    impl_->spike_interval_ms = interval_ms;
    impl_->spike_duration_ms = std::min(duration_ms, interval_ms);
}

void KcpTransport::set_network_trace(std::vector<WanTraceSample> samples) {
    if (!impl_) {
        return;
    }
    impl_->trace = std::move(samples);
    impl_->trace_index = 0;
    impl_->trace_epoch_ms = 0;
    impl_->trace_started = false;
}

bool KcpTransport::send(const std::vector<std::uint8_t>& data) {
    if (!is_open() || data.empty() || data.size() > kMaxMessageSize) {
        return false;
    }
    if (ikcp_send(
            impl_->kcp,
            reinterpret_cast<const char*>(data.data()),
            static_cast<int>(data.size())) < 0) {
        return false;
    }
    ikcp_flush(impl_->kcp);
    impl_->flush_outbound(impl_->clock_ms);
    update_kcp_stats(impl_->stats, impl_->kcp);
    return true;
}

bool KcpTransport::poll(std::vector<std::vector<std::uint8_t>>& messages) {
    if (!is_open()) {
        return false;
    }

    impl_->deliver_inbound(impl_->clock_ms);

    char buffer[kUdpBufferSize];
    for (;;) {
        sockaddr_in from{};
        int from_size = sizeof(from);
        const int size = recvfrom(
            impl_->socket,
            buffer,
            sizeof(buffer),
            0,
            reinterpret_cast<sockaddr*>(&from),
            &from_size);
        if (size == SOCKET_ERROR) {
            const int code = WSAGetLastError();
            if (code == WSAEWOULDBLOCK || code == WSAECONNRESET) {
                break;
            }
            return false;
        }
        if (size <= 0) {
            break;
        }
        if (!same_peer(from, impl_->peer)) {
            continue;
        }
        ++impl_->stats.packets_received;
        impl_->stats.bytes_received += static_cast<std::uint64_t>(size);
        std::vector<std::uint8_t> bytes(
            reinterpret_cast<const std::uint8_t*>(buffer),
            reinterpret_cast<const std::uint8_t*>(buffer) + size);
        if (!impl_->schedule_inbound(std::move(bytes))) {
            return false;
        }
    }

    impl_->deliver_inbound(impl_->clock_ms);

    for (;;) {
        const int peek = ikcp_peeksize(impl_->kcp);
        if (peek <= 0) {
            break;
        }
        if (static_cast<std::size_t>(peek) > kMaxMessageSize) {
            return false;
        }
        std::vector<std::uint8_t> message(static_cast<std::size_t>(peek));
        const int received = ikcp_recv(
            impl_->kcp,
            reinterpret_cast<char*>(message.data()),
            static_cast<int>(message.size()));
        if (received <= 0) {
            break;
        }
        message.resize(static_cast<std::size_t>(received));
        messages.push_back(std::move(message));
    }
    update_kcp_stats(impl_->stats, impl_->kcp);
    return true;
}

void KcpTransport::update(std::uint32_t now_ms) {
    if (!is_open()) {
        return;
    }
    impl_->clock_ms = now_ms;
    impl_->flush_outbound(now_ms);
    ikcp_update(impl_->kcp, now_ms);
    impl_->flush_outbound(now_ms);
    update_kcp_stats(impl_->stats, impl_->kcp);
}

void KcpTransport::close() {
    if (impl_) {
        impl_->close();
    }
}

bool KcpTransport::is_open() const {
    return impl_ && impl_->open;
}

TransportStats KcpTransport::stats() const {
    return impl_ ? impl_->stats : TransportStats{};
}

struct KcpServerTransport::Impl {
    struct Session {
        Impl* owner = nullptr;
        std::uint32_t conv = 0;
        sockaddr_in peer{};
        ikcpcb* kcp = nullptr;
        std::uint32_t last_seen_ms = 0;
    };

    SOCKET socket = INVALID_SOCKET;
    std::uint16_t bound_port = 0;
    std::unordered_map<std::uint32_t, std::unique_ptr<Session>> sessions;
    TransportStats stats{};
    bool open = false;

    static int output(
        const char* data,
        int size,
        ikcpcb*,
        void* user) {
        auto* session = static_cast<Session*>(user);
        if (session == nullptr || session->kcp == nullptr) {
            return -1;
        }
        auto* impl = session->owner;
        if (impl == nullptr || !impl->open ||
            impl->socket == INVALID_SOCKET) {
            return -1;
        }
        const int sent = sendto(
            impl->socket,
            data,
            size,
            0,
            reinterpret_cast<const sockaddr*>(&session->peer),
            sizeof(session->peer));
        if (sent == size) {
            ++impl->stats.packets_sent;
            impl->stats.bytes_sent += static_cast<std::uint64_t>(size);
            return 0;
        }
        return -1;
    }

    void close_sessions() {
        for (auto& entry : sessions) {
            if (entry.second->kcp != nullptr) {
                ikcp_release(entry.second->kcp);
                entry.second->kcp = nullptr;
            }
        }
        sessions.clear();
    }

    void close() {
        open = false;
        close_sessions();
        if (socket != INVALID_SOCKET) {
            closesocket(socket);
            socket = INVALID_SOCKET;
        }
        stats = {};
        bound_port = 0;
    }

    Session* find(std::uint32_t conv) {
        const auto found = sessions.find(conv);
        return found == sessions.end() ? nullptr : found->second.get();
    }

    Session* create(std::uint32_t conv, const sockaddr_in& peer) {
        if (conv == 0 || sessions.size() >= kMaxServerSessions) {
            return nullptr;
        }
        auto session = std::make_unique<Session>();
        session->owner = this;
        session->conv = conv;
        session->peer = peer;
        session->kcp = ikcp_create(conv, session.get());
        if (session->kcp == nullptr || !configure_kcp(session->kcp)) {
            if (session->kcp != nullptr) {
                ikcp_release(session->kcp);
            }
            return nullptr;
        }
        ikcp_setoutput(session->kcp, &Impl::output);
        const std::uint32_t key = conv;
        Session* pointer = session.get();
        sessions.emplace(key, std::move(session));
        return pointer;
    }

    void update_digest() {
        stats.sessions = static_cast<std::uint32_t>(sessions.size());
        stats.waitsnd = 0;
        stats.retransmissions = 0;
        stats.rtt_ms = 0;
        for (const auto& entry : sessions) {
            const ikcpcb* kcp = entry.second->kcp;
            if (kcp == nullptr) {
                continue;
            }
            stats.waitsnd = std::max(
                stats.waitsnd,
                static_cast<std::uint32_t>(ikcp_waitsnd(kcp)));
            stats.retransmissions = std::max(
                stats.retransmissions,
                kcp->xmit);
            if (kcp->rx_srtt > 0) {
                stats.rtt_ms = std::max(
                    stats.rtt_ms,
                    static_cast<std::uint32_t>(kcp->rx_srtt));
            }
        }
    }
};

KcpServerTransport::KcpServerTransport() : impl_(std::make_unique<Impl>()) {}

KcpServerTransport::~KcpServerTransport() {
    close();
}

bool KcpServerTransport::start(std::uint16_t port, std::string* error) {
    close();
    if (!ensure_wsa(error)) {
        return false;
    }
    impl_->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->socket == INVALID_SOCKET) {
        if (error) {
            *error = socket_error("socket");
        }
        return false;
    }
    const int reuse = 1;
    setsockopt(
        impl_->socket,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse),
        sizeof(reuse));
    if (!set_non_blocking(impl_->socket, error)) {
        close();
        return false;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);
    if (bind(
            impl_->socket,
            reinterpret_cast<const sockaddr*>(&local),
            sizeof(local)) == SOCKET_ERROR) {
        if (error) {
            *error = socket_error("bind server");
        }
        close();
        return false;
    }

    sockaddr_in bound{};
    int bound_size = sizeof(bound);
    if (getsockname(
            impl_->socket,
            reinterpret_cast<sockaddr*>(&bound),
            &bound_size) == SOCKET_ERROR) {
        if (error) {
            *error = socket_error("getsockname");
        }
        close();
        return false;
    }
    impl_->bound_port = ntohs(bound.sin_port);
    impl_->open = true;
    impl_->stats = {};
    return true;
}

void KcpServerTransport::close() {
    if (impl_) {
        impl_->close();
    }
}

bool KcpServerTransport::poll(
    std::vector<TransportMessage>& messages,
    std::string* error) {
    if (!impl_ || !impl_->open) {
        return false;
    }

    char buffer[kUdpBufferSize];
    for (;;) {
        sockaddr_in from{};
        int from_size = sizeof(from);
        const int size = recvfrom(
            impl_->socket,
            buffer,
            sizeof(buffer),
            0,
            reinterpret_cast<sockaddr*>(&from),
            &from_size);
        if (size == SOCKET_ERROR) {
            const int code = WSAGetLastError();
            if (code == WSAEWOULDBLOCK) {
                break;
            }
            if (code == WSAECONNRESET) {
                break;
            }
            if (error) {
                *error = socket_error("recvfrom");
            }
            return false;
        }
        if (size < static_cast<int>(sizeof(IUINT32))) {
            continue;
        }
        ++impl_->stats.packets_received;
        impl_->stats.bytes_received += static_cast<std::uint64_t>(size);

        const std::uint32_t conv = ikcp_getconv(buffer);
        if (conv == 0) {
            continue;
        }
        Impl::Session* session = impl_->find(conv);
        if (session == nullptr) {
            session = impl_->create(conv, from);
            if (session == nullptr) {
                continue;
            }
        } else if (!same_peer(session->peer, from)) {
            continue;
        }
        session->last_seen_ms = 0;
        ikcp_input(session->kcp, buffer, size);
    }

    std::vector<std::uint8_t> receive_buffer(kMaxMessageSize);
    for (auto& entry : impl_->sessions) {
        Impl::Session& session = *entry.second;
        for (;;) {
            const int peek = ikcp_peeksize(session.kcp);
            if (peek <= 0) {
                break;
            }
            if (static_cast<std::size_t>(peek) > receive_buffer.size()) {
                break;
            }
            const int received = ikcp_recv(
                session.kcp,
                reinterpret_cast<char*>(receive_buffer.data()),
                static_cast<int>(receive_buffer.size()));
            if (received <= 0) {
                break;
            }
            TransportMessage message;
            message.peer = static_cast<std::uint64_t>(session.conv);
            message.data.assign(
                receive_buffer.begin(),
                receive_buffer.begin() + received);
            messages.push_back(std::move(message));
        }
    }
    impl_->update_digest();
    return true;
}

bool KcpServerTransport::send(
    std::uint64_t peer,
    const std::vector<std::uint8_t>& data,
    std::string* error) {
    if (!impl_ || !impl_->open || data.empty() ||
        data.size() > kMaxMessageSize) {
        if (error) {
            *error = "server transport is closed or message is invalid";
        }
        return false;
    }
    auto* session = impl_->find(static_cast<std::uint32_t>(peer));
    if (session == nullptr) {
        if (error) {
            *error = "unknown KCP peer";
        }
        return false;
    }
    if (ikcp_send(
            session->kcp,
            reinterpret_cast<const char*>(data.data()),
            static_cast<int>(data.size())) < 0) {
        if (error) {
            *error = "ikcp_send failed";
        }
        return false;
    }
    ikcp_flush(session->kcp);
    impl_->update_digest();
    return true;
}

void KcpServerTransport::update(std::uint32_t now_ms) {
    if (!impl_ || !impl_->open) {
        return;
    }
    for (auto& entry : impl_->sessions) {
        ikcp_update(entry.second->kcp, now_ms);
    }
    impl_->update_digest();
}

void KcpServerTransport::remove_peer(std::uint64_t peer) {
    if (!impl_) {
        return;
    }
    const auto found = impl_->sessions.find(static_cast<std::uint32_t>(peer));
    if (found == impl_->sessions.end()) {
        return;
    }
    if (found->second->kcp != nullptr) {
        ikcp_release(found->second->kcp);
    }
    impl_->sessions.erase(found);
    impl_->update_digest();
}

std::uint16_t KcpServerTransport::port() const {
    return impl_ ? impl_->bound_port : 0;
}

bool KcpServerTransport::is_open() const {
    return impl_ && impl_->open;
}

TransportStats KcpServerTransport::stats() const {
    return impl_ ? impl_->stats : TransportStats{};
}

int KcpServerTransport::session_count() const {
    return impl_ ? static_cast<int>(impl_->sessions.size()) : 0;
}

} // namespace nsync::net


