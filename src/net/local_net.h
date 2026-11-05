#pragma once

#include "core/input_command.h"
#include "core/map.h"
#include "core/rng.h"
#include "core/snapshot.h"
#include "core/world.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nsync::net {

struct NetConfig {
    std::uint32_t latency_ticks = 5;
    std::uint32_t jitter_ticks = 2;
    std::uint32_t loss_percent = 0;
    std::uint32_t duplicate_percent = 0;
    std::uint32_t input_delay_ticks = 16;
    std::uint32_t snapshot_interval = 1;
    std::uint32_t rollback_history = 240;
    std::uint64_t seed = 1;
};

struct InputPacket {
    std::uint8_t slot = 0;
    core::InputCommand command{};
};

struct SnapshotPacket {
    int client_index = 0;
    bool authoritative = true;
    std::uint64_t hash = 0;
    core::WorldSnapshot snapshot{};
};

struct NetStats {
    std::uint64_t packets_sent = 0;
    std::uint64_t packets_dropped = 0;
    std::uint64_t packets_duplicated = 0;
    std::uint64_t packets_delivered = 0;
    std::uint32_t rtt_ticks = 0;
    std::uint32_t rtt_ms = 0;
};

struct Deliveries {
    std::vector<InputPacket> server_inputs;
    std::vector<InputPacket> broadcasts;
    std::vector<SnapshotPacket> snapshots;
};

class NetworkSimulator {
public:
    void configure(const NetConfig& config);
    [[nodiscard]] const NetConfig& config() const { return config_; }

    void send_input(const InputPacket& packet, std::uint32_t now_tick);
    void send_input_broadcast(const InputPacket& packet, std::uint32_t now_tick);
    void send_snapshot(const SnapshotPacket& packet, std::uint32_t now_tick);

    [[nodiscard]] Deliveries pump(std::uint32_t now_tick);
    [[nodiscard]] const NetStats& stats() const { return stats_; }

private:
    struct PendingPacket {
        std::uint64_t order = 0;
        std::uint32_t due_tick = 0;
        bool snapshot = false;
        bool broadcast = false;
        InputPacket input{};
        SnapshotPacket snapshot_packet{};
    };

    void enqueue(
        const InputPacket& input,
        const SnapshotPacket& snapshot,
        bool snapshot_packet,
        bool broadcast,
        std::uint32_t now_tick);

    NetConfig config_{};
    NetStats stats_{};
    core::Pcg32 rng_;
    std::vector<PendingPacket> queue_;
    std::uint64_t next_order_ = 0;
};

struct NetGraphSample {
    std::uint32_t tick = 0;
    std::uint32_t ack_tick = 0;
    std::uint32_t rtt_ticks = 0;
    std::uint32_t rollback_ticks = 0;
    std::uint64_t client_hash = 0;
    std::uint64_t server_hash = 0;
    bool matched = true;
};

class NetGraph {
public:
    explicit NetGraph(std::size_t capacity = 240);

    void add(const NetGraphSample& sample);
    void clear();
    [[nodiscard]] const std::deque<NetGraphSample>& samples() const {
        return samples_;
    }

private:
    std::size_t capacity_ = 240;
    std::deque<NetGraphSample> samples_;
};

struct ClientStats {
    std::uint32_t predicted_tick = 0;
    std::uint32_t server_tick = 0;
    std::uint32_t ack_tick = 0;
    std::uint32_t rollbacks = 0;
    std::uint32_t corrections = 0;
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
    std::uint32_t last_rollback_ticks = 0;
    std::uint32_t max_rollback_ticks = 0;
    std::uint32_t rtt_ticks = 0;
    std::uint64_t last_client_hash = 0;
    std::uint64_t last_server_hash = 0;
    bool last_snapshot_matched = true;
};

using InputProvider = std::function<core::InputCommand(std::uint32_t target_tick)>;

class LocalServer {
public:
    bool start(
        const core::Map& map,
        std::uint64_t seed,
        int player_count,
        std::uint32_t match_duration_ticks,
        std::uint16_t score_limit,
        std::string* error = nullptr);

    bool submit_input(std::uint8_t slot, const core::InputCommand& command);
    void step();

    [[nodiscard]] core::World& world() { return world_; }
    [[nodiscard]] const core::World& world() const { return world_; }
    [[nodiscard]] int player_count() const { return player_count_; }
    [[nodiscard]] core::EntityId entity_for_slot(std::uint8_t slot) const;

private:
    using SlotCommands = std::array<std::optional<core::InputCommand>, core::kMaxPlayers>;

    core::World world_;
    std::array<core::EntityId, core::kMaxPlayers> entities_{};
    std::array<core::InputCommand, core::kMaxPlayers> input_state_{};
    std::array<bool, core::kMaxPlayers> has_input_state_{};
    std::map<std::uint32_t, SlotCommands> pending_;
    int player_count_ = 0;
};

