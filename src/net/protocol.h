#pragma once

#include "core/content.h"
#include "core/input_command.h"
#include "core/snapshot.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nsync::net {

inline constexpr std::uint32_t kProtocolMagic = 0x4F4E5244U; // "ONRD"
inline constexpr std::uint16_t kProtocolVersion = 13;
inline constexpr std::uint32_t kMaxPacketPayload = 128U * 1024U;
inline constexpr std::size_t kPacketHeaderSize = 20;
inline constexpr std::uint16_t kDefaultPort = 40100;
inline constexpr std::uint32_t kMaxSessionToken = 0xFFFFFFFFU;
inline constexpr std::size_t kMaxDisplayNameLength = 32;
inline constexpr std::size_t kMaxLoginTokenLength = 64;
inline constexpr std::size_t kMaxMapIdLength = 64;

enum class MessageType : std::uint16_t {
    LoginRequest = 151,
    LoginResponse = 1151,
    LobbyRequest = 152,
    LobbyResponse = 1152,
    LobbyState = 2108,
    MatchStart = 2109,
    MatchEnd = 2110,
    PingRequest = 153,
    PingResponse = 1153,
    Input = 104,
    InputBroadcast = 1104,
    Snapshot = 2113,
    SnapshotAck = 1106,
    SyncRequest = 105,
    SyncResponse = 1105,
    Disconnect = 109,
};

enum class ClientAction : std::uint8_t {
    GoLeft = 1,
    GoRight = 2,
    GoUp = 3,
    GoDown = 4,
    WeaponEnable = 5,
    WeaponFire = 6,
    WeaponReload = 7,
    ActivateGadget = 8,
    Exit = 9,
    DropFlag = 10,
    WeaponCharge = 11,
    Respawn = 12,
    WeaponRedirect = 13,
    Disconnect = 14,
    Reconnect = 15,
};

using ClientActionMask = std::uint16_t;

constexpr ClientActionMask client_action_bit(ClientAction action) {
    return static_cast<ClientActionMask>(
        1U << (static_cast<std::uint16_t>(action) - 1U));
}

constexpr bool client_action_toggle(ClientAction action) {
    return action == ClientAction::GoLeft ||
        action == ClientAction::GoRight ||
        action == ClientAction::GoUp ||
        action == ClientAction::GoDown ||
        action == ClientAction::WeaponFire;
}

enum class LobbyPhase : std::uint8_t {
    Lobby = 0,
    Countdown = 1,
    Match = 2,
    MatchEnd = 3,
};

enum class LobbyAction : std::uint8_t {
    Ready = 1,
    Unready = 2,
    SelectHero = 3,
    Rematch = 4,
    Leave = 5,
    Start = 6,
};

struct PacketHeader {
    std::uint32_t magic = kProtocolMagic;
    std::uint16_t version = kProtocolVersion;
    MessageType type = MessageType::LoginRequest;
    std::uint32_t session_token = 0;
    std::uint32_t sequence = 0;
    std::uint32_t payload_size = 0;
};

struct Packet {
    MessageType type = MessageType::LoginRequest;
    std::uint32_t session_token = 0;
    std::uint32_t sequence = 0;
    std::vector<std::uint8_t> payload;
};

struct SyncTiming {
    std::uint16_t server_lag_ticks = 0;
    std::uint16_t client_event_lag_ticks = 0;
    std::uint16_t sync_period_ticks = 0;

    [[nodiscard]] bool configured() const {
        return sync_period_ticks != 0;
    }

    [[nodiscard]] std::uint16_t normal_input_lead_ticks() const {
        if (!configured()) {
            return 0;
        }
        return client_event_lag_ticks;
    }

    [[nodiscard]] std::uint16_t recovery_input_lead_ticks() const {
        if (!configured()) {
            return 0;
        }
        return static_cast<std::uint16_t>(
            server_lag_ticks +
            client_event_lag_ticks +
            client_event_lag_ticks / 2U +
            1U);
    }

    [[nodiscard]] std::uint16_t input_lead_ticks() const {
        return recovery_input_lead_ticks();
    }
};

struct LoginRequest {
    std::uint32_t protocol_version = kProtocolVersion;
    std::uint64_t map_fingerprint = 0;
    std::uint64_t content_fingerprint = 0;
    std::uint32_t reconnect_token = 0;
    std::uint8_t desired_slot = 0xFFU;
    std::uint8_t desired_hero = 0;
    std::uint8_t desired_team = 0xFFU;
    std::uint8_t reserved = 0;
    std::uint16_t input_delay = 16;
    std::string display_name;
    std::string login_token;
    std::string map_id;
};

