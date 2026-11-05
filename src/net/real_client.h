#pragma once

#include "core/map.h"
#include "net/kcp_transport.h"
#include "net/local_net.h"
#include "net/protocol.h"
#include "net/server_clock.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace nsync::net {

[[nodiscard]] std::uint32_t effective_server_lag_ticks(
    std::uint16_t server_lag_ticks,
    std::int16_t sync_time_to_step_ms,
    std::uint16_t tick_rate);

[[nodiscard]] std::uint32_t rtt_compensation_ticks(
    std::uint32_t rtt_ms,
    std::uint16_t tick_rate);

struct NetClientConfig {
    std::uint32_t input_delay_ticks = 16;
    std::uint32_t input_refresh_ticks = 6;
    bool use_server_timing = true;
    std::uint32_t ack_interval_ticks = 15;
    std::uint32_t sync_interval_ms = 1000;
    std::uint32_t ping_interval_ms = 1000;
    std::uint32_t connect_timeout_ms = 3000;
    std::uint32_t session_timeout_ms = 5000;
    std::uint32_t reconnect_interval_ms = 500;
    std::uint32_t simulated_rtt_ms = 0;
    std::uint32_t simulated_jitter_ms = 0;
    std::uint32_t simulated_loss_percent = 0;
    std::uint32_t simulated_duplicate_percent = 0;
    std::uint32_t simulated_burst_length = 1;
    std::uint32_t simulated_spike_ms = 0;
    std::uint32_t simulated_spike_interval_ms = 0;
    std::uint32_t simulated_spike_duration_ms = 0;
    std::vector<WanTraceSample> network_trace;
    bool client_step_lag = false;
    bool fixed_step_scheduler = false;
    std::uint64_t seed = 1;
    bool auto_reconnect = true;
    bool auto_ready = true;
    bool auto_rematch = false;
    std::string display_name = "player";
    std::string login_token;
    std::uint8_t desired_hero = 0;
    std::uint8_t desired_team = 0xFFU;
};

enum class NetClientState {
    Disconnected,
    Connecting,
    Connected,
    Reconnecting,
    Failed,
};

struct NetClientStats {
    std::uint32_t slot = 0;
    std::uint32_t server_tick = 0;
    std::uint32_t predicted_tick = 0;
    std::uint32_t ack_tick = 0;
    std::uint32_t corrections = 0;
    std::uint32_t rollbacks = 0;
    std::uint32_t desyncs = 0;
    std::uint32_t snapshot_mismatches = 0;
    std::uint32_t snapshot_missing_predictions = 0;
    std::uint32_t snapshot_periodic_mismatches = 0;
    std::uint32_t snapshot_authoritative_mismatches = 0;
    std::uint32_t snapshot_restores = 0;
    std::uint32_t remote_snapshot_corrections = 0;
    std::uint32_t event_rollbacks = 0;
    std::uint32_t covered_inputs = 0;
    std::uint64_t total_rollback_ticks = 0;
    std::uint32_t rtt_ms = 0;
    std::uint16_t server_lag_ticks = 0;
    std::uint16_t effective_server_lag_ticks = 0;
    std::uint16_t client_event_lag_ticks = 0;
    std::uint16_t sync_period_ticks = 0;
    std::uint16_t rtt_compensation_ticks = 0;
    std::uint32_t estimated_server_tick = 0;
    std::uint16_t input_lead_ticks = 0;
    std::uint16_t recovery_lead_ticks = 0;
    std::int16_t sync_time_to_step_ms = 0;
    std::uint32_t snapshots_queued = 0;
    std::uint32_t scheduler_target_tick = 0;
    std::uint32_t scheduler_steps = 0;
    std::uint32_t scheduler_skips = 0;
    std::uint32_t scheduler_debt_ticks = 0;
    std::uint32_t alignment_wait_ticks = 0;
    std::int64_t clock_offset_ms = 0;
    std::uint64_t input_packets = 0;
    std::uint64_t snapshot_packets = 0;
    std::uint64_t snapshot_bytes = 0;
    std::uint64_t sync_requests = 0;
    std::uint8_t phase = 0;
    std::uint32_t match_id = 0;
    std::uint8_t lobby_players = 0;
    std::uint8_t lobby_ready = 0;
    std::uint32_t lobby_countdown_ms = 0;
    std::uint8_t winner = 0;
    std::int32_t score0 = 0;
    std::int32_t score1 = 0;
};

