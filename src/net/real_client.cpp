#include "net/real_client.h"

#include "core/snapshot.h"
#include "shared/crash_handler.h"
#include "shared/log.h"
#include "shared/paths.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <random>
#include <thread>
#include <utility>

namespace nsync::net {
namespace {

std::uint32_t current_time_ms() {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return static_cast<std::uint32_t>(
        duration_cast<milliseconds>(steady_clock::now() - start).count());
}

std::uint32_t clamp_delay(std::uint32_t value) {
    return std::clamp<std::uint32_t>(value, 2U, 60U);
}

std::uint32_t clamp_lead(std::uint32_t value) {
    return std::clamp<std::uint32_t>(value, 0U, 240U);
}

} // namespace

std::uint32_t effective_server_lag_ticks(
    std::uint16_t server_lag_ticks,
    std::int16_t sync_time_to_step_ms,
    std::uint16_t tick_rate) {
    const std::int64_t rate = std::clamp<std::int64_t>(tick_rate, 1, 240);
    const std::int64_t sync_ticks =
        static_cast<std::int64_t>(sync_time_to_step_ms) * rate / 1000;
    const std::int64_t lag = static_cast<std::int64_t>(server_lag_ticks) -
        sync_ticks;
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(lag, 0, 60));
}

std::uint32_t rtt_compensation_ticks(
    std::uint32_t rtt_ms,
    std::uint16_t tick_rate) {
    const std::int64_t rate = std::clamp<std::int64_t>(tick_rate, 1, 240);
    const std::int64_t ticks =
        static_cast<std::int64_t>(rtt_ms) * rate / 2000;
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(ticks, 0, 60));
}

RealClient::RealClient() = default;

std::uint32_t RealClient::original_frames_to_ticks(std::uint32_t frames) const {
    // serverLag / clientEventLag of the original are BattleMaster frames of
    // PhysicsConsts.FRAME_TIME_MILLS = 33ms (30 FPS). Online PvP uses
    // clientEventLag = 3 frames = ~99ms of input lead; 3 ticks of our 60Hz
    // simulation are only 50ms, so any short hitch of a client made its
    // inputs late for the server and the other players saw it pop back.
    // Fallback (no server timing) values are already in ticks.
    if (!timing_valid_ || server_tick_rate_ <= 30U) {
        return frames;
    }
    return clamp_lead(static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(frames) * server_tick_rate_ + 29U) / 30U));
}

RealClient::~RealClient() {
    stop();
}

bool RealClient::start(
    const std::string& host,
    std::uint16_t port,
    const core::Map& map,
    const NetClientConfig& config,
    std::uint32_t now_ms,
    std::string* error) {
    stop();
    if (host.empty() || port == 0) {
        if (error) {
            *error = "invalid server endpoint";
        }
        return false;
    }
    host_ = host;
    port_ = port;
    map_ = map;
    config_ = config;
    timing_ = {};
    timing_valid_ = false;
    normal_lead_ticks_ = clamp_delay(config_.input_delay_ticks);
    recovery_lead_ticks_ = normal_lead_ticks_;
    input_lead_ticks_ = normal_lead_ticks_;
    recovery_lead_pending_ = false;
    has_sent_input_ = false;
    sync_time_to_step_ms_ = 0;
    server_tick_rate_ = 60;
    alignment_complete_ = true;
    alignment_wait_ticks_ = 0;
    highest_server_tick_ = 0;
    future_snapshots_.clear();
    last_sync_tick_ = 0;
    last_snapshot_tick_ = 0;
    last_snapshot_server_time_ms_ = 0;
    last_snapshot_receive_ms_ = 0;
    started_ms_ = now_ms;
    connect_started_ms_ = now_ms;
    last_receive_ms_ = now_ms;
    state_ = NetClientState::Connecting;
    last_error_.clear();
    lobby_ = {};
    match_end_ = {};
    match_id_ = 0;
    selected_hero_ = config_.desired_hero;
    in_match_ = false;
    ready_ = false;
    auto_ready_sent_ = false;
    match_end_received_ = false;
    static std::atomic<std::uint32_t> next_conv{0};
    std::random_device random;
    conv_ = (static_cast<std::uint32_t>(config_.seed) ^
             static_cast<std::uint32_t>(port * 2654435761U) ^
             random() ^ next_conv.fetch_add(1)) |
        1U;
    transport_.set_latency_ms(config_.simulated_rtt_ms / 2U);
    transport_.set_network_simulation(
        config_.simulated_jitter_ms,
        config_.simulated_loss_percent,
        config_.simulated_duplicate_percent,
        config_.simulated_burst_length);
    transport_.set_latency_spikes(
        config_.simulated_spike_ms,
        config_.simulated_spike_interval_ms,
        config_.simulated_spike_duration_ms);
    transport_.set_network_trace(config_.network_trace);

    if (!open_transport(now_ms, error)) {
        state_ = NetClientState::Failed;
        return false;
    }
    send_login(now_ms);
    NSYNC_LOG_INFO(
        "net-client",
        "connecting to ",
        host_,
        ":",
        port_,
        " conv=",
        conv_);
    return true;
}

bool RealClient::load_map_by_id(
    const std::string& map_id,
    std::uint64_t expected_fingerprint,
    std::string* error) {
    if (map_id.empty()) {
        if (error) {
            *error = "server did not provide a map id";
        }
        return false;
    }
    if (map_id.size() > kMaxMapIdLength) {
        if (error) {
            *error = "server map id is too long";
        }
        return false;
    }
    if (map_id.find_first_of("/\\") != std::string::npos ||
        map_id == "." || map_id == "..") {
        if (error) {
            *error = "invalid server map id: " + map_id;
        }
        return false;
    }
    if (map_id == map_.id &&
        (expected_fingerprint == 0 ||
         core::map_fingerprint(map_) == expected_fingerprint)) {
        return true;
    }

    std::string errors;
    for (const char* extension : {".json", ".bin"}) {
        const std::string relative =
            "maps/" + map_id + extension;
        std::filesystem::path path;
        if (const auto found =
                shared::paths::find_file("assets/" + relative)) {
            path = *found;
        } else if (const auto found =
                       shared::paths::find_file(relative)) {
            path = *found;
        } else {
            std::error_code fs_error;
            if (std::filesystem::is_regular_file(relative, fs_error)) {
                path = relative;
            }
        }
        if (path.empty()) {
            continue;
        }

        core::Map loaded;
        std::string load_error;
        if (!core::load_map_file(path, loaded, &load_error)) {
            if (!errors.empty()) {
                errors += "; ";
            }
            errors += path.string() + ": " + load_error;
            continue;
        }
        if (loaded.id != map_id) {
            if (!errors.empty()) {
                errors += "; ";
            }
            errors += path.string() + ": id mismatch (" + loaded.id + ")";
            continue;
        }
        if (expected_fingerprint != 0 &&
            core::map_fingerprint(loaded) != expected_fingerprint) {
            if (!errors.empty()) {
                errors += "; ";
            }
            errors += path.string() + ": fingerprint mismatch";
            continue;
        }

        map_ = std::move(loaded);
        ++map_revision_;
        NSYNC_LOG_INFO(
            "net-client",
            "loaded server map: ",
            map_id,
            " path=",
            path.string());
        if (error) {
            error->clear();
        }
        return true;
    }

    if (error) {
        *error = errors.empty()
            ? "server map not available locally: " + map_id
            : "server map " + map_id + " is unavailable: " + errors;
    }
    return false;
}

bool RealClient::open_transport(
    std::uint32_t now_ms,
    std::string* error) {
    if (!transport_.open(host_, port_, conv_, error)) {
        return false;
    }
    transport_.update(now_ms);
    return true;
}

