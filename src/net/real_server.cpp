#include "net/real_server.h"

#include "core/byte_io.h"
#include "core/heroes.h"
#include "core/map.h"
#include "shared/crash_handler.h"
#include "shared/log.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

namespace nsync::net {
namespace {

std::uint32_t clamp_input_delay(std::uint16_t value) {
    return std::clamp<std::uint32_t>(value, 2U, 60U);
}

std::uint16_t apply_action_frame(
    std::uint16_t raw_buttons,
    const ActionEventFrame& frame) {
    const auto button_mask = [](core::Button button) {
        return static_cast<std::uint16_t>(button);
    };
    std::uint16_t buttons = static_cast<std::uint16_t>(
        raw_buttons &
        (button_mask(core::kButtonLeft) |
         button_mask(core::kButtonRight) |
         button_mask(core::kButtonJump) |
         button_mask(core::kButtonDown) |
         button_mask(core::kButtonFire) |
         button_mask(core::kButtonWeaponSwitch)));
    const auto apply_toggle =
        [&](ClientAction action, core::Button button) {
            if ((frame.actions & client_action_bit(action)) == 0U) {
                return;
            }
            if ((frame.toggles & client_action_bit(action)) != 0U) {
                buttons |= button_mask(button);
            } else {
                buttons &= static_cast<std::uint16_t>(
                    ~button_mask(button));
            }
        };
    apply_toggle(ClientAction::GoLeft, core::kButtonLeft);
    apply_toggle(ClientAction::GoRight, core::kButtonRight);
    apply_toggle(ClientAction::GoUp, core::kButtonJump);
    apply_toggle(ClientAction::GoDown, core::kButtonDown);
    apply_toggle(ClientAction::WeaponFire, core::kButtonFire);
    if ((frame.actions &
         client_action_bit(ClientAction::WeaponReload)) != 0U) {
        buttons |= button_mask(core::kButtonReload);
    }
    if ((frame.actions &
         client_action_bit(ClientAction::ActivateGadget)) != 0U) {
        core::Button gadget = core::kButtonGadget1;
        if (frame.param == 1U) {
            gadget = core::kButtonGadget2;
        } else if (frame.param == 2U) {
            gadget = core::kButtonGadget3;
        }
        buttons |= button_mask(gadget);
    }
    return static_cast<std::uint16_t>(
        (raw_buttons & 0xFF00U) | buttons);
}
std::uint16_t held_action_button_mask() {
    return static_cast<std::uint16_t>(
        core::kButtonLeft | core::kButtonRight | core::kButtonJump |
        core::kButtonDown | core::kButtonFire);
}

std::uint64_t count_mask_bits(std::uint16_t value) {
    std::uint64_t count = 0;
    while (value != 0U) {
        value = static_cast<std::uint16_t>(value & (value - 1U));
        ++count;
    }
    return count;
}

} // namespace

RealServer::RealServer() = default;

RealServer::~RealServer() {
    stop();
}

bool RealServer::start(
    const core::Map& map,
    const NetServerConfig& config,
    std::string* error) {
    return start(std::vector<core::Map>{map}, config, error);
}

bool RealServer::start(
    const std::vector<core::Map>& maps,
    const NetServerConfig& config,
    std::string* error) {
    stop();
    if (maps.empty()) {
        if (error) {
            *error = "no maps configured";
        }
        return false;
    }
    for (const core::Map& map : maps) {
        if (map.id.empty() || map.id.size() > kMaxMapIdLength) {
            if (error) {
                *error = "invalid map id";
            }
            return false;
        }
    }
    if (config.player_count < 1 || config.player_count > core::kMaxPlayers) {
        if (error) {
            *error = "invalid player count";
        }
        return false;
    }
    if (config.tick_rate < 1 || config.tick_rate > 1000) {
        if (error) {
            *error = "invalid tick rate";
        }
        return false;
    }
    if (config.snapshot_interval < 1 || config.snapshot_interval > 60) {
        if (error) {
            *error = "invalid snapshot interval";
        }
        return false;
    }
    if (config.original_input_sync) {
        const std::uint32_t normal_lead =
            static_cast<std::uint32_t>(config.client_event_lag_ticks);
        const std::uint32_t recovery_lead =
            static_cast<std::uint32_t>(config.server_lag_ticks) +
            static_cast<std::uint32_t>(config.client_event_lag_ticks) +
            static_cast<std::uint32_t>(config.client_event_lag_ticks) / 2U +
            1U;
        if (config.server_lag_ticks > 60 ||
            config.client_event_lag_ticks < 1 ||
            config.client_event_lag_ticks > 10 ||
            config.sync_period_ticks < 1 ||
            config.sync_period_ticks > 240 ||
            normal_lead > config.max_input_ahead_ticks ||
            recovery_lead > config.max_input_ahead_ticks ||
            recovery_lead > 240) {
            if (error) {
                *error = "invalid input sync timing";
            }
            return false;
        }
    }
    if (config.game_mode != core::GameMode::Tdm &&
        config.game_mode != core::GameMode::Control &&
        config.game_mode != core::GameMode::Crystal) {
        if (error) {
            *error = "invalid game mode";
        }
        return false;
    }
    if (config.late_input_replay &&
        (config.late_input_replay_window_ticks < 1 ||
         config.late_input_replay_window_ticks > 600)) {
        if (error) {
            *error = "invalid late input replay window";
        }
        return false;
    }
    if (config.lobby_countdown_ms > 60000U ||
        config.match_end_delay_ms > 60000U ||
        config.lobby_min_humans < 1 ||
        config.lobby_min_humans > static_cast<std::uint32_t>(config.player_count)) {
        if (error) {
            *error = "invalid lobby config";
        }
        return false;
    }

    maps_ = maps;
    map_index_ = 0;
    map_ = maps_.front();
    config_ = config;
    if (!world_.start(
            map_,
            config_.seed,
            config_.player_count,
            config_.match_duration_ticks,
            config_.score_limit,
            error)) {
        return false;
    }
    world_.world().set_game_mode(config_.game_mode);
    if (!transport_.start(config_.port, error)) {
        return false;
    }

    sessions_.clear();
    slot_tokens_.fill(0);
    snapshot_history_.clear();
    stats_ = {};
    token_counter_ = 1;
    sequence_ = 1;
    start_ms_ = 0;
    last_update_ms_ = 0;
    next_tick_ms_ = 0;
    initialized_time_ = false;
    started_ = true;
    running_ = true;
    phase_ = config_.lobby_enabled ? LobbyPhase::Lobby : LobbyPhase::Match;
    match_id_ = config_.lobby_enabled ? 0 : 1;
    match_seed_ = config_.seed;
    countdown_deadline_ms_ = 0;
    match_end_deadline_ms_ = 0;
    last_lobby_broadcast_ms_ = 0;
    match_result_sent_ = false;
    world_.world().set_bot_slots(0);

    NSYNC_LOG_INFO(
        "net-server",
        "listening on udp port ",
        transport_.port(),
        " players=",
        config_.player_count,
        " lobby=",
        config_.lobby_enabled ? "on" : "off",
        " mode=",
        config_.game_mode == core::GameMode::Control
            ? "control"
            : (config_.game_mode == core::GameMode::Crystal ? "crystal" : "tdm"),
        " map=",
        map_.id,
        " input_sync=",
        config_.original_input_sync ? "original" : "legacy",
        " server_lag=",
        config_.server_lag_ticks,
        " client_event_lag=",
        config_.client_event_lag_ticks,
        " sync_period=",
        config_.sync_period_ticks,
        " late_input_replay=",
        config_.late_input_replay ? "on" : "off");
    return true;
}

void RealServer::stop() {
    running_ = false;
    started_ = false;
    sessions_.clear();
    slot_tokens_.fill(0);
    snapshot_history_.clear();
    maps_.clear();
    map_index_ = 0;
    phase_ = LobbyPhase::Lobby;
    match_id_ = 0;
    countdown_deadline_ms_ = 0;
    match_end_deadline_ms_ = 0;
    transport_.close();
}

std::uint16_t RealServer::port() const {
    return transport_.port();
}

std::uint32_t RealServer::tick() const {
    return world_.world().tick();
}

SyncTiming RealServer::timing() const {
    if (!config_.original_input_sync) {
        return {};
    }
    SyncTiming timing;
    timing.server_lag_ticks = config_.server_lag_ticks;
    timing.client_event_lag_ticks = config_.client_event_lag_ticks;
    timing.sync_period_ticks = config_.sync_period_ticks;
    return timing;
}

int RealServer::connected_clients() const {
    int count = 0;
    for (const auto& entry : sessions_) {
        if (entry.second.connected) {
            ++count;
        }
    }
    return count;
}

const core::World& RealServer::world() const {
    return world_.world();
}

TransportStats RealServer::transport_stats() const {
    return transport_.stats();
}

core::WorldSnapshot RealServer::snapshot() const {
    core::WorldSnapshot snapshot;
    world_.world().save(snapshot);
    return snapshot;
}

RealServer::Session* RealServer::find_session(std::uint64_t peer) {
    const auto found = sessions_.find(peer);
    return found == sessions_.end() ? nullptr : &found->second;
}

RealServer::Session* RealServer::find_session_by_token(std::uint32_t token) {
    if (token == 0) {
        return nullptr;
    }
    for (auto& entry : sessions_) {
        if (entry.second.token == token && entry.second.slot < core::kMaxPlayers) {
            return &entry.second;
        }
    }
    return nullptr;
}

int RealServer::free_slot() const {
    for (int slot = 0; slot < config_.player_count; ++slot) {
        if (slot_tokens_[static_cast<std::size_t>(slot)] == 0) {
            return slot;
        }
    }
    return -1;
}

std::uint32_t RealServer::server_time(std::uint32_t now_ms) const {
    if (!initialized_time_) {
        return 0;
    }
    return now_ms - start_ms_;
}

std::uint32_t RealServer::next_token() {
    for (std::uint32_t attempt = 0; attempt < 0xFFFFU; ++attempt) {
        const std::uint32_t token = token_counter_++;
        if (token != 0 &&
            std::find(slot_tokens_.begin(), slot_tokens_.end(), token) ==
                slot_tokens_.end()) {
            return token;
        }
    }
    return 0;
}

void RealServer::update(std::uint32_t now_ms) {
    if (!started_) {
        return;
    }
    if (!initialized_time_) {
        start_ms_ = now_ms;
        last_update_ms_ = now_ms;
        next_tick_ms_ = static_cast<std::uint64_t>(now_ms) *
            static_cast<std::uint64_t>(config_.tick_rate);
        initialized_time_ = true;
    }

    std::vector<TransportMessage> messages;
    std::string transport_error;
    if (!transport_.poll(messages, &transport_error)) {
        NSYNC_LOG_WARN("net-server", transport_error);
    }
    for (const TransportMessage& message : messages) {
        handle_message(message.peer, message.data, now_ms);
    }

    expire_sessions(now_ms);
    update_lobby(now_ms);

    if (running_) {
        // Exact fixed-rate scheduling. The previous `next += 1000 / rate`
        // truncated 16.67ms to 16ms, so a 60Hz server really ran at 62.5Hz
        // while clients ran at 60Hz; clients then had to double-step about
        // 2.5 times per second, which made remote players visibly hitch.
        // Time is tracked in units of (ms * tick_rate) to avoid drift.
        const std::uint64_t rate =
            static_cast<std::uint64_t>(config_.tick_rate);
        const std::uint64_t now_scaled =
            static_cast<std::uint64_t>(now_ms) * rate;
        constexpr std::uint64_t kTickScaled = 1000U;
        int steps = 0;
        while (now_scaled >= next_tick_ms_ &&
               steps < config_.max_catchup_steps) {
            simulate_tick(now_ms);
            next_tick_ms_ += kTickScaled;
            ++steps;
            // Snapshot every `snapshot_interval` ticks exactly. Checking only
            // the last tick of a multi-step update skipped snapshots (and
            // the sync-period history ticks) whenever the loop stepped
            // twice, so clients got snapshots in irregular bursts.
            if (phase_ == LobbyPhase::Match &&
                (world_.world().tick() % config_.snapshot_interval) == 0) {
                if (config_.broadcast_snapshots) {
                    send_snapshot_to_all(now_ms);
                } else {
                    record_snapshot();
                }
            }
        }
        if (now_scaled > next_tick_ms_ + kTickScaled * 2U) {
            next_tick_ms_ = now_scaled;
        }
        if (config_.lobby_enabled && phase_ == LobbyPhase::Match &&
            world_.world().match_over()) {
            finish_match(now_ms);
        }
    }

    transport_.update(now_ms);
    last_update_ms_ = now_ms;
}

void RealServer::handle_message(
    std::uint64_t peer,
    const std::vector<std::uint8_t>& payload,
    std::uint32_t now_ms) {
    Packet packet;
    std::string error;
    if (!decode_packet(payload.data(), payload.size(), packet, &error)) {
        NSYNC_LOG_DEBUG("net-server", "invalid packet from ", peer, ": ", error);
        return;
    }

    if (packet.type == MessageType::LoginRequest) {
        handle_login(peer, packet, now_ms);
        return;
    }

    Session* session = find_session(peer);
    if (session == nullptr) {
        return;
    }
    if (packet.session_token != session->token) {
        ++stats_.rejected_tokens;
        return;
    }
    session->last_seen_ms = now_ms;
    if (!session->packet_rate.allow(now_ms)) {
        ++stats_.rate_limited_packets;
        return;
    }

    switch (packet.type) {
    case MessageType::Input:
        handle_input(*session, packet, now_ms);
        break;
    case MessageType::LobbyRequest:
        handle_lobby_request(*session, packet, now_ms);
        break;
    case MessageType::PingRequest:
        handle_ping(*session, packet, now_ms);
        break;
    case MessageType::Snapshot:
    case MessageType::InputBroadcast:
    case MessageType::SyncResponse:
    case MessageType::LobbyResponse:
    case MessageType::LobbyState:
    case MessageType::MatchStart:
    case MessageType::MatchEnd:
        break;
    case MessageType::SnapshotAck:
        handle_snapshot_ack(*session, packet, now_ms);
        break;
    case MessageType::SyncRequest:
        handle_sync_request(*session, packet, now_ms);
        break;
    case MessageType::Disconnect:
        handle_disconnect(*session, packet);
        break;
    case MessageType::LoginResponse:
    case MessageType::PingResponse:
    default:
        break;
    }
}

void RealServer::handle_login(
    std::uint64_t peer,
    const Packet& packet,
    std::uint32_t now_ms) {
    LoginRequest request;
    std::string error;
    LoginResponse response;
    response.player_count = static_cast<std::uint8_t>(config_.player_count);
    response.protocol_version = kProtocolVersion;
    response.phase = static_cast<std::uint8_t>(phase_);
    response.match_id = match_id_;
    response.map_fingerprint = core::map_fingerprint(map_);
    response.map_id = map_.id;
    response.content_fingerprint = core::content().fingerprint();
    response.server_tick = world_.world().tick();
    response.server_time_ms = server_time(now_ms);
    response.tick_rate = static_cast<std::uint16_t>(config_.tick_rate);
    response.snapshot_interval = static_cast<std::uint16_t>(
        config_.snapshot_interval);
    response.timing = timing();
    response.sync_time_to_step_ms = config_.sync_time_to_step_ms;

    if (!decode_login_request(
            packet.payload.data(), packet.payload.size(), request, &error)) {
        response.error = error;
        Session temporary;
        temporary.peer = peer;
        send_packet(temporary, MessageType::LoginResponse, [&] {
            std::vector<std::uint8_t> encoded;
            (void)encode_login_response(response, encoded);
            return encoded;
        }(), 0);
        return;
    }

    if (request.protocol_version != kProtocolVersion) {
        response.error = "protocol version mismatch";
    } else if (request.map_fingerprint != response.map_fingerprint) {
        response.error = "map fingerprint mismatch";
        NSYNC_LOG_WARN(
            "net-server",
            "map mismatch: server=",
            response.map_id,
            " client=",
            request.map_id.empty() ? "unknown" : request.map_id);
    } else if (request.content_fingerprint != response.content_fingerprint) {
        response.error = "content fingerprint mismatch";
    } else if (request.desired_hero >= core::content().hero_count()) {
        response.error = "invalid hero";
    }

    Session previous;
    bool has_previous = false;
    Session* reconnect = find_session_by_token(request.reconnect_token);
    if (reconnect != nullptr) {
        previous = *reconnect;
        has_previous = true;
    } else if (Session* existing = find_session(peer);
               existing != nullptr && existing->token != 0) {
        previous = *existing;
        has_previous = true;
    }

    int slot = -1;
    if (response.error.empty()) {
        slot = has_previous ? previous.slot : free_slot();
        if (slot < 0) {
            response.error = "server is full";
        }
    }
    if (slot < 0) {
        if (!response.error.empty()) {
            NSYNC_LOG_WARN("net-server", "login rejected: ", response.error);
        }
        Session temporary;
        temporary.peer = peer;
        std::vector<std::uint8_t> encoded;
        if (encode_login_response(response, encoded)) {
            Packet reply;
            reply.type = MessageType::LoginResponse;
            reply.session_token = 0;
            reply.sequence = sequence_++;
            reply.payload = std::move(encoded);
            std::vector<std::uint8_t> bytes;
            if (encode_packet(reply, bytes)) {
                transport_.send(peer, bytes);
            }
        }
        return;
    }

    std::uint32_t token = 0;
    if (has_previous) {
        token = previous.token;
        if (previous.peer != peer) {
            sessions_.erase(previous.peer);
            transport_.remove_peer(previous.peer);
        }
    } else {
        token = next_token();
    }
    if (token == 0) {
        response.error = "session token exhausted";
        return;
    }

    Session session;
    session.peer = peer;
    session.token = token;
    session.slot = static_cast<std::uint8_t>(slot);
    session.connected = true;
    session.last_seen_ms = now_ms;
    session.disconnect_deadline_ms = 0;
    session.in_match = phase_ == LobbyPhase::Match;
    session.hero = request.desired_hero;
    session.team = request.desired_team <= 1
        ? request.desired_team
        : (session.slot % 2U);
    session.ready = false;
    session.packet_rate =
        RateLimiter(config_.max_packets_per_second);
    session.input_rate =
        RateLimiter(config_.max_inputs_per_second);
    if (has_previous) {
        session.name = previous.name;
        session.hero = previous.hero;
        session.team = previous.team;
        session.ready = previous.ready;
        session.in_match = previous.in_match || phase_ == LobbyPhase::Match;
    } else {
        session.name = request.display_name;
        if (session.name.empty()) {
            session.name = "player" + std::to_string(slot);
        }
        if (session.name.size() > kMaxDisplayNameLength) {
            session.name.resize(kMaxDisplayNameLength);
        }
    }
    sessions_[peer] = session;
    slot_tokens_[static_cast<std::size_t>(slot)] = token;
    if (config_.lobby_enabled && phase_ == LobbyPhase::Match) {
        refresh_bot_slots();
    }

    core::WorldSnapshot state = snapshot();
    response.accepted = 1;
    response.slot = session.slot;
    response.session_token = token;
    response.input_delay = config_.original_input_sync
        ? static_cast<std::uint32_t>(
              response.timing.normal_input_lead_ticks())
        : clamp_input_delay(request.input_delay);
    response.timing = timing();
    response.match_id = match_id_;
    response.phase = static_cast<std::uint8_t>(phase_);
    response.snapshot_hash = core::snapshot_hash(state);
    response.snapshot = core::encode_snapshot(state);
    response.error.clear();

    if (!running_) {
        running_ = true;
        next_tick_ms_ = static_cast<std::uint64_t>(now_ms) *
            static_cast<std::uint64_t>(config_.tick_rate);
    }
    Session* created = find_session(peer);
    if (created != nullptr) {
        send_packet(
            *created,
            MessageType::LoginResponse,
            [&] {
                std::vector<std::uint8_t> encoded;
                (void)encode_login_response(response, encoded);
                return encoded;
            }(),
            0);
        if (phase_ == LobbyPhase::Lobby ||
            phase_ == LobbyPhase::Countdown ||
            phase_ == LobbyPhase::MatchEnd) {
            send_lobby_state(*created, now_ms);
        }
    }
    ++stats_.logins;
    NSYNC_LOG_INFO(
        "net-server",
        "peer=",
        peer,
        " login slot=",
        static_cast<int>(session.slot),
        " token=",
        token,
        " name=",
        session.name,
        " phase=",
        static_cast<int>(phase_),
        " tick=",
        response.server_tick,
        " server_lag=",
        response.timing.server_lag_ticks,
        " client_event_lag=",
        response.timing.client_event_lag_ticks,
        " sync_period=",
        response.timing.sync_period_ticks);
}

void RealServer::handle_input(
    Session& session,
    const Packet& packet,
    std::uint32_t now_ms) {
    if (!session.input_rate.allow(now_ms)) {
        ++stats_.rate_limited_inputs;
        return;
    }
    if (world_.world().match_over()) {
        ++stats_.rejected_post_match_inputs;
        return;
    }
    if (!session.connected || !session.in_match ||
        phase_ != LobbyPhase::Match) {
        ++stats_.rejected_inputs;
        ++stats_.rejected_phase_inputs;
        return;
    }
    InputMessage input;
    std::string error;
    if (!decode_input(
            packet.payload.data(), packet.payload.size(), input, &error)) {
        ++stats_.rejected_inputs;
        ++stats_.rejected_malformed_inputs;
        return;
    }
    constexpr std::uint32_t kAcceptedButtons =
        0x00FFU |
        static_cast<std::uint32_t>(core::kButtonGadget2) |
        static_cast<std::uint32_t>(core::kButtonGadget3);
    if (input.slot != session.slot ||
        (static_cast<std::uint32_t>(input.buttons) & ~kAcceptedButtons) != 0 ||
        (input.actions & 0x8000U) != 0) {
        ++stats_.rejected_inputs;
        ++stats_.rejected_slot_inputs;
        return;
    }
    if (input.actions != 0U) {
        const auto count_bits = [](std::uint16_t value) {
            std::uint64_t count = 0;
            while (value != 0U) {
                value = static_cast<std::uint16_t>(value & (value - 1U));
                ++count;
            }
            return count;
        };
        ++stats_.action_packets;
        stats_.action_events += count_bits(input.actions);
        stats_.action_toggle_events += count_bits(input.action_toggles);
    }
    const std::uint32_t server_tick = world_.world().tick();
    const bool replay_window =
        config_.late_input_replay && input.tick > 0U &&
        input.tick <= server_tick &&
        server_tick - input.tick <=
            config_.late_input_replay_window_ticks;
    if (!replay_window &&
        (input.tick + config_.server_lag_ticks + 2U < server_tick ||
         input.tick > server_tick + config_.max_input_ahead_ticks)) {
        ++stats_.rejected_inputs;
        if (input.tick > server_tick + config_.max_input_ahead_ticks) {
            ++stats_.rejected_future_inputs;
        } else {
            ++stats_.rejected_late_inputs;
        }
        return;
    }

    core::InputCommand command;
    command.buttons = input.buttons;
    command.aim_x = input.aim_x;
    command.aim_y = input.aim_y;
    command.sequence = input.sequence;
    bool action_late = false;
    bool action_replay = false;
    const bool use_action_input = config_.action_input;
    const auto enqueue_actions = [&](std::uint32_t target) {
        if (input.actions == 0U) {
            return;
        }
        if (!use_action_input ||
            (!action_replay && (action_late || target < server_tick))) {
            session.action_fallback_ticks.insert(target);
            return;
        }
        if (!session.action_queue.enqueue(
                target,
                input.sequence,
                input.actions,
                input.action_toggles,
                input.action_param)) {
            ++stats_.action_duplicate_packets;
        }
    };
    std::uint32_t target_tick = input.tick;
    if (target_tick < server_tick) {
        action_late = true;
        command.tick = target_tick;
        if (replay_window) {
            action_replay = true;
            enqueue_actions(target_tick);
            session.pending_late_inputs.push_back(command);
            session.pending_late_broadcasts.push_back(input);
            ++stats_.input_packets;
            session.last_seen_ms = now_ms;
            return;
        }
        NSYNC_LOG_DEBUG(
            "net-server",
            "late input slot=",
            static_cast<int>(session.slot),
            " target=",
            input.tick,
            " server_tick=",
            server_tick);
        target_tick = server_tick + 1U;
        ++stats_.late_inputs;
    }
    command.tick = target_tick;
    enqueue_actions(target_tick);
    session.pending_inputs[target_tick] = command;
    ++stats_.input_packets;
    session.last_seen_ms = now_ms;
    input.tick = target_tick;
    broadcast_input(session.slot, input, now_ms);
}

void RealServer::handle_ping(
    Session& session,
    const Packet& packet,
    std::uint32_t now_ms) {
    PingMessage ping;
    if (!decode_ping(
            packet.payload.data(), packet.payload.size(), ping, nullptr)) {
        return;
    }
    PongMessage pong;
    pong.client_time_ms = ping.client_time_ms;
    pong.server_time_ms = server_time(now_ms);
    pong.sequence = ping.sequence;
    std::vector<std::uint8_t> payload;
    if (encode_pong(pong, payload)) {
        send_packet(session, MessageType::PingResponse, std::move(payload), 0);
    }
}

void RealServer::handle_snapshot_ack(
    Session& session,
    const Packet& packet,
    std::uint32_t now_ms) {
    SnapshotAck ack;
    if (!decode_snapshot_ack(
            packet.payload.data(), packet.payload.size(), ack, nullptr)) {
        return;
    }
    session.last_ack_tick = ack.tick;
    session.last_ack_hash = ack.client_hash;
    std::uint64_t authoritative_hash = 0;
    bool found = false;
    for (const SnapshotRecord& record : snapshot_history_) {
        if (record.tick == ack.tick) {
            authoritative_hash = record.hash;
            found = true;
            break;
        }
    }
    if (!found) {
        return;
    }
    if (ack.client_hash == authoritative_hash) {
        return;
    }
    if (ack.tick < rewritten_ack_ignore_before_tick_) {
        ++stats_.rewritten_ack_mismatches;
        return;
    }
    ++stats_.ack_mismatches;
    if (session.last_sync_ms == 0 ||
        now_ms - session.last_sync_ms >= 500U) {
        session.last_sync_ms = now_ms;
        send_sync_response(session, now_ms, ack.tick);
    }
}

void RealServer::handle_sync_request(
    Session& session,
    const Packet& packet,
    std::uint32_t now_ms) {
    SyncRequest request;
    if (!decode_sync_request(
            packet.payload.data(), packet.payload.size(), request, nullptr)) {
        return;
    }
    session.last_ack_tick = request.tick;
    session.last_ack_hash = request.client_hash;
    if (session.last_sync_ms == 0 ||
        now_ms - session.last_sync_ms >= 250U) {
        session.last_sync_ms = now_ms;
        send_sync_response(session, now_ms, request.tick);
    }
}

void RealServer::handle_disconnect(Session& session, const Packet& packet) {
    (void)packet;
    if (session.connected) {
        session.connected = false;
        session.ready = false;
        session.disconnect_deadline_ms =
            last_update_ms_ + config_.reconnect_window_ms;
        ++stats_.disconnects;
        transport_.remove_peer(session.peer);
        if (config_.lobby_enabled && phase_ == LobbyPhase::Match) {
            refresh_bot_slots();
            ++stats_.bot_takeovers;
            broadcast_lobby_state(last_update_ms_);
        }
    }
}

void RealServer::handle_lobby_request(
    Session& session,
    const Packet& packet,
    std::uint32_t now_ms) {
    LobbyRequest request;
    std::string error;
    LobbyResponse response;
    if (!decode_lobby_request(
            packet.payload.data(), packet.payload.size(), request, &error)) {
        response.error = error;
        std::vector<std::uint8_t> payload;
        if (encode_lobby_response(response, payload)) {
            send_packet(
                session,
                MessageType::LobbyResponse,
                std::move(payload),
                0);
        }
        return;
    }

    response.action = static_cast<std::uint8_t>(request.action);
    switch (request.action) {
    case LobbyAction::Ready:
    case LobbyAction::Start:
        if (phase_ == LobbyPhase::Match) {
            response.error = "match already running";
        } else {
            session.ready = true;
            if (request.hero < core::content().hero_count()) {
                session.hero = request.hero;
            }
            response.accepted = 1;
        }
        break;
    case LobbyAction::Unready:
        session.ready = false;
        response.accepted = 1;
        break;
    case LobbyAction::SelectHero:
        if (request.hero < core::content().hero_count()) {
            session.hero = request.hero;
            response.accepted = 1;
        } else {
            response.error = "invalid hero";
        }
        break;
    case LobbyAction::Rematch:
        if (phase_ != LobbyPhase::Match) {
            session.rematch = true;
            session.ready = true;
            ++stats_.rematches;
            response.accepted = 1;
        } else {
            response.error = "match already running";
        }
        break;
    case LobbyAction::Leave:
        response.accepted = 1;
        {
            std::vector<std::uint8_t> payload;
            if (encode_lobby_response(response, payload)) {
                send_packet(
                    session,
                    MessageType::LobbyResponse,
                    std::move(payload),
                    0);
            }
        }
        handle_disconnect(session, packet);
        return;
    }

    std::vector<std::uint8_t> payload;
    if (encode_lobby_response(response, payload)) {
        send_packet(
            session,
            MessageType::LobbyResponse,
            std::move(payload),
            0);
    }
    broadcast_lobby_state(now_ms);
}

int RealServer::human_count() const {
    int count = 0;
    for (const auto& entry : sessions_) {
        if (entry.second.connected) {
            ++count;
        }
    }
    return count;
}

int RealServer::ready_count() const {
    int count = 0;
    for (const auto& entry : sessions_) {
        if (entry.second.connected && entry.second.ready) {
            ++count;
        }
    }
    return count;
}

bool RealServer::all_ready() const {
    const int humans = human_count();
    return humans >= static_cast<int>(config_.lobby_min_humans) &&
        humans > 0 && ready_count() == humans;
}

LobbyState RealServer::build_lobby_state(std::uint32_t now_ms) const {
    LobbyState state;
    state.phase = static_cast<std::uint8_t>(phase_);
    state.player_count = static_cast<std::uint8_t>(config_.player_count);
    state.human_count = static_cast<std::uint8_t>(human_count());
    state.ready_count = static_cast<std::uint8_t>(ready_count());
    state.min_humans = static_cast<std::uint8_t>(config_.lobby_min_humans);
    state.match_id = match_id_;
    state.map_id = map_.id;
    state.server_tick = world_.world().tick();
    state.server_time_ms = server_time(now_ms);
    if (phase_ == LobbyPhase::Countdown && countdown_deadline_ms_ > now_ms) {
        state.countdown_ms = countdown_deadline_ms_ - now_ms;
    } else if (phase_ == LobbyPhase::MatchEnd &&
               match_end_deadline_ms_ > now_ms) {
        state.countdown_ms = match_end_deadline_ms_ - now_ms;
    }
    if (phase_ == LobbyPhase::Lobby) {
        state.status = "waiting for ready";
    } else if (phase_ == LobbyPhase::Countdown) {
        state.status = "match starting";
    } else if (phase_ == LobbyPhase::Match) {
        state.status = "in match";
    } else {
        state.status = "match over";
    }
    for (const auto& entry : sessions_) {
        const Session& session = entry.second;
        LobbyMember member;
        member.slot = session.slot;
        member.team = session.team;
        member.hero = session.hero;
        member.ready = session.ready ? 1U : 0U;
        member.connected = session.connected ? 1U : 0U;
        member.name = session.name;
        state.members.push_back(std::move(member));
    }
    std::sort(
        state.members.begin(),
        state.members.end(),
        [](const LobbyMember& a, const LobbyMember& b) {
            return a.slot < b.slot;
        });
    return state;
}

void RealServer::send_lobby_state(
    Session& session,
    std::uint32_t now_ms) {
    const LobbyState state = build_lobby_state(now_ms);
    std::vector<std::uint8_t> payload;
    if (encode_lobby_state(state, payload)) {
        send_packet(session, MessageType::LobbyState, std::move(payload), 0);
    }
}

void RealServer::broadcast_lobby_state(std::uint32_t now_ms) {
    last_lobby_broadcast_ms_ = now_ms;
    for (auto& entry : sessions_) {
        if (entry.second.connected) {
            send_lobby_state(entry.second, now_ms);
        }
    }
}

void RealServer::update_lobby(std::uint32_t now_ms) {
    if (!config_.lobby_enabled) {
        return;
    }
    if (phase_ == LobbyPhase::MatchEnd) {
        if (match_end_deadline_ms_ != 0 &&
            now_ms >= match_end_deadline_ms_) {
            return_to_lobby(now_ms);
        }
        return;
    }
    if (phase_ == LobbyPhase::Match) {
        return;
    }

    const bool ready = all_ready();
    if (ready) {
        if (phase_ == LobbyPhase::Lobby) {
            phase_ = LobbyPhase::Countdown;
            countdown_deadline_ms_ = now_ms + config_.lobby_countdown_ms;
            broadcast_lobby_state(now_ms);
        }
        if (phase_ == LobbyPhase::Countdown &&
            now_ms >= countdown_deadline_ms_) {
            start_match(now_ms);
        }
    } else if (phase_ == LobbyPhase::Countdown) {
        phase_ = LobbyPhase::Lobby;
        countdown_deadline_ms_ = 0;
        broadcast_lobby_state(now_ms);
    }

    if ((phase_ == LobbyPhase::Lobby ||
         phase_ == LobbyPhase::Countdown) &&
        now_ms - last_lobby_broadcast_ms_ >= 500U) {
        broadcast_lobby_state(now_ms);
    }
}

void RealServer::refresh_bot_slots() {
    std::uint16_t human_mask = 0;
    for (const auto& entry : sessions_) {
        const Session& session = entry.second;
        if (session.connected && session.slot < config_.player_count) {
            human_mask |= static_cast<std::uint16_t>(1U << session.slot);
        }
    }
    std::uint16_t bot_mask = 0;
    for (int slot = 0; slot < config_.player_count; ++slot) {
        if ((human_mask & (1U << slot)) == 0) {
            bot_mask |= static_cast<std::uint16_t>(1U << slot);
        }
    }
    world_.world().set_bot_slots(bot_mask);
}

void RealServer::start_match(std::uint32_t now_ms) {
    std::string error;
    ++match_id_;
    match_start_inputs_ = stats_.input_packets;
    match_start_snapshots_ = stats_.snapshots_sent;
    match_start_snapshot_bytes_ = stats_.snapshot_bytes;
    match_start_ack_mismatches_ = stats_.ack_mismatches;
    rewritten_ack_ignore_before_tick_ = 0;
    match_seed_ = config_.seed +
        static_cast<std::uint64_t>(match_id_) * 0x9E3779B97F4A7C15ULL;
    if (maps_.size() > 1 && stats_.matches_started > 0) {
        map_index_ = (map_index_ + 1) % maps_.size();
        map_ = maps_[map_index_];
        NSYNC_LOG_INFO(
            "net-server",
            "map rotation: next=",
            map_.id,
            " index=",
            map_index_);
    }
    if (!world_.start(
            map_,
            match_seed_,
            config_.player_count,
            config_.match_duration_ticks,
            config_.score_limit,
            &error)) {
        NSYNC_LOG_ERROR("net-server", "match start failed: ", error);
        phase_ = LobbyPhase::Lobby;
        countdown_deadline_ms_ = 0;
        broadcast_lobby_state(now_ms);
        return;
    }
    phase_ = LobbyPhase::Match;
    countdown_deadline_ms_ = 0;
    match_end_deadline_ms_ = 0;
    match_result_sent_ = false;
    next_tick_ms_ = static_cast<std::uint64_t>(now_ms) *
            static_cast<std::uint64_t>(config_.tick_rate);
    has_last_broadcast_ = false;
    last_broadcast_tick_ = 0;
    last_broadcast_hash_ = 0;
    snapshot_history_.clear();
    for (auto& entry : sessions_) {
        Session& session = entry.second;
        session.ready = false;
        session.rematch = false;
        session.in_match = true;
        session.last_ack_tick = 0;
        session.last_ack_hash = 0;
        session.last_sync_ms = 0;
        session.input_state = {};
        session.has_input_state = false;
        session.pending_inputs.clear();
        session.pending_late_inputs.clear();
        session.pending_late_broadcasts.clear();
        session.input_events.clear();
        session.input_history.clear();
        session.action_queue.clear();
        session.action_fallback_ticks.clear();
        session.action_buttons = 0;
        session.has_action_state = false;
    }
    refresh_bot_slots();
    for (auto& entry : sessions_) {
        const Session& session = entry.second;
        world_.world().players().for_each([&](core::Player& player) {
            if (player.slot != session.slot) {
                return;
            }
            const core::HeroStats& hero = core::hero_stats(
                static_cast<core::HeroType>(session.hero));
            player.hero = session.hero;
            player.health = core::Fix32::from_int(hero.health);
            player.shield = hero.shield;
            core::reset_player_gadgets(player);
            player.invulnerable_ticks = 0;
            player.heal_ticks = 0;
            player.heal_interval = 0;
            player.slow_ticks = 0;
            player.stun_ticks = 0;
            player.gadget_latch = 0;
            player.shield_regen_delay = 0;
            core::equip_hero_default_weapon(player);
        });
    }
    world_.world().players().for_each([](core::Player& player) {
        core::equip_hero_default_weapon(player);
    });
    SnapshotRecord initial_record;
    world_.world().save(initial_record.snapshot);
    initial_record.tick = initial_record.snapshot.tick;
    initial_record.hash = core::snapshot_hash(initial_record.snapshot);
    snapshot_history_.push_back(std::move(initial_record));
    for (auto& entry : sessions_) {
        if (entry.second.connected) {
            send_match_start(entry.second, now_ms);
        }
    }
    ++stats_.matches_started;
    NSYNC_LOG_INFO(
        "net-server",
        "match started id=",
        match_id_,
        " map=",
        map_.id,
        " seed=",
        match_seed_,
        " humans=",
        human_count(),
        " slots=",
        config_.player_count,
        " bots=",
        config_.player_count - human_count(),
        " server_lag=",
        config_.server_lag_ticks,
        " client_event_lag=",
        config_.client_event_lag_ticks,
        " sync_period=",
        config_.sync_period_ticks,
        " input_lead=",
        timing().normal_input_lead_ticks(),
        " recovery_lead=",
        timing().recovery_input_lead_ticks());
}

void RealServer::finish_match(std::uint32_t now_ms) {
    if (match_result_sent_) {
        return;
    }
    match_result_sent_ = true;
    phase_ = LobbyPhase::MatchEnd;
    match_end_deadline_ms_ = now_ms + config_.match_end_delay_ms;
    send_snapshot_to_all(now_ms);
    for (auto& entry : sessions_) {
        if (entry.second.connected) {
            send_match_end(entry.second, now_ms);
        }
    }
    ++stats_.matches_ended;
    broadcast_lobby_state(now_ms);
    const std::uint64_t match_snapshots =
        stats_.snapshots_sent - match_start_snapshots_;
    const std::uint64_t match_snapshot_bytes =
        stats_.snapshot_bytes - match_start_snapshot_bytes_;
    const std::uint64_t match_inputs =
        stats_.input_packets - match_start_inputs_;
    const std::uint64_t match_ack_mismatches =
        stats_.ack_mismatches - match_start_ack_mismatches_;
    NSYNC_LOG_INFO(
        "net-server",
        "match ended id=",
        match_id_,
        " winner=",
        static_cast<int>(world_.world().winner()),
        " score=",
        world_.world().scores()[0],
        ":",
        world_.world().scores()[1],
        " ticks=",
        world_.world().tick(),
        " inputs=",
        match_inputs,
        " snapshots=",
        match_snapshots,
        " snapshot_avg=",
        match_snapshots > 0
            ? match_snapshot_bytes / match_snapshots
            : 0,
        " ack_mismatches=",
        match_ack_mismatches);
}

void RealServer::return_to_lobby(std::uint32_t now_ms) {
    phase_ = LobbyPhase::Lobby;
    match_end_deadline_ms_ = 0;
    countdown_deadline_ms_ = 0;
    match_result_sent_ = false;
    for (auto& entry : sessions_) {
        Session& session = entry.second;
        session.in_match = false;
        session.ready = session.rematch;
        session.rematch = false;
        session.input_state = {};
        session.has_input_state = false;
        session.pending_inputs.clear();
        session.pending_late_inputs.clear();
        session.pending_late_broadcasts.clear();
        session.input_events.clear();
        session.input_history.clear();
        session.action_queue.clear();
        session.action_fallback_ticks.clear();
        session.action_buttons = 0;
        session.has_action_state = false;
    }
    broadcast_lobby_state(now_ms);
}

void RealServer::send_match_start(
    Session& session,
    std::uint32_t now_ms) {
    const core::WorldSnapshot state = snapshot();
    MatchStart start;
    start.match_id = match_id_;
    start.server_tick = state.tick;
    start.server_time_ms = server_time(now_ms);
    start.hash = core::snapshot_hash(state);
    start.map_fingerprint = core::map_fingerprint(map_);
    start.map_id = map_.id;
    start.seed = match_seed_;
    start.tick_rate = static_cast<std::uint16_t>(config_.tick_rate);
    start.snapshot_interval = static_cast<std::uint16_t>(
        config_.snapshot_interval);
    start.timing = timing();
    start.sync_time_to_step_ms = config_.sync_time_to_step_ms;
    start.player_count = static_cast<std::uint8_t>(config_.player_count);
    start.local_slot = session.slot;
    start.snapshot = core::encode_snapshot(state);
    std::vector<std::uint8_t> payload;
    if (encode_match_start(start, payload)) {
        send_packet(session, MessageType::MatchStart, std::move(payload), 0);
    }
}

void RealServer::send_match_end(
    Session& session,
    std::uint32_t now_ms) {
    MatchEnd end;
    end.match_id = match_id_;
    end.server_tick = world_.world().tick();
    end.rematch_delay_ms = config_.match_end_delay_ms;
    end.score0 = world_.world().scores()[0];
    end.score1 = world_.world().scores()[1];
    end.winner = world_.world().winner();
    end.player_count = static_cast<std::uint8_t>(config_.player_count);
    std::vector<std::uint8_t> payload;
    if (encode_match_end(end, payload)) {
        send_packet(session, MessageType::MatchEnd, std::move(payload), 0);
    }
    (void)now_ms;
}

void RealServer::simulate_tick(std::uint32_t now_ms) {
    (void)now_ms;
    if (world_.world().match_over()) {
        for (auto& entry : sessions_) {
            entry.second.pending_late_inputs.clear();
            entry.second.pending_late_broadcasts.clear();
            entry.second.action_queue.clear();
            entry.second.action_fallback_ticks.clear();
            entry.second.action_buttons = 0;
            entry.second.has_action_state = false;
        }
    }
    for (auto& entry : sessions_) {
        Session& session = entry.second;
        if (session.pending_late_inputs.empty()) {
            continue;
        }
        std::vector<core::InputCommand> commands =
            std::move(session.pending_late_inputs);
        session.pending_late_inputs.clear();
        const std::uint64_t count = commands.size();
        const bool replayed = replay_late_inputs(session, commands);
        if (replayed) {
            stats_.late_replays += count;
            ++stats_.late_replay_batches;
            for (const InputMessage& late : session.pending_late_broadcasts) {
                broadcast_input(session.slot, late, now_ms);
            }
        } else {
            stats_.late_replay_failures += count;
        }
        session.pending_late_broadcasts.clear();
    }
    const std::uint32_t tick = world_.world().tick();
    const bool use_action_input = config_.action_input;
    for (auto& entry : sessions_) {
        Session& session = entry.second;
        if (!session.connected || !session.in_match) {
            continue;
        }
        const auto found = session.pending_inputs.find(tick);
        if (found != session.pending_inputs.end()) {
            session.input_state = found->second;
            session.input_state.tick = tick;
            session.has_input_state = true;
            session.input_events[tick] = session.input_state;
            session.pending_inputs.erase(found);
        }
        ActionEventFrame action_frame;
        const bool has_action_frame =
            session.action_queue.consume(tick, action_frame);
        const bool action_fallback =
            session.action_fallback_ticks.erase(tick) > 0U;
        std::uint16_t action_pulse = 0;
        if (session.has_input_state && use_action_input) {
            const std::uint16_t held_mask = held_action_button_mask();
            if (!session.has_action_state) {
                session.has_action_state = true;
                session.action_buttons = static_cast<std::uint16_t>(
                    session.input_state.buttons & held_mask);
            } else {
                session.action_buttons = synchronize_held_action_buttons(
                    session.input_state.buttons,
                    session.action_buttons,
                    held_mask);
            }
        }
        if (has_action_frame) {
            ++stats_.action_event_frames;
            stats_.action_events_applied +=
                count_mask_bits(action_frame.actions);
            if (session.has_input_state && use_action_input) {
                std::uint16_t expected_mask = 0;
                std::uint16_t expected_buttons = 0;
                const auto toggle_mask = [](ClientAction action) {
                    return client_action_bit(action);
                };
                const auto apply_toggle =
                    [&](ClientAction action, std::uint16_t button) {
                        const ClientActionMask mask = toggle_mask(action);
                        if ((action_frame.actions & mask) == 0U) {
                            return;
                        }
                        expected_mask |= button;
                        if ((action_frame.toggles & mask) != 0U) {
                            expected_buttons |= button;
                            session.action_buttons |= button;
                        } else {
                            session.action_buttons &=
                                static_cast<std::uint16_t>(~button);
                        }
                    };
                apply_toggle(
                    ClientAction::GoLeft,
                    static_cast<std::uint16_t>(core::kButtonLeft));
                apply_toggle(
                    ClientAction::GoRight,
                    static_cast<std::uint16_t>(core::kButtonRight));
                apply_toggle(
                    ClientAction::GoUp,
                    static_cast<std::uint16_t>(core::kButtonJump));
                apply_toggle(
                    ClientAction::GoDown,
                    static_cast<std::uint16_t>(core::kButtonDown));
                apply_toggle(
                    ClientAction::WeaponFire,
                    static_cast<std::uint16_t>(core::kButtonFire));
                if ((action_frame.actions &
                     client_action_bit(ClientAction::WeaponReload)) != 0U) {
                    expected_mask |=
                        static_cast<std::uint16_t>(core::kButtonReload);
                    expected_buttons |=
                        static_cast<std::uint16_t>(core::kButtonReload);
                    action_pulse |=
                        static_cast<std::uint16_t>(core::kButtonReload);
                }
                if ((action_frame.actions &
                     client_action_bit(ClientAction::ActivateGadget)) != 0U) {
                    core::Button gadget = core::kButtonGadget1;
                    if (action_frame.param == 1U) {
                        gadget = core::kButtonGadget2;
                    } else if (action_frame.param == 2U) {
                        gadget = core::kButtonGadget3;
                    }
                    expected_mask |= static_cast<std::uint16_t>(gadget);
                    expected_buttons |= static_cast<std::uint16_t>(gadget);
                    action_pulse |= static_cast<std::uint16_t>(gadget);
                }
                if (((session.input_state.buttons ^ expected_buttons) &
                     expected_mask) != 0U) {
                    ++stats_.action_mismatches;
                    NSYNC_LOG_DEBUG(
                        "net-server",
                        "action mismatch slot=",
                        static_cast<int>(session.slot),
                        " tick=",
                        tick,
                        " state=",
                        session.input_state.buttons,
                        " expected=",
                        expected_buttons,
                        " mask=",
                        expected_mask);
                }
            }
        }
        if (session.has_input_state) {
            const std::uint32_t raw_buttons = session.input_state.buttons;
            if (action_fallback || !use_action_input) {
                ++stats_.action_fallback_inputs;
            } else if (session.has_action_state) {
                session.input_state.buttons = static_cast<std::uint16_t>(
                    (raw_buttons & (0xFF00U | core::kButtonWeaponSwitch)) |
                    session.action_buttons |
                    action_pulse);
                ++stats_.action_driven_inputs;
            }
        }
        if (session.has_input_state) {
            session.input_state.tick = tick;
            world_.submit_input(session.slot, session.input_state);
            session.input_history[tick] = session.input_state;
        }
        const std::uint32_t cutoff = tick > 240U ? tick - 240U : 0U;
        while (!session.input_events.empty() &&
               session.input_events.begin()->first < cutoff) {
            session.input_events.erase(session.input_events.begin());
        }
        while (!session.input_history.empty() &&
               session.input_history.begin()->first < cutoff) {
            session.input_history.erase(session.input_history.begin());
        }
        session.action_queue.prune_before(cutoff);
        while (!session.action_fallback_ticks.empty() &&
               *session.action_fallback_ticks.begin() < cutoff) {
            session.action_fallback_ticks.erase(
                session.action_fallback_ticks.begin());
        }
    }
    world_.step();
}

bool RealServer::replay_late_inputs(
    Session& session,
    const std::vector<core::InputCommand>& commands) {
    if (!config_.late_input_replay || commands.empty()) {
        return false;
    }
    const std::uint32_t current_tick = world_.world().tick();
    std::uint32_t min_target_tick = current_tick;
    for (const core::InputCommand& command : commands) {
        const std::uint32_t target_tick = command.tick;
        if (target_tick == 0U || target_tick > current_tick ||
            current_tick - target_tick >
                config_.late_input_replay_window_ticks) {
            ++stats_.late_replay_failure_window;
            return false;
        }
        min_target_tick = std::min(min_target_tick, target_tick);
    }

    const SnapshotRecord* base = nullptr;
    for (auto it = snapshot_history_.rbegin();
         it != snapshot_history_.rend(); ++it) {
        if (it->tick <= min_target_tick - 1U) {
            base = &*it;
            break;
        }
    }
    if (base == nullptr ||
        current_tick - base->tick >
            config_.late_input_replay_window_ticks) {
        ++stats_.late_replay_failure_base;
        return false;
    }

    std::vector<core::InputCommand> effective_commands = commands;
    std::map<std::uint32_t, ActionEventFrame> action_frames;
    for (core::InputCommand& command : effective_commands) {
        const auto found = action_frames.find(command.tick);
        if (found == action_frames.end()) {
            ActionEventFrame frame;
            if (session.action_queue.consume(command.tick, frame)) {
                ++stats_.action_event_frames;
                stats_.action_events_applied +=
                    count_mask_bits(frame.actions);
                action_frames[command.tick] = frame;
            }
        }
        const auto frame = action_frames.find(command.tick);
        if (config_.action_input && frame != action_frames.end()) {
            command.buttons = apply_action_frame(
                command.buttons,
                frame->second);
            ++stats_.late_action_driven_inputs;
        } else {
            ++stats_.late_action_fallback_inputs;
        }
    }

    core::WorldSnapshot current_snapshot;
    world_.world().save(current_snapshot);

    std::map<std::uint32_t, std::pair<bool, core::InputCommand>> old_events;
    for (const core::InputCommand& command : effective_commands) {
        const auto existing = session.input_events.find(command.tick);
        if (old_events.find(command.tick) == old_events.end()) {
            old_events[command.tick] = {
                existing != session.input_events.end(),
                existing != session.input_events.end()
                    ? existing->second
                    : core::InputCommand{}};
        }
        session.input_events[command.tick] = command;
    }

    std::map<std::uint64_t, std::map<std::uint32_t, core::InputCommand>>
        old_histories;
    std::map<std::uint64_t, std::pair<core::InputCommand, bool>>
        old_states;
    for (auto& entry : sessions_) {
        old_histories[entry.first] = entry.second.input_history;
        old_states[entry.first] = {
            entry.second.input_state,
            entry.second.has_input_state};
    }

    for (auto& entry : sessions_) {
        Session& replay_session = entry.second;
        core::InputCommand state{};
        bool has_state = false;
        const auto base_state = replay_session.input_history.find(base->tick);
        if (base_state != replay_session.input_history.end()) {
            state = base_state->second;
            has_state = true;
        }
        for (std::uint32_t tick = base->tick + 1U;
             tick <= current_tick; ++tick) {
            const auto event = replay_session.input_events.find(tick);
            if (event != replay_session.input_events.end()) {
                state = event->second;
                has_state = true;
            }
            if (has_state) {
                state.tick = tick;
                replay_session.input_history[tick] = state;
            } else {
                replay_session.input_history.erase(tick);
            }
        }
        replay_session.input_state = state;
        replay_session.input_state.tick =
            has_state ? current_tick : 0U;
        replay_session.has_input_state = has_state;
    }

    const auto restore_events = [&]() {
        for (const auto& entry : old_events) {
            if (entry.second.first) {
                session.input_events[entry.first] = entry.second.second;
            } else {
                session.input_events.erase(entry.first);
            }
        }
        for (auto& entry : sessions_) {
            entry.second.input_history = old_histories[entry.first];
            entry.second.input_state = old_states[entry.first].first;
            entry.second.has_input_state = old_states[entry.first].second;
        }
    };

    if (!world_.world().restore(base->snapshot, nullptr)) {
        ++stats_.late_replay_failure_restore;
        restore_events();
        (void)world_.world().restore(current_snapshot, nullptr);
        return false;
    }

    std::deque<SnapshotRecord> corrected_history;
    for (std::uint32_t tick = base->tick + 1U;
         tick <= current_tick; ++tick) {
        for (auto& entry : sessions_) {
            Session& replay_session = entry.second;
            const auto input = replay_session.input_history.find(tick);
            if (input == replay_session.input_history.end()) {
                continue;
            }
            core::InputCommand replay_command = input->second;
            replay_command.tick = tick;
            world_.submit_input(replay_session.slot, replay_command);
        }
        world_.step();
        if ((world_.world().tick() % config_.snapshot_interval) == 0U) {
            SnapshotRecord record;
            world_.world().save(record.snapshot);
            record.tick = record.snapshot.tick;
            record.hash = core::snapshot_hash(record.snapshot);
            corrected_history.push_back(std::move(record));
        }
    }

    if (world_.world().tick() != current_tick) {
        ++stats_.late_replay_failure_tick;
        restore_events();
        (void)world_.world().restore(current_snapshot, nullptr);
        return false;
    }

    while (!snapshot_history_.empty() &&
           snapshot_history_.back().tick > base->tick) {
        snapshot_history_.pop_back();
    }
    for (SnapshotRecord& record : corrected_history) {
        snapshot_history_.push_back(std::move(record));
    }
    if (snapshot_history_.empty() ||
        snapshot_history_.back().tick != current_tick) {
        SnapshotRecord corrected;
        world_.world().save(corrected.snapshot);
        corrected.tick = corrected.snapshot.tick;
        corrected.hash = core::snapshot_hash(corrected.snapshot);
        snapshot_history_.push_back(std::move(corrected));
    }
    const std::size_t history_limit = std::max<std::size_t>(
        120U,
        static_cast<std::size_t>(
            config_.late_input_replay_window_ticks) * 2U);
    while (snapshot_history_.size() > history_limit) {
        snapshot_history_.pop_front();
    }
    rewritten_ack_ignore_before_tick_ = current_tick + 1U;
    return true;
}
std::size_t RealServer::snapshot_history_limit() const {
    return std::max<std::size_t>(
        120,
        std::max<std::size_t>(
            static_cast<std::size_t>(config_.server_lag_ticks) + 64U,
            static_cast<std::size_t>(
                config_.late_input_replay_window_ticks) * 2U));
}

void RealServer::record_snapshot() {
    const core::WorldSnapshot state = snapshot();
    const std::uint64_t hash = core::snapshot_hash(state);
    shared::set_last_snapshot(state.tick, hash);
    snapshot_history_.push_back(SnapshotRecord{state.tick, hash, state});
    while (snapshot_history_.size() > snapshot_history_limit()) {
        snapshot_history_.pop_front();
    }
}

void RealServer::send_snapshot_to_all(std::uint32_t now_ms) {
    if (transport_.stats().waitsnd > 128U) {
        return;
    }
    bool has_client = false;
    for (const auto& entry : sessions_) {
        if (entry.second.connected) {
            has_client = true;
            break;
        }
    }
    if (!has_client) {
        return;
    }

    const auto started = std::chrono::steady_clock::now();
    const core::WorldSnapshot state = snapshot();
    const std::vector<std::uint8_t> snapshot_payload =
        core::encode_snapshot_payload(state);
    const std::uint64_t hash =
        core::snapshot_hash_payload(snapshot_payload.data(), snapshot_payload.size());
    shared::set_last_snapshot(state.tick, hash);

    snapshot_history_.push_back(SnapshotRecord{state.tick, hash, state});
    const std::size_t history_limit = std::max<std::size_t>(
        120,
        std::max<std::size_t>(
            static_cast<std::size_t>(config_.server_lag_ticks) + 64U,
            static_cast<std::size_t>(
                config_.late_input_replay_window_ticks) * 2U));
    while (snapshot_history_.size() > history_limit) {
        snapshot_history_.pop_front();
    }

    SnapshotMessage message;
    message.tick = state.tick;
    message.server_time_ms = server_time(now_ms);
    message.hash = hash;
    core::ByteWriter snapshot_writer;
    snapshot_writer.reserve(4 + snapshot_payload.size());
    snapshot_writer.u32(static_cast<std::uint32_t>(snapshot_payload.size()));
    snapshot_writer.bytes(
        snapshot_payload.data(),
        snapshot_payload.size());
    message.snapshot = snapshot_writer.take();

    std::vector<std::uint8_t> payload;
    if (!encode_snapshot(message, payload)) {
        return;
    }
    const std::uint64_t generation_us =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started)
                .count());
    stats_.snapshot_generation_us_total += generation_us;
    stats_.snapshot_generation_us_max = std::max(
        stats_.snapshot_generation_us_max,
        static_cast<std::uint32_t>(std::min<std::uint64_t>(
            generation_us,
            std::numeric_limits<std::uint32_t>::max())));

    for (auto& entry : sessions_) {
        if (!entry.second.connected) {
            continue;
        }
        if (send_packet_ref(entry.second, MessageType::Snapshot, payload, 0)) {
            ++stats_.snapshots_sent;
            stats_.snapshot_bytes += message.snapshot.size();
        }
    }
    last_broadcast_tick_ = state.tick;
    last_broadcast_hash_ = hash;
    has_last_broadcast_ = true;
    ++stats_.snapshot_broadcasts;

    const std::uint64_t elapsed_us =
        static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started)
                .count());
    stats_.snapshot_broadcast_us_total += elapsed_us;
    stats_.snapshot_broadcast_us_max = std::max(
        stats_.snapshot_broadcast_us_max,
        static_cast<std::uint32_t>(std::min<std::uint64_t>(
            elapsed_us,
            std::numeric_limits<std::uint32_t>::max())));
}