class RealClient {
public:
    RealClient();
    ~RealClient();

    RealClient(const RealClient&) = delete;
    RealClient& operator=(const RealClient&) = delete;

    bool start(
        const std::string& host,
        std::uint16_t port,
        const core::Map& map,
        const NetClientConfig& config,
        std::uint32_t now_ms,
        std::string* error = nullptr);
    bool wait_connected(
        std::uint32_t timeout_ms,
        std::string* error = nullptr);
    void tick(const InputProvider& provider, std::uint32_t now_ms);
    bool reconnect(std::uint32_t now_ms);
    void stop();

    [[nodiscard]] NetClientState state() const { return state_; }
    [[nodiscard]] bool connected() const {
        return state_ == NetClientState::Connected;
    }
    [[nodiscard]] std::uint8_t slot() const { return slot_; }
    [[nodiscard]] std::uint32_t session_token() const { return session_token_; }
    [[nodiscard]] core::World& world() { return local_.world(); }
    [[nodiscard]] const core::World& world() const;
    [[nodiscard]] const core::Map& map() const { return map_; }
    [[nodiscard]] const std::string& map_id() const { return map_.id; }
    [[nodiscard]] std::uint32_t map_revision() const { return map_revision_; }
    [[nodiscard]] const NetClientStats& stats() const { return stats_; }
    [[nodiscard]] const ClientStats& local_stats() const;
    [[nodiscard]] TransportStats transport_stats() const;
    [[nodiscard]] const ServerClock& clock() const { return clock_; }
    [[nodiscard]] const std::string& last_error() const { return last_error_; }
    [[nodiscard]] std::uint32_t input_delay() const { return input_lead_ticks_; }
    [[nodiscard]] const SyncTiming& timing() const { return timing_; }
    [[nodiscard]] bool server_timing_active() const {
        return timing_valid_;
    }
    [[nodiscard]] bool aligned() const { return alignment_complete_; }
    [[nodiscard]] const LobbyState& lobby_state() const { return lobby_; }
    [[nodiscard]] const MatchEnd& match_end() const { return match_end_; }
    [[nodiscard]] bool in_match() const { return in_match_; }
    [[nodiscard]] std::vector<LocalClient::VisualCorrection>
    take_visual_corrections() {
        return local_.take_visual_corrections();
    }
    [[nodiscard]] bool ready() const { return ready_; }
    void set_ready(bool ready);
    void select_hero(std::uint8_t hero);
    void request_rematch();
    void leave();

private:
    bool open_transport(std::uint32_t now_ms, std::string* error);
    bool load_map_by_id(
        const std::string& map_id,
        std::uint64_t expected_fingerprint,
        std::string* error);
    void send_login(std::uint32_t now_ms);
    void pump(std::uint32_t now_ms);
    void handle_packet(
        const std::vector<std::uint8_t>& data,
        std::uint32_t now_ms);
    bool handle_login_response(const Packet& packet, std::uint32_t now_ms);
    bool handle_lobby_state(const Packet& packet);
    bool handle_match_start(const Packet& packet, std::uint32_t now_ms);
    bool handle_match_end(const Packet& packet);
    void send_lobby_request(
        LobbyAction action,
        std::uint8_t hero = 0,
        std::uint8_t team = 0xFFU,
        bool ready = true);
    bool accept_snapshot(
        std::uint32_t tick,
        std::uint32_t server_time_ms,
        std::uint64_t hash,
        const std::vector<std::uint8_t>& bytes,
        bool authoritative,
        std::uint32_t now_ms);
    bool apply_decoded_snapshot(
        std::uint32_t tick,
        std::uint64_t hash,
        const core::WorldSnapshot& snapshot,
        std::size_t encoded_size,
        bool authoritative,
        std::uint32_t now_ms);
    void flush_due_snapshots(std::uint32_t now_ms);
    [[nodiscard]] std::uint32_t estimated_server_tick(
        std::uint32_t now_ms) const;
    void catch_up_lag(std::uint32_t now_ms);
    void configure_timing(const SyncTiming& timing, std::uint32_t fallback_delay);
    void apply_initial_catch_up(
        std::uint32_t anchor_server_time_ms,
        std::uint32_t now_ms);
    void begin_alignment(const core::WorldSnapshot& snapshot, std::uint32_t now_ms);
    void update_alignment(std::uint32_t now_ms);
    void update_sync_stats();
    [[nodiscard]] std::uint32_t original_frames_to_ticks(
        std::uint32_t frames) const;
    void send_input(const InputPacket& input, std::uint32_t now_ms);
    void send_heartbeat(std::uint32_t now_ms);
    void send_sync(std::uint32_t now_ms);
    void send_ack(std::uint32_t now_ms);
    void begin_reconnect(std::uint32_t now_ms);
    void fail(const std::string& error);

