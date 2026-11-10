#include "net/local_net.h"

#include "core/rng.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

namespace nsync::net {
namespace {

std::uint32_t clamp_u32(std::uint32_t value, std::uint32_t minimum, std::uint32_t maximum) {
    return std::min(std::max(value, minimum), maximum);
}

const core::Player* snapshot_player(
    const core::WorldSnapshot& snapshot,
    std::uint8_t slot) {
    for (const core::Player& player : snapshot.players) {
        if (player.slot == slot) {
            return &player;
        }
    }
    return nullptr;
}

void trace_player(const char* label, const core::Player* player) {
    if (player == nullptr) {
        std::fprintf(stderr, " %s=<missing>", label);
        return;
    }
    std::fprintf(
        stderr,
        " %s=(x%d y%d vx%d vy%d g%d b%u)",
        label,
        static_cast<int>(player->position.x.raw()),
        static_cast<int>(player->position.y.raw()),
        static_cast<int>(player->velocity.x.raw()),
        static_cast<int>(player->velocity.y.raw()),
        static_cast<int>(player->grounded),
        static_cast<unsigned>(player->buttons));
}

const core::Player* find_player(
    const core::WorldSnapshot& snapshot,
    std::uint8_t slot) {
    for (const core::Player& player : snapshot.players) {
        if (player.slot == slot) {
            return &player;
        }
    }
    return nullptr;
}

const core::Projectile* find_projectile(
    const core::WorldSnapshot& snapshot,
    core::EntityId id) {
    for (const core::Projectile& projectile : snapshot.projectiles) {
        if (projectile.id == id) {
            return &projectile;
        }
    }
    return nullptr;
}

void trace_snapshot_diff(
    const core::WorldSnapshot& predicted,
    const core::WorldSnapshot& server,
    unsigned local_slot = 255U) {
    std::fprintf(
        stderr,
        "[net-diff] local=%u tick=%u/%u rng=%d players=%zu/%zu proj=%zu/%zu\n",
        local_slot,
        predicted.tick,
        server.tick,
        (predicted.rng.state == server.rng.state &&
         predicted.rng.increment == server.rng.increment) ? 0 : 1,
        predicted.players.size(),
        server.players.size(),
        predicted.projectiles.size(),
        server.projectiles.size());
    for (const core::Player& local : predicted.players) {
        const core::Player* remote = find_player(server, local.slot);
        if (remote == nullptr) {
            std::fprintf(
                stderr,
                "[net-diff] player slot=%u missing\n",
                static_cast<unsigned>(local.slot));
            continue;
        }
        std::fprintf(
            stderr,
            "[net-diff] slot=%u pos=%d vel=%d aim=%d hp=%d sh=%d ammo=%d "
            "weapon=%d buttons=%d alive=%d grounded=%d facing=%d fcd=%d "
            "reload=%d other=%d [fcd %u/%u ammo %u/%u btn %u/%u]\n",
            static_cast<unsigned>(local.slot),
            (local.position != remote->position) ? 1 : 0,
            (local.velocity != remote->velocity) ? 1 : 0,
            (local.aim != remote->aim) ? 1 : 0,
            (local.health != remote->health) ? 1 : 0,
            (local.shield != remote->shield) ? 1 : 0,
            (local.ammo != remote->ammo) ? 1 : 0,
            (local.weapon != remote->weapon) ? 1 : 0,
            (local.buttons != remote->buttons) ? 1 : 0,
            (local.alive != remote->alive) ? 1 : 0,
            (local.grounded != remote->grounded) ? 1 : 0,
            (local.facing != remote->facing) ? 1 : 0,
            (local.fire_cooldown != remote->fire_cooldown) ? 1 : 0,
            (local.reload_timer != remote->reload_timer) ? 1 : 0,
            (local.gadget_cooldowns != remote->gadget_cooldowns ||
             local.gadget_active_ticks != remote->gadget_active_ticks ||
             local.gadget_fuel != remote->gadget_fuel ||
             local.damage_dealt != remote->damage_dealt ||
             local.respawn_timer != remote->respawn_timer ||
             local.weapon_switch_cooldown != remote->weapon_switch_cooldown ||
             local.gadget_latch != remote->gadget_latch ||
             local.pad_boost != remote->pad_boost ||
             local.pad_boost_ticks != remote->pad_boost_ticks ||
             local.shield_regen_delay != remote->shield_regen_delay)
                ? 1
                : 0,
            static_cast<unsigned>(local.fire_cooldown),
            static_cast<unsigned>(remote->fire_cooldown),
            static_cast<unsigned>(local.ammo),
            static_cast<unsigned>(remote->ammo),
            static_cast<unsigned>(local.buttons),
            static_cast<unsigned>(remote->buttons));
    }
    for (const core::Projectile& local : predicted.projectiles) {
        const core::Projectile* remote = find_projectile(server, local.id);
        if (remote == nullptr) {
            std::fprintf(
                stderr,
                "[net-diff] proj id=%u missing\n",
                local.id.raw());
            continue;
        }
        if (local.position == remote->position &&
            local.velocity == remote->velocity &&
            local.life_ticks == remote->life_ticks &&
            local.terrain_bounces == remote->terrain_bounces) {
            continue;
        }
        std::fprintf(
            stderr,
            "[net-diff] proj id=%u pos=%d vel=%d life=%d terrain=%d\n",
            local.id.raw(),
            (local.position != remote->position) ? 1 : 0,
            (local.velocity != remote->velocity) ? 1 : 0,
            (local.life_ticks != remote->life_ticks) ? 1 : 0,
            (local.terrain_bounces != remote->terrain_bounces) ? 1 : 0);
    }
    for (const core::Projectile& remote : server.projectiles) {
        if (find_projectile(predicted, remote.id) == nullptr) {
            std::fprintf(
                stderr,
                "[net-diff] proj id=%u only-on-server\n",
                remote.id.raw());
        }
    }
}

bool local_player_matches(
    const core::WorldSnapshot& local_snapshot,
    const core::WorldSnapshot& server_snapshot,
    std::uint8_t local_slot) {
    const core::Player* current = nullptr;
    for (const core::Player& player : local_snapshot.players) {
        if (player.slot == local_slot) {
            current = &player;
            break;
        }
    }
    if (current == nullptr) {
        return false;
    }
    const core::Player* remote = nullptr;
    for (const core::Player& player : server_snapshot.players) {
        if (player.slot == local_slot) {
            remote = &player;
            break;
        }
    }
    if (remote == nullptr || remote->alive != current->alive ||
        remote->health.raw() != current->health.raw() ||
        remote->shield != current->shield) {
        return false;
    }
    const core::Fix32 dx = remote->position.x - current->position.x;
    const core::Fix32 dy = remote->position.y - current->position.y;
    constexpr std::int64_t kPositionTolerance =
        core::Fix32::kOne / 2;
    const std::int64_t distance_squared =
        static_cast<std::int64_t>(dx.raw()) * dx.raw() +
        static_cast<std::int64_t>(dy.raw()) * dy.raw();
    return distance_squared <=
        kPositionTolerance * kPositionTolerance;
}

core::InputCommand command_from_player(const core::Player& player) {
    constexpr std::int64_t kAimMax = 32767;
    core::InputCommand command;
    command.buttons = player.buttons;
    command.aim_x = static_cast<std::int16_t>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(player.aim.x.raw()) * kAimMax /
            core::Fix32::kOne,
        -kAimMax,
        kAimMax));
    command.aim_y = static_cast<std::int16_t>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(player.aim.y.raw()) * kAimMax /
            core::Fix32::kOne,
        -kAimMax,
        kAimMax));
    return command;
}

} // namespace

