#include "net/protocol.h"

#include "core/byte_io.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace nsync::net {
namespace {

constexpr std::size_t kMaxErrorLength = 240;

bool fail(std::string* error, const char* message) {
    if (error) {
        *error = message;
    }
    return false;
}

void write_string(core::ByteWriter& writer, const std::string& value) {
    const std::size_t length = std::min(value.size(), kMaxErrorLength);
    writer.u16(static_cast<std::uint16_t>(length));
    writer.bytes(value.data(), length);
}

bool read_string(
    core::ByteReader& reader,
    std::string& value,
    std::string* error) {
    std::uint16_t length = 0;
    if (!reader.u16(length) || length > kMaxErrorLength) {
        return fail(error, "invalid string length");
    }
    std::vector<std::uint8_t> bytes(length);
    if (length > 0 && !reader.bytes(bytes.data(), bytes.size())) {
        return fail(error, "truncated string");
    }
    value.assign(bytes.begin(), bytes.end());
    return true;
}

bool read_exact_end(core::ByteReader& reader, std::string* error) {
    if (!reader.at_end()) {
        return fail(error, "trailing payload bytes");
    }
    return true;
}

bool write_snapshot_bytes(
    core::ByteWriter& writer,
    const std::vector<std::uint8_t>& snapshot) {
    if (snapshot.size() > kMaxPacketPayload ||
        snapshot.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    writer.u32(static_cast<std::uint32_t>(snapshot.size()));
    if (!snapshot.empty()) {
        writer.bytes(snapshot.data(), snapshot.size());
    }
    return true;
}

bool read_snapshot_bytes(
    core::ByteReader& reader,
    std::vector<std::uint8_t>& snapshot,
    std::string* error) {
    std::uint32_t size = 0;
    if (!reader.u32(size) || size > kMaxPacketPayload) {
        return fail(error, "invalid snapshot length");
    }
    snapshot.resize(size);
    if (size > 0 && !reader.bytes(snapshot.data(), snapshot.size())) {
        return fail(error, "truncated snapshot");
    }
    return true;
}

bool timing_valid(const SyncTiming& timing) {
    if (timing.server_lag_ticks > 60 ||
        timing.client_event_lag_ticks > 10 ||
        timing.sync_period_ticks > 240) {
        return false;
    }
    if (timing.sync_period_ticks == 0) {
        return timing.server_lag_ticks == 0 &&
            timing.client_event_lag_ticks == 0;
    }
    return timing.client_event_lag_ticks != 0;
}

void write_timing(core::ByteWriter& writer, const SyncTiming& timing) {
    writer.u16(timing.server_lag_ticks);
    writer.u16(timing.client_event_lag_ticks);
    writer.u16(timing.sync_period_ticks);
}

bool read_timing(
    core::ByteReader& reader,
    SyncTiming& timing,
    std::string* error) {
    if (!reader.u16(timing.server_lag_ticks) ||
        !reader.u16(timing.client_event_lag_ticks) ||
        !reader.u16(timing.sync_period_ticks)) {
        return false;
    }
    if (!timing_valid(timing)) {
        return fail(error, "invalid sync timing");
    }
    return true;
}

} // namespace

bool message_type_known(MessageType type) {
    switch (type) {
    case MessageType::LoginRequest:
    case MessageType::LoginResponse:
    case MessageType::LobbyRequest:
    case MessageType::LobbyResponse:
    case MessageType::LobbyState:
    case MessageType::MatchStart:
    case MessageType::MatchEnd:
    case MessageType::PingRequest:
    case MessageType::PingResponse:
    case MessageType::Input:
    case MessageType::InputBroadcast:
    case MessageType::Snapshot:
    case MessageType::SnapshotAck:
    case MessageType::SyncRequest:
    case MessageType::SyncResponse:
    case MessageType::Disconnect:
        return true;
    default:
        return false;
    }
}

const char* message_type_name(MessageType type) {
    switch (type) {
    case MessageType::LoginRequest:
        return "login_request";
    case MessageType::LoginResponse:
        return "login_response";
    case MessageType::LobbyRequest:
        return "lobby_request";
    case MessageType::LobbyResponse:
        return "lobby_response";
    case MessageType::LobbyState:
        return "lobby_state";
    case MessageType::MatchStart:
        return "match_start";
    case MessageType::MatchEnd:
        return "match_end";
    case MessageType::PingRequest:
        return "ping_request";
    case MessageType::PingResponse:
        return "ping_response";
    case MessageType::Input:
        return "input";
    case MessageType::InputBroadcast:
        return "input_broadcast";
    case MessageType::Snapshot:
        return "snapshot";
    case MessageType::SnapshotAck:
        return "snapshot_ack";
    case MessageType::SyncRequest:
        return "sync_request";
    case MessageType::SyncResponse:
        return "sync_response";
    case MessageType::Disconnect:
        return "disconnect";
    default:
        return "unknown";
    }
}

bool encode_packet(
    const Packet& packet,
    std::vector<std::uint8_t>& output,
    std::string* error) {
    if (!message_type_known(packet.type)) {
        return fail(error, "unknown packet type");
    }
    if (packet.payload.size() > kMaxPacketPayload) {
        return fail(error, "packet payload too large");
    }

    core::ByteWriter writer;
    writer.reserve(kPacketHeaderSize + packet.payload.size());
    writer.u32(kProtocolMagic);
    writer.u16(kProtocolVersion);
    writer.u16(static_cast<std::uint16_t>(packet.type));
    writer.u32(packet.session_token);
    writer.u32(packet.sequence);
    writer.u32(static_cast<std::uint32_t>(packet.payload.size()));
    if (!packet.payload.empty()) {
        writer.bytes(packet.payload.data(), packet.payload.size());
    }
    output = writer.data();
    return true;
}

bool decode_packet(
    const std::uint8_t* data,
    std::size_t size,
    Packet& packet,
    std::string* error) {
    if (data == nullptr || size < kPacketHeaderSize) {
        return fail(error, "packet header truncated");
    }
    core::ByteReader reader(data, size);
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t type = 0;
    std::uint32_t token = 0;
    std::uint32_t sequence = 0;
    std::uint32_t payload_size = 0;
    if (!reader.u32(magic) ||
        !reader.u16(version) ||
        !reader.u16(type) ||
        !reader.u32(token) ||
        !reader.u32(sequence) ||
        !reader.u32(payload_size)) {
        return fail(error, "packet header truncated");
    }
    if (magic != kProtocolMagic) {
        return fail(error, "packet magic mismatch");
    }
    if (version != kProtocolVersion) {
        return fail(error, "protocol version mismatch");
    }
    if (!message_type_known(static_cast<MessageType>(type))) {
        return fail(error, "unknown packet type");
    }
    if (payload_size > kMaxPacketPayload ||
        payload_size != reader.remaining()) {
        return fail(error, "packet payload length mismatch");
    }

    Packet decoded;
    decoded.type = static_cast<MessageType>(type);
    decoded.session_token = token;
    decoded.sequence = sequence;
    decoded.payload.resize(payload_size);
    if (payload_size > 0 &&
        !reader.bytes(decoded.payload.data(), decoded.payload.size())) {
        return fail(error, "packet payload truncated");
    }
    packet = std::move(decoded);
    return true;
}

bool encode_login_request(
    const LoginRequest& request,
    std::vector<std::uint8_t>& output) {
    if (request.display_name.size() > kMaxDisplayNameLength ||
        request.login_token.size() > kMaxLoginTokenLength ||
        request.map_id.size() > kMaxMapIdLength) {
        return false;
    }
    core::ByteWriter writer;
    write_string(writer, request.display_name);
    write_string(writer, request.login_token);
    write_string(writer, request.map_id);
    writer.u32(request.protocol_version);
    writer.u64(request.map_fingerprint);
    writer.u64(request.content_fingerprint);
    writer.u32(request.reconnect_token);
    writer.u8(request.desired_slot);
    writer.u8(request.desired_hero);
    writer.u8(request.desired_team);
    writer.u8(request.reserved);
    writer.u16(request.input_delay);
    output = writer.data();
    return true;
}

bool decode_login_request(
    const std::uint8_t* data,
    std::size_t size,
    LoginRequest& request,
    std::string* error) {
    core::ByteReader reader(data, size);
    LoginRequest decoded;
    if (!read_string(reader, decoded.display_name, error) ||
        !read_string(reader, decoded.login_token, error) ||
        !read_string(reader, decoded.map_id, error) ||
        !reader.u32(decoded.protocol_version) ||
        !reader.u64(decoded.map_fingerprint) ||
        !reader.u64(decoded.content_fingerprint) ||
        !reader.u32(decoded.reconnect_token) ||
        !reader.u8(decoded.desired_slot) ||
        !reader.u8(decoded.desired_hero) ||
        !reader.u8(decoded.desired_team) ||
        !reader.u8(decoded.reserved) ||
        !reader.u16(decoded.input_delay)) {
        return false;
    }
    if (!read_exact_end(reader, error)) {
        return false;
    }
    if (decoded.input_delay > 240 ||
        decoded.desired_hero >= core::kMaxHeroes ||
        decoded.map_id.size() > kMaxMapIdLength ||
        (decoded.desired_team > 1 && decoded.desired_team != 0xFFU)) {
        return fail(error, "invalid login request fields");
    }
    request = decoded;
    return true;
}

bool encode_login_response(
    const LoginResponse& response,
    std::vector<std::uint8_t>& output) {
    if (response.error.size() > kMaxErrorLength ||
        response.map_id.size() > kMaxMapIdLength ||
        !timing_valid(response.timing)) {
        return false;
    }
    core::ByteWriter writer;
    writer.u8(response.accepted);
    writer.u8(response.slot);
    writer.u8(response.player_count);
    writer.u8(response.phase);
    writer.u32(response.protocol_version);
    writer.u32(response.session_token);
    writer.u32(response.server_tick);
    writer.u32(response.server_time_ms);
    writer.u32(response.input_delay);
    writer.u32(response.match_id);
    writer.u64(response.map_fingerprint);
    writer.u64(response.content_fingerprint);
    writer.u64(response.snapshot_hash);
    write_string(writer, response.map_id);
    writer.u16(response.tick_rate);
    writer.u16(response.snapshot_interval);
    write_timing(writer, response.timing);
    writer.i16(response.sync_time_to_step_ms);
    if (!write_snapshot_bytes(writer, response.snapshot)) {
        return false;
    }
    write_string(writer, response.error);
    output = writer.data();
    return true;
}

bool decode_login_response(
    const std::uint8_t* data,
    std::size_t size,
    LoginResponse& response,
    std::string* error) {
    core::ByteReader reader(data, size);
    LoginResponse decoded;
    if (!reader.u8(decoded.accepted) ||
        !reader.u8(decoded.slot) ||
        !reader.u8(decoded.player_count) ||
        !reader.u8(decoded.phase) ||
        !reader.u32(decoded.protocol_version) ||
        !reader.u32(decoded.session_token) ||
        !reader.u32(decoded.server_tick) ||
        !reader.u32(decoded.server_time_ms) ||
        !reader.u32(decoded.input_delay) ||
        !reader.u32(decoded.match_id) ||
        !reader.u64(decoded.map_fingerprint) ||
        !reader.u64(decoded.content_fingerprint) ||
        !reader.u64(decoded.snapshot_hash) ||
        !read_string(reader, decoded.map_id, error) ||
        !reader.u16(decoded.tick_rate) ||
        !reader.u16(decoded.snapshot_interval) ||
        !read_timing(reader, decoded.timing, error) ||
        !reader.i16(decoded.sync_time_to_step_ms) ||
        !read_snapshot_bytes(reader, decoded.snapshot, error) ||
        !read_string(reader, decoded.error, error)) {
        return false;
    }
    if (!read_exact_end(reader, error)) {
        return false;
    }
    if (decoded.accepted > 1 || decoded.slot >= 10 ||
        decoded.player_count == 0 || decoded.player_count > 10 ||
        decoded.map_id.size() > kMaxMapIdLength ||
        decoded.input_delay > 240 || decoded.tick_rate < 1 ||
        decoded.tick_rate > 1000 || decoded.snapshot_interval == 0 ||
        decoded.snapshot_interval > 60 || decoded.phase > 3) {
        return fail(error, "invalid login response fields");
    }
    response = std::move(decoded);
    return true;
}

bool encode_lobby_request(
    const LobbyRequest& request,
    std::vector<std::uint8_t>& output) {
    core::ByteWriter writer;
    writer.u8(static_cast<std::uint8_t>(request.action));
    writer.u8(request.hero);
    writer.u8(request.team);
    writer.u8(request.ready);
    writer.u8(request.reserved);
    output = writer.data();
    return true;
}

bool decode_lobby_request(
    const std::uint8_t* data,
    std::size_t size,
    LobbyRequest& request,
    std::string* error) {
    core::ByteReader reader(data, size);
    LobbyRequest decoded;
    std::uint8_t action = 0;
    if (!reader.u8(action) ||
        !reader.u8(decoded.hero) ||
        !reader.u8(decoded.team) ||
        !reader.u8(decoded.ready) ||
        !reader.u8(decoded.reserved) ||
        !read_exact_end(reader, error)) {
        return false;
    }
    if (action < static_cast<std::uint8_t>(LobbyAction::Ready) ||
        action > static_cast<std::uint8_t>(LobbyAction::Start)) {
        return fail(error, "invalid lobby action");
    }
    if (decoded.hero >= core::kMaxHeroes || (decoded.team > 1 && decoded.team != 0xFFU) ||
        decoded.ready > 1) {
        return fail(error, "invalid lobby request fields");
    }
    decoded.action = static_cast<LobbyAction>(action);
    request = decoded;
    return true;
}

bool encode_lobby_response(
    const LobbyResponse& response,
    std::vector<std::uint8_t>& output) {
    if (response.error.size() > kMaxErrorLength) {
        return false;
    }
    core::ByteWriter writer;
    writer.u8(response.accepted);
    writer.u8(response.action);
    writer.u8(response.reserved);
    write_string(writer, response.error);
    output = writer.data();
    return true;
}

bool decode_lobby_response(
    const std::uint8_t* data,
    std::size_t size,
    LobbyResponse& response,
    std::string* error) {
    core::ByteReader reader(data, size);
    LobbyResponse decoded;
    if (!reader.u8(decoded.accepted) ||
        !reader.u8(decoded.action) ||
        !reader.u8(decoded.reserved) ||
        !read_string(reader, decoded.error, error) ||
        !read_exact_end(reader, error)) {
        return false;
    }
    if (decoded.accepted > 1) {
        return fail(error, "invalid lobby response");
    }
    response = std::move(decoded);
    return true;
}

bool encode_lobby_state(
    const LobbyState& state,
    std::vector<std::uint8_t>& output) {
    if (state.members.size() > 10 ||
        state.status.size() > kMaxErrorLength ||
        state.map_id.size() > kMaxMapIdLength) {
        return false;
    }
    core::ByteWriter writer;
    writer.u8(state.phase);
    writer.u8(state.player_count);
    writer.u8(state.human_count);
    writer.u8(state.ready_count);
    writer.u8(state.min_humans);
    writer.u32(state.countdown_ms);
    writer.u32(state.match_id);
    writer.u32(state.server_tick);
    writer.u32(state.server_time_ms);
    writer.u8(static_cast<std::uint8_t>(state.members.size()));
    for (const LobbyMember& member : state.members) {
        writer.u8(member.slot);
        writer.u8(member.team);
        writer.u8(member.hero);
        writer.u8(member.ready);
        writer.u8(member.connected);
        writer.u8(member.bot);
        write_string(writer, member.name);
    }
    write_string(writer, state.map_id);
    write_string(writer, state.status);
    output = writer.data();
    return true;
}

bool decode_lobby_state(
    const std::uint8_t* data,
    std::size_t size,
    LobbyState& state,
    std::string* error) {
    core::ByteReader reader(data, size);
    LobbyState decoded;
    std::uint8_t member_count = 0;
    if (!reader.u8(decoded.phase) ||
        !reader.u8(decoded.player_count) ||
        !reader.u8(decoded.human_count) ||
        !reader.u8(decoded.ready_count) ||
        !reader.u8(decoded.min_humans) ||
        !reader.u32(decoded.countdown_ms) ||
        !reader.u32(decoded.match_id) ||
        !reader.u32(decoded.server_tick) ||
        !reader.u32(decoded.server_time_ms) ||
        !reader.u8(member_count)) {
        return false;
    }
    if (decoded.phase > 3 || decoded.player_count == 0 ||
        decoded.player_count > 10 || member_count > 10 ||
        decoded.human_count > decoded.player_count ||
        decoded.ready_count > decoded.human_count) {
        return fail(error, "invalid lobby state fields");
    }
    decoded.members.resize(member_count);
    for (LobbyMember& member : decoded.members) {
        if (!reader.u8(member.slot) ||
            !reader.u8(member.team) ||
            !reader.u8(member.hero) ||
            !reader.u8(member.ready) ||
            !reader.u8(member.connected) ||
            !reader.u8(member.bot) ||
            !read_string(reader, member.name, error)) {
            return false;
        }
        if (member.slot >= 10 || member.team > 1 || member.hero >= core::kMaxHeroes ||
            member.ready > 1 || member.connected > 1 || member.bot > 1) {
            return fail(error, "invalid lobby member fields");
        }
    }
    if (!read_string(reader, decoded.map_id, error) ||
        decoded.map_id.size() > kMaxMapIdLength ||
        !read_string(reader, decoded.status, error) ||
        !read_exact_end(reader, error)) {
        return false;
    }
    state = std::move(decoded);
    return true;
}

bool encode_match_start(
    const MatchStart& start,
    std::vector<std::uint8_t>& output) {
    if (!timing_valid(start.timing) ||
        start.map_id.size() > kMaxMapIdLength) {
        return false;
    }
    core::ByteWriter writer;
    writer.u32(start.match_id);
    writer.u32(start.server_tick);
    writer.u32(start.server_time_ms);
    writer.u64(start.hash);
    writer.u64(start.map_fingerprint);
    writer.u64(start.seed);
    write_string(writer, start.map_id);
    writer.u16(start.tick_rate);
    writer.u16(start.snapshot_interval);
    write_timing(writer, start.timing);
    writer.i16(start.sync_time_to_step_ms);
    writer.u8(start.player_count);
    writer.u8(start.local_slot);
    if (!write_snapshot_bytes(writer, start.snapshot)) {
        return false;
    }
    output = writer.data();
    return true;
}

bool decode_match_start(
    const std::uint8_t* data,
    std::size_t size,
    MatchStart& start,
    std::string* error) {
    core::ByteReader reader(data, size);
    MatchStart decoded;
    if (!reader.u32(decoded.match_id) ||
        !reader.u32(decoded.server_tick) ||
        !reader.u32(decoded.server_time_ms) ||
        !reader.u64(decoded.hash) ||
        !reader.u64(decoded.map_fingerprint) ||
        !reader.u64(decoded.seed) ||
        !read_string(reader, decoded.map_id, error) ||
        !reader.u16(decoded.tick_rate) ||
        !reader.u16(decoded.snapshot_interval) ||
        !read_timing(reader, decoded.timing, error) ||
        !reader.i16(decoded.sync_time_to_step_ms) ||
        !reader.u8(decoded.player_count) ||
        !reader.u8(decoded.local_slot) ||
        !read_snapshot_bytes(reader, decoded.snapshot, error) ||
        !read_exact_end(reader, error)) {
        return false;
    }
    if (decoded.player_count == 0 || decoded.player_count > 10 ||
        decoded.local_slot >= decoded.player_count ||
        decoded.map_id.size() > kMaxMapIdLength ||
        decoded.tick_rate == 0 || decoded.tick_rate > 1000 ||
        decoded.snapshot_interval == 0 || decoded.snapshot_interval > 60 ||
        decoded.snapshot.empty()) {
        return fail(error, "invalid match start fields");
    }
    start = std::move(decoded);
    return true;
}

bool encode_match_end(
    const MatchEnd& end,
    std::vector<std::uint8_t>& output) {
    core::ByteWriter writer;
    writer.u32(end.match_id);
    writer.u32(end.server_tick);
    writer.u32(end.rematch_delay_ms);
    writer.i32(end.score0);
    writer.i32(end.score1);
    writer.u8(end.winner);
    writer.u8(end.player_count);
    writer.u8(end.reserved);
    output = writer.data();
    return true;
}

bool decode_match_end(
    const std::uint8_t* data,
    std::size_t size,
    MatchEnd& end,
    std::string* error) {
    core::ByteReader reader(data, size);
    MatchEnd decoded;
    if (!reader.u32(decoded.match_id) ||
        !reader.u32(decoded.server_tick) ||
        !reader.u32(decoded.rematch_delay_ms) ||
        !reader.i32(decoded.score0) ||
        !reader.i32(decoded.score1) ||
        !reader.u8(decoded.winner) ||
        !reader.u8(decoded.player_count) ||
        !reader.u8(decoded.reserved) ||
        !read_exact_end(reader, error)) {
        return false;
    }
    if (decoded.winner > 3 || decoded.player_count == 0 ||
        decoded.player_count > 10) {
        return fail(error, "invalid match end fields");
    }
    end = decoded;
    return true;
}

bool encode_input(
    const InputMessage& input,
    std::vector<std::uint8_t>& output) {
    core::ByteWriter writer;
    writer.u8(input.slot);
    writer.u8(input.sequence);
    writer.u16(input.buttons);
    writer.i16(input.aim_x);
    writer.i16(input.aim_y);
    writer.u32(input.tick);
    writer.u16(input.actions);
    writer.u16(input.action_toggles);
    writer.u8(input.action_param);
    writer.u8(input.reserved);
    output = writer.data();
    return true;
}

bool decode_input(
    const std::uint8_t* data,
    std::size_t size,
    InputMessage& input,
    std::string* error) {
    core::ByteReader reader(data, size);
    InputMessage decoded;
    if (!reader.u8(decoded.slot) ||
        !reader.u8(decoded.sequence) ||
        !reader.u16(decoded.buttons) ||
        !reader.i16(decoded.aim_x) ||
        !reader.i16(decoded.aim_y) ||
        !reader.u32(decoded.tick) ||
        !reader.u16(decoded.actions) ||
        !reader.u16(decoded.action_toggles) ||
        !reader.u8(decoded.action_param) ||
        !reader.u8(decoded.reserved) ||
        !read_exact_end(reader, error)) {
        return false;
    }
    if (decoded.slot >= 10 || decoded.reserved != 0) {
        return fail(error, "invalid input slot");
    }
    input = decoded;
    return true;
}

bool encode_snapshot(
    const SnapshotMessage& snapshot,
    std::vector<std::uint8_t>& output) {
    core::ByteWriter writer;
    writer.reserve(16 + 4 + snapshot.snapshot.size());
    writer.u32(snapshot.tick);
    writer.u32(snapshot.server_time_ms);
    writer.u64(snapshot.hash);
    if (!write_snapshot_bytes(writer, snapshot.snapshot)) {
        return false;
    }
    output = writer.data();
    return true;
}

bool decode_snapshot_message(
    const std::uint8_t* data,
    std::size_t size,
    SnapshotMessage& snapshot,
    std::string* error) {
    core::ByteReader reader(data, size);
    SnapshotMessage decoded;
    if (!reader.u32(decoded.tick) ||
        !reader.u32(decoded.server_time_ms) ||
        !reader.u64(decoded.hash) ||
        !read_snapshot_bytes(reader, decoded.snapshot, error) ||
        !read_exact_end(reader, error)) {
        return false;
    }
    if (decoded.snapshot.empty()) {
        return fail(error, "empty snapshot payload");
    }
    snapshot = std::move(decoded);
    return true;
}

bool encode_snapshot_ack(
    const SnapshotAck& ack,
    std::vector<std::uint8_t>& output) {
    core::ByteWriter writer;
    writer.u32(ack.tick);
    writer.u32(ack.client_tick);
    writer.u64(ack.client_hash);
    output = writer.data();
    return true;
}

bool decode_snapshot_ack(
    const std::uint8_t* data,
    std::size_t size,
    SnapshotAck& ack,
    std::string* error) {
    core::ByteReader reader(data, size);
    SnapshotAck decoded;
    if (!reader.u32(decoded.tick) ||
        !reader.u32(decoded.client_tick) ||
        !reader.u64(decoded.client_hash) ||
        !read_exact_end(reader, error)) {
        return false;
    }
    ack = decoded;
    return true;
}

bool encode_ping(
    const PingMessage& ping,
    std::vector<std::uint8_t>& output) {
    core::ByteWriter writer;
    writer.u32(ping.client_time_ms);
    writer.u32(ping.sequence);
    output = writer.data();
    return true;
}

bool decode_ping(
    const std::uint8_t* data,
    std::size_t size,
    PingMessage& ping,
    std::string* error) {
    core::ByteReader reader(data, size);
    PingMessage decoded;
    if (!reader.u32(decoded.client_time_ms) ||
        !reader.u32(decoded.sequence) ||
        !read_exact_end(reader, error)) {
        return false;
    }
    ping = decoded;
    return true;
}

bool encode_pong(
    const PongMessage& pong,
    std::vector<std::uint8_t>& output) {
    core::ByteWriter writer;
    writer.u32(pong.client_time_ms);
    writer.u32(pong.server_time_ms);
    writer.u32(pong.sequence);
    output = writer.data();
    return true;
}

bool decode_pong(
    const std::uint8_t* data,
    std::size_t size,
    PongMessage& pong,
    std::string* error) {
    core::ByteReader reader(data, size);
    PongMessage decoded;
    if (!reader.u32(decoded.client_time_ms) ||
        !reader.u32(decoded.server_time_ms) ||
        !reader.u32(decoded.sequence) ||
        !read_exact_end(reader, error)) {
        return false;
    }
    pong = decoded;
    return true;
}

bool encode_sync_request(
    const SyncRequest& request,
    std::vector<std::uint8_t>& output) {
    core::ByteWriter writer;
    writer.u32(request.tick);
    writer.u64(request.client_hash);
    output = writer.data();
    return true;
}

bool decode_sync_request(
    const std::uint8_t* data,
    std::size_t size,
    SyncRequest& request,
    std::string* error) {
    core::ByteReader reader(data, size);
    SyncRequest decoded;
    if (!reader.u32(decoded.tick) ||
        !reader.u64(decoded.client_hash) ||
        !read_exact_end(reader, error)) {
        return false;
    }
    request = decoded;
    return true;
}

bool encode_sync_response(
    const SyncResponse& response,
    std::vector<std::uint8_t>& output) {
    core::ByteWriter writer;
    writer.u32(response.tick);
    writer.u32(response.server_time_ms);
    writer.i16(response.sync_time_to_step_ms);
    writer.u64(response.hash);
    if (!write_snapshot_bytes(writer, response.snapshot)) {
        return false;
    }
    output = writer.data();
    return true;
}

bool decode_sync_response(
    const std::uint8_t* data,
    std::size_t size,
    SyncResponse& response,
    std::string* error) {
    core::ByteReader reader(data, size);
    SyncResponse decoded;
    if (!reader.u32(decoded.tick) ||
        !reader.u32(decoded.server_time_ms) ||
        !reader.i16(decoded.sync_time_to_step_ms) ||
        !reader.u64(decoded.hash) ||
        !read_snapshot_bytes(reader, decoded.snapshot, error) ||
        !read_exact_end(reader, error)) {
        return false;
    }
    if (decoded.snapshot.empty()) {
        return fail(error, "empty sync snapshot");
    }
    response = std::move(decoded);
    return true;
}

bool encode_disconnect(
    const DisconnectMessage& message,
    std::vector<std::uint8_t>& output) {
    core::ByteWriter writer;
    writer.u32(message.reason);
    output = writer.data();
    return true;
}

bool decode_disconnect(
    const std::uint8_t* data,
    std::size_t size,
    DisconnectMessage& message,
    std::string* error) {
    core::ByteReader reader(data, size);
    DisconnectMessage decoded;
    if (!reader.u32(decoded.reason) || !read_exact_end(reader, error)) {
        return false;
    }
    message = decoded;
    return true;
}

} // namespace nsync::net