    std::string host_;
    std::uint16_t port_ = 0;
    core::Map map_{};
    std::uint32_t map_revision_ = 0;
    NetClientConfig config_{};
    KcpTransport transport_;
    LocalClient local_;
    ServerClock clock_;
    NetClientStats stats_{};
    std::string last_error_;
    std::vector<std::uint8_t> connect_snapshot_;
    NetClientState state_ = NetClientState::Disconnected;
    std::uint32_t conv_ = 0;
    std::uint32_t session_token_ = 0;
    std::uint8_t slot_ = 0;
    std::uint32_t started_ms_ = 0;
    std::uint32_t connect_started_ms_ = 0;
    std::uint32_t login_started_ms_ = 0;
    std::uint32_t last_receive_ms_ = 0;
    std::uint32_t last_login_send_ms_ = 0;
    std::uint32_t last_reconnect_ms_ = 0;
    std::uint32_t last_ping_ms_ = 0;
    std::uint32_t last_sync_ms_ = 0;
    std::uint32_t last_sync_tick_ = 0;
    std::uint32_t last_ack_tick_ = 0;
    std::uint32_t last_snapshot_tick_ = 0;
    std::uint32_t last_snapshot_server_time_ms_ = 0;
    std::uint32_t last_snapshot_receive_ms_ = 0;
    std::uint32_t ping_sequence_ = 1;
    std::uint32_t message_sequence_ = 1;
    LobbyState lobby_{};
    MatchEnd match_end_{};
    std::uint32_t match_id_ = 0;
    std::uint8_t selected_hero_ = 0;
    [[maybe_unused]] bool initialized_ = false;
    bool local_started_ = false;
    bool in_match_ = false;
    bool ready_ = false;
    bool auto_ready_sent_ = false;
    bool auto_rematch_sent_ = false;
    bool match_end_received_ = false;

    struct QueuedSnapshot {
        std::uint32_t tick = 0;
        std::uint32_t server_time_ms = 0;
        std::uint64_t hash = 0;
        bool authoritative = false;
        core::WorldSnapshot snapshot{};
        std::vector<std::uint8_t> bytes;
    };

    SyncTiming timing_{};
    bool timing_valid_ = false;
    std::uint16_t server_tick_rate_ = 60;
    std::uint16_t snapshot_interval_ticks_ = 3;
    std::int16_t sync_time_to_step_ms_ = 0;
    std::uint32_t input_lead_ticks_ = 16;
    std::uint32_t normal_lead_ticks_ = 16;
    std::uint32_t recovery_lead_ticks_ = 16;
    bool recovery_lead_pending_ = false;
    core::InputCommand last_sent_input_{};
    std::uint32_t highest_sent_input_tick_ = 0;
    bool has_sent_input_ = false;
    std::uint32_t highest_server_tick_ = 0;
    std::uint32_t alignment_start_tick_ = 0;
    std::uint32_t alignment_target_tick_ = 0;
    std::uint32_t alignment_start_ms_ = 0;
    std::uint32_t alignment_wait_ticks_ = 0;
    bool alignment_complete_ = true;
    std::deque<QueuedSnapshot> future_snapshots_;
};

} // namespace nsync::net