void NetworkSimulator::configure(const NetConfig& config) {
    config_ = config;
    config_.latency_ticks = clamp_u32(config_.latency_ticks, 0, 240);
    config_.jitter_ticks = clamp_u32(config_.jitter_ticks, 0, 120);
    config_.loss_percent = clamp_u32(config_.loss_percent, 0, 100);
    config_.duplicate_percent = clamp_u32(config_.duplicate_percent, 0, 100);
    config_.input_delay_ticks = clamp_u32(config_.input_delay_ticks, 0, 240);
    config_.snapshot_interval = clamp_u32(config_.snapshot_interval, 1, 60);
    config_.rollback_history = clamp_u32(config_.rollback_history, 16, 2000);

    stats_ = {};
    stats_.rtt_ticks = config_.latency_ticks * 2U;
    stats_.rtt_ms = stats_.rtt_ticks * 1000U / 60U;
    queue_.clear();
    next_order_ = 0;
    rng_.seed(config_.seed);
}

void NetworkSimulator::enqueue(
    const InputPacket& input,
    const SnapshotPacket& snapshot,
    bool snapshot_packet,
    bool broadcast,
    std::uint32_t now_tick) {
    ++stats_.packets_sent;
    if (config_.loss_percent > 0 &&
        rng_.bounded(100) < config_.loss_percent) {
        ++stats_.packets_dropped;
        return;
    }

    auto schedule = [&](std::uint32_t extra_delay) {
        std::int64_t due = static_cast<std::int64_t>(now_tick) +
            static_cast<std::int64_t>(config_.latency_ticks) +
            static_cast<std::int64_t>(extra_delay);
        if (config_.jitter_ticks > 0) {
            const std::uint32_t span = config_.jitter_ticks * 2U + 1U;
            due += static_cast<std::int64_t>(rng_.bounded(span)) -
                static_cast<std::int64_t>(config_.jitter_ticks);
        }
        if (due < static_cast<std::int64_t>(now_tick)) {
            due = now_tick;
        }

        PendingPacket pending;
        pending.order = next_order_++;
        pending.due_tick = static_cast<std::uint32_t>(due);
        pending.snapshot = snapshot_packet;
        pending.broadcast = broadcast;
        pending.input = input;
        pending.snapshot_packet = snapshot;
        queue_.push_back(std::move(pending));
    };

    schedule(0);
    if (config_.duplicate_percent > 0 &&
        rng_.bounded(100) < config_.duplicate_percent) {
        ++stats_.packets_duplicated;
        const std::uint32_t extra = 1U +
            (config_.jitter_ticks > 0
                 ? rng_.bounded(config_.jitter_ticks + 1U)
                 : 0U);
        schedule(extra);
    }
}

