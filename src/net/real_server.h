#pragma once

#include "core/map.h"
#include "net/action_queue.h"
#include "net/kcp_transport.h"
#include "net/local_net.h"
#include "net/protocol.h"
#include "net/rate_limit.h"

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace nsync::net {

struct NetServerConfig {
    std::uint16_t port = kDefaultPort;
    std::uint64_t seed = 1;
    int player_count = 2;
    std::uint32_t match_duration_ticks = 5U * 60U * 60U;
    std::uint16_t score_limit = 30;
    core::GameMode game_mode = core::GameMode::Tdm;
    std::uint32_t snapshot_interval = 3;
    bool broadcast_snapshots = true;
    bool original_input_sync = true;
    std::uint16_t server_lag_ticks = 8;
    std::uint16_t client_event_lag_ticks = 3;
    std::uint16_t sync_period_ticks = 30;
    std::int16_t sync_time_to_step_ms = 0;
    std::uint32_t max_input_ahead_ticks = 120;
    bool late_input_replay = false;
    std::uint32_t late_input_replay_window_ticks = 120;
    bool action_input = true;
    std::uint32_t max_packets_per_second = 600;
    std::uint32_t max_inputs_per_second = 240;
    std::uint32_t session_timeout_ms = 10000;
    std::uint32_t reconnect_window_ms = 10000;
    int tick_rate = 60;
    int max_catchup_steps = 5;
    bool lobby_enabled = false;
    std::uint32_t lobby_countdown_ms = 3000;
    std::uint32_t match_end_delay_ms = 3000;
    std::uint32_t lobby_min_humans = 1;
};

struct NetServerStats {
    std::uint64_t input_packets = 0;
    std::uint64_t action_events = 0;
    std::uint64_t action_toggle_events = 0;
    std::uint64_t action_packets = 0;
    std::uint64_t action_duplicate_packets = 0;
    std::uint64_t action_event_frames = 0;
    std::uint64_t action_events_applied = 0;
    std::uint64_t action_mismatches = 0;
    std::uint64_t action_driven_inputs = 0;
    std::uint64_t action_fallback_inputs = 0;
    std::uint64_t late_action_driven_inputs = 0;
    std::uint64_t late_action_fallback_inputs = 0;
    std::uint64_t rejected_inputs = 0;
    std::uint64_t rejected_post_match_inputs = 0;
    std::uint64_t rejected_phase_inputs = 0;
    std::uint64_t rejected_malformed_inputs = 0;
    std::uint64_t rejected_slot_inputs = 0;
    std::uint64_t rejected_future_inputs = 0;
    std::uint64_t rejected_late_inputs = 0;
    std::uint64_t late_inputs = 0;
    std::uint64_t late_replays = 0;
    std::uint64_t late_replay_batches = 0;
    std::uint64_t late_replay_failures = 0;
    std::uint64_t late_replay_failure_window = 0;
    std::uint64_t late_replay_failure_base = 0;
    std::uint64_t late_replay_failure_restore = 0;
    std::uint64_t late_replay_failure_tick = 0;
    std::uint64_t snapshots_sent = 0;
    std::uint64_t snapshot_broadcasts = 0;
    std::uint64_t snapshot_bytes = 0;
    std::uint64_t snapshot_generation_us_total = 0;
    std::uint32_t snapshot_generation_us_max = 0;
    std::uint64_t snapshot_broadcast_us_total = 0;
    std::uint32_t snapshot_broadcast_us_max = 0;
    std::uint64_t sync_responses = 0;
    std::uint64_t rejected_tokens = 0;
    std::uint64_t rate_limited_packets = 0;
    std::uint64_t rate_limited_inputs = 0;
    std::uint64_t logins = 0;
    std::uint64_t disconnects = 0;
    std::uint64_t ack_mismatches = 0;
    std::uint64_t rewritten_ack_mismatches = 0;
    std::uint64_t matches_started = 0;
    std::uint64_t matches_ended = 0;
    std::uint64_t rematches = 0;
    std::uint64_t bot_takeovers = 0;
};

class RealServer {
public:
    RealServer();
    ~RealServer();

    RealServer(const RealServer&) = delete;
    RealServer& operator=(const RealServer&) = delete;

    bool start(
        const core::Map& map,
        const NetServerConfig& config,
        std::string* error = nullptr);
    bool start(
        const std::vector<core::Map>& maps,
        const NetServerConfig& config,
        std::string* error = nullptr);
    void update(std::uint32_t now_ms);
    void stop();

    [[nodiscard]] bool running() const { return running_; }
    [[nodiscard]] std::uint16_t port() const;
    [[nodiscard]] std::uint32_t tick() const;
    [[nodiscard]] int connected_clients() const;
    [[nodiscard]] const core::World& world() const;
    [[nodiscard]] const NetServerStats& stats() const { return stats_; }
    [[nodiscard]] TransportStats transport_stats() const;
    [[nodiscard]] core::WorldSnapshot snapshot() const;
    [[nodiscard]] LobbyPhase phase() const { return phase_; }
    [[nodiscard]] std::uint32_t match_id() const { return match_id_; }
    [[nodiscard]] SyncTiming timing() const;

private:
    struct SnapshotRecord {
        std::uint32_t tick = 0;
        std::uint64_t hash = 0;
        core::WorldSnapshot snapshot{};
    };

    struct Session {
        std::uint64_t peer = 0;
        std::uint32_t token = 0;
        std::uint8_t slot = 0;
        std::uint8_t hero = 0;
        std::uint8_t team = 0;
        bool connected = false;
        bool ready = false;
        bool in_match = false;
        bool rematch = false;
        std::string name;
        std::uint32_t last_seen_ms = 0;
        std::uint32_t disconnect_deadline_ms = 0;
        std::uint32_t last_ack_tick = 0;
        std::uint64_t last_ack_hash = 0;
        std::uint32_t last_sync_ms = 0;
        core::InputCommand input_state{};
        bool has_input_state = false;
        std::map<std::uint32_t, core::InputCommand> pending_inputs;
        std::vector<core::InputCommand> pending_late_inputs;
        std::vector<InputMessage> pending_late_broadcasts;
        std::map<std::uint32_t, core::InputCommand> input_events;
        std::map<std::uint32_t, core::InputCommand> input_history;
        ActionEventQueue action_queue;
        std::set<std::uint32_t> action_fallback_ticks;
        std::uint16_t action_buttons = 0;
        bool has_action_state = false;
        RateLimiter packet_rate;
        RateLimiter input_rate;
    };

    void handle_message(
        std::uint64_t peer,
        const std::vector<std::uint8_t>& payload,
        std::uint32_t now_ms);
    void handle_login(
        std::uint64_t peer,
        const Packet& packet,
        std::uint32_t now_ms);
    void handle_input(
        Session& session,
        const Packet& packet,
        std::uint32_t now_ms);
    void handle_ping(
        Session& session,
        const Packet& packet,
        std::uint32_t now_ms);
    void handle_snapshot_ack(
        Session& session,
        const Packet& packet,
        std::uint32_t now_ms);
    void handle_sync_request(
        Session& session,
        const Packet& packet,
        std::uint32_t now_ms);
    void handle_lobby_request(
        Session& session,
        const Packet& packet,
        std::uint32_t now_ms);
    void handle_disconnect(Session& session, const Packet& packet);

    void update_lobby(std::uint32_t now_ms);
    void start_match(std::uint32_t now_ms);
    void finish_match(std::uint32_t now_ms);
    void return_to_lobby(std::uint32_t now_ms);
    void refresh_bot_slots();
    void send_lobby_state(Session& session, std::uint32_t now_ms);
    void broadcast_lobby_state(std::uint32_t now_ms);
    void send_match_start(Session& session, std::uint32_t now_ms);
    void send_match_end(Session& session, std::uint32_t now_ms);
    [[nodiscard]] LobbyState build_lobby_state(std::uint32_t now_ms) const;
    [[nodiscard]] bool all_ready() const;
    [[nodiscard]] int human_count() const;
    [[nodiscard]] int ready_count() const;

    void simulate_tick(std::uint32_t now_ms);
    bool replay_late_inputs(
        Session& session,
        const std::vector<core::InputCommand>& commands);
    void send_snapshot_to_all(std::uint32_t now_ms);
    void record_snapshot();
    [[nodiscard]] std::size_t snapshot_history_limit() const;
    void send_sync_response(
        Session& session,
        std::uint32_t now_ms,
        std::uint32_t requested_tick = 0);
    void broadcast_input(
        std::uint8_t slot,
        const InputMessage& input,
        std::uint32_t now_ms);
    bool send_packet(
        Session& session,
        MessageType type,
        std::vector<std::uint8_t> payload,
        std::uint32_t sequence);
    bool send_packet_ref(
        Session& session,
        MessageType type,
        const std::vector<std::uint8_t>& payload,
        std::uint32_t sequence);
    void expire_sessions(std::uint32_t now_ms);

    [[nodiscard]] Session* find_session(std::uint64_t peer);
    [[nodiscard]] Session* find_session_by_token(std::uint32_t token);
    [[nodiscard]] int free_slot() const;
    [[nodiscard]] std::uint32_t server_time(std::uint32_t now_ms) const;
    [[nodiscard]] std::uint32_t next_token();

    LocalServer world_;
    KcpServerTransport transport_;
    NetServerConfig config_{};
    core::Map map_{};
    std::vector<core::Map> maps_{};
    std::size_t map_index_ = 0;
    std::map<std::uint64_t, Session> sessions_;
    std::array<std::uint32_t, core::kMaxPlayers> slot_tokens_{};
    std::deque<SnapshotRecord> snapshot_history_;
    NetServerStats stats_{};
    std::uint32_t start_ms_ = 0;
    std::uint32_t last_update_ms_ = 0;
    // Next tick due time in units of (ms * tick_rate); exact for any rate.
    std::uint64_t next_tick_ms_ = 0;
    std::uint32_t last_broadcast_tick_ = 0;
    std::uint64_t last_broadcast_hash_ = 0;
    bool has_last_broadcast_ = false;
    std::uint32_t token_counter_ = 1;
    std::uint32_t sequence_ = 1;
    bool started_ = false;
    bool running_ = false;
    bool initialized_time_ = false;
    LobbyPhase phase_ = LobbyPhase::Lobby;
    std::uint32_t match_id_ = 0;
    std::uint64_t match_start_inputs_ = 0;
    std::uint64_t match_start_snapshots_ = 0;
    std::uint64_t match_start_snapshot_bytes_ = 0;
    std::uint64_t match_start_ack_mismatches_ = 0;
    std::uint32_t rewritten_ack_ignore_before_tick_ = 0;
    std::uint32_t countdown_deadline_ms_ = 0;
    std::uint32_t match_end_deadline_ms_ = 0;
    std::uint32_t last_lobby_broadcast_ms_ = 0;
    std::uint64_t match_seed_ = 1;
    bool match_result_sent_ = false;
};

} // namespace nsync::net