struct LoginResponse {
    std::uint8_t accepted = 0;
    std::uint8_t slot = 0;
    std::uint8_t player_count = 0;
    std::uint8_t phase = 0;
    std::uint32_t protocol_version = kProtocolVersion;
    std::uint32_t session_token = 0;
    std::uint32_t server_tick = 0;
    std::uint32_t server_time_ms = 0;
    std::uint32_t input_delay = 16;
    std::uint32_t match_id = 0;
    std::uint64_t map_fingerprint = 0;
    std::uint64_t content_fingerprint = 0;
    std::uint64_t snapshot_hash = 0;
    std::string map_id;
    std::uint16_t tick_rate = 60;
    std::uint16_t snapshot_interval = 3;
    SyncTiming timing{};
    std::int16_t sync_time_to_step_ms = 0;
    std::vector<std::uint8_t> snapshot;
    std::string error;
};

struct LobbyMember {
    std::uint8_t slot = 0;
    std::uint8_t team = 0;
    std::uint8_t hero = 0;
    std::uint8_t ready = 0;
    std::uint8_t connected = 0;
    std::uint8_t bot = 0;
    std::string name;
};

struct LobbyState {
    std::uint8_t phase = 0;
    std::uint8_t player_count = 0;
    std::uint8_t human_count = 0;
    std::uint8_t ready_count = 0;
    std::uint8_t min_humans = 1;
    std::uint32_t countdown_ms = 0;
    std::uint32_t match_id = 0;
    std::uint32_t server_tick = 0;
    std::uint32_t server_time_ms = 0;
    std::vector<LobbyMember> members;
    std::string status;
    std::string map_id;
};

struct LobbyRequest {
    LobbyAction action = LobbyAction::Ready;
    std::uint8_t hero = 0;
    std::uint8_t team = 0xFFU;
    std::uint8_t ready = 0;
    std::uint8_t reserved = 0;
};

struct LobbyResponse {
    std::uint8_t accepted = 0;
    std::uint8_t action = 0;
    std::uint8_t reserved = 0;
    std::string error;
};

struct MatchStart {
    std::uint32_t match_id = 0;
    std::uint32_t server_tick = 0;
    std::uint32_t server_time_ms = 0;
    std::uint64_t hash = 0;
    std::uint64_t map_fingerprint = 0;
    std::uint64_t seed = 0;
    std::string map_id;
    std::uint16_t tick_rate = 60;
    std::uint16_t snapshot_interval = 3;
    SyncTiming timing{};
    std::int16_t sync_time_to_step_ms = 0;
    std::uint8_t player_count = 0;
    std::uint8_t local_slot = 0;
    std::vector<std::uint8_t> snapshot;
};

struct MatchEnd {
    std::uint32_t match_id = 0;
    std::uint32_t server_tick = 0;
    std::uint32_t rematch_delay_ms = 0;
    std::int32_t score0 = 0;
    std::int32_t score1 = 0;
    std::uint8_t winner = 0;
    std::uint8_t player_count = 0;
    std::uint8_t reserved = 0;
};

struct InputMessage {
    std::uint8_t slot = 0;
    std::uint8_t sequence = 0;
    std::uint16_t buttons = 0;
    std::int16_t aim_x = 0;
    std::int16_t aim_y = 0;
    std::uint32_t tick = 0;
    ClientActionMask actions = 0;
    ClientActionMask action_toggles = 0;
    std::uint8_t action_param = 0;
    std::uint8_t reserved = 0;
};

struct SnapshotMessage {
    std::uint32_t tick = 0;
    std::uint32_t server_time_ms = 0;
    std::uint64_t hash = 0;
    std::vector<std::uint8_t> snapshot;
};

struct SnapshotAck {
    std::uint32_t tick = 0;
    std::uint32_t client_tick = 0;
    std::uint64_t client_hash = 0;
};

struct PingMessage {
    std::uint32_t client_time_ms = 0;
    std::uint32_t sequence = 0;
};

struct PongMessage {
    std::uint32_t client_time_ms = 0;
    std::uint32_t server_time_ms = 0;
    std::uint32_t sequence = 0;
};

struct SyncRequest {
    std::uint32_t tick = 0;
    std::uint64_t client_hash = 0;
};

struct SyncResponse {
    std::uint32_t tick = 0;
    std::uint32_t server_time_ms = 0;
    std::int16_t sync_time_to_step_ms = 0;
    std::uint64_t hash = 0;
    std::vector<std::uint8_t> snapshot;
};

struct DisconnectMessage {
    std::uint32_t reason = 0;
};

[[nodiscard]] bool encode_packet(
    const Packet& packet,
    std::vector<std::uint8_t>& output,
    std::string* error = nullptr);