void NetworkSimulator::send_input(
    const InputPacket& packet,
    std::uint32_t now_tick) {
    enqueue(packet, {}, false, false, now_tick);
}

void NetworkSimulator::send_input_broadcast(
    const InputPacket& packet,
    std::uint32_t now_tick) {
    enqueue(packet, {}, false, true, now_tick);
}

void NetworkSimulator::send_snapshot(
    const SnapshotPacket& packet,
    std::uint32_t now_tick) {
    enqueue({}, packet, true, false, now_tick);
}

Deliveries NetworkSimulator::pump(std::uint32_t now_tick) {
    Deliveries deliveries;
    std::vector<PendingPacket> due;
    for (auto it = queue_.begin(); it != queue_.end();) {
        if (it->due_tick <= now_tick) {
            due.push_back(std::move(*it));
            it = queue_.erase(it);
        } else {
            ++it;
        }
    }

    std::stable_sort(
        due.begin(),
        due.end(),
        [](const PendingPacket& a, const PendingPacket& b) {
            if (a.due_tick != b.due_tick) {
                return a.due_tick < b.due_tick;
            }
            return a.order < b.order;
        });

    for (PendingPacket& packet : due) {
        ++stats_.packets_delivered;
        if (packet.snapshot) {
            deliveries.snapshots.push_back(std::move(packet.snapshot_packet));
        } else if (packet.broadcast) {
            deliveries.broadcasts.push_back(packet.input);
        } else {
            deliveries.server_inputs.push_back(packet.input);
        }
    }
    return deliveries;
}

NetGraph::NetGraph(std::size_t capacity)
    : capacity_(std::max<std::size_t>(16, capacity)) {}

void NetGraph::add(const NetGraphSample& sample) {
    samples_.push_back(sample);
    while (samples_.size() > capacity_) {
        samples_.pop_front();
    }
}

void NetGraph::clear() {
    samples_.clear();
}

bool LocalServer::start(
    const core::Map& map,
    std::uint64_t seed,
    int player_count,
    std::uint32_t match_duration_ticks,
    std::uint16_t score_limit,
    std::string* error) {
    if (player_count < 1 || player_count > core::kMaxPlayers) {
        if (error) {
            *error = "invalid server player count";
        }
        return false;
    }

    world_.set_map(map);
    world_.reset(seed, player_count);
    world_.set_match_rules(match_duration_ticks, score_limit);
    player_count_ = player_count;
    pending_.clear();
    entities_.fill({});
    input_state_.fill({});
    has_input_state_.fill(false);

    world_.players().for_each([&](const core::Player& player) {
        if (player.slot < core::kMaxPlayers) {
            entities_[player.slot] = player.id;
        }
    });
    return true;
}

bool LocalServer::submit_input(
    std::uint8_t slot,
    const core::InputCommand& command) {
    if (slot >= static_cast<std::uint8_t>(player_count_)) {
        return false;
    }
    if (command.tick < world_.tick()) {
        return false;
    }
    auto& commands = pending_[command.tick];
    if (commands[slot].has_value()) {
        return false;
    }
    commands[slot] = command;
    return true;
}

void LocalServer::step() {
    const std::uint32_t tick = world_.tick();
    core::InputFrame frame;
    const auto found = pending_.find(tick);
    for (int slot = 0; slot < player_count_; ++slot) {
        const auto slot_index = static_cast<std::size_t>(slot);
        if (found != pending_.end()) {
            const auto& command = found->second[slot_index];
            if (command.has_value() && command->tick == tick) {
                input_state_[slot_index] = *command;
                has_input_state_[slot_index] = true;
            }
        }
        if (!has_input_state_[slot_index]) {
            continue;
        }
        core::PlayerCommand player_command;
        player_command.entity = entities_[slot_index];
        player_command.command = input_state_[slot_index];
        player_command.command.tick = tick;
        if (player_command.entity.valid()) {
            frame.commands.push_back(player_command);
        }
    }
    std::sort(
        frame.commands.begin(),
        frame.commands.end(),
        core::player_command_less);
    world_.step(frame);
    pending_.erase(tick);
}