class LocalClient {
public:
    bool start(
        const core::Map& map,
        const core::WorldSnapshot& initial_snapshot,
        std::uint8_t local_slot,
        const NetConfig& config,
        std::string* error = nullptr);

    bool step(
        const InputProvider& provider,
        InputPacket& output,
        std::string* error = nullptr);

    void receive_input_broadcast(const InputPacket& packet);
    void queue_input_broadcast(const InputPacket& packet);
    void flush_pending_rollbacks();
    void advance_idle(std::uint32_t ticks);
    bool apply_snapshot(const SnapshotPacket& packet, std::string* error = nullptr);

    [[nodiscard]] bool inject_desync();

    [[nodiscard]] core::World& world() { return world_; }
    [[nodiscard]] const core::World& world() const { return world_; }
    [[nodiscard]] std::uint8_t local_slot() const { return local_slot_; }
    [[nodiscard]] std::optional<std::uint64_t> predicted_hash_at(
        std::uint32_t tick);
    [[nodiscard]] const ClientStats& stats() const { return stats_; }
    [[nodiscard]] const NetGraph& graph() const { return graph_; }
    [[nodiscard]] std::uint32_t input_delay() const { return input_delay_; }
    void set_input_delay(std::uint32_t delay_ticks);

    // Presentation-only: when a rollback/snapshot correction moves a player
    // at the current tick, the renderer can keep the drawn position
    // continuous and blend the error out instead of popping. `offset` is
    // (position before correction - position after correction).
    struct VisualCorrection {
        core::EntityId entity{};
        core::Vec2 offset{};
    };
    [[nodiscard]] std::vector<VisualCorrection> take_visual_corrections();

private:
    struct PlayerPosition {
        core::EntityId entity{};
        core::Vec2 position{};
        bool alive = false;
    };

    [[nodiscard]] std::vector<PlayerPosition> capture_player_positions() const;
    void accumulate_visual_corrections(
        const std::vector<PlayerPosition>& before);
    struct PredictedState {
        core::WorldSnapshot snapshot{};
        std::uint64_t hash = 0;
        bool has_hash = false;
    };

    core::InputFrame make_frame(std::uint32_t tick) const;
    void record_predicted();
    void trim_history();
    bool rollback_to(std::uint32_t tick);
    void seed_snapshot_commands(
        std::uint32_t tick,
        const core::WorldSnapshot& snapshot);

    core::World world_;
    std::array<core::EntityId, core::kMaxPlayers> entities_{};
    std::map<std::uint32_t, core::InputCommand> local_commands_;
    std::map<
        std::uint32_t,
        std::array<std::optional<core::InputCommand>, core::kMaxPlayers>>
        remote_commands_;
    std::map<std::uint32_t, PredictedState> predicted_;
    std::vector<VisualCorrection> visual_corrections_;
    NetGraph graph_{240};
    ClientStats stats_{};
    std::uint8_t local_slot_ = 0;
    std::uint32_t input_delay_ = 16;
    std::uint32_t rollback_history_ = 240;
    std::uint32_t rtt_ticks_ = 10;
    std::uint32_t last_server_tick_ = 0;
    std::uint32_t pending_rollback_tick_ = 0;
    int player_count_ = 0;
    bool started_ = false;
    bool has_server_tick_ = false;
    bool rollback_pending_ = false;
};

class LocalSession {
public:
    bool start(
        const core::Map& map,
        std::uint64_t seed,
        int client_count,
        int player_count,
        std::vector<InputProvider> providers,
        const NetConfig& config,
        std::uint32_t match_duration_ticks = 10U * 60U * 60U,
        std::uint16_t score_limit = 999,
        std::string* error = nullptr);

    void tick();

    [[nodiscard]] LocalServer& server() { return server_; }
    [[nodiscard]] const LocalServer& server() const { return server_; }
    [[nodiscard]] LocalClient& client(int index) { return clients_[index]; }
    [[nodiscard]] const LocalClient& client(int index) const { return clients_[index]; }
    [[nodiscard]] int client_count() const {
        return static_cast<int>(clients_.size());
    }
    [[nodiscard]] NetworkSimulator& network() { return network_; }
    [[nodiscard]] const NetworkSimulator& network() const { return network_; }
    [[nodiscard]] std::uint32_t tick() const { return wall_tick_; }

private:
    LocalServer server_;
    std::vector<LocalClient> clients_;
    std::vector<InputProvider> providers_;
    NetworkSimulator network_;
    std::uint32_t wall_tick_ = 0;
    bool started_ = false;
};

} // namespace nsync::net