[[nodiscard]] bool decode_packet(
    const std::uint8_t* data,
    std::size_t size,
    Packet& packet,
    std::string* error = nullptr);

[[nodiscard]] bool encode_login_request(
    const LoginRequest& request,
    std::vector<std::uint8_t>& output);
[[nodiscard]] bool decode_login_request(
    const std::uint8_t* data,
    std::size_t size,
    LoginRequest& request,
    std::string* error = nullptr);

[[nodiscard]] bool encode_login_response(
    const LoginResponse& response,
    std::vector<std::uint8_t>& output);
[[nodiscard]] bool decode_login_response(
    const std::uint8_t* data,
    std::size_t size,
    LoginResponse& response,
    std::string* error = nullptr);

[[nodiscard]] bool encode_lobby_request(
    const LobbyRequest& request,
    std::vector<std::uint8_t>& output);
[[nodiscard]] bool decode_lobby_request(
    const std::uint8_t* data,
    std::size_t size,
    LobbyRequest& request,
    std::string* error = nullptr);

[[nodiscard]] bool encode_lobby_response(
    const LobbyResponse& response,
    std::vector<std::uint8_t>& output);
[[nodiscard]] bool decode_lobby_response(
    const std::uint8_t* data,
    std::size_t size,
    LobbyResponse& response,
    std::string* error = nullptr);

[[nodiscard]] bool encode_lobby_state(
    const LobbyState& state,
    std::vector<std::uint8_t>& output);
[[nodiscard]] bool decode_lobby_state(
    const std::uint8_t* data,
    std::size_t size,
    LobbyState& state,
    std::string* error = nullptr);

[[nodiscard]] bool encode_match_start(
    const MatchStart& start,
    std::vector<std::uint8_t>& output);
[[nodiscard]] bool decode_match_start(
    const std::uint8_t* data,
    std::size_t size,
    MatchStart& start,
    std::string* error = nullptr);

[[nodiscard]] bool encode_match_end(
    const MatchEnd& end,
    std::vector<std::uint8_t>& output);
[[nodiscard]] bool decode_match_end(
    const std::uint8_t* data,
    std::size_t size,
    MatchEnd& end,
    std::string* error = nullptr);

[[nodiscard]] bool encode_input(
    const InputMessage& input,
    std::vector<std::uint8_t>& output);
[[nodiscard]] bool decode_input(
    const std::uint8_t* data,
    std::size_t size,
    InputMessage& input,
    std::string* error = nullptr);

[[nodiscard]] bool encode_snapshot(
    const SnapshotMessage& snapshot,
    std::vector<std::uint8_t>& output);
[[nodiscard]] bool decode_snapshot_message(
    const std::uint8_t* data,
    std::size_t size,
    SnapshotMessage& snapshot,
    std::string* error = nullptr);

[[nodiscard]] bool encode_snapshot_ack(
    const SnapshotAck& ack,
    std::vector<std::uint8_t>& output);
[[nodiscard]] bool decode_snapshot_ack(
    const std::uint8_t* data,
    std::size_t size,
    SnapshotAck& ack,
    std::string* error = nullptr);

[[nodiscard]] bool encode_ping(
    const PingMessage& ping,
    std::vector<std::uint8_t>& output);
[[nodiscard]] bool decode_ping(
    const std::uint8_t* data,
    std::size_t size,
    PingMessage& ping,
    std::string* error = nullptr);

[[nodiscard]] bool encode_pong(
    const PongMessage& pong,
    std::vector<std::uint8_t>& output);
[[nodiscard]] bool decode_pong(
    const std::uint8_t* data,
    std::size_t size,
    PongMessage& pong,
    std::string* error = nullptr);

[[nodiscard]] bool encode_sync_request(
    const SyncRequest& request,
    std::vector<std::uint8_t>& output);
[[nodiscard]] bool decode_sync_request(
    const std::uint8_t* data,
    std::size_t size,
    SyncRequest& request,
    std::string* error = nullptr);

[[nodiscard]] bool encode_sync_response(
    const SyncResponse& response,
    std::vector<std::uint8_t>& output);
[[nodiscard]] bool decode_sync_response(
    const std::uint8_t* data,
    std::size_t size,
    SyncResponse& response,
    std::string* error = nullptr);

[[nodiscard]] bool encode_disconnect(
    const DisconnectMessage& message,
    std::vector<std::uint8_t>& output);
[[nodiscard]] bool decode_disconnect(
    const std::uint8_t* data,
    std::size_t size,
    DisconnectMessage& message,
    std::string* error = nullptr);

[[nodiscard]] bool message_type_known(MessageType type);
[[nodiscard]] const char* message_type_name(MessageType type);

} // namespace nsync::net