core::EntityId LocalServer::entity_for_slot(std::uint8_t slot) const {
    if (slot >= static_cast<std::uint8_t>(player_count_)) {
        return {};
    }
    return entities_[slot];
}

bool LocalClient::start(
    const core::Map& map,
    const core::WorldSnapshot& initial_snapshot,
    std::uint8_t local_slot,
    const NetConfig& config,
    std::string* error) {
    world_.set_map(map);
    if (!world_.restore(initial_snapshot, error)) {
        return false;
    }
    player_count_ = world_.player_count();
    if (local_slot >= static_cast<std::uint8_t>(player_count_)) {
        if (error) {
            *error = "invalid local client slot";
        }
        return false;
    }

    entities_.fill({});
    world_.players().for_each([&](const core::Player& player) {
        if (player.slot < core::kMaxPlayers) {
            entities_[player.slot] = player.id;
        }
    });

    local_slot_ = local_slot;
    input_delay_ = clamp_u32(config.input_delay_ticks, 0, 240);
    rollback_history_ = std::max<std::uint32_t>(16, config.rollback_history);
    rtt_ticks_ = config.latency_ticks * 2U;
    last_server_tick_ = initial_snapshot.tick;
    pending_rollback_tick_ = 0;
    has_server_tick_ = true;
    rollback_pending_ = false;
    started_ = true;

    local_commands_.clear();
    remote_commands_.clear();
    predicted_.clear();
    visual_corrections_.clear();
    graph_.clear();
    stats_ = {};
    stats_.predicted_tick = world_.tick();
    stats_.server_tick = initial_snapshot.tick;
    stats_.ack_tick = initial_snapshot.tick;
    stats_.rtt_ticks = rtt_ticks_;

    PredictedState state;
    state.snapshot = initial_snapshot;
    state.hash = core::snapshot_hash(initial_snapshot);
    state.has_hash = true;
    predicted_[initial_snapshot.tick] = std::move(state);
    return true;
}

core::InputFrame LocalClient::make_frame(std::uint32_t tick) const {
    core::InputFrame frame;
    for (int slot = 0; slot < player_count_; ++slot) {
        core::InputCommand command;
        bool found_command = false;
        if (slot == static_cast<int>(local_slot_)) {
            auto found = local_commands_.upper_bound(tick);
            while (found != local_commands_.begin()) {
                --found;
                command = found->second;
                found_command = true;
                break;
            }
        } else {
            auto found = remote_commands_.upper_bound(tick);
            while (found != remote_commands_.begin()) {
                --found;
                const auto& value =
                    found->second[static_cast<std::size_t>(slot)];
                if (value.has_value()) {
                    command = *value;
                    found_command = true;
                    break;
                }
            }
        }
        if (!found_command) {
            continue;
        }
        const core::EntityId entity =
            entities_[static_cast<std::size_t>(slot)];
        if (!entity.valid()) {
            continue;
        }
        command.tick = tick;
        core::PlayerCommand player_command;
        player_command.entity = entity;
        player_command.command = command;
        frame.commands.push_back(player_command);
    }
    std::sort(
        frame.commands.begin(),
        frame.commands.end(),
        core::player_command_less);
    return frame;
}

void LocalClient::record_predicted() {
    PredictedState state;
    world_.save(state.snapshot);
    predicted_[world_.tick()] = std::move(state);
}

std::optional<std::uint64_t> LocalClient::predicted_hash_at(
    std::uint32_t tick) {
    auto found = predicted_.find(tick);
    if (found == predicted_.end()) {
        return std::nullopt;
    }
    if (!found->second.has_hash) {
        found->second.hash = core::snapshot_hash(found->second.snapshot);
        found->second.has_hash = true;
    }
    return found->second.hash;
}

void LocalClient::trim_history() {
    const std::uint32_t tick = world_.tick();
    if (tick <= rollback_history_) {
        return;
    }
    const std::uint32_t cutoff = tick - rollback_history_;
    auto trim = [&](auto& values) {
        for (auto it = values.begin();
             it != values.end() && it->first < cutoff;) {
            it = values.erase(it);
        }
    };
    trim(local_commands_);
    trim(remote_commands_);
    trim(predicted_);
}