void RealServer::send_sync_response(
    Session& session,
    std::uint32_t now_ms,
    std::uint32_t requested_tick) {
    core::WorldSnapshot state;
    std::uint64_t hash = 0;
    bool found = false;
    if (requested_tick != 0) {
        for (const SnapshotRecord& record : snapshot_history_) {
            if (record.tick == requested_tick) {
                state = record.snapshot;
                hash = record.hash;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        state = snapshot();
        hash = core::snapshot_hash(state);
    }
    SyncResponse response;
    response.tick = state.tick;
    response.server_time_ms = server_time(now_ms);
    response.sync_time_to_step_ms = config_.sync_time_to_step_ms;
    response.hash = hash;
    response.snapshot = core::encode_snapshot(state);
    std::vector<std::uint8_t> payload;
    if (!encode_sync_response(response, payload)) {
        return;
    }
    if (send_packet(
            session, MessageType::SyncResponse, std::move(payload), 0)) {
        ++stats_.sync_responses;
    }
}

void RealServer::broadcast_input(
    std::uint8_t slot,
    const InputMessage& input,
    std::uint32_t now_ms) {
    (void)now_ms;
    InputMessage broadcast = input;
    broadcast.slot = slot;
    std::vector<std::uint8_t> payload;
    if (!encode_input(broadcast, payload)) {
        return;
    }
    for (auto& entry : sessions_) {
        if (entry.second.connected) {
            send_packet_ref(
                entry.second,
                MessageType::InputBroadcast,
                payload,
                0);
        }
    }
}

bool RealServer::send_packet(
    Session& session,
    MessageType type,
    std::vector<std::uint8_t> payload,
    std::uint32_t sequence) {
    return send_packet_ref(session, type, payload, sequence);
}

bool RealServer::send_packet_ref(
    Session& session,
    MessageType type,
    const std::vector<std::uint8_t>& payload,
    std::uint32_t sequence) {
    if (payload.size() > kMaxPacketPayload) {
        NSYNC_LOG_WARN("net-server", "packet payload too large");
        return false;
    }
    core::ByteWriter writer;
    writer.reserve(kPacketHeaderSize + payload.size());
    writer.u32(kProtocolMagic);
    writer.u16(kProtocolVersion);
    writer.u16(static_cast<std::uint16_t>(type));
    writer.u32(session.token);
    writer.u32(sequence == 0 ? sequence_++ : sequence);
    writer.u32(static_cast<std::uint32_t>(payload.size()));
    if (!payload.empty()) {
        writer.bytes(payload.data(), payload.size());
    }
    std::vector<std::uint8_t> bytes = writer.take();
    std::string error;
    if (!transport_.send(session.peer, bytes, &error)) {
        NSYNC_LOG_DEBUG("net-server", "send failed: ", error);
        return false;
    }
    return true;
}

void RealServer::expire_sessions(std::uint32_t now_ms) {
    std::vector<std::uint64_t> remove;
    for (auto& entry : sessions_) {
        Session& session = entry.second;
        if (session.connected &&
            now_ms - session.last_seen_ms > config_.session_timeout_ms) {
            session.connected = false;
            session.ready = false;
            session.disconnect_deadline_ms =
                now_ms + config_.reconnect_window_ms;
            ++stats_.disconnects;
            transport_.remove_peer(session.peer);
            if (config_.lobby_enabled) {
                if (phase_ == LobbyPhase::Match) {
                    ++stats_.bot_takeovers;
                }
                refresh_bot_slots();
                broadcast_lobby_state(now_ms);
            }
            NSYNC_LOG_INFO(
                "net-server",
                "session timeout slot=",
                static_cast<int>(session.slot));
        }
        if (!session.connected && session.disconnect_deadline_ms != 0 &&
            now_ms >= session.disconnect_deadline_ms) {
            remove.push_back(entry.first);
        }
    }
    for (std::uint64_t peer : remove) {
        const auto found = sessions_.find(peer);
        if (found != sessions_.end()) {
            const std::uint8_t slot = found->second.slot;
            if (slot < core::kMaxPlayers) {
                slot_tokens_[static_cast<std::size_t>(slot)] = 0;
            }
            sessions_.erase(found);
        }
    }
    if (!remove.empty() && config_.lobby_enabled) {
        refresh_bot_slots();
        broadcast_lobby_state(now_ms);
    }
}

} // namespace nsync::net




