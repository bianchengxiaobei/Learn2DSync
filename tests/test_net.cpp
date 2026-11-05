#include "test_net.h"

#include "core/input_command.h"
#include "core/map.h"
#include "net/action_queue.h"
#include "net/local_net.h"
#include "net/protocol.h"
#include "net/real_client.h"
#include "net/real_server.h"
#include "net/rate_limit.h"
#include "net/server_clock.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

#define CHECK(...) \
    do { \
        if (!(__VA_ARGS__)) { \
            std::fprintf( \
                stderr, \
                "FAIL %s:%d: %s\n", \
                __FILE__, \
                __LINE__, \
                #__VA_ARGS__); \
            ++g_failures; \
        } \
    } while (0)

nsync::net::InputProvider make_provider(int slot) {
    return [slot](std::uint32_t tick) {
        using namespace nsync::core;
        InputCommand command;
        command.tick = tick;
        command.sequence = static_cast<std::uint8_t>(tick & 0xFFU);

        const bool moving_right = ((tick / 45U) + static_cast<std::uint32_t>(slot)) % 2U == 0U;
        command.buttons |= moving_right ? kButtonRight : kButtonLeft;
        if (tick % 47U == 0U) {
            command.buttons |= kButtonJump;
        }
        if (tick % 3U != 2U) {
            command.buttons |= kButtonFire;
        }
        if (tick == 1000U) {
            command.buttons |= kButtonReload;
        }

        command.aim_x = slot % 2 == 0 ? 32767 : -32767;
        command.aim_y = 0;
        return command;
    };
}

using DecodeFuzzFn = std::function<bool(
    const std::uint8_t*,
    std::size_t,
    std::string*)>;

void fuzz_decoder(
    const char* name,
    const std::vector<std::uint8_t>& seed,
    const DecodeFuzzFn& decode) {
    std::string error;
    CHECK(decode(seed.data(), seed.size(), &error));

    for (std::size_t length = 0; length < seed.size(); ++length) {
        const bool accepted = decode(seed.data(), length, &error);
        if (accepted) {
            std::fprintf(
                stderr,
                "FAIL fuzz %s accepted truncation at %zu/%zu\n",
                name,
                length,
                seed.size());
            ++g_failures;
        }
    }

    std::vector<std::uint8_t> extra = seed;
    extra.push_back(0x7FU);
    CHECK(!decode(extra.data(), extra.size(), &error));

    std::uint32_t state = 0x9E3779B9U;
    std::vector<std::uint8_t> random(256);
    for (int round = 0; round < 128; ++round) {
        state = state * 1664525U + 1013904223U;
        const std::size_t length = state % (random.size() + 1U);
        for (std::size_t i = 0; i < length; ++i) {
            state = state * 1664525U + 1013904223U;
            random[i] = static_cast<std::uint8_t>(state >> 24U);
        }
        (void)decode(random.data(), length, &error);
    }

    const std::uint8_t masks[] = {0x01U, 0x80U, 0xFFU};
    for (std::size_t i = 0; i < seed.size(); ++i) {
        for (const std::uint8_t mask : masks) {
            std::vector<std::uint8_t> mutated = seed;
            mutated[i] ^= mask;
            (void)decode(mutated.data(), mutated.size(), &error);
        }
    }

    for (std::size_t i = 0; i + 1 < seed.size(); ++i) {
        std::vector<std::uint8_t> mutated = seed;
        mutated[i] = 0xFFU;
        mutated[i + 1] = 0xFFU;
        (void)decode(mutated.data(), mutated.size(), &error);
    }
    for (std::size_t i = 0; i + 3 < seed.size(); ++i) {
        std::vector<std::uint8_t> mutated = seed;
        mutated[i] = 0xFFU;
        mutated[i + 1] = 0xFFU;
        mutated[i + 2] = 0xFFU;
        mutated[i + 3] = 0xFFU;
        (void)decode(mutated.data(), mutated.size(), &error);
    }
}

void test_protocol_fuzz() {
    using namespace nsync::net;

    {
        Packet packet;
        packet.type = MessageType::Snapshot;
        packet.session_token = 7;
        packet.sequence = 9;
        packet.payload = {1, 2, 3};
        std::vector<std::uint8_t> seed;
        CHECK(encode_packet(packet, seed));
        fuzz_decoder(
            "packet",
            seed,
            [](const std::uint8_t* data,
               std::size_t size,
               std::string* error) {
                Packet value;
                return decode_packet(data, size, value, error);
            });
    }

    {
        LoginRequest message;
        message.display_name = "fuzz";
        message.login_token = "token";
        std::vector<std::uint8_t> seed;
        CHECK(encode_login_request(message, seed));
        fuzz_decoder(
            "login_request",
            seed,
            [](const std::uint8_t* data,
               std::size_t size,
               std::string* error) {
                LoginRequest value;
                return decode_login_request(data, size, value, error);
            });
    }

    {
        LoginResponse message;
        message.accepted = 1;
        message.player_count = 2;
        message.tick_rate = 60;
        message.snapshot_interval = 3;
        message.snapshot = {1, 2, 3};
        std::vector<std::uint8_t> seed;
        CHECK(encode_login_response(message, seed));
        fuzz_decoder(
            "login_response",
            seed,
            [](const std::uint8_t* data,
               std::size_t size,
               std::string* error) {
                LoginResponse value;
                return decode_login_response(data, size, value, error);
            });
    }

    {
        LobbyRequest message;
        message.action = LobbyAction::Ready;
        message.hero = 0;
        message.team = 0;
        message.ready = 1;
        std::vector<std::uint8_t> seed;
        CHECK(encode_lobby_request(message, seed));
        fuzz_decoder(
            "lobby_request",
            seed,
            [](const std::uint8_t* data,
               std::size_t size,
               std::string* error) {
                LobbyRequest value;
                return decode_lobby_request(data, size, value, error);
            });
    }

    {
        LobbyResponse message;
        message.accepted = 1;
        message.action = static_cast<std::uint8_t>(LobbyAction::Ready);
        message.error = "ok";
        std::vector<std::uint8_t> seed;
        CHECK(encode_lobby_response(message, seed));
        fuzz_decoder(
            "lobby_response",
            seed,
            [](const std::uint8_t* data,
               std::size_t size,
               std::string* error) {
                LobbyResponse value;
                return decode_lobby_response(data, size, value, error);
            });
    }

    {
        LobbyState message;
        message.player_count = 2;
        message.human_count = 2;
        message.ready_count = 2;
        message.min_humans = 1;
        message.members.resize(2);
        message.members[0].slot = 0;
        message.members[0].name = "a";
        message.members[1].slot = 1;
        message.members[1].team = 1;
        message.members[1].name = "b";
        std::vector<std::uint8_t> seed;
        CHECK(encode_lobby_state(message, seed));
        fuzz_decoder(
            "lobby_state",
            seed,
            [](const std::uint8_t* data,
               std::size_t size,
               std::string* error) {
                LobbyState value;
                return decode_lobby_state(data, size, value, error);
            });
    }

    {
        MatchStart message;
        message.player_count = 2;
        message.tick_rate = 60;
        message.snapshot_interval = 3;
        message.snapshot = {1, 2, 3};
        std::vector<std::uint8_t> seed;
        CHECK(encode_match_start(message, seed));
        fuzz_decoder(
            "match_start",
            seed,
            [](const std::uint8_t* data,
               std::size_t size,
               std::string* error) {
                MatchStart value;
                return decode_match_start(data, size, value, error);
            });
    }

    {
        MatchEnd message;
        message.player_count = 2;
        message.winner = 1;
        std::vector<std::uint8_t> seed;
        CHECK(encode_match_end(message, seed));
        fuzz_decoder(
            "match_end",
            seed,
            [](const std::uint8_t* data,
               std::size_t size,
               std::string* error) {
                MatchEnd value;
                return decode_match_end(data, size, value, error);
            });
    }

    {
        InputMessage message;
        message.slot = 0;
        message.tick = 5;
        std::vector<std::uint8_t> seed;
        CHECK(encode_input(message, seed));
        fuzz_decoder(
            "input",
            seed,
            [](const std::uint8_t* data,
               std::size_t size,
               std::string* error) {
                InputMessage value;
                return decode_input(data, size, value, error);
            });
    }

    {
        SnapshotMessage message;
        message.tick = 5;
        message.snapshot = {1, 2, 3};
        std::vector<std::uint8_t> seed;
        CHECK(encode_snapshot(message, seed));
        fuzz_decoder(
            "snapshot",
            seed,
            [](const std::uint8_t* data,
               std::size_t size,
               std::string* error) {
                SnapshotMessage value;
                return decode_snapshot_message(data, size, value, error);
            });
    }

    {
        SnapshotAck message;
        message.tick = 5;
        std::vector<std::uint8_t> seed;
        CHECK(encode_snapshot_ack(message, seed));
        fuzz_decoder(
            "snapshot_ack",
            seed,
            [](const std::uint8_t* data,
               std::size_t size,
               std::string* error) {
                SnapshotAck value;
                return decode_snapshot_ack(data, size, value, error);
            });
    }

    {
        PingMessage message;
        message.client_time_ms = 5;
        std::vector<std::uint8_t> seed;
        CHECK(encode_ping(message, seed));
        fuzz_decoder(
            "ping",
            seed,
            [](const std::uint8_t* data,
               std::size_t size,
               std::string* error) {
                PingMessage value;
                return decode_ping(data, size, value, error);
            });
    }

    {
        PongMessage message;
        message.client_time_ms = 5;
        std::vector<std::uint8_t> seed;
        CHECK(encode_pong(message, seed));
        fuzz_decoder(
            "pong",
            seed,
            [](const std::uint8_t* data,
               std::size_t size,
               std::string* error) {
                PongMessage value;
                return decode_pong(data, size, value, error);
            });
    }

    {
        SyncRequest message;
        message.tick = 5;
        std::vector<std::uint8_t> seed;
        CHECK(encode_sync_request(message, seed));
        fuzz_decoder(
            "sync_request",
            seed,
            [](const std::uint8_t* data,
               std::size_t size,
               std::string* error) {
                SyncRequest value;
                return decode_sync_request(data, size, value, error);
            });
    }

    {
        SyncResponse message;
        message.tick = 5;
        message.snapshot = {1, 2, 3};
        std::vector<std::uint8_t> seed;
        CHECK(encode_sync_response(message, seed));
        fuzz_decoder(
            "sync_response",
            seed,
            [](const std::uint8_t* data,
               std::size_t size,
               std::string* error) {
                SyncResponse value;
                return decode_sync_response(data, size, value, error);
            });
    }

    {
        DisconnectMessage message;
        message.reason = 5;
        std::vector<std::uint8_t> seed;
        CHECK(encode_disconnect(message, seed));
        fuzz_decoder(
            "disconnect",
            seed,
            [](const std::uint8_t* data,
               std::size_t size,
               std::string* error) {
                DisconnectMessage value;
                return decode_disconnect(data, size, value, error);
            });
    }
}

void test_rate_limiter() {
    using namespace nsync::net;

    RateLimiter limiter(3, 1000);
    CHECK(limiter.allow(100));
    CHECK(limiter.allow(100));
    CHECK(limiter.allow(100));
    CHECK(!limiter.allow(100));
    CHECK(!limiter.allow(1099));
    CHECK(limiter.allow(1100));
    CHECK(limiter.allow(1100));
    CHECK(limiter.allow(1101));
    CHECK(!limiter.allow(1101));

    RateLimiter unlimited(0);
    bool unlimited_allowed = true;
    for (int i = 0; i < 1000; ++i) {
        unlimited_allowed = unlimited.allow(0) && unlimited_allowed;
    }
    CHECK(unlimited_allowed);

    RateLimiter wrapped(2, 1000);
    CHECK(wrapped.allow(0xFFFFFC00U));
    CHECK(wrapped.allow(0xFFFFFC00U));
    CHECK(!wrapped.allow(0xFFFFFC00U));
    CHECK(wrapped.allow(0x10U));
}

void test_server_clock() {
    using namespace nsync::net;
    ServerClock clock;
    clock.reset();
    clock.add_sample(1000, 5000, 1100);
    CHECK(clock.rtt_ms() == 100);
    CHECK(clock.server_to_local(5000) == 1050);
    CHECK(clock.local_to_server(1050) == 5000);
    clock.add_sample(2000, 6000, 2040);
    CHECK(clock.rtt_ms() <= 100);
    CHECK(clock.sample_count() == 2);
}
void test_protocol_roundtrip() {
    using namespace nsync::net;

    LoginRequest login;
    login.map_fingerprint = 0x1234567890ABCDEFULL;
    login.content_fingerprint = 0x0FEDCBA987654321ULL;
    login.reconnect_token = 42;
    login.desired_slot = 1;
    login.input_delay = 12;
    login.display_name = "tester";
    login.login_token = "fake-token";
    login.map_id = "world5";

    std::vector<std::uint8_t> payload;
    CHECK(encode_login_request(login, payload));
    LoginRequest decoded_login;
    std::string error;
    CHECK(decode_login_request(payload.data(), payload.size(), decoded_login, &error));
    CHECK(error.empty());
    CHECK(decoded_login.map_fingerprint == login.map_fingerprint);
    CHECK(decoded_login.content_fingerprint == login.content_fingerprint);
    CHECK(decoded_login.reconnect_token == login.reconnect_token);
    CHECK(decoded_login.desired_slot == login.desired_slot);
    CHECK(decoded_login.input_delay == login.input_delay);
    CHECK(decoded_login.display_name == login.display_name);
    CHECK(decoded_login.login_token == login.login_token);
    CHECK(decoded_login.map_id == login.map_id);

    LoginResponse response;
    response.accepted = 1;
    response.slot = 1;
    response.player_count = 2;
    response.protocol_version = kProtocolVersion;
    response.session_token = 0xAABBCCDDU;
    response.server_tick = 77;
    response.server_time_ms = 1234;
    response.input_delay = 12;
    response.phase = 1;
    response.match_id = 7;
    response.map_fingerprint = login.map_fingerprint;
    response.map_id = login.map_id;
    response.content_fingerprint = login.content_fingerprint;
    response.snapshot_hash = 0xDEADBEEFULL;
    response.tick_rate = 60;
    response.snapshot_interval = 3;
    response.timing = SyncTiming{8, 3, 30};
    response.sync_time_to_step_ms = 11;
    response.snapshot = {1, 2, 3, 4, 5};
    response.error = "ok";

    CHECK(encode_login_response(response, payload));
    LoginResponse decoded_response;
    error.clear();
    CHECK(decode_login_response(payload.data(), payload.size(), decoded_response, &error));
    CHECK(error.empty());
    CHECK(decoded_response.accepted == 1);
    CHECK(decoded_response.session_token == response.session_token);
    CHECK(decoded_response.phase == response.phase);
    CHECK(decoded_response.match_id == response.match_id);
    CHECK(decoded_response.map_id == response.map_id);
    CHECK(decoded_response.content_fingerprint ==
          response.content_fingerprint);
    CHECK(decoded_response.timing.server_lag_ticks == 8);
    CHECK(decoded_response.timing.client_event_lag_ticks == 3);
    CHECK(decoded_response.timing.sync_period_ticks == 30);
    CHECK(decoded_response.timing.input_lead_ticks() == 13);
    CHECK(decoded_response.sync_time_to_step_ms == 11);
    CHECK(decoded_response.snapshot == response.snapshot);
    CHECK(decoded_response.error == response.error);

    LoginRequest long_map_login = login;
    long_map_login.map_id.assign(kMaxMapIdLength + 1, 'x');
    CHECK(!encode_login_request(long_map_login, payload));
    LoginResponse long_map_response = response;
    long_map_response.map_id.assign(kMaxMapIdLength + 1, 'x');
    CHECK(!encode_login_response(long_map_response, payload));

    InputMessage input;
    input.slot = 1;
    input.sequence = 9;
    input.buttons = nsync::core::kButtonFire | nsync::core::kButtonRight | nsync::core::kButtonAbility;
    input.aim_x = -123;
    input.aim_y = 456;
    input.tick = 900;
    input.actions = client_action_bit(ClientAction::WeaponReload) |
        client_action_bit(ClientAction::ActivateGadget);
    input.action_toggles = client_action_bit(ClientAction::WeaponFire);
    input.action_param = 1;
    CHECK(encode_input(input, payload));
    InputMessage decoded_input;
    error.clear();
    CHECK(decode_input(payload.data(), payload.size(), decoded_input, &error));
    CHECK(error.empty());
    CHECK(decoded_input.tick == input.tick);
    CHECK(decoded_input.buttons == input.buttons);
    CHECK(decoded_input.aim_x == input.aim_x);
    CHECK(decoded_input.actions == input.actions);
    CHECK(decoded_input.action_toggles == input.action_toggles);
    CHECK(decoded_input.action_param == input.action_param);
    CHECK(client_action_bit(ClientAction::GoLeft) == 0x0001U);
    CHECK(client_action_bit(ClientAction::Reconnect) == 0x4000U);
    CHECK(client_action_toggle(ClientAction::WeaponFire));
    CHECK(!client_action_toggle(ClientAction::WeaponReload));

    SnapshotMessage snapshot;
    snapshot.tick = 12;
    snapshot.server_time_ms = 300;
    snapshot.hash = 0x1122334455667788ULL;
    snapshot.snapshot = {9, 8, 7};
    CHECK(encode_snapshot(snapshot, payload));
    SnapshotMessage decoded_snapshot;
    error.clear();
    CHECK(decode_snapshot_message(payload.data(), payload.size(), decoded_snapshot, &error));
    CHECK(error.empty());
    CHECK(decoded_snapshot.hash == snapshot.hash);
    CHECK(decoded_snapshot.snapshot == snapshot.snapshot);

    SnapshotAck ack;
    ack.tick = 12;
    ack.client_tick = 14;
    ack.client_hash = 0x99ULL;
    CHECK(encode_snapshot_ack(ack, payload));
    SnapshotAck decoded_ack;
    CHECK(decode_snapshot_ack(payload.data(), payload.size(), decoded_ack));
    CHECK(decoded_ack.tick == ack.tick);
    CHECK(decoded_ack.client_hash == ack.client_hash);

    PingMessage ping;
    ping.client_time_ms = 100;
    ping.sequence = 7;
    CHECK(encode_ping(ping, payload));
    PingMessage decoded_ping;
    CHECK(decode_ping(payload.data(), payload.size(), decoded_ping));
    CHECK(decoded_ping.sequence == ping.sequence);

    PongMessage pong;
    pong.client_time_ms = 100;
    pong.server_time_ms = 160;
    pong.sequence = 7;
    CHECK(encode_pong(pong, payload));
    PongMessage decoded_pong;
    CHECK(decode_pong(payload.data(), payload.size(), decoded_pong));
    CHECK(decoded_pong.server_time_ms == pong.server_time_ms);

    SyncRequest sync_request;
    sync_request.tick = 55;
    sync_request.client_hash = 0xABCULL;
    CHECK(encode_sync_request(sync_request, payload));
    SyncRequest decoded_sync_request;
    CHECK(decode_sync_request(payload.data(), payload.size(), decoded_sync_request));
    CHECK(decoded_sync_request.tick == sync_request.tick);

    SyncResponse sync_response;
    sync_response.tick = 55;
    sync_response.server_time_ms = 800;
    sync_response.sync_time_to_step_ms = -17;
    sync_response.hash = 0xDEFULL;
    sync_response.snapshot = {4, 4, 4};
    CHECK(encode_sync_response(sync_response, payload));
    SyncResponse decoded_sync_response;
    CHECK(decode_sync_response(payload.data(), payload.size(), decoded_sync_response));
    CHECK(decoded_sync_response.snapshot == sync_response.snapshot);
    CHECK(decoded_sync_response.sync_time_to_step_ms == -17);

    DisconnectMessage disconnect;
    disconnect.reason = 3;
    CHECK(encode_disconnect(disconnect, payload));
    DisconnectMessage decoded_disconnect;
    CHECK(decode_disconnect(payload.data(), payload.size(), decoded_disconnect));
    CHECK(decoded_disconnect.reason == disconnect.reason);

    LobbyRequest lobby_request;
    lobby_request.action = LobbyAction::SelectHero;
    lobby_request.hero = 1;
    lobby_request.team = 0;
    lobby_request.ready = 1;
    CHECK(encode_lobby_request(lobby_request, payload));
    LobbyRequest decoded_lobby_request;
    CHECK(decode_lobby_request(
        payload.data(), payload.size(), decoded_lobby_request));
    CHECK(decoded_lobby_request.action == lobby_request.action);
    CHECK(decoded_lobby_request.hero == lobby_request.hero);
    CHECK(decoded_lobby_request.ready == 1);

    LobbyState lobby;
    lobby.phase = 1;
    lobby.player_count = 4;
    lobby.human_count = 2;
    lobby.ready_count = 1;
    lobby.min_humans = 1;
    lobby.countdown_ms = 1500;
    lobby.match_id = 9;
    lobby.server_tick = 42;
    lobby.server_time_ms = 700;
    lobby.members.push_back(LobbyMember{0, 0, 0, 1, 1, 0, "alpha"});
    lobby.members.push_back(LobbyMember{1, 1, 1, 0, 1, 0, "beta"});
    lobby.status = "match starting";
    lobby.map_id = "crystal_02";
    CHECK(encode_lobby_state(lobby, payload));
    LobbyState decoded_lobby;
    CHECK(decode_lobby_state(payload.data(), payload.size(), decoded_lobby));
    CHECK(decoded_lobby.phase == lobby.phase);
    CHECK(decoded_lobby.members.size() == 2);
    CHECK(decoded_lobby.members[1].name == "beta");
    CHECK(decoded_lobby.map_id == lobby.map_id);
    CHECK(decoded_lobby.status == lobby.status);

    LobbyResponse lobby_response;
    lobby_response.accepted = 1;
    lobby_response.action = static_cast<std::uint8_t>(LobbyAction::Ready);
    CHECK(encode_lobby_response(lobby_response, payload));
    LobbyResponse decoded_lobby_response;
    CHECK(decode_lobby_response(
        payload.data(), payload.size(), decoded_lobby_response));
    CHECK(decoded_lobby_response.accepted == 1);

    MatchStart match_start;
    match_start.match_id = 3;
    match_start.server_tick = 10;
    match_start.server_time_ms = 200;
    match_start.hash = 0x1234ULL;
    match_start.map_fingerprint = 0x5678ULL;
    match_start.seed = 99;
    match_start.map_id = "world5";
    match_start.timing = SyncTiming{8, 3, 30};
    match_start.sync_time_to_step_ms = 23;
    match_start.player_count = 4;
    match_start.local_slot = 2;
    match_start.snapshot = {8, 9, 10};
    CHECK(encode_match_start(match_start, payload));
    MatchStart decoded_match_start;
    CHECK(decode_match_start(
        payload.data(), payload.size(), decoded_match_start));
    CHECK(decoded_match_start.match_id == 3);
    CHECK(decoded_match_start.local_slot == 2);
    CHECK(decoded_match_start.map_id == match_start.map_id);
    CHECK(decoded_match_start.timing.input_lead_ticks() == 13);
    CHECK(decoded_match_start.sync_time_to_step_ms == 23);
    CHECK(decoded_match_start.snapshot == match_start.snapshot);

    MatchEnd match_end;
    match_end.match_id = 3;
    match_end.score0 = 12;
    match_end.score1 = 7;
    match_end.winner = 1;
    match_end.player_count = 4;
    match_end.rematch_delay_ms = 1000;
    CHECK(encode_match_end(match_end, payload));
    MatchEnd decoded_match_end;
    CHECK(decode_match_end(payload.data(), payload.size(), decoded_match_end));
    CHECK(decoded_match_end.score0 == 12);
    CHECK(decoded_match_end.winner == 1);

    Packet packet;
    packet.type = MessageType::Input;
    packet.session_token = 0xCAFEULL;
    packet.sequence = 19;
    packet.payload = snapshot.snapshot;
    std::vector<std::uint8_t> encoded_packet;
    error.clear();
    CHECK(encode_packet(packet, encoded_packet, &error));
    Packet decoded_packet;
    CHECK(decode_packet(
        encoded_packet.data(), encoded_packet.size(), decoded_packet, &error));
    CHECK(error.empty());
    CHECK(decoded_packet.type == packet.type);
    CHECK(decoded_packet.session_token == packet.session_token);
    CHECK(decoded_packet.sequence == packet.sequence);
    CHECK(decoded_packet.payload == packet.payload);
}

void test_sync_timing_formula() {
    using namespace nsync::net;

    const SyncTiming legacy{};
    CHECK(!legacy.configured());
    CHECK(legacy.input_lead_ticks() == 0);

    const SyncTiming online{8, 3, 30};
    CHECK(online.configured());
    CHECK(online.normal_input_lead_ticks() == 3);
    CHECK(online.recovery_input_lead_ticks() == 13);
    CHECK(online.input_lead_ticks() == 13);

    const SyncTiming odd{5, 5, 30};
    CHECK(odd.normal_input_lead_ticks() == 5);
    CHECK(odd.recovery_input_lead_ticks() == 13);
    CHECK(odd.input_lead_ticks() == 13);

    LoginResponse response;
    response.accepted = 1;
    response.player_count = 2;
    response.tick_rate = 60;
    response.snapshot_interval = 3;
    response.timing = SyncTiming{1, 0, 30};
    std::vector<std::uint8_t> payload;
    CHECK(!encode_login_response(response, payload));

    response.timing = SyncTiming{0, 0, 0};
    CHECK(encode_login_response(response, payload));
}

void test_protocol_rejects_invalid_packets() {
    using namespace nsync::net;

    Packet packet;
    packet.type = MessageType::PingRequest;
    packet.payload = {1, 2, 3};
    std::vector<std::uint8_t> encoded;
    std::string error;
    CHECK(encode_packet(packet, encoded, &error));

    std::vector<std::uint8_t> broken = encoded;
    broken[0] ^= 0xFFU;
    CHECK(!decode_packet(broken.data(), broken.size(), packet, &error));
    CHECK(!error.empty());

    broken = encoded;
    broken[4] = 0xFFU;
    broken[5] = 0xFFU;
    CHECK(!decode_packet(broken.data(), broken.size(), packet, &error));

    broken = encoded;
    broken[16] = 0xFFU;
    broken[17] = 0xFFU;
    CHECK(!decode_packet(broken.data(), broken.size(), packet, &error));

    CHECK(!decode_packet(encoded.data(), encoded.size() - 1, packet, &error));

    LoginRequest login;
    std::vector<std::uint8_t> payload;
    CHECK(encode_login_request(login, payload));
    LoginRequest decoded_login;
    CHECK(!decode_login_request(payload.data(), payload.size() - 1, decoded_login, &error));
    payload.push_back(0);
    CHECK(!decode_login_request(payload.data(), payload.size(), decoded_login, &error));

    std::uint32_t state = 0x12345678U;
    std::vector<std::uint8_t> random_bytes(96);
    for (int round = 0; round < 200; ++round) {
        for (std::uint8_t& byte : random_bytes) {
            state = state * 1664525U + 1013904223U;
            byte = static_cast<std::uint8_t>(state >> 24U);
        }
        Packet ignored;
        (void)decode_packet(
            random_bytes.data(),
            random_bytes.size(),
            ignored,
            &error);
    }
}

void test_map_fingerprint() {
    using namespace nsync::core;
    const Map first = Map::default_arena();
    Map second = first;
    CHECK(map_fingerprint(first) == map_fingerprint(second));
    second.id = "arena_02";
    CHECK(map_fingerprint(first) != map_fingerprint(second));
    second = first;
    second.spawns[0].x += Fix32::from_int(1);
    CHECK(map_fingerprint(first) != map_fingerprint(second));
}

void test_network_simulator() {
    using namespace nsync::net;
    NetworkSimulator network;

    NetConfig config;
    config.latency_ticks = 3;
    config.jitter_ticks = 0;
    config.loss_percent = 0;
    config.duplicate_percent = 0;
    config.seed = 11;
    network.configure(config);

    InputPacket input;
    input.slot = 0;
    input.command.tick = 5;
    input.command.buttons = nsync::core::kButtonFire;
    network.send_input(input, 0);
    CHECK(network.pump(2).server_inputs.empty());
    CHECK(network.pump(3).server_inputs.size() == 1);
    CHECK(network.stats().packets_sent == 1);
    CHECK(network.stats().packets_delivered == 1);
    CHECK(network.stats().rtt_ticks == 6);
    CHECK(network.stats().rtt_ms == 100);

    config.loss_percent = 100;
    network.configure(config);
    network.send_input(input, 10);
    CHECK(network.pump(20).server_inputs.empty());
    CHECK(network.stats().packets_dropped == 1);

    config.loss_percent = 0;
    config.duplicate_percent = 100;
    network.configure(config);
    network.send_input(input, 30);
    const Deliveries duplicated = network.pump(40);
    CHECK(duplicated.server_inputs.size() >= 2);
    CHECK(network.stats().packets_duplicated >= 1);
}

void test_local_prediction_exact() {
    using namespace nsync::net;
    NetConfig config;
    config.latency_ticks = 5;
    config.jitter_ticks = 2;
    config.loss_percent = 0;
    config.duplicate_percent = 0;
    config.input_delay_ticks = 16;
    config.snapshot_interval = 1;
    config.seed = 7;

    std::vector<InputProvider> providers;
    providers.push_back(make_provider(0));
    providers.push_back(make_provider(1));

    LocalSession session;
    std::string error;
    CHECK(session.start(
        nsync::core::Map::default_arena(),
        123,
        2,
        2,
        std::move(providers),
        config,
        600U * 60U,
        999,
        &error));
    CHECK(error.empty());

    bool exact = true;
    for (int tick = 0; tick < 600; ++tick) {
        session.tick();
        if (tick > 40) {
            for (int client = 0; client < 2; ++client) {
                if (session.client(client).world().hash() !=
                    session.server().world().hash()) {
                    exact = false;
                }
            }
        }
    }
    CHECK(exact);
    for (int client = 0; client < 2; ++client) {
        CHECK(session.client(client).stats().corrections == 0);
        CHECK(session.client(client).stats().desyncs == 0);
        CHECK(!session.client(client).graph().samples().empty());
        CHECK(session.client(client).stats().ack_tick > 0);
    }
    CHECK(session.network().stats().packets_delivered > 0);
}

void test_local_late_input_rollback() {
    using namespace nsync::net;
    using namespace nsync::core;
    NetConfig config;
    config.latency_ticks = 0;
    config.jitter_ticks = 0;
    config.input_delay_ticks = 0;
    config.rollback_history = 32;

    LocalServer server;
    std::string error;
    CHECK(server.start(
        nsync::core::Map::default_arena(),
        321,
        2,
        600U * 60U,
        999,
        &error));
    WorldSnapshot initial;
    server.world().save(initial);

    LocalClient client;
    CHECK(client.start(
        nsync::core::Map::default_arena(),
        initial,
        0,
        config,
        &error));

    InputProvider provider = [](std::uint32_t tick) {
        InputCommand command;
        command.tick = tick;
        return command;
    };
    for (int step = 0; step < 8; ++step) {
        InputPacket output;
        CHECK(client.step(provider, output, &error));
    }
    CHECK(client.world().tick() == 8);

    InputPacket late;
    late.slot = 1;
    late.command.tick = 3;
    late.command.buttons = kButtonRight;
    late.command.aim_x = -32767;
    client.receive_input_broadcast(late);

    CHECK(client.stats().rollbacks == 1);
    CHECK(client.stats().event_rollbacks == 1);
    // Restores the state before tick 3 and replays ticks 3..7.
    CHECK(client.stats().total_rollback_ticks == 5);
    CHECK(client.stats().snapshot_mismatches == 0);
    CHECK(client.world().tick() == 8);
    bool remote_buttons = false;
    client.world().players().for_each([&](const Player& player) {
        if (player.slot == 1) {
            remote_buttons = player.buttons == kButtonRight;
        }
    });
    CHECK(remote_buttons);

    InputPacket queued_a;
    queued_a.slot = 1;
    queued_a.command.tick = 2;
    queued_a.command.buttons = kButtonLeft;
    queued_a.command.aim_x = 32767;
    client.queue_input_broadcast(queued_a);

    InputPacket queued_b;
    queued_b.slot = 1;
    queued_b.command.tick = 5;
    queued_b.command.buttons = kButtonJump;
    queued_b.command.aim_x = -32767;
    client.queue_input_broadcast(queued_b);

    CHECK(client.stats().event_rollbacks == 1);
    client.flush_pending_rollbacks();
    CHECK(client.stats().event_rollbacks == 2);
    InputPacket output;
    CHECK(client.step(provider, output, &error));
    CHECK(client.world().tick() == 9);
    CHECK(client.step(provider, output, &error));
    CHECK(client.world().tick() == 10);
    bool remote_jump = false;
    client.world().players().for_each([&](const Player& player) {
        if (player.slot == 1) {
            remote_jump = player.buttons == kButtonJump;
        }
    });
    CHECK(remote_jump);

    SnapshotPacket authoritative;
    client.world().save(authoritative.snapshot);
    authoritative.authoritative = true;
    CHECK(client.apply_snapshot(authoritative));
    const std::uint32_t rollbacks_before = client.stats().rollbacks;
    InputPacket covered;
    covered.slot = 1;
    covered.command.tick = 5;
    covered.command.buttons = kButtonReload;
    covered.command.aim_x = 32767;
    client.receive_input_broadcast(covered);
    // Older than the last applied snapshot, but the server may have
    // rewritten its history (late input replay), so it is replayed anyway.
    CHECK(client.stats().rollbacks == rollbacks_before + 1U);
    CHECK(client.stats().covered_inputs == 1);

    // A snapshot for tick T is the state before tick T runs, so an input for
    // exactly T is not "covered".
    const std::uint32_t snapshot_tick = client.world().tick();
    CHECK(client.step(provider, output, &error));
    CHECK(client.step(provider, output, &error));
    InputPacket boundary;
    boundary.slot = 1;
    boundary.command.tick = snapshot_tick;
    boundary.command.buttons = kButtonLeft;
    boundary.command.aim_x = 32767;
    client.receive_input_broadcast(boundary);
    CHECK(client.stats().covered_inputs == 1);
    CHECK(client.stats().rollbacks == rollbacks_before + 2U);

    // Aim-only changes (weapon redirect) are replayed like the original.
    InputPacket aim_only = boundary;
    aim_only.command.tick = snapshot_tick + 1U;
    aim_only.command.aim_x = -32767;
    client.receive_input_broadcast(aim_only);
    CHECK(client.stats().rollbacks == rollbacks_before + 3U);
    bool remote_facing_left = false;
    client.world().players().for_each([&](const Player& player) {
        if (player.slot == 1) {
            remote_facing_left = player.facing < 0;
        }
    });
    CHECK(remote_facing_left);

    // Re-sending an identical command never rolls back.
    client.receive_input_broadcast(aim_only);
    CHECK(client.stats().rollbacks == rollbacks_before + 3U);
}

void test_sync_time_to_step_mapping() {
    using namespace nsync::net;
    CHECK(effective_server_lag_ticks(8, 0, 60) == 8);
    CHECK(effective_server_lag_ticks(8, 100, 60) == 2);
    CHECK(effective_server_lag_ticks(8, -100, 60) == 14);
    CHECK(effective_server_lag_ticks(8, 1000, 60) == 0);
    CHECK(effective_server_lag_ticks(8, -2000, 60) == 60);
    CHECK(effective_server_lag_ticks(8, 100, 30) == 5);
    CHECK(rtt_compensation_ticks(0, 60) == 0);
    CHECK(rtt_compensation_ticks(50, 60) == 1);
    CHECK(rtt_compensation_ticks(150, 60) == 4);
    CHECK(rtt_compensation_ticks(1000, 60) == 30);
}

void test_local_snapshot_stats() {
    using namespace nsync::net;
    using namespace nsync::core;
    NetConfig config;
    config.latency_ticks = 0;
    config.jitter_ticks = 0;
    config.input_delay_ticks = 0;
    config.rollback_history = 32;

    LocalServer server;
    std::string error;
    CHECK(server.start(
        nsync::core::Map::default_arena(),
        322,
        2,
        600U * 60U,
        999,
        &error));
    WorldSnapshot initial;
    server.world().save(initial);

    LocalClient client;
    CHECK(client.start(
        nsync::core::Map::default_arena(),
        initial,
        0,
        config,
        &error));

    InputProvider provider = [](std::uint32_t tick) {
        InputCommand command;
        command.tick = tick;
        return command;
    };
    for (int step = 0; step < 8; ++step) {
        InputPacket output;
        CHECK(client.step(provider, output, &error));
    }

    WorldSnapshot divergent;
    client.world().save(divergent);
    divergent.tick = 5;
    divergent.players[0].position.x += Fix32::from_int(1);
    SnapshotPacket non_authoritative;
    non_authoritative.snapshot = divergent;
    non_authoritative.authoritative = false;
    CHECK(client.apply_snapshot(non_authoritative, &error));
    CHECK(client.stats().snapshot_mismatches == 1);
    CHECK(client.stats().snapshot_missing_predictions == 0);
    CHECK(client.stats().snapshot_periodic_mismatches == 1);
    CHECK(client.stats().snapshot_authoritative_mismatches == 0);
    CHECK(client.stats().snapshot_restores == 1);
    CHECK(client.stats().rollbacks == 1);
    CHECK(client.stats().event_rollbacks == 0);
    CHECK(client.stats().total_rollback_ticks == 3);
    CHECK(client.stats().corrections == 0);
    CHECK(client.stats().desyncs == 0);
    CHECK(client.stats().last_client_hash == nsync::core::snapshot_hash(divergent));

    client.world().save(divergent);
    divergent.tick = 6;
    divergent.players[0].position.x += Fix32::from_int(1);
    SnapshotPacket authoritative;
    authoritative.snapshot = divergent;
    authoritative.authoritative = true;
    CHECK(client.apply_snapshot(authoritative, &error));
    CHECK(client.stats().snapshot_mismatches == 2);
    CHECK(client.stats().snapshot_missing_predictions == 0);
    CHECK(client.stats().snapshot_periodic_mismatches == 1);
    CHECK(client.stats().snapshot_authoritative_mismatches == 1);
    CHECK(client.stats().snapshot_restores == 2);
    CHECK(client.stats().rollbacks == 2);
    CHECK(client.stats().event_rollbacks == 0);
    CHECK(client.stats().total_rollback_ticks == 5);
    CHECK(client.stats().corrections == 1);
    CHECK(client.stats().desyncs == 1);
    CHECK(client.world().tick() == 8);

    WorldSnapshot remote_only;
    client.world().save(remote_only);
    remote_only.tick = 7;
    for (Player& player : remote_only.players) {
        if (player.slot == 1) {
            player.position.x += Fix32::from_int(1);
            player.velocity = {};
        }
    }
    SnapshotPacket remote_snapshot;
    remote_snapshot.snapshot = remote_only;
    remote_snapshot.authoritative = false;
    CHECK(client.apply_snapshot(remote_snapshot, &error));
    CHECK(client.stats().snapshot_mismatches == 3);
    CHECK(client.stats().snapshot_periodic_mismatches == 2);
    CHECK(client.stats().snapshot_restores == 2);
    CHECK(client.stats().remote_snapshot_corrections == 1);
    CHECK(client.stats().rollbacks == 2);
    CHECK(client.stats().total_rollback_ticks == 5);
    CHECK(client.stats().corrections == 1);
    CHECK(client.stats().desyncs == 1);
    const Player* local_world_remote = nullptr;
    client.world().players().for_each([&](const Player& player) {
        if (player.slot == 1) {
            local_world_remote = &player;
        }
    });
    const Player* server_remote = nullptr;
    for (const Player& player : remote_only.players) {
        if (player.slot == 1) {
            server_remote = &player;
        }
    }
    CHECK(local_world_remote != nullptr);
    CHECK(server_remote != nullptr);
    if (local_world_remote != nullptr && server_remote != nullptr) {
        CHECK(local_world_remote->position.x == server_remote->position.x);
        CHECK(local_world_remote->position.y == server_remote->position.y);
        // The full server state (not only the position) is rebuilt.
        CHECK(local_world_remote->velocity.x == server_remote->velocity.x);
    }
    // The renderer gets the correction so it can blend instead of popping.
    bool remote_visual_correction = false;
    for (const LocalClient::VisualCorrection& correction :
         client.take_visual_corrections()) {
        if (server_remote != nullptr && correction.entity == server_remote->id) {
            remote_visual_correction =
                correction.offset.x == -Fix32::from_int(1);
        }
    }
    CHECK(remote_visual_correction);
    CHECK(client.take_visual_corrections().empty());
}

void test_local_desync_repair() {
    using namespace nsync::net;
    NetConfig config;
    config.latency_ticks = 5;
    config.jitter_ticks = 0;
    config.input_delay_ticks = 14;
    config.seed = 9;

    std::vector<InputProvider> providers;
    providers.push_back(make_provider(0));
    providers.push_back(make_provider(1));

    LocalSession session;
    std::string error;
    CHECK(session.start(
        nsync::core::Map::default_arena(),
        456,
        2,
        2,
        std::move(providers),
        config,
        600U * 60U,
        999,
        &error));

    for (int tick = 0; tick < 120; ++tick) {
        session.tick();
    }
    CHECK(session.client(0).world().hash() ==
          session.server().world().hash());
    CHECK(session.client(0).inject_desync());
    CHECK(session.client(0).world().hash() !=
          session.server().world().hash());

    for (int tick = 0; tick < 12; ++tick) {
        session.tick();
    }
    CHECK(session.client(0).world().hash() ==
          session.server().world().hash());
    CHECK(session.client(0).stats().corrections > 0);
}

void test_local_loss_and_duplicate() {
    using namespace nsync::net;
    NetConfig config;
    config.latency_ticks = 5;
    config.jitter_ticks = 2;
    config.loss_percent = 10;
    config.duplicate_percent = 10;
    config.input_delay_ticks = 16;
    config.seed = 13;

    std::vector<InputProvider> providers;
    providers.push_back(make_provider(0));
    providers.push_back(make_provider(1));

    LocalSession session;
    std::string error;
    CHECK(session.start(
        nsync::core::Map::default_arena(),
        789,
        2,
        2,
        std::move(providers),
        config,
        600U * 60U,
        999,
        &error));

    for (int tick = 0; tick < 1200; ++tick) {
        session.tick();
    }
    CHECK(session.network().stats().packets_dropped > 0);
    CHECK(session.network().stats().packets_duplicated > 0);
    for (int client = 0; client < 2; ++client) {
        CHECK(session.client(client).stats().ack_tick > 0);
        CHECK(session.client(client).stats().corrections > 0);
        CHECK(session.client(client).stats().desyncs > 0);
        CHECK(session.client(client).stats().rollbacks > 0);
    }
}

void test_ten_minute_prediction() {
    using namespace nsync::net;
    NetConfig config;
    config.latency_ticks = 5;
    config.jitter_ticks = 2;
    config.loss_percent = 0;
    config.duplicate_percent = 0;
    config.input_delay_ticks = 16;
    config.snapshot_interval = 1;
    config.seed = 21;

    std::vector<InputProvider> providers;
    providers.push_back(make_provider(0));
    providers.push_back(make_provider(1));

    LocalSession session;
    std::string error;
    CHECK(session.start(
        nsync::core::Map::default_arena(),
        999,
        2,
        2,
        std::move(providers),
        config,
        10U * 60U * 60U,
        999,
        &error));

    bool exact = true;
    for (int tick = 0; tick < 10 * 60 * 60; ++tick) {
        session.tick();
        if (tick > 40 && (tick % 600) == 0) {
            for (int client = 0; client < 2; ++client) {
                if (session.client(client).world().hash() !=
                    session.server().world().hash()) {
                    exact = false;
                }
            }
        }
    }
    CHECK(exact);
    for (int client = 0; client < 2; ++client) {
        CHECK(session.client(client).world().hash() ==
              session.server().world().hash());
        CHECK(session.client(client).stats().corrections == 0);
        CHECK(session.client(client).stats().desyncs == 0);
    }
}

void test_ten_concurrent_matches() {
    using namespace nsync::net;

    constexpr int match_count = 10;
    constexpr int players = 10;
    constexpr int humans = 2;
    constexpr int ticks = 600;

    std::vector<std::unique_ptr<LocalSession>> sessions;
    sessions.reserve(match_count);
    for (int match = 0; match < match_count; ++match) {
        auto session = std::make_unique<LocalSession>();
        NetConfig config;
        config.latency_ticks = 5;
        config.jitter_ticks = 2;
        config.input_delay_ticks = 16;
        config.snapshot_interval = 1;
        config.seed = static_cast<std::uint64_t>(100 + match);

        std::vector<InputProvider> providers;
        providers.push_back(make_provider(0));
        providers.push_back(make_provider(1));
        std::string error;
        const bool started = session->start(
            nsync::core::Map::default_arena(),
            static_cast<std::uint64_t>(7000 + match),
            humans,
            players,
            std::move(providers),
            config,
            10U * 60U * 60U,
            999,
            &error);
        CHECK(started);
        if (started) {
            sessions.push_back(std::move(session));
        }
    }

    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(sessions.size());
    for (const std::unique_ptr<LocalSession>& session : sessions) {
        LocalSession* value = session.get();
        threads.emplace_back([value, &failures] {
            for (int tick = 0; tick < ticks; ++tick) {
                value->tick();
                if (tick > 60 && (tick % 60) == 0) {
                    for (int client = 0; client < value->client_count();
                         ++client) {
                        if (value->client(client).world().hash() !=
                            value->server().world().hash()) {
                            ++failures;
                        }
                        if (value->client(client).stats().desyncs != 0) {
                            ++failures;
                        }
                    }
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    CHECK(failures.load() == 0);
    for (const std::unique_ptr<LocalSession>& session : sessions) {
        for (int client = 0; client < session->client_count(); ++client) {
            CHECK(session->client(client).world().hash() ==
                  session->server().world().hash());
            CHECK(session->client(client).stats().corrections == 0);
            CHECK(session->client(client).stats().desyncs == 0);
        }
    }
}

void test_action_event_queue() {
    using namespace nsync::net;
    ActionEventQueue queue;
    const ClientActionMask fire =
        client_action_bit(ClientAction::WeaponFire);
    const ClientActionMask reload =
        client_action_bit(ClientAction::WeaponReload);
    CHECK(queue.enqueue(10, 3, fire | reload, fire, 0));
    CHECK(!queue.enqueue(10, 3, fire, fire, 0));
    CHECK(queue.duplicate_packets() == 1);
    CHECK(queue.accepted_packets() == 1);

    CHECK(queue.enqueue(
        12,
        4,
        reload | client_action_bit(ClientAction::ActivateGadget),
        0,
        1));
    CHECK(queue.enqueue(11, 5, fire, 0, 0));
    ActionEventFrame frame;
    CHECK(queue.consume(11, frame));
    CHECK(frame.actions == fire);
    CHECK(!queue.consume(11, frame));
    CHECK(queue.consume(10, frame));
    CHECK((frame.actions & reload) != 0U);
    CHECK((frame.toggles & fire) != 0U);
    CHECK(frame.packets == 1);

    queue.prune_before(12);
    CHECK(!queue.consume(10, frame));
    CHECK(queue.consume(12, frame));
    CHECK(frame.param == 1);
    queue.clear();
    CHECK(queue.pending_frames() == 0);

    const std::uint16_t held_fire =
        static_cast<std::uint16_t>(nsync::core::kButtonFire);
    const std::uint16_t held_left =
        static_cast<std::uint16_t>(nsync::core::kButtonLeft);
    CHECK(synchronize_held_action_buttons(0, held_fire, held_fire) == 0);
    CHECK(synchronize_held_action_buttons(
              held_fire | held_left, 0, held_fire) == held_fire);
    CHECK(synchronize_held_action_buttons(
              held_fire, held_fire | held_left, held_left) == held_fire);
}

void test_server_map_autofollow() {
    using namespace nsync;
    namespace fs = std::filesystem;

    core::Map server_map;
    core::Map client_map;
    std::string error;
    CHECK(core::load_map_file(
        fs::path(NSYNC_SOURCE_DIR) / "assets" / "maps" / "world5.json",
        server_map,
        &error));
    CHECK(core::load_map_file(
        fs::path(NSYNC_SOURCE_DIR) / "assets" / "maps" / "crystal_01.json",
        client_map,
        &error));
    CHECK(server_map.id == "world5");
    CHECK(client_map.id == "crystal_01");

    net::NetServerConfig server_config;
    server_config.port = 0;
    server_config.player_count = 2;
    server_config.game_mode = core::GameMode::Control;
    server_config.lobby_enabled = true;
    server_config.lobby_countdown_ms = 60000;
    server_config.match_end_delay_ms = 1000;
    server_config.match_duration_ticks = 600;

    net::RealServer server;
    CHECK(server.start(server_map, server_config, &error));
    const std::uint16_t port = server.port();
    CHECK(port != 0);

    std::atomic_bool running{true};
    std::thread server_thread([&] {
        const auto start = std::chrono::steady_clock::now();
        while (running.load(std::memory_order_relaxed)) {
            const auto elapsed = std::chrono::duration_cast<
                std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            server.update(static_cast<std::uint32_t>(elapsed.count()));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    net::NetClientConfig client_config;
    client_config.display_name = "auto-follow";
    client_config.login_token = "auto-follow";
    client_config.auto_ready = false;
    client_config.auto_reconnect = false;
    client_config.connect_timeout_ms = 5000;

    net::RealClient client;
    CHECK(client.start(
        "127.0.0.1",
        port,
        client_map,
        client_config,
        0,
        &error));
    CHECK(client.wait_connected(5000, &error));
    CHECK(client.map_id() == "world5");
    CHECK(core::map_fingerprint(client.map()) ==
          core::map_fingerprint(server_map));

    client.stop();
    running.store(false, std::memory_order_relaxed);
    server_thread.join();
    server.stop();
}

} // namespace

bool run_net_tests() {
    g_failures = 0;
    test_rate_limiter();
    test_server_clock();
    test_protocol_roundtrip();
    test_action_event_queue();
    test_sync_timing_formula();
    test_protocol_rejects_invalid_packets();
    test_protocol_fuzz();
    test_server_map_autofollow();
    test_map_fingerprint();
    test_network_simulator();
    test_local_prediction_exact();
    test_local_late_input_rollback();
    test_sync_time_to_step_mapping();
    test_local_snapshot_stats();
    test_local_desync_repair();
    test_local_loss_and_duplicate();
    test_ten_minute_prediction();
    test_ten_concurrent_matches();

    if (g_failures != 0) {
        std::fprintf(stderr, "net tests: %d failure(s)\n", g_failures);
        return false;
    }
    std::printf("net tests: PASS\n");
    return true;
}