void LocalClient::set_input_delay(std::uint32_t delay_ticks) {
    input_delay_ = clamp_u32(delay_ticks, 0, 240);
}

std::vector<LocalClient::PlayerPosition>
LocalClient::capture_player_positions() const {
    std::vector<PlayerPosition> positions;
    positions.reserve(world_.players().size());
    world_.players().for_each([&](const core::Player& player) {
        positions.push_back(
            PlayerPosition{player.id, player.position, player.alive != 0});
    });
    return positions;
}

void LocalClient::accumulate_visual_corrections(
    const std::vector<PlayerPosition>& before) {
    // Larger jumps are real teleports (respawn, out-of-sync recovery); the
    // renderer snaps those instead of sliding across the map.
    constexpr std::int64_t kMaxSmoothedRaw = 4LL * core::Fix32::kOne;
    for (const PlayerPosition& previous : before) {
        if (!previous.alive) {
            continue;
        }
        const core::Player* now = world_.players().get(previous.entity);
        if (now == nullptr || now->alive == 0) {
            continue;
        }
        const core::Vec2 offset = previous.position - now->position;
        if (offset.x == core::Fix32::zero() && offset.y == core::Fix32::zero()) {
            continue;
        }
        if (std::llabs(static_cast<long long>(offset.x.raw())) > kMaxSmoothedRaw ||
            std::llabs(static_cast<long long>(offset.y.raw())) > kMaxSmoothedRaw) {
            continue;
        }
        auto existing = std::find_if(
            visual_corrections_.begin(),
            visual_corrections_.end(),
            [&](const VisualCorrection& value) {
                return value.entity == previous.entity;
            });
        if (existing == visual_corrections_.end()) {
            // Bounded: entries are merged per entity and only
            // player-count many entities exist.
            if (visual_corrections_.size() < core::kMaxPlayers) {
                visual_corrections_.push_back(
                    VisualCorrection{previous.entity, offset});
            }
        } else {
            existing->offset += offset;
        }
    }
}

std::vector<LocalClient::VisualCorrection>
LocalClient::take_visual_corrections() {
    std::vector<VisualCorrection> result;
    result.swap(visual_corrections_);
    return result;
}

void LocalClient::seed_snapshot_commands(
    std::uint32_t tick,
    const core::WorldSnapshot& snapshot) {
    for (const core::Player& player : snapshot.players) {
        if (player.slot >= core::kMaxPlayers) {
            continue;
        }
        core::InputCommand command = command_from_player(player);
        command.tick = tick;
        if (player.slot == local_slot_) {
            local_commands_.emplace(tick, command);
            continue;
        }
        bool has_command = false;
        auto previous = remote_commands_.upper_bound(tick);
        while (previous != remote_commands_.begin()) {
            --previous;
            if (previous->second[static_cast<std::size_t>(player.slot)]
                    .has_value()) {
                has_command = true;
                break;
            }
        }
        if (has_command) {
            continue;
        }
        auto& slot =
            remote_commands_[tick][static_cast<std::size_t>(player.slot)];
        if (!slot.has_value()) {
            slot = command;
        }
    }
}

bool LocalClient::rollback_to(std::uint32_t tick) {
    const std::uint32_t current = world_.tick();
    if (tick > current) {
        return false;
    }
    // predicted_[T] is the state *before* tick T is simulated (it may be an
    // authoritative server snapshot). Restore exactly that state and replay
    // from T; restoring T-1 could replay over an authoritative snapshot with
    // an older, already corrected prediction.
    auto base = predicted_.find(tick);
    if (base == predicted_.end()) {
        base = predicted_.upper_bound(tick);
        if (base == predicted_.begin()) {
            return false;
        }
        --base;
    }
    const std::uint32_t base_tick = base->first;
    const std::vector<PlayerPosition> before = capture_player_positions();
    if (!world_.restore(base->second.snapshot, nullptr)) {
        return false;
    }
    auto erase = predicted_.upper_bound(base_tick);
    predicted_.erase(erase, predicted_.end());
    for (std::uint32_t replay = base_tick; replay < current; ++replay) {
        world_.step(make_frame(replay));
        record_predicted();
    }
    accumulate_visual_corrections(before);
    ++stats_.rollbacks;
    ++stats_.event_rollbacks;
    const std::uint32_t distance = current - base_tick;
    stats_.total_rollback_ticks += distance;
    stats_.last_rollback_ticks = distance;
    stats_.max_rollback_ticks =
        std::max(stats_.max_rollback_ticks, distance);
    stats_.predicted_tick = world_.tick();
    return true;
}