void RealClient::stop() {
    transport_.close();
    state_ = NetClientState::Disconnected;
    local_started_ = false;
    session_token_ = 0;
    slot_ = 0;
    in_match_ = false;
    ready_ = false;
    auto_ready_sent_ = false;
    match_end_received_ = false;
    lobby_ = {};
    match_end_ = {};
    match_id_ = 0;
    clock_.reset();
    timing_ = {};
    timing_valid_ = false;
    recovery_lead_pending_ = false;
    has_sent_input_ = false;
    alignment_complete_ = true;
    alignment_wait_ticks_ = 0;
    future_snapshots_.clear();
    last_sync_tick_ = 0;
    last_snapshot_tick_ = 0;
    last_snapshot_server_time_ms_ = 0;
    stats_ = {};
}

bool RealClient::wait_connected(
    std::uint32_t timeout_ms,
    std::string* error) {
    const std::uint32_t started = current_time_ms();
    while (state_ != NetClientState::Connected) {
        const std::uint32_t now = current_time_ms();
        if (state_ == NetClientState::Failed) {
            if (error) {
                *error = last_error_;
            }
            return false;
        }
        pump(now);
        if (state_ == NetClientState::Connecting) {
            if (now - last_login_send_ms_ >= 300U) {
                send_login(now);
            }
            if (now - connect_started_ms_ > config_.connect_timeout_ms) {
                fail("connection timeout");
            }
        }
        if (now - started > timeout_ms) {
            if (error) {
                *error = "connection wait timeout";
            }
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

void RealClient::tick(const InputProvider& provider, std::uint32_t now_ms) {
    if (state_ == NetClientState::Disconnected ||
        state_ == NetClientState::Failed) {
        return;
    }

    pump(now_ms);

    if (state_ == NetClientState::Connecting) {
        if (now_ms - last_login_send_ms_ >= 300U) {
            send_login(now_ms);
        }
        if (now_ms - connect_started_ms_ > config_.connect_timeout_ms) {
            if (config_.auto_reconnect) {
                begin_reconnect(now_ms);
            } else {
                fail("connection timeout");
            }
        }
        return;
    }

    if (state_ == NetClientState::Reconnecting) {
        if (now_ms - last_reconnect_ms_ >= config_.reconnect_interval_ms) {
            last_reconnect_ms_ = now_ms;
            conv_ = (conv_ + 2U) | 1U;
            std::string error;
            if (transport_.open(host_, port_, conv_, &error)) {
                transport_.update(now_ms);
                connect_started_ms_ = now_ms;
                send_login(now_ms);
            }
        }
        if (now_ms - connect_started_ms_ > config_.session_timeout_ms) {
            last_reconnect_ms_ = now_ms;
        }
        return;
    }

    if (state_ != NetClientState::Connected) {
        return;
    }
    if (now_ms - last_receive_ms_ > config_.session_timeout_ms) {
        if (config_.auto_reconnect) {
            begin_reconnect(now_ms);
        } else {
            fail("server timeout");
        }
        return;
    }

    send_heartbeat(now_ms);
    if (in_match_) {
        send_sync(now_ms);
    }

    if (in_match_ && local_started_) {
        if (!alignment_complete_) {
            update_alignment(now_ms);
            if (!alignment_complete_) {
                update_sync_stats();
                transport_.update(now_ms);
                return;
            }
        }

        catch_up_lag(now_ms);
        flush_due_snapshots(now_ms);

        const std::uint32_t configured_lead = recovery_lead_pending_
            ? original_frames_to_ticks(recovery_lead_ticks_)
            : original_frames_to_ticks(normal_lead_ticks_);
        const std::uint32_t measured_rtt_ms = clock_.rtt_ms() != 0
            ? clock_.rtt_ms()
            : transport_.stats().rtt_ms;
        const std::uint32_t network_slack =
            rtt_compensation_ticks(measured_rtt_ms, server_tick_rate_) + 4U;
        const std::uint32_t step_lead = config_.client_step_lag
            ? configured_lead
            : std::max(
                  configured_lead,
                  std::min<std::uint32_t>(network_slack, 60U));
        local_.set_input_delay(step_lead);
        input_lead_ticks_ = step_lead;
        recovery_lead_pending_ = false;

        int scheduled_steps = 1;
        if (config_.fixed_step_scheduler && timing_valid_) {
            const std::uint32_t lag = config_.client_step_lag
                ? effective_server_lag_ticks(
                      timing_.server_lag_ticks,
                      sync_time_to_step_ms_,
                      server_tick_rate_)
                : 0U;
            const std::uint32_t observed_server_tick =
                estimated_server_tick(now_ms);
            const std::uint32_t target =
                observed_server_tick > lag ? observed_server_tick - lag : 0U;
            const std::uint32_t local_tick = local_.world().tick();
            stats_.estimated_server_tick = observed_server_tick;
            stats_.scheduler_target_tick = target;
            if (target > local_tick) {
                const std::uint32_t deficit = target - local_tick;
                scheduled_steps = 1;
                stats_.scheduler_debt_ticks = deficit - 1U;
            } else {
                scheduled_steps = 0;
                ++stats_.scheduler_skips;
            }
        } else if (timing_valid_) {
            const std::uint32_t local_tick = local_.world().tick();
            const std::uint32_t observed_server_tick =
                estimated_server_tick(now_ms);
            stats_.estimated_server_tick = observed_server_tick;
            if (observed_server_tick > local_tick + 2U) {
                const std::uint32_t debt =
                    observed_server_tick - local_tick;
                scheduled_steps += static_cast<int>(
                    std::min<std::uint32_t>(debt - 2U, 1U));
            }
        }

        std::string error;
        for (int step = 0; step < scheduled_steps; ++step) {
            InputPacket output;
            if (!local_.step(provider, output, &error)) {
                fail(error);
                return;
            }
            if (config_.fixed_step_scheduler) {
                ++stats_.scheduler_steps;
            }
            send_input(output, now_ms);
        }
        catch_up_lag(now_ms);
        flush_due_snapshots(now_ms);

        const std::uint32_t ack_interval =
            std::max<std::uint32_t>(1U, config_.ack_interval_ticks);
        if (local_.world().tick() >= last_ack_tick_ + ack_interval) {
            last_ack_tick_ = local_.world().tick();
            send_ack(now_ms);
        }

        update_sync_stats();
    } else if (!in_match_ && !match_end_received_ &&
               lobby_.phase == static_cast<std::uint8_t>(LobbyPhase::Lobby) &&
               config_.auto_ready && !auto_ready_sent_) {
        set_ready(true);
    } else if (!in_match_ && match_end_received_ &&
               lobby_.phase == static_cast<std::uint8_t>(LobbyPhase::MatchEnd) &&
               config_.auto_rematch && !auto_rematch_sent_) {
        auto_rematch_sent_ = true;
        request_rematch();
    }
    if (!in_match_) {
        stats_.phase = static_cast<std::uint8_t>(lobby_.phase);
    }
    stats_.lobby_countdown_ms = lobby_.countdown_ms;
    stats_.lobby_players = lobby_.human_count;
    stats_.lobby_ready = lobby_.ready_count;
    transport_.update(now_ms);
}

void RealClient::pump(std::uint32_t now_ms) {
    if (!transport_.is_open()) {
        return;
    }
    std::vector<std::vector<std::uint8_t>> messages;
    if (!transport_.poll(messages)) {
        fail("transport receive failed");
        return;
    }
    for (const std::vector<std::uint8_t>& message : messages) {
        handle_packet(message, now_ms);
        if (state_ == NetClientState::Failed) {
            return;
        }
    }
    if (local_started_) {
        local_.flush_pending_rollbacks();
    }
    transport_.update(now_ms);
}

void RealClient::send_login(std::uint32_t now_ms) {
    if (!transport_.is_open()) {
        return;
    }
    login_started_ms_ = now_ms;
    LoginRequest request;
    request.protocol_version = kProtocolVersion;
    request.map_fingerprint = core::map_fingerprint(map_);
    request.content_fingerprint = core::content().fingerprint();
    request.reconnect_token = session_token_;
    request.desired_slot = session_token_ != 0 ? slot_ : 0xFFU;
    request.desired_hero = selected_hero_;
    request.desired_team = config_.desired_team;
    request.input_delay = static_cast<std::uint16_t>(input_lead_ticks_);
    request.display_name = config_.display_name;
    request.login_token = config_.login_token;
    request.map_id = map_.id;

    std::vector<std::uint8_t> payload;
    std::vector<std::uint8_t> bytes;
    std::string error;
    if (!encode_login_request(request, payload) ||
        !encode_packet(
            Packet{MessageType::LoginRequest, 0, 0, std::move(payload)},
            bytes,
            &error)) {
        fail("failed to encode login");
        return;
    }
    if (!transport_.send(bytes)) {
        fail("failed to send login");
        return;
    }
    last_login_send_ms_ = now_ms;
}

void RealClient::handle_packet(
    const std::vector<std::uint8_t>& data,
    std::uint32_t now_ms) {
    Packet packet;
    std::string error;
    if (!decode_packet(data.data(), data.size(), packet, &error)) {
        NSYNC_LOG_DEBUG("net-client", "invalid packet: ", error);
        return;
    }
    if (packet.type == MessageType::LoginResponse) {
        handle_login_response(packet, now_ms);
        return;
    }
    if (state_ != NetClientState::Connected &&
        state_ != NetClientState::Reconnecting) {
        return;
    }
    if (packet.session_token != session_token_) {
        return;
    }
    last_receive_ms_ = now_ms;

    switch (packet.type) {
    case MessageType::LobbyState:
        handle_lobby_state(packet);
        break;
    case MessageType::MatchStart:
        handle_match_start(packet, now_ms);
        break;
    case MessageType::MatchEnd:
        handle_match_end(packet);
        break;
    case MessageType::LobbyResponse: {
        LobbyResponse response;
        if (decode_lobby_response(
                packet.payload.data(),
                packet.payload.size(),
                response,
                nullptr) &&
            response.accepted == 0 &&
            !response.error.empty()) {
            NSYNC_LOG_DEBUG("net-client", "lobby request rejected: ", response.error);
        }
        break;
    }
    case MessageType::InputBroadcast: {
        if (!in_match_) {
            break;
        }
        InputMessage input;
        if (!decode_input(
                packet.payload.data(),
                packet.payload.size(),
                input,
                nullptr)) {
            return;
        }
        InputPacket packet_input;
        packet_input.slot = input.slot;
        packet_input.command.tick = input.tick;
        packet_input.command.buttons = input.buttons;
        packet_input.command.aim_x = input.aim_x;
        packet_input.command.aim_y = input.aim_y;
        packet_input.command.sequence = input.sequence;
        local_.queue_input_broadcast(packet_input);
        break;
    }
    case MessageType::Snapshot: {
        if (!in_match_) {
            break;
        }
        SnapshotMessage snapshot;
        if (!decode_snapshot_message(
                packet.payload.data(),
                packet.payload.size(),
                snapshot,
                nullptr)) {
            return;
        }
        accept_snapshot(
            snapshot.tick,
            snapshot.server_time_ms,
            snapshot.hash,
            snapshot.snapshot,
            false,
            now_ms);
        break;
    }
    case MessageType::SyncResponse: {
        if (!in_match_) {
            break;
        }
        SyncResponse snapshot;
        if (!decode_sync_response(
                packet.payload.data(),
                packet.payload.size(),
                snapshot,
                nullptr)) {
            return;
        }
        sync_time_to_step_ms_ = snapshot.sync_time_to_step_ms;
        stats_.sync_time_to_step_ms = sync_time_to_step_ms_;
        const std::uint32_t mismatches_before =
            local_.stats().snapshot_mismatches;
        accept_snapshot(
            snapshot.tick,
            snapshot.server_time_ms,
            snapshot.hash,
            snapshot.snapshot,
            true,
            now_ms);
        // BattleMaster.handleSync: frameOnDesync (the long recovery lead)
        // is reset when "No desync, snapshots are equal"; only a real
        // desync restore uses serverLag + clientEventLag*1.5 + 1.
        if (local_.stats().snapshot_mismatches != mismatches_before) {
            recovery_lead_pending_ = true;
        }
        break;
    }
    case MessageType::PingResponse: {
        PongMessage pong;
        if (!decode_pong(
                packet.payload.data(),
                packet.payload.size(),
                pong,
                nullptr)) {
            return;
        }
        clock_.add_sample(pong.client_time_ms, pong.server_time_ms, now_ms);
        stats_.rtt_ms = clock_.rtt_ms();
        stats_.clock_offset_ms = clock_.offset_ms();
        break;
    }
    case MessageType::Disconnect:
        fail("server disconnected the session");
        break;
    case MessageType::LoginRequest:
    case MessageType::LoginResponse:
    case MessageType::PingRequest:
    case MessageType::Input:
    case MessageType::SnapshotAck:
    case MessageType::SyncRequest:
    case MessageType::LobbyRequest:
        break;
    default:
        break;
    }
}

bool RealClient::handle_login_response(
    const Packet& packet,
    std::uint32_t now_ms) {
    LoginResponse response;
    std::string error;
    if (!decode_login_response(
            packet.payload.data(),
            packet.payload.size(),
            response,
            &error)) {
        fail("invalid login response: " + error);
        return false;
    }
    if (response.protocol_version != kProtocolVersion) {
        fail("protocol version mismatch");
        return false;
    }
    if (response.accepted == 0) {
        if (response.error == "map fingerprint mismatch" &&
            !response.map_id.empty() &&
            response.map_id != map_.id) {
            std::string map_error;
            if (!load_map_by_id(
                    response.map_id,
                    response.map_fingerprint,
                    &map_error)) {
                fail(map_error);
                return false;
            }
            send_login(now_ms);
            return true;
        }
        fail(response.error.empty() ? "login rejected" : response.error);
        return false;
    }
    if (!response.map_id.empty() && response.map_id != map_.id) {
        std::string map_error;
        if (!load_map_by_id(
                response.map_id,
                response.map_fingerprint,
                &map_error)) {
            fail(map_error);
            return false;
        }
    }
    if (response.map_fingerprint != core::map_fingerprint(map_)) {
        fail(
            "map fingerprint mismatch server=" +
            std::to_string(response.map_fingerprint) +
            " client=" + std::to_string(core::map_fingerprint(map_)));
        return false;
    }
    if (response.content_fingerprint != core::content().fingerprint()) {
        fail(
            "content fingerprint mismatch server=" +
            std::to_string(response.content_fingerprint) +
            " client=" + std::to_string(core::content().fingerprint()));
        return false;
    }
    if (response.snapshot.empty()) {
        fail("login response has no snapshot");
        return false;
    }

    core::WorldSnapshot snapshot;
    if (!core::decode_snapshot(response.snapshot, snapshot, &error)) {
        fail("invalid login snapshot: " + error);
        return false;
    }
    std::uint64_t login_hash = 0;
    if (!core::snapshot_hash_encoded(response.snapshot, login_hash) ||
        snapshot.tick != response.server_tick ||
        login_hash != response.snapshot_hash) {
        fail("login snapshot hash mismatch");
        return false;
    }
    shared::set_last_snapshot(snapshot.tick, login_hash);

    configure_timing(response.timing, response.input_delay);

    NetConfig local_config;
    local_config.latency_ticks = 0;
    local_config.jitter_ticks = 0;
    local_config.input_delay_ticks = input_lead_ticks_;
    local_config.snapshot_interval = 1;
    local_config.rollback_history = 240;
    local_config.seed = config_.seed;
    if (!local_.start(
            map_,
            snapshot,
            static_cast<std::uint8_t>(response.slot),
            local_config,
            &error)) {
        fail(error);
        return false;
    }

    session_token_ = response.session_token;
    slot_ = static_cast<std::uint8_t>(response.slot);
    local_started_ = true;
    has_sent_input_ = false;
    recovery_lead_pending_ = false;
    in_match_ = response.phase == static_cast<std::uint8_t>(LobbyPhase::Match);
    future_snapshots_.clear();
    last_sync_tick_ = snapshot.tick;
    highest_server_tick_ = snapshot.tick;
    alignment_start_tick_ = snapshot.tick;
    alignment_complete_ = !config_.client_step_lag || !in_match_ ||
        !timing_valid_ || timing_.server_lag_ticks == 0;
    alignment_wait_ticks_ = 0;
    match_id_ = response.match_id;
    ready_ = false;
    auto_ready_sent_ = false;
    auto_rematch_sent_ = false;
    match_end_received_ = false;
    state_ = NetClientState::Connected;
    last_receive_ms_ = now_ms;
    last_ping_ms_ = now_ms;
    last_sync_ms_ = now_ms;
    last_ack_tick_ = 0;
    clock_.reset();
    if (login_started_ms_ != 0 && now_ms >= login_started_ms_) {
        clock_.add_sample(
            login_started_ms_, response.server_time_ms, now_ms);
    }
    stats_ = {};
    stats_.slot = slot_;
    stats_.phase = response.phase;
    stats_.match_id = match_id_;
    stats_.server_tick = snapshot.tick;
    stats_.predicted_tick = snapshot.tick;
    stats_.ack_tick = snapshot.tick;
    stats_.server_lag_ticks = timing_.server_lag_ticks;
    stats_.client_event_lag_ticks = timing_.client_event_lag_ticks;
    stats_.sync_period_ticks = timing_.sync_period_ticks;
    stats_.input_lead_ticks = static_cast<std::uint16_t>(input_lead_ticks_);
    stats_.recovery_lead_ticks =
        static_cast<std::uint16_t>(recovery_lead_ticks_);
    server_tick_rate_ = response.tick_rate != 0 ? response.tick_rate : 60U;
    snapshot_interval_ticks_ = response.snapshot_interval != 0
        ? response.snapshot_interval
        : 3U;
    sync_time_to_step_ms_ = response.sync_time_to_step_ms;
    stats_.sync_time_to_step_ms = sync_time_to_step_ms_;
    stats_.effective_server_lag_ticks = effective_server_lag_ticks(
        timing_.server_lag_ticks,
        sync_time_to_step_ms_,
        server_tick_rate_);
    if (in_match_ && timing_valid_ && timing_.server_lag_ticks > 0) {
        begin_alignment(snapshot, now_ms);
    }
    last_error_.clear();
    NSYNC_LOG_INFO(
        "net-client",
        "login accepted slot=",
        static_cast<int>(slot_),
        " token=",
        session_token_,
        " phase=",
        static_cast<int>(response.phase),
        " match=",
        match_id_,
        " map=",
        map_.id,
        " tick=",
        snapshot.tick,
        " input_lead=",
        input_lead_ticks_,
        " server_lag=",
        timing_.server_lag_ticks,
        " client_event_lag=",
        timing_.client_event_lag_ticks,
        " sync_period=",
        timing_.sync_period_ticks,
        " timing=",
        timing_valid_ ? "server" : "fallback");
    return true;
}

bool RealClient::handle_lobby_state(const Packet& packet) {
    LobbyState state;
    std::string error;
    if (!decode_lobby_state(
            packet.payload.data(), packet.payload.size(), state, &error)) {
        NSYNC_LOG_DEBUG("net-client", "invalid lobby state: ", error);
        return false;
    }
    lobby_ = std::move(state);
    if (!in_match_) {
        stats_.phase = lobby_.phase;
        stats_.match_id = lobby_.match_id;
    }
    stats_.lobby_players = lobby_.human_count;
    stats_.lobby_ready = lobby_.ready_count;
    stats_.lobby_countdown_ms = lobby_.countdown_ms;
    for (const LobbyMember& member : lobby_.members) {
        if (member.slot == slot_) {
            ready_ = member.ready != 0;
            break;
        }
    }
    if (lobby_.phase == static_cast<std::uint8_t>(LobbyPhase::Lobby) &&
        !in_match_) {
        if (!ready_) {
            auto_ready_sent_ = false;
        }
        auto_rematch_sent_ = false;
    }
    if (lobby_.phase == static_cast<std::uint8_t>(LobbyPhase::MatchEnd) &&
        config_.auto_rematch && !auto_rematch_sent_) {
        auto_rematch_sent_ = true;
        request_rematch();
    }
    return true;
}

bool RealClient::handle_match_start(
    const Packet& packet,
    std::uint32_t now_ms) {
    MatchStart start;
    std::string error;
    if (!decode_match_start(
            packet.payload.data(), packet.payload.size(), start, &error)) {
        fail("invalid match start: " + error);
        return false;
    }
    if (!start.map_id.empty() && start.map_id != map_.id) {
        std::string map_error;
        if (!load_map_by_id(
                start.map_id,
                start.map_fingerprint,
                &map_error)) {
            fail(map_error);
            return false;
        }
    }
    if (start.map_fingerprint != core::map_fingerprint(map_)) {
        fail("match map fingerprint mismatch");
        return false;
    }
    if (start.local_slot != slot_) {
        fail("match slot mismatch");
        return false;
    }
    core::WorldSnapshot snapshot;
    if (!core::decode_snapshot(start.snapshot, snapshot, &error)) {
        fail("invalid match snapshot: " + error);
        return false;
    }
    std::uint64_t match_hash = 0;
    if (!core::snapshot_hash_encoded(start.snapshot, match_hash) ||
        snapshot.tick != start.server_tick ||
        match_hash != start.hash) {
        fail("match snapshot hash mismatch");
        return false;
    }
    shared::set_last_snapshot(snapshot.tick, match_hash);
    server_tick_rate_ = start.tick_rate != 0 ? start.tick_rate : 60U;
    snapshot_interval_ticks_ = start.snapshot_interval != 0
        ? start.snapshot_interval
        : 3U;
    sync_time_to_step_ms_ = start.sync_time_to_step_ms;
    stats_.sync_time_to_step_ms = sync_time_to_step_ms_;

    configure_timing(start.timing, input_lead_ticks_);

    NetConfig local_config;
    local_config.latency_ticks = 0;
    local_config.jitter_ticks = 0;
    local_config.input_delay_ticks = input_lead_ticks_;
    local_config.snapshot_interval = 1;
    local_config.rollback_history = 240;
    local_config.seed = start.seed;
    if (!local_.start(
            map_,
            snapshot,
            start.local_slot,
            local_config,
            &error)) {
        fail("match local start failed: " + error);
        return false;
    }

    slot_ = start.local_slot;
    match_id_ = start.match_id;
    in_match_ = true;
    ready_ = false;
    match_end_ = {};
    match_end_received_ = false;
    local_started_ = true;
    has_sent_input_ = false;
    recovery_lead_pending_ = false;
    future_snapshots_.clear();
    last_ack_tick_ = 0;
    last_sync_ms_ = now_ms;
    last_sync_tick_ = snapshot.tick;
    last_receive_ms_ = now_ms;
    highest_server_tick_ = snapshot.tick;
    last_snapshot_tick_ = snapshot.tick;
    last_snapshot_server_time_ms_ = start.server_time_ms;
    last_snapshot_receive_ms_ = now_ms;
    stats_.scheduler_target_tick = 0;
    stats_.scheduler_steps = 0;
    stats_.scheduler_skips = 0;
    stats_.scheduler_debt_ticks = 0;
    stats_.phase = static_cast<std::uint8_t>(LobbyPhase::Match);
    stats_.match_id = match_id_;
    stats_.slot = slot_;
    stats_.server_tick = snapshot.tick;
    stats_.predicted_tick = snapshot.tick;
    stats_.ack_tick = snapshot.tick;
    stats_.corrections = 0;
    stats_.rollbacks = 0;
    stats_.desyncs = 0;
    stats_.snapshot_mismatches = 0;
    stats_.snapshot_restores = 0;
    stats_.event_rollbacks = 0;
    stats_.total_rollback_ticks = 0;
    stats_.sync_requests = 0;
    stats_.input_packets = 0;
    stats_.snapshot_packets = 0;
    stats_.snapshot_bytes = 0;
    stats_.server_lag_ticks = timing_.server_lag_ticks;
    stats_.client_event_lag_ticks = timing_.client_event_lag_ticks;
    stats_.sync_period_ticks = timing_.sync_period_ticks;
    stats_.input_lead_ticks = static_cast<std::uint16_t>(input_lead_ticks_);
    stats_.recovery_lead_ticks =
        static_cast<std::uint16_t>(recovery_lead_ticks_);
    stats_.sync_time_to_step_ms = sync_time_to_step_ms_;
    stats_.effective_server_lag_ticks = effective_server_lag_ticks(
        timing_.server_lag_ticks,
        sync_time_to_step_ms_,
        server_tick_rate_);
    stats_.snapshots_queued = 0;
    stats_.alignment_wait_ticks = 0;
    stats_.winner = 0;
    stats_.score0 = 0;
    stats_.score1 = 0;
    begin_alignment(snapshot, now_ms);
    if (!config_.client_step_lag) {
        apply_initial_catch_up(start.server_time_ms, now_ms);
    }
    NSYNC_LOG_INFO(
        "net-client",
        "match started id=",
        match_id_,
        " slot=",
        static_cast<int>(slot_),
        " players=",
        static_cast<int>(start.player_count),
        " map=",
        map_.id,
        " tick=",
        snapshot.tick,
        " input_lead=",
        input_lead_ticks_,
        " server_lag=",
        timing_.server_lag_ticks,
        " client_event_lag=",
        timing_.client_event_lag_ticks,
        " sync_period=",
        timing_.sync_period_ticks,
        " timing=",
        timing_valid_ ? "server" : "fallback");
    return true;
}

bool RealClient::handle_match_end(const Packet& packet) {
    MatchEnd end;
    std::string error;
    if (!decode_match_end(
            packet.payload.data(), packet.payload.size(), end, &error)) {
        NSYNC_LOG_WARN("net-client", "invalid match end: ", error);
        return false;
    }
    match_end_ = end;
    match_id_ = end.match_id;
    in_match_ = false;
    alignment_complete_ = true;
    future_snapshots_.clear();
    stats_.snapshots_queued = 0;
    lobby_.phase = static_cast<std::uint8_t>(LobbyPhase::MatchEnd);
    match_end_received_ = true;
    auto_rematch_sent_ = false;
    stats_.phase = static_cast<std::uint8_t>(LobbyPhase::MatchEnd);
    stats_.match_id = end.match_id;
    stats_.winner = end.winner;
    stats_.score0 = end.score0;
    stats_.score1 = end.score1;
    NSYNC_LOG_INFO(
        "net-client",
        "match ended id=",
        end.match_id,
        " winner=",
        static_cast<int>(end.winner),
        " score=",
        end.score0,
        ":",
        end.score1);
    NSYNC_LOG_INFO(
        "net-client",
        "match summary corrections=",
        stats_.corrections,
        " rollbacks=",
        stats_.rollbacks,
        " event_rollbacks=",
        stats_.event_rollbacks,
        " snapshot_mismatches=",
        stats_.snapshot_mismatches,
        " snapshot_restores=",
        stats_.snapshot_restores,
        " rollback_ticks=",
        stats_.total_rollback_ticks,
        " desyncs=",
        stats_.desyncs,
        " inputs=",
        stats_.input_packets,
        " snapshots=",
        stats_.snapshot_packets,
        " snapshot_avg=",
        stats_.snapshot_packets > 0
            ? stats_.snapshot_bytes / stats_.snapshot_packets
            : 0);
    return true;
}

void RealClient::send_lobby_request(
    LobbyAction action,
    std::uint8_t hero,
    std::uint8_t team,
    bool ready) {
    if (!transport_.is_open() || state_ != NetClientState::Connected) {
        return;
    }
    LobbyRequest request;
    request.action = action;
    request.hero = hero;
    request.team = team;
    request.ready = ready ? 1U : 0U;
    std::vector<std::uint8_t> payload;
    if (!encode_lobby_request(request, payload)) {
        return;
    }
    Packet packet;
    packet.type = MessageType::LobbyRequest;
    packet.session_token = session_token_;
    packet.sequence = message_sequence_++;
    packet.payload = std::move(payload);
    std::vector<std::uint8_t> bytes;
    if (encode_packet(packet, bytes)) {
        transport_.send(bytes);
    }
}

void RealClient::set_ready(bool ready) {
    ready_ = ready;
    auto_ready_sent_ = true;
    send_lobby_request(
        ready ? LobbyAction::Ready : LobbyAction::Unready,
        selected_hero_,
        config_.desired_team,
        ready);
}

void RealClient::select_hero(std::uint8_t hero) {
    selected_hero_ = hero;
    send_lobby_request(
        LobbyAction::SelectHero,
        selected_hero_,
        config_.desired_team,
        ready_);
}

void RealClient::request_rematch() {
    send_lobby_request(
        LobbyAction::Rematch,
        selected_hero_,
        config_.desired_team,
        true);
}

void RealClient::apply_initial_catch_up(
    std::uint32_t anchor_server_time_ms,
    std::uint32_t now_ms) {
    if (!local_started_ || server_tick_rate_ == 0 ||
        anchor_server_time_ms == 0) {
        return;
    }
    const std::uint32_t server_now = clock_.local_to_server(now_ms);
    if (server_now <= anchor_server_time_ms) {
        return;
    }
    const std::uint32_t delta_ms = server_now - anchor_server_time_ms;
    std::uint32_t ticks = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(delta_ms) * server_tick_rate_ + 999) /
        1000);
    ticks = std::min(ticks + 1U, 60U);
    if (ticks == 0) {
        return;
    }
    local_.advance_idle(ticks);
    last_sync_tick_ = local_.world().tick();
    highest_server_tick_ =
        std::max(highest_server_tick_, local_.world().tick());
    alignment_start_tick_ = local_.world().tick();
    stats_.predicted_tick = local_.world().tick();
    NSYNC_LOG_INFO(
        "net-client",
        "initial catch-up ticks=",
        ticks,
        " local_tick=",
        local_.world().tick(),
        " anchor=",
        anchor_server_time_ms,
        " server_now=",
        server_now);
}

void RealClient::configure_timing(
    const SyncTiming& timing,
    std::uint32_t fallback_delay) {
    timing_ = timing;
    timing_valid_ = config_.use_server_timing && timing_.configured();
    if (timing_valid_) {
        normal_lead_ticks_ =
            clamp_lead(timing_.normal_input_lead_ticks());
        recovery_lead_ticks_ =
            clamp_lead(timing_.recovery_input_lead_ticks());
        input_lead_ticks_ = normal_lead_ticks_;
    } else {
        timing_ = {};
        normal_lead_ticks_ = clamp_delay(fallback_delay);
        recovery_lead_ticks_ = normal_lead_ticks_;
        input_lead_ticks_ = normal_lead_ticks_;
    }
}

void RealClient::begin_alignment(
    const core::WorldSnapshot& snapshot,
    std::uint32_t now_ms) {
    alignment_start_tick_ = snapshot.tick;
    alignment_target_tick_ = snapshot.tick;
    alignment_start_ms_ = now_ms;
    alignment_wait_ticks_ = 0;
    const std::uint32_t effective_lag = effective_server_lag_ticks(
        timing_.server_lag_ticks,
        sync_time_to_step_ms_,
        server_tick_rate_);
    const bool wait_for_server_lag =
        config_.client_step_lag && timing_valid_ && effective_lag > 0;
    if (wait_for_server_lag) {
        alignment_target_tick_ = snapshot.tick + effective_lag;
        alignment_complete_ = false;
    } else {
        alignment_complete_ = true;
    }
    stats_.alignment_wait_ticks = 0;
    NSYNC_LOG_INFO(
        "net-client",
        "input sync ",
        alignment_complete_ ? "aligned" : "waiting",
        " local_tick=",
        snapshot.tick,
        " target=",
        alignment_target_tick_,
        " normal_lead=",
        normal_lead_ticks_,
        " recovery_lead=",
        recovery_lead_ticks_,
        " server_lag=",
        timing_.server_lag_ticks,
        " effective_lag=",
        effective_lag);
}

void RealClient::update_alignment(std::uint32_t now_ms) {
    if (alignment_complete_) {
        return;
    }
    if (highest_server_tick_ > alignment_start_tick_) {
        alignment_wait_ticks_ =
            highest_server_tick_ - alignment_start_tick_;
    }
    stats_.alignment_wait_ticks = alignment_wait_ticks_;
    if (highest_server_tick_ >= alignment_target_tick_) {
        alignment_complete_ = true;
        catch_up_lag(now_ms);
        NSYNC_LOG_INFO(
            "net-client",
            "server lag alignment complete local_tick=",
            local_.world().tick(),
            " server_tick=",
            highest_server_tick_,
            " lag=",
            highest_server_tick_ - local_.world().tick(),
            " sync_ms=",
            static_cast<int>(sync_time_to_step_ms_));
        return;
    }
    const std::uint32_t effective_lag = effective_server_lag_ticks(
        timing_.server_lag_ticks,
        sync_time_to_step_ms_,
        server_tick_rate_);
    const std::uint32_t fallback_ms =
        effective_lag * 1000U / 60U + 250U;
    if (now_ms - alignment_start_ms_ >= fallback_ms) {
        alignment_complete_ = true;
        NSYNC_LOG_WARN(
            "net-client",
            "server lag alignment timed out local_tick=",
            local_.world().tick(),
            " observed_server_tick=",
            highest_server_tick_,
            " target=",
            alignment_target_tick_);
    }
}

void RealClient::update_sync_stats() {
    const ClientStats& local_stats = local_.stats();
    stats_.predicted_tick = local_stats.predicted_tick;
    stats_.server_tick = local_stats.server_tick;
    stats_.ack_tick = local_stats.ack_tick;
    stats_.corrections = local_stats.corrections;
    stats_.rollbacks = local_stats.rollbacks;
    stats_.desyncs = local_stats.desyncs;
    stats_.snapshot_mismatches = local_stats.snapshot_mismatches;
    stats_.snapshot_missing_predictions =
        local_stats.snapshot_missing_predictions;
    stats_.snapshot_periodic_mismatches =
        local_stats.snapshot_periodic_mismatches;
    stats_.snapshot_authoritative_mismatches =
        local_stats.snapshot_authoritative_mismatches;
    stats_.snapshot_restores = local_stats.snapshot_restores;
    stats_.remote_snapshot_corrections =
        local_stats.remote_snapshot_corrections;
    stats_.event_rollbacks = local_stats.event_rollbacks;
    stats_.covered_inputs = local_stats.covered_inputs;
    stats_.total_rollback_ticks = local_stats.total_rollback_ticks;
    stats_.rtt_ms = clock_.rtt_ms() != 0
        ? clock_.rtt_ms()
        : transport_.stats().rtt_ms;
    stats_.rtt_compensation_ticks = static_cast<std::uint16_t>(
        rtt_compensation_ticks(stats_.rtt_ms, server_tick_rate_));
    stats_.clock_offset_ms = clock_.offset_ms();
    stats_.server_lag_ticks = timing_.server_lag_ticks;
    stats_.effective_server_lag_ticks = effective_server_lag_ticks(
        timing_.server_lag_ticks,
        sync_time_to_step_ms_,
        server_tick_rate_);
    stats_.client_event_lag_ticks = timing_.client_event_lag_ticks;
    stats_.sync_period_ticks = timing_.sync_period_ticks;
    stats_.input_lead_ticks = static_cast<std::uint16_t>(input_lead_ticks_);
    stats_.recovery_lead_ticks =
        static_cast<std::uint16_t>(recovery_lead_ticks_);
    stats_.sync_time_to_step_ms = sync_time_to_step_ms_;
    stats_.snapshots_queued =
        static_cast<std::uint32_t>(future_snapshots_.size());
    stats_.alignment_wait_ticks = alignment_wait_ticks_;
}

bool RealClient::accept_snapshot(
    std::uint32_t tick,
    std::uint32_t server_time_ms,
    std::uint64_t hash,
    const std::vector<std::uint8_t>& bytes,
    bool authoritative,
    std::uint32_t now_ms) {
    if (!local_started_) {
        return false;
    }
    core::WorldSnapshot snapshot;
    std::string error;
    if (!core::decode_snapshot(bytes, snapshot, &error)) {
        NSYNC_LOG_WARN("net-client", "bad snapshot: ", error);
        return false;
    }
    std::uint64_t encoded_hash = 0;
    if (!core::snapshot_hash_encoded(bytes, encoded_hash)) {
        NSYNC_LOG_WARN("net-client", "invalid snapshot encoding at tick ", tick);
        return false;
    }
    if (snapshot.tick != tick || encoded_hash != hash) {
        NSYNC_LOG_WARN("net-client", "snapshot hash mismatch at tick ", tick);
        return false;
    }
    shared::set_last_snapshot(tick, hash);
    if (tick > highest_server_tick_) {
        highest_server_tick_ = tick;
    }
    if (server_time_ms != 0 && tick >= last_snapshot_tick_) {
        last_snapshot_tick_ = tick;
        last_snapshot_server_time_ms_ = server_time_ms;
        last_snapshot_receive_ms_ = now_ms;
    }
    ++stats_.snapshot_packets;
    stats_.snapshot_bytes += bytes.size();

    const std::uint32_t local_tick = local_.world().tick();
    if (timing_valid_ && tick > local_tick) {
        auto position = std::lower_bound(
            future_snapshots_.begin(),
            future_snapshots_.end(),
            tick,
            [](const QueuedSnapshot& queued, std::uint32_t value) {
                return queued.tick < value;
            });
        if (position != future_snapshots_.end() && position->tick == tick) {
            position->hash = hash;
            position->authoritative = authoritative;
            position->snapshot = std::move(snapshot);
            position->bytes = bytes;
        } else {
            QueuedSnapshot queued;
            queued.tick = tick;
            queued.server_time_ms = server_time_ms;
            queued.hash = hash;
            queued.authoritative = authoritative;
            queued.snapshot = std::move(snapshot);
            queued.bytes = bytes;
            future_snapshots_.insert(position, std::move(queued));
        }
        constexpr std::size_t kMaxQueuedSnapshots = 128;
        while (future_snapshots_.size() > kMaxQueuedSnapshots) {
            future_snapshots_.pop_front();
        }
        stats_.snapshots_queued =
            static_cast<std::uint32_t>(future_snapshots_.size());
        last_receive_ms_ = now_ms;
        return true;
    }
    return apply_decoded_snapshot(
        tick, hash, snapshot, bytes.size(), authoritative, now_ms);
}

bool RealClient::apply_decoded_snapshot(
    std::uint32_t tick,
    std::uint64_t hash,
    const core::WorldSnapshot& snapshot,
    std::size_t encoded_size,
    bool authoritative,
    std::uint32_t now_ms) {
    (void)encoded_size;
    const std::uint32_t local_tick_before = local_.world().tick();
    const std::uint32_t corrections_before = local_.stats().corrections;
    SnapshotPacket packet;
    packet.client_index = 0;
    packet.authoritative = authoritative;
    packet.hash = hash;
    packet.snapshot = snapshot;
    std::string error;
    if (!local_.apply_snapshot(packet, &error)) {
        NSYNC_LOG_WARN("net-client", "snapshot apply failed: ", error);
        return false;
    }
    if (local_.stats().corrections > corrections_before) {
        NSYNC_LOG_DEBUG(
            "net-client",
            "correction snapshot=",
            tick,
            " local=",
            local_tick_before,
            " predicted=",
            local_.stats().predicted_tick,
            " ack=",
            local_.stats().ack_tick);
    }
    last_receive_ms_ = now_ms;
    update_sync_stats();
    return true;
}

std::uint32_t RealClient::estimated_server_tick(
    std::uint32_t now_ms) const {
    const std::uint32_t rtt_ticks = rtt_compensation_ticks(
        clock_.rtt_ms() != 0 ? clock_.rtt_ms() : transport_.stats().rtt_ms,
        server_tick_rate_);
    if (last_snapshot_tick_ != 0 && last_snapshot_receive_ms_ != 0) {
        const std::uint32_t elapsed_ms =
            now_ms > last_snapshot_receive_ms_
            ? now_ms - last_snapshot_receive_ms_
            : 0U;
        const std::uint32_t elapsed_ticks =
            elapsed_ms * server_tick_rate_ / 1000;
        const std::uint32_t max_extrapolation =
            static_cast<std::uint32_t>(snapshot_interval_ticks_) +
            rtt_ticks + 2U;
        return std::max(
            highest_server_tick_,
            last_snapshot_tick_ +
                std::min(elapsed_ticks, max_extrapolation) + rtt_ticks);
    }
    return highest_server_tick_ + rtt_ticks;
}

void RealClient::catch_up_lag(std::uint32_t now_ms) {
    if (!timing_valid_ || !alignment_complete_) {
        return;
    }
    if (!config_.client_step_lag && !config_.fixed_step_scheduler) {
        return;
    }
    const std::uint32_t lag = config_.client_step_lag
        ? effective_server_lag_ticks(
              timing_.server_lag_ticks,
              sync_time_to_step_ms_,
              server_tick_rate_)
        : 0U;
    const std::uint32_t measured_rtt_ms = clock_.rtt_ms() != 0
        ? clock_.rtt_ms()
        : transport_.stats().rtt_ms;
    const std::uint32_t rtt_ticks =
        rtt_compensation_ticks(measured_rtt_ms, server_tick_rate_);
    stats_.rtt_compensation_ticks =
        static_cast<std::uint16_t>(rtt_ticks);
    const std::uint32_t observed_server_tick =
        estimated_server_tick(now_ms);
    const std::uint32_t target =
        observed_server_tick > lag ? observed_server_tick - lag : 0U;
    stats_.estimated_server_tick = observed_server_tick;
    if (config_.fixed_step_scheduler &&
        target <= local_.world().tick() + 4U) {
        return;
    }
    const std::uint32_t minimum_gap = config_.client_step_lag ? 0U : 2U;
    if (target <= local_.world().tick() + minimum_gap) {
        return;
    }

    QueuedSnapshot selected;
    bool found = false;
    for (const QueuedSnapshot& queued : future_snapshots_) {
        if (queued.tick > target) {
            break;
        }
        selected = queued;
        found = true;
    }
    if (!found || selected.tick > target ||
        selected.tick <= local_.world().tick()) {
        return;
    }

    const std::uint32_t before = local_.world().tick();
    if (!apply_decoded_snapshot(
            selected.tick,
            selected.hash,
            selected.snapshot,
            selected.bytes.size(),
            selected.authoritative,
            now_ms)) {
        return;
    }
    while (!future_snapshots_.empty() &&
           future_snapshots_.front().tick <= selected.tick) {
        future_snapshots_.pop_front();
    }
    stats_.snapshots_queued =
        static_cast<std::uint32_t>(future_snapshots_.size());
    update_sync_stats();
    NSYNC_LOG_DEBUG(
        "net-client",
        "lag catch-up local=",
        before,
        " snapshot=",
        selected.tick,
        " server_tick=",
        highest_server_tick_,
        " lag=",
        timing_.server_lag_ticks);
}

void RealClient::flush_due_snapshots(std::uint32_t now_ms) {
    if (!timing_valid_ || future_snapshots_.empty()) {
        return;
    }
    const std::uint32_t local_tick = local_.world().tick();
    bool have_due = false;
    QueuedSnapshot due;
    while (!future_snapshots_.empty() &&
           future_snapshots_.front().tick <= local_tick) {
        due = std::move(future_snapshots_.front());
        future_snapshots_.pop_front();
        have_due = true;
    }
    stats_.snapshots_queued =
        static_cast<std::uint32_t>(future_snapshots_.size());
    if (have_due) {
        apply_decoded_snapshot(
            due.tick,
            due.hash,
            due.snapshot,
            due.bytes.size(),
            due.authoritative,
            now_ms);
    }
}

void RealClient::send_input(const InputPacket& input, std::uint32_t now_ms) {
    (void)now_ms;
    if (!local_started_ || !in_match_ || !alignment_complete_) {
        return;
    }
    const bool unchanged =
        has_sent_input_ &&
        input.command.buttons == last_sent_input_.buttons &&
        input.command.aim_x == last_sent_input_.aim_x &&
        input.command.aim_y == last_sent_input_.aim_y;
    const std::uint32_t refresh_ticks =
        std::max<std::uint32_t>(1U, config_.input_refresh_ticks);
    // After the input lead shrinks (e.g. after a recovery lead) a target
    // tick may already hold an older command on the server; always resend
    // so the server's state for that tick is overwritten.
    const bool overwrites_sent =
        has_sent_input_ && input.command.tick <= highest_sent_input_tick_;
    const bool refresh_due =
        !has_sent_input_ || overwrites_sent ||
        input.command.tick < last_sent_input_.tick ||
        input.command.tick - last_sent_input_.tick >= refresh_ticks;
    if (unchanged && !refresh_due) {
        return;
    }
    const std::uint16_t previous_buttons =
        has_sent_input_ ? last_sent_input_.buttons : 0U;
    const std::uint16_t pressed =
        static_cast<std::uint16_t>(input.command.buttons & ~previous_buttons);
    const std::uint16_t released =
        static_cast<std::uint16_t>(~input.command.buttons & previous_buttons);
    ClientActionMask actions = 0;
    ClientActionMask action_toggles = 0;
    const auto capture_toggle = [&](core::Button button, ClientAction action) {
        const std::uint16_t mask = static_cast<std::uint16_t>(button);
        if ((pressed & mask) == 0U && (released & mask) == 0U) {
            return;
        }
        actions |= client_action_bit(action);
        if ((input.command.buttons & mask) != 0U) {
            action_toggles |= client_action_bit(action);
        }
    };
    capture_toggle(core::kButtonLeft, ClientAction::GoLeft);
    capture_toggle(core::kButtonRight, ClientAction::GoRight);
    capture_toggle(core::kButtonJump, ClientAction::GoUp);
    capture_toggle(core::kButtonDown, ClientAction::GoDown);
    capture_toggle(core::kButtonFire, ClientAction::WeaponFire);
    if ((pressed & core::kButtonReload) != 0U) {
        actions |= client_action_bit(ClientAction::WeaponReload);
    }
    std::uint8_t action_param = 0;
    if ((pressed & core::kButtonGadget1) != 0U) {
        actions |= client_action_bit(ClientAction::ActivateGadget);
        action_param = 0;
    }
    if ((pressed & core::kButtonGadget2) != 0U) {
        actions |= client_action_bit(ClientAction::ActivateGadget);
        action_param = 1;
    }
    if ((pressed & core::kButtonGadget3) != 0U) {
        actions |= client_action_bit(ClientAction::ActivateGadget);
        action_param = 2;
    }

    InputMessage message;
    message.slot = input.slot;
    message.sequence = input.command.sequence;
    message.buttons = input.command.buttons;
    message.aim_x = input.command.aim_x;
    message.aim_y = input.command.aim_y;
    message.tick = input.command.tick;
    message.actions = actions;
    message.action_toggles = action_toggles;
    message.action_param = action_param;

    std::vector<std::uint8_t> payload;
    if (!encode_input(message, payload)) {
        return;
    }
    Packet packet;
    packet.type = MessageType::Input;
    packet.session_token = session_token_;
    packet.sequence = message.tick;
    packet.payload = std::move(payload);
    std::vector<std::uint8_t> bytes;
    if (encode_packet(packet, bytes) && transport_.send(bytes)) {
        last_sent_input_ = input.command;
        highest_sent_input_tick_ = has_sent_input_
            ? std::max(highest_sent_input_tick_, input.command.tick)
            : input.command.tick;
        has_sent_input_ = true;
        ++stats_.input_packets;
    }
}
void RealClient::send_heartbeat(std::uint32_t now_ms) {
    if (now_ms - last_ping_ms_ < config_.ping_interval_ms) {
        return;
    }
    last_ping_ms_ = now_ms;
    PingMessage ping;
    ping.client_time_ms = now_ms;
    ping.sequence = ping_sequence_++;
    std::vector<std::uint8_t> payload;
    if (!encode_ping(ping, payload)) {
        return;
    }
    Packet packet;
    packet.type = MessageType::PingRequest;
    packet.session_token = session_token_;
    packet.sequence = ping.sequence;
    packet.payload = std::move(payload);
    std::vector<std::uint8_t> bytes;
    if (encode_packet(packet, bytes)) {
        transport_.send(bytes);
    }
}

void RealClient::send_sync(std::uint32_t now_ms) {
    if (!local_started_ || !in_match_ || !alignment_complete_) {
        return;
    }
    const std::uint32_t local_tick = local_.world().tick();
    if (timing_valid_ && timing_.sync_period_ticks != 0) {
        if (local_tick < last_sync_tick_ + timing_.sync_period_ticks) {
            return;
        }
    } else if (now_ms - last_sync_ms_ < config_.sync_interval_ms) {
        return;
    }
    const std::uint32_t server_lag = timing_valid_
        ? static_cast<std::uint32_t>(timing_.server_lag_ticks)
        : 8U;
    const std::uint32_t back = server_lag + 2U;
    const std::uint32_t period =
        timing_valid_ && timing_.sync_period_ticks != 0
        ? static_cast<std::uint32_t>(timing_.sync_period_ticks)
        : 30U;
    std::uint32_t sync_tick = 0;
    std::optional<std::uint64_t> client_hash;
    if (config_.client_step_lag) {
        sync_tick = local_.stats().ack_tick;
        if (sync_tick != 0) {
            client_hash = local_.stats().last_client_hash;
        }
    } else if (local_tick > back) {
        const std::uint32_t candidate = local_tick - back;
        sync_tick = candidate - candidate % period;
        client_hash = local_.predicted_hash_at(sync_tick);
    }
    if (sync_tick == 0 || !client_hash.has_value()) {
        return;
    }
    last_sync_ms_ = now_ms;
    last_sync_tick_ = local_tick;
    SyncRequest request;
    request.tick = sync_tick;
    request.client_hash = *client_hash;
    std::vector<std::uint8_t> payload;
    if (!encode_sync_request(request, payload)) {
        return;
    }
    Packet packet;
    packet.type = MessageType::SyncRequest;
    packet.session_token = session_token_;
    packet.sequence = request.tick;
    packet.payload = std::move(payload);
    std::vector<std::uint8_t> bytes;
    if (encode_packet(packet, bytes)) {
        if (transport_.send(bytes)) {
            ++stats_.sync_requests;
        }
    }
}

void RealClient::send_ack(std::uint32_t now_ms) {
    (void)now_ms;
    if (!local_started_ || !in_match_ || !alignment_complete_) {
        return;
    }
    SnapshotAck ack;
    ack.tick = local_.stats().ack_tick;
    ack.client_tick = local_.stats().predicted_tick;
    ack.client_hash = local_.stats().last_client_hash;
    std::vector<std::uint8_t> payload;
    if (!encode_snapshot_ack(ack, payload)) {
        return;
    }
    Packet packet;
    packet.type = MessageType::SnapshotAck;
    packet.session_token = session_token_;
    packet.sequence = ack.tick;
    packet.payload = std::move(payload);
    std::vector<std::uint8_t> bytes;
    if (encode_packet(packet, bytes)) {
        transport_.send(bytes);
    }
}

void RealClient::leave() {
    if (transport_.is_open() && session_token_ != 0) {
        DisconnectMessage disconnect;
        disconnect.reason = 1;
        std::vector<std::uint8_t> payload;
        if (encode_disconnect(disconnect, payload)) {
            Packet packet;
            packet.type = MessageType::Disconnect;
            packet.session_token = session_token_;
            packet.sequence = message_sequence_++;
            packet.payload = std::move(payload);
            std::vector<std::uint8_t> bytes;
            if (encode_packet(packet, bytes)) {
                transport_.send(bytes);
            }
        }
    }
    transport_.close();
    state_ = NetClientState::Disconnected;
    in_match_ = false;
}

bool RealClient::reconnect(std::uint32_t now_ms) {
    if (state_ == NetClientState::Failed || session_token_ == 0) {
        return false;
    }
    begin_reconnect(now_ms);
    return true;
}

void RealClient::begin_reconnect(std::uint32_t now_ms) {
    if (state_ == NetClientState::Reconnecting) {
        return;
    }
    state_ = NetClientState::Reconnecting;
    last_reconnect_ms_ = now_ms;
    connect_started_ms_ = now_ms;
    transport_.close();
    conv_ = (conv_ + 2U) | 1U;
    std::string error;
    if (transport_.open(host_, port_, conv_, &error)) {
        transport_.update(now_ms);
        send_login(now_ms);
    }
    NSYNC_LOG_INFO("net-client", "reconnecting with token=", session_token_);
}

void RealClient::fail(const std::string& error) {
    last_error_ = error;
    state_ = NetClientState::Failed;
    transport_.close();
    NSYNC_LOG_WARN("net-client", error);
}

const core::World& RealClient::world() const {
    return local_.world();
}

const ClientStats& RealClient::local_stats() const {
    return local_.stats();
}

TransportStats RealClient::transport_stats() const {
    return transport_.stats();
}

} // namespace nsync::net