bool LocalClient::step(
    const InputProvider& provider,
    InputPacket& output,
    std::string* error) {
    if (!started_) {
        if (error) {
            *error = "client not started";
        }
        return false;
    }

    flush_pending_rollbacks();
    const std::uint32_t target_tick = world_.tick() + input_delay_;
    core::InputCommand command =
        provider ? provider(target_tick) : core::InputCommand{};
    command.tick = target_tick;
    command.sequence = static_cast<std::uint8_t>(target_tick & 0xFFU);
    local_commands_[target_tick] = command;

    output.slot = local_slot_;
    output.command = command;

    world_.step(make_frame(world_.tick()));
    record_predicted();
    stats_.predicted_tick = world_.tick();
    trim_history();
    return true;
}

void LocalClient::advance_idle(std::uint32_t ticks) {
    if (!started_) {
        return;
    }
    for (std::uint32_t step = 0; step < ticks; ++step) {
        world_.step(core::InputFrame{});
        record_predicted();
    }
    stats_.predicted_tick = world_.tick();
    trim_history();
}

void LocalClient::receive_input_broadcast(const InputPacket& packet) {
    queue_input_broadcast(packet);
    flush_pending_rollbacks();
}

void LocalClient::queue_input_broadcast(const InputPacket& packet) {
    if (!started_ || packet.slot >= static_cast<std::uint8_t>(player_count_) ||
        packet.slot == local_slot_) {
        return;
    }
    const std::uint32_t tick = packet.command.tick;
    if (tick + rollback_history_ < world_.tick()) {
        return;
    }
    // The original BattleMaster.enqueueServerEvent replays *every* late
    // ClientEventBroadcast that is still inside the snapshot history, even
    // if a server sync snapshot for a later frame was already applied. The
    // server (late_input_replay) rewrites its history for such inputs, so
    // the snapshots we already applied after `tick` are stale: dropping the
    // input as "covered" left the remote player wrong until the next
    // snapshot mismatch and made it pop. Count it, then roll back normally.
    if (has_server_tick_ && tick < last_server_tick_) {
        ++stats_.covered_inputs;
    }
    const std::uint32_t current_tick = world_.tick();
    auto& commands = remote_commands_[tick];
    auto& slot = commands[static_cast<std::size_t>(packet.slot)];
    const auto same_command = [&packet](const core::InputCommand& value) {
        return value.buttons == packet.command.buttons &&
            value.aim_x == packet.command.aim_x &&
            value.aim_y == packet.command.aim_y;
    };
    bool behavior_unchanged = false;
    if (slot.has_value()) {
        if (same_command(*slot)) {
            return;
        }
    } else {
        auto previous = remote_commands_.lower_bound(tick);
        if (previous != remote_commands_.begin()) {
            --previous;
            const auto& value =
                previous->second[static_cast<std::size_t>(packet.slot)];
            if (value.has_value() && same_command(*value)) {
                behavior_unchanged = true;
            }
        }
    }
    slot = packet.command;
    slot->tick = tick;
    // Original BattleMaster replays every late ClientEventBroadcast (move,
    // jump, fire, weapon redirect/aim ...). Aim changes facing, projectile
    // direction and therefore hits, so they must roll back as well;
    // otherwise the remote prediction diverges and every periodic snapshot
    // has to correct it (visible as remote stutter).
    if (!behavior_unchanged && tick < current_tick) {
        if (!rollback_pending_ || tick < pending_rollback_tick_) {
            pending_rollback_tick_ = tick;
        }
        rollback_pending_ = true;
    }
}

void LocalClient::flush_pending_rollbacks() {
    if (!rollback_pending_) {
        return;
    }
    const std::uint32_t tick = pending_rollback_tick_;
    rollback_pending_ = false;
    pending_rollback_tick_ = 0;
    if (tick <= world_.tick()) {
        (void)rollback_to(tick);
    }
}

bool LocalClient::apply_snapshot(
    const SnapshotPacket& packet,
    std::string* error) {
    if (!started_) {
        if (error) {
            *error = "client not started";
        }
        return false;
    }

    const std::uint32_t target_tick = packet.snapshot.tick;
    if (has_server_tick_ && target_tick <= last_server_tick_) {
        return true;
    }

    const std::uint32_t current_tick = world_.tick();
    const std::uint64_t server_hash = packet.hash != 0
        ? packet.hash
        : core::snapshot_hash(packet.snapshot);
    if (target_tick > current_tick) {
        if (!world_.restore(packet.snapshot, error)) {
            return false;
        }
        seed_snapshot_commands(target_tick, packet.snapshot);
        PredictedState state;
        state.snapshot = packet.snapshot;
        state.hash = server_hash;
        state.has_hash = true;
        predicted_[target_tick] = std::move(state);
        last_server_tick_ = target_tick;
        has_server_tick_ = true;
        stats_.predicted_tick = world_.tick();
        stats_.server_tick = target_tick;
        stats_.ack_tick = target_tick;
        stats_.rtt_ticks = rtt_ticks_;
        stats_.last_client_hash = server_hash;
        stats_.last_server_hash = server_hash;
        stats_.last_snapshot_matched = true;
        NetGraphSample sample;
        sample.tick = current_tick;
        sample.ack_tick = target_tick;
        sample.rtt_ticks = rtt_ticks_;
        sample.rollback_ticks = 0;
        sample.client_hash = server_hash;
        sample.server_hash = server_hash;
        sample.matched = true;
        graph_.add(sample);
        trim_history();
        return true;
    }
    auto predicted = predicted_.find(target_tick);
    if (predicted != predicted_.end() && !predicted->second.has_hash) {
        predicted->second.hash = core::snapshot_hash(predicted->second.snapshot);
        predicted->second.has_hash = true;
    }
    const bool matched =
        predicted != predicted_.end() && predicted->second.hash == server_hash;

    stats_.last_snapshot_matched = matched;
    stats_.last_server_hash = server_hash;
    stats_.last_client_hash = predicted != predicted_.end()
        ? predicted->second.hash
        : server_hash;
    const std::uint32_t potential_rollback =
        current_tick > target_tick ? current_tick - target_tick : 0;
    const bool preserve_local =
        !packet.authoritative &&
        predicted != predicted_.end() &&
        local_player_matches(
            predicted->second.snapshot,
            packet.snapshot,
            local_slot_);
    if (!matched) {
        ++stats_.snapshot_mismatches;
        if (predicted == predicted_.end()) {
            ++stats_.snapshot_missing_predictions;
        }
        if (packet.authoritative) {
            ++stats_.snapshot_authoritative_mismatches;
        } else {
            ++stats_.snapshot_periodic_mismatches;
        }
        if (std::getenv("NSYNC_NET_TRACE") != nullptr &&
            predicted != predicted_.end() &&
            stats_.snapshot_mismatches <= 24U) {
            trace_snapshot_diff(
                predicted->second.snapshot, packet.snapshot, local_slot_);
        }
        // Converge to the server like the original BattleMaster.handleSync:
        // always restore the full authoritative snapshot and re-simulate the
        // predicted ticks with all known inputs. Keeping a slightly wrong
        // local prediction (the old "preserve_local" path) never converged,
        // so every later snapshot mismatched again; patching only remote
        // positions left their velocity wrong. The renderer blends the
        // resulting position change (take_visual_corrections), so the full
        // restore does not pop on screen.
        {
            if (std::getenv("NSYNC_NET_TRACE") != nullptr &&
                stats_.snapshot_restores < 64U) {
                const core::Player* current_player = nullptr;
                world_.players().for_each([&](const core::Player& player) {
                    if (player.slot == local_slot_) {
                        current_player = &player;
                    }
                });
                const auto command_at = [&](std::uint32_t tick) {
                    const auto found = local_commands_.find(tick);
                    return found == local_commands_.end()
                        ? 0U
                        : static_cast<unsigned>(found->second.buttons);
                };
                std::fprintf(
                    stderr,
                    "[net-mismatch] target=%u current=%u pred=%s cmdt=%u cmdt1=%u cmdc=%u",
                    target_tick,
                    current_tick,
                    predicted != predicted_.end() ? "yes" : "no",
                    command_at(target_tick),
                    command_at(target_tick > 0 ? target_tick - 1 : 0),
                    command_at(current_tick));
                trace_player(
                    "server",
                    snapshot_player(packet.snapshot, local_slot_));
                trace_player(
                    "pred",
                    predicted != predicted_.end()
                        ? snapshot_player(
                              predicted->second.snapshot,
                              local_slot_)
                        : nullptr);
                trace_player("local", current_player);
                std::fprintf(stderr, "\n");
            }
            const std::vector<PlayerPosition> before =
                capture_player_positions();
            if (!world_.restore(packet.snapshot, error)) {
                return false;
            }
            if (preserve_local) {
                // Only remote state (or the local player within tolerance)
                // differed.
                ++stats_.remote_snapshot_corrections;
            } else {
                ++stats_.snapshot_restores;
                if (potential_rollback > 0) {
                    ++stats_.rollbacks;
                    stats_.total_rollback_ticks += potential_rollback;
                    stats_.last_rollback_ticks = potential_rollback;
                    stats_.max_rollback_ticks = std::max(
                        stats_.max_rollback_ticks, potential_rollback);
                }
            }
            if (packet.authoritative) {
                ++stats_.corrections;
                ++stats_.desyncs;
            }
            seed_snapshot_commands(target_tick, packet.snapshot);

            PredictedState state;
            state.snapshot = packet.snapshot;
            state.hash = server_hash;
            state.has_hash = true;
            predicted_[target_tick] = std::move(state);
            predicted_.erase(
                predicted_.upper_bound(target_tick),
                predicted_.end());

            for (std::uint32_t tick = target_tick; tick < current_tick; ++tick) {
                world_.step(make_frame(tick));
                record_predicted();
            }
            accumulate_visual_corrections(before);
        }
    }

    last_server_tick_ = target_tick;
    has_server_tick_ = true;
    stats_.predicted_tick = world_.tick();
    stats_.server_tick = target_tick;
    stats_.ack_tick = target_tick;
    stats_.rtt_ticks = rtt_ticks_;

    NetGraphSample sample;
    sample.tick = current_tick;
    sample.ack_tick = target_tick;
    sample.rtt_ticks = rtt_ticks_;
    sample.rollback_ticks = matched ? 0U : potential_rollback;
    sample.client_hash = stats_.last_client_hash;
    sample.server_hash = server_hash;
    sample.matched = matched;
    graph_.add(sample);
    if (!matched) {
        stats_.last_client_hash = server_hash;
    }

    trim_history();
    return true;
}

bool LocalClient::inject_desync() {
    if (!started_) {
        return false;
    }
    core::Player* player = world_.players().get(entities_[local_slot_]);
    if (player == nullptr) {
        return false;
    }
    player->position.x += core::Fix32::from_int(1);
    record_predicted();
    return true;
}

bool LocalSession::start(
    const core::Map& map,
    std::uint64_t seed,
    int client_count,
    int player_count,
    std::vector<InputProvider> providers,
    const NetConfig& config,
    std::uint32_t match_duration_ticks,
    std::uint16_t score_limit,
    std::string* error) {
    if (client_count < 1 || client_count > core::kMaxPlayers) {
        if (error) {
            *error = "invalid local client count";
        }
        return false;
    }
    if (player_count < client_count || player_count > core::kMaxPlayers) {
        if (error) {
            *error = "invalid local player count";
        }
        return false;
    }
    if (providers.size() < static_cast<std::size_t>(client_count)) {
        if (error) {
            *error = "not enough input providers";
        }
        return false;
    }

    network_.configure(config);
    if (!server_.start(
            map,
            seed,
            player_count,
            match_duration_ticks,
            score_limit,
            error)) {
        return false;
    }

    core::WorldSnapshot initial;
    server_.world().save(initial);

    clients_.clear();
    clients_.reserve(static_cast<std::size_t>(client_count));
    for (int index = 0; index < client_count; ++index) {
        LocalClient client;
        if (!client.start(
                map,
                initial,
                static_cast<std::uint8_t>(index),
                network_.config(),
                error)) {
            return false;
        }
        clients_.push_back(std::move(client));
    }

    providers_.clear();
    providers_.reserve(static_cast<std::size_t>(client_count));
    for (int index = 0; index < client_count; ++index) {
        providers_.push_back(std::move(providers[static_cast<std::size_t>(index)]));
    }

    wall_tick_ = 0;
    started_ = true;
    return true;
}

void LocalSession::tick() {
    if (!started_) {
        return;
    }

    const Deliveries deliveries = network_.pump(wall_tick_);

    for (const InputPacket& input : deliveries.server_inputs) {
        if (server_.submit_input(input.slot, input.command)) {
            network_.send_input_broadcast(input, wall_tick_);
        }
    }

    for (const InputPacket& broadcast : deliveries.broadcasts) {
        for (LocalClient& client : clients_) {
            client.receive_input_broadcast(broadcast);
        }
    }

    for (const SnapshotPacket& snapshot : deliveries.snapshots) {
        if (snapshot.client_index >= 0 &&
            snapshot.client_index < static_cast<int>(clients_.size())) {
            clients_[static_cast<std::size_t>(snapshot.client_index)]
                .apply_snapshot(snapshot);
        }
    }

    for (std::size_t index = 0; index < clients_.size(); ++index) {
        InputPacket output;
        clients_[index].step(providers_[index], output);
        network_.send_input(output, wall_tick_);
    }

    server_.step();

    const std::uint32_t interval = network_.config().snapshot_interval;
    if (interval == 0 || (wall_tick_ % interval) == 0) {
        core::WorldSnapshot snapshot;
        server_.world().save(snapshot);
        for (std::size_t index = 0; index < clients_.size(); ++index) {
            SnapshotPacket packet;
            packet.client_index = static_cast<int>(index);
            packet.snapshot = snapshot;
            network_.send_snapshot(packet, wall_tick_);
        }
    }

    ++wall_tick_;
}

} // namespace nsync::net

