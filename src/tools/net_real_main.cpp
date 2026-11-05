#include "core/content.h"
#include "core/fixed_step.h"
#include "core/input_command.h"
#include "core/map.h"
#include "net/real_client.h"
#include "net/real_server.h"
#include "net/wan_trace.h"
#include "shared/log.h"
#include "shared/paths.h"
#include "shared/timer_resolution.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <array>
#include <filesystem>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

std::uint32_t now_ms() {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return static_cast<std::uint32_t>(
        duration_cast<milliseconds>(steady_clock::now() - start).count());
}

nsync::net::InputProvider make_provider(int slot) {
    if (std::getenv("NSYNC_PROBE_HUMAN") != nullptr) {
        // Human-like input: held directions, short jump presses, fire bursts
        // and mouse aim that changes every tick. Like the GUI client the
        // state is sampled in wall-clock time (not derived from the target
        // tick), so a changing input lead really delays/reorders commands.
        return [slot](std::uint32_t tick) {
            using namespace nsync::core;
            InputCommand command;
            command.tick = tick;
            command.sequence = static_cast<std::uint8_t>(tick & 0xFFU);
            const std::uint32_t s = static_cast<std::uint32_t>(slot);
            const std::uint32_t t = now_ms() * 60U / 1000U;
            const std::uint32_t segment = (t + s * 37U) / 53U;
            const std::uint32_t mix = (segment * 2654435761U) >> 28;
            if (mix % 5U != 0U) {
                command.buttons |= (mix & 1U) != 0U ? kButtonRight : kButtonLeft;
            }
            if ((t + s * 11U) % 71U < 9U) {
                command.buttons |= kButtonJump;
            }
            if ((t + s * 23U) % 100U < 55U) {
                command.buttons |= kButtonFire;
            }
            const double angle = static_cast<double>(t) * 0.03 +
                static_cast<double>(slot);
            command.aim_x = static_cast<std::int16_t>(std::cos(angle) * 32767.0);
            command.aim_y = static_cast<std::int16_t>(std::sin(angle) * 32767.0);
            return command;
        };
    }
    return [slot](std::uint32_t tick) {
        using namespace nsync::core;
        InputCommand command;
        command.tick = tick;
        command.sequence = static_cast<std::uint8_t>(tick & 0xFFU);
        const bool right =
            ((tick / 45U) + static_cast<std::uint32_t>(slot)) % 2U == 0U;
        command.buttons |= right ? kButtonRight : kButtonLeft;
        if (tick % 47U == 0U) {
            command.buttons |= kButtonJump;
        }
        if (tick % 3U != 2U) {
            command.buttons |= kButtonFire;
        }
        command.aim_x = slot % 2 == 0 ? 32767 : -32767;
        if (std::getenv("NSYNC_PROBE_AIM") != nullptr) {
            // Human-like aim: mouse-relative aim changes every tick.
            const double angle = static_cast<double>(tick) * 0.03 +
                static_cast<double>(slot);
            command.aim_x = static_cast<std::int16_t>(std::cos(angle) * 32767.0);
            command.aim_y = static_cast<std::int16_t>(std::sin(angle) * 32767.0);
        }
        return command;
    };
}

bool load_arena(
    const char* map_path,
    nsync::core::Map& map,
    std::string* error) {
    if (map_path == nullptr || *map_path == '\0') {
        map = nsync::core::Map::default_arena();
        return true;
    }
    return nsync::core::load_map_file(
        std::filesystem::path(map_path), map, error);
}

void print_usage() {
    std::printf(
        "usage: nsync_net_real [--loopback] [--client] [--host HOST]\n"
        "                       [--port N] [--ticks N] [--players N] [--humans N]\n"
        "                       [--input-delay N] [--input-sync-model M]\n"
        "                       [--reconnect-at N] [--rtt-ms N]\n"
        "                       [--jitter-ms N] [--loss-percent N] [--burst-length N]\n"
        "                       [--duplicate-percent N]\n"
        "                       [--lobby]\n"
        "                       [--match-ticks N] [--drop-at N] [--resume-at N]\n"
        "                       [--auto-rematch] [--mode tdm|control|crystal] [--map PATH]\n"
        "                       [--client-lag] [--fixed-step-scheduler]\n"
        "                       [--sync-time-to-step-ms N]\n"
        "                       [--log-level LEVEL]\n");
}

bool parse_int(const char* text, int& value) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

int run_client(
    const char* host,
    std::uint16_t port,
    int ticks,
    int input_delay,
    int reconnect_at,
    int rtt_ms,
    int jitter_ms,
    int loss_percent,
    int duplicate_percent,
    int burst_length,
    int spike_ms,
    int spike_interval_ms,
    int spike_duration_ms,
    const std::vector<nsync::net::WanTraceSample>& network_trace,
    const char* map_path,
    bool client_lag,
    bool fixed_step_scheduler,
    bool auto_rematch) {
    nsync::core::Map map;
    std::string map_error;
    if (!load_arena(map_path, map, &map_error)) {
        std::fprintf(stderr, "map load failed: %s\n", map_error.c_str());
        return 1;
    }
    nsync::net::NetClientConfig config;
    config.input_delay_ticks = static_cast<std::uint32_t>(input_delay);
    config.simulated_rtt_ms = static_cast<std::uint32_t>(rtt_ms);
    config.simulated_jitter_ms = static_cast<std::uint32_t>(jitter_ms);
    config.simulated_loss_percent =
        static_cast<std::uint32_t>(loss_percent);
    config.simulated_duplicate_percent =
        static_cast<std::uint32_t>(duplicate_percent);
    config.simulated_burst_length =
        static_cast<std::uint32_t>(burst_length);
    config.simulated_spike_ms = static_cast<std::uint32_t>(spike_ms);
    config.simulated_spike_interval_ms =
        static_cast<std::uint32_t>(spike_interval_ms);
    config.simulated_spike_duration_ms =
        static_cast<std::uint32_t>(spike_duration_ms);
    config.network_trace = network_trace;
    config.client_step_lag = client_lag;
    config.fixed_step_scheduler = fixed_step_scheduler;
    config.auto_rematch = auto_rematch;
    nsync::net::RealClient client;
    std::string error;
    if (!client.start(host, port, map, config, now_ms(), &error)) {
        std::fprintf(stderr, "client start failed: %s\n", error.c_str());
        return 1;
    }
    if (!client.wait_connected(5000, &error)) {
        std::fprintf(
            stderr,
            "client connect failed: %s (map=%s)\n",
            error.c_str(),
            map_path != nullptr ? map_path : "<default>");
        return 1;
    }

    const auto provider = make_provider(static_cast<int>(client.slot()));
    const std::uint32_t match_wait = now_ms();
    while (!client.in_match() && now_ms() - match_wait < 5000) {
        client.tick(nullptr, now_ms());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    nsync::core::FixedStepClock clock(60, 5);
    clock.reset(nsync::core::FixedStepClock::Clock::now());
    int steps = 0;
    while (steps < ticks) {
        const std::uint32_t now = now_ms();
        const auto advance = clock.advance(
            nsync::core::FixedStepClock::Clock::now());
        for (int step = 0; step < advance.steps && steps < ticks; ++step) {
            client.tick(provider, now);
            ++steps;
            if (reconnect_at > 0 && steps == reconnect_at) {
                client.reconnect(now);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto& stats = client.stats();
    const auto transport = client.transport_stats();
    const bool connected = client.connected();
    std::printf(
        "client slot=%u phase=%u match=%u tick=%u ack=%u corrections=%u rollbacks=%u desyncs=%u sm=%u smm=%u smp=%u sma=%u sr=%u erb=%u cov=%u rbt=%llu rtt=%ums sts=%d elag=%u rttc=%u est=%u steps=%u skips=%u debt=%u target=%u lost=%llu dup=%llu spike=%llu trace=%llu retx=%u ended=%d\n",
        static_cast<unsigned>(stats.slot),
        static_cast<unsigned>(stats.phase),
        stats.match_id,
        stats.predicted_tick,
        stats.ack_tick,
        stats.corrections,
        stats.rollbacks,
        stats.desyncs,
        stats.snapshot_mismatches,
        stats.snapshot_missing_predictions,
        stats.snapshot_periodic_mismatches,
        stats.snapshot_authoritative_mismatches,
        stats.snapshot_restores,
        stats.event_rollbacks,
        stats.covered_inputs,
        static_cast<unsigned long long>(stats.total_rollback_ticks),
        stats.rtt_ms,
        static_cast<int>(stats.sync_time_to_step_ms),
        static_cast<unsigned>(stats.effective_server_lag_ticks),
        static_cast<unsigned>(stats.rtt_compensation_ticks),
        stats.estimated_server_tick,
        static_cast<unsigned>(stats.scheduler_steps),
        static_cast<unsigned>(stats.scheduler_skips),
        static_cast<unsigned>(stats.scheduler_debt_ticks),
        static_cast<unsigned>(stats.scheduler_target_tick),
        static_cast<unsigned long long>(transport.packets_dropped),
        static_cast<unsigned long long>(transport.packets_duplicated),
        static_cast<unsigned long long>(transport.packets_delayed_spike),
        static_cast<unsigned long long>(transport.packets_traced),
        transport.retransmissions,
        client.match_end().match_id != 0 ? 1 : 0);
    client.stop();
    return connected ? 0 : 1;
}

int run_loopback(
    int ticks,
    int players,
    int humans,
    int input_delay,
    int rtt_ms,
    bool original_sync,
    bool lobby,
    int match_ticks,
    int drop_at,
    int resume_at,
    bool auto_rematch,
    nsync::core::GameMode mode,
    const char* map_path,
    bool client_lag,
    bool fixed_step_scheduler,
    int sync_time_to_step_ms,
    const std::vector<nsync::net::WanTraceSample>& network_trace) {
    using namespace nsync::net;
    nsync::core::Map map;
    std::string map_error;
    if (!load_arena(map_path, map, &map_error)) {
        std::fprintf(stderr, "map load failed: %s\n", map_error.c_str());
        return 1;
    }
    if (mode == nsync::core::GameMode::Control) {
        map.objective = nsync::core::AABB{
            nsync::core::Vec2{
                nsync::core::Fix32::from_int(-2),
                nsync::core::Fix32::from_int(9)},
            nsync::core::Vec2{
                nsync::core::Fix32::from_int(2),
                nsync::core::Fix32::from_int(11)}};
    }

    NetServerConfig server_config;
    server_config.game_mode = mode;
    server_config.port = 0;
    server_config.player_count = players;
    server_config.seed = 12345;
    server_config.snapshot_interval = 3;
    server_config.original_input_sync = original_sync;
    server_config.sync_time_to_step_ms =
        static_cast<std::int16_t>(sync_time_to_step_ms);
    server_config.late_input_replay =
        client_lag || std::getenv("NSYNC_PROBE_SERVER_REPLAY") != nullptr;
    server_config.lobby_enabled = lobby;
    server_config.lobby_countdown_ms = 300;
    server_config.match_end_delay_ms = 500;
    if (match_ticks > 0) {
        server_config.match_duration_ticks =
            static_cast<std::uint32_t>(match_ticks);
    }
    RealServer server;
    std::string error;
    if (!server.start(map, server_config, &error)) {
        std::fprintf(stderr, "server start failed: %s\n", error.c_str());
        return 1;
    }

    NetClientConfig client_config;
    client_config.input_delay_ticks = static_cast<std::uint32_t>(input_delay);
    client_config.simulated_rtt_ms = static_cast<std::uint32_t>(rtt_ms);
    client_config.network_trace = network_trace;
    client_config.client_step_lag = client_lag;
    client_config.fixed_step_scheduler = fixed_step_scheduler;
    client_config.auto_ready = true;
    client_config.auto_rematch = auto_rematch;
    std::vector<RealClient> clients(static_cast<std::size_t>(humans));
    std::vector<InputProvider> providers;
    for (int slot = 0; slot < humans; ++slot) {
        if (!clients[static_cast<std::size_t>(slot)].start(
                "127.0.0.1",
                server.port(),
                map,
                client_config,
                now_ms(),
                &error)) {
            std::fprintf(stderr, "client start failed: %s\n", error.c_str());
            server.stop();
            return 1;
        }
        providers.push_back(make_provider(slot));
    }

    const std::uint32_t handshake_start = now_ms();
    while (now_ms() - handshake_start < 5000) {
        const std::uint32_t now = now_ms();
        server.update(now);
        bool all_connected = true;
        for (RealClient& client : clients) {
            client.tick(nullptr, now);
            all_connected = all_connected && client.connected();
        }
        if (all_connected) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (RealClient& client : clients) {
        if (!client.connected()) {
            std::fprintf(
                stderr,
                "client did not connect: %s\n",
                client.last_error().c_str());
            server.stop();
            return 1;
        }
    }
    providers.clear();
    for (const RealClient& client : clients) {
        providers.push_back(make_provider(static_cast<int>(client.slot())));
    }

    if (lobby) {
        const std::uint32_t match_wait = now_ms();
        while (now_ms() - match_wait < 5000) {
            const std::uint32_t now = now_ms();
            server.update(now);
            bool all_in_match = true;
            for (RealClient& client : clients) {
                client.tick(nullptr, now);
                all_in_match = all_in_match && client.in_match();
            }
            if (all_in_match) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    std::printf(
        "loopback: players=%d humans=%d ticks=%d rtt=%dms lobby=%d port=%u match=%u map=%s\n",
        players,
        humans,
        ticks,
        rtt_ms,
        lobby ? 1 : 0,
        static_cast<unsigned>(server.port()),
        server.match_id(),
        map.id.c_str());

    const std::uint32_t measure_start_ms = now_ms();
    const TransportStats server_transport_before = server.transport_stats();
    nsync::core::FixedStepClock clock(60, 5);
    clock.reset(nsync::core::FixedStepClock::Clock::now());
    // NSYNC_PROBE_LAG_SLOT1=N: client 1 runs every frame N ms late (as if
    // its process were descheduled / its window throttled), so its inputs
    // reach the server after their target tick.
    const int probe_lag_ms = std::getenv("NSYNC_PROBE_LAG_SLOT1") != nullptr
        ? std::atoi(std::getenv("NSYNC_PROBE_LAG_SLOT1"))
        : 0;
    std::deque<std::uint32_t> lagged_steps;
    int steps = 0;
    // Smoothness probe (client 0): for the local and the first remote player
    // compare the client's simulated position at tick T with the server's
    // position at T. A smooth view keeps that error constant between frames;
    // a "jump" is a frame-to-frame change of the error (rollback pop,
    // snapshot correction, stale input on the server ...). "smoothed" mirrors
    // the renderer's correction blending.
    struct Probe {
        std::uint64_t frames = 0;
        std::uint64_t jumps = 0;
        std::uint64_t missing = 0;
        float max_jump = 0.0f;
        double sum_jump = 0.0;
        double sum_abs_err = 0.0;
        std::uint64_t smooth_jumps = 0;
        float smooth_max_jump = 0.0f;
    };
    std::array<Probe, 2> probes{};
    std::array<std::uint64_t, 5> advance_hist{};
    std::array<std::map<std::uint32_t, std::pair<float, float>>, 2> server_track;
    std::uint64_t probe_tick_us_total = 0;
    std::uint64_t probe_tick_us_max = 0;
    std::uint64_t probe_slow_ticks = 0;
    const auto to_f = [](nsync::core::Fix32 value) {
        return static_cast<float>(value.raw()) /
            static_cast<float>(nsync::core::Fix32::kOne);
    };
    const auto probe_slot = [&](int which) -> std::uint8_t {
        const std::uint8_t local_slot = clients[0].slot();
        if (which == 0) {
            return local_slot;
        }
        return local_slot == 0 ? 1U : 0U;
    };
    const auto record_server = [&]() {
        if (humans < 2) {
            return;
        }
        for (int which = 0; which < 2; ++which) {
            const std::uint8_t slot = probe_slot(which);
            server.world().players().for_each(
                [&](const nsync::core::Player& player) {
                    if (player.slot == slot && player.alive != 0) {
                        server_track[static_cast<std::size_t>(which)]
                                    [server.world().tick()] = {
                            to_f(player.position.x), to_f(player.position.y)};
                    }
                });
        }
    };
    struct ClientSample {
        std::uint32_t tick = 0;
        float x = 0.0f;
        float y = 0.0f;
        float sx = 0.0f;
        float sy = 0.0f;
        bool valid = false;
    };
    std::array<std::vector<ClientSample>, 2> client_samples;
    std::array<float, 2> smooth_x{};
    std::array<float, 2> smooth_y{};
    const auto sample_remote = [&]() {
        if (humans < 2 || !clients[0].in_match()) {
            return;
        }
        const nsync::core::World& view = clients[0].world();
        const std::vector<nsync::net::LocalClient::VisualCorrection>
            corrections = clients[0].take_visual_corrections();
        const float decay = std::exp(-12.0f / 60.0f);
        for (int which = 0; which < 2; ++which) {
            const std::size_t index = static_cast<std::size_t>(which);
            const std::uint8_t slot = probe_slot(which);
            ClientSample sample;
            sample.tick = view.tick();
            nsync::core::EntityId id;
            view.players().for_each([&](const nsync::core::Player& player) {
                if (player.slot == slot && player.alive != 0) {
                    sample.x = to_f(player.position.x);
                    sample.y = to_f(player.position.y);
                    sample.valid = true;
                    id = player.id;
                }
            });
            smooth_x[index] *= decay;
            smooth_y[index] *= decay;
            for (const auto& correction : corrections) {
                if (correction.entity == id) {
                    smooth_x[index] += to_f(correction.offset.x);
                    smooth_y[index] += to_f(correction.offset.y);
                }
            }
            sample.sx = sample.x + smooth_x[index];
            sample.sy = sample.y + smooth_y[index];
            client_samples[index].push_back(sample);
        }
    };
    const auto evaluate_probe = [&]() {
        for (int which = 0; which < 2; ++which) {
            const std::size_t index = static_cast<std::size_t>(which);
            Probe& probe = probes[index];
            bool has_prev = false;
            float prev_err_x = 0.0f;
            float prev_err_y = 0.0f;
            float prev_serr_x = 0.0f;
            float prev_serr_y = 0.0f;
            std::uint32_t prev_tick = 0;
            for (const ClientSample& sample : client_samples[index]) {
                const auto truth = server_track[index].find(sample.tick);
                if (!sample.valid || truth == server_track[index].end()) {
                    if (sample.valid) {
                        ++probe.missing;
                    }
                    has_prev = false;
                    continue;
                }
                const float err_x = sample.x - truth->second.first;
                const float err_y = sample.y - truth->second.second;
                const float serr_x = sample.sx - truth->second.first;
                const float serr_y = sample.sy - truth->second.second;
                probe.sum_abs_err += std::sqrt(err_x * err_x + err_y * err_y);
                if (has_prev) {
                    ++probe.frames;
                    if (which == 1) {
                        const std::uint32_t advanced = sample.tick > prev_tick
                            ? sample.tick - prev_tick
                            : 0U;
                        ++advance_hist[std::min<std::uint32_t>(advanced, 4U)];
                    }
                    const float jx = err_x - prev_err_x;
                    const float jy = err_y - prev_err_y;
                    const float jump = std::sqrt(jx * jx + jy * jy);
                    if (jump > 0.05f) {
                        ++probe.jumps;
                        probe.max_jump = std::max(probe.max_jump, jump);
                        probe.sum_jump += jump;
                    }
                    const float sjx = serr_x - prev_serr_x;
                    const float sjy = serr_y - prev_serr_y;
                    const float sjump = std::sqrt(sjx * sjx + sjy * sjy);
                    if (sjump > 0.05f) {
                        ++probe.smooth_jumps;
                    }
                    probe.smooth_max_jump =
                        std::max(probe.smooth_max_jump, sjump);
                }
                prev_err_x = err_x;
                prev_err_y = err_y;
                prev_serr_x = serr_x;
                prev_serr_y = serr_y;
                prev_tick = sample.tick;
                has_prev = true;
            }
        }
    };
    while (steps < ticks) {
        const std::uint32_t now = now_ms();
        server.update(now);
        record_server();
        const auto advance = clock.advance(
            nsync::core::FixedStepClock::Clock::now());
        for (int step = 0; step < advance.steps && steps < ticks; ++step) {
            for (int slot = 0; slot < humans; ++slot) {
                if (slot == 1 && probe_lag_ms > 0) {
                    continue;
                }
                const auto tick_start = std::chrono::steady_clock::now();
                clients[static_cast<std::size_t>(slot)].tick(
                    providers[static_cast<std::size_t>(slot)], now);
                if (slot == 0) {
                    const auto us = std::chrono::duration_cast<
                        std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - tick_start).count();
                    probe_tick_us_total += static_cast<std::uint64_t>(us);
                    probe_tick_us_max = std::max<std::uint64_t>(
                        probe_tick_us_max, static_cast<std::uint64_t>(us));
                    if (us > 4000) {
                        ++probe_slow_ticks;
                    }
                }
            }
            sample_remote();
            if (probe_lag_ms > 0 && humans > 1) {
                lagged_steps.push_back(now_ms());
            }
            ++steps;
            if (!clients.empty() && drop_at > 0 && steps == drop_at) {
                clients[0].leave();
            }
            if (!clients.empty() && resume_at > 0 && steps == resume_at) {
                clients[0].reconnect(now);
            }
        }
        if (probe_lag_ms > 0 && humans > 1) {
            // Like an unfocused / hitching GUI window: every 700ms client 1
            // freezes for probe_lag_ms, then FixedStepClock catches up with
            // at most 5 steps in one frame and drops the rest.
            const std::uint32_t current = now_ms();
            const bool stalled = (current % 700U) <
                static_cast<std::uint32_t>(probe_lag_ms);
            if (!stalled && !lagged_steps.empty()) {
                const std::size_t run =
                    std::min<std::size_t>(lagged_steps.size(), 5U);
                for (std::size_t i = 0; i < run; ++i) {
                    clients[1].tick(providers[1], current);
                }
                lagged_steps.clear();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    server.update(now_ms());

    const std::uint32_t elapsed_ms =
        std::max<std::uint32_t>(1U, now_ms() - measure_start_ms);
    const TransportStats server_transport_after =
        server.transport_stats();
    const std::uint64_t server_tx_bytes =
        server_transport_after.bytes_sent -
        server_transport_before.bytes_sent;

    if (humans >= 2) {
        evaluate_probe();
        for (int which = 0; which < 2; ++which) {
            const Probe& probe = probes[static_cast<std::size_t>(which)];
            std::printf(
                "%s frames=%llu jumps=%llu missing=%llu max_jump=%.3f avg_jump=%.3f avg_err=%.3f smoothed_jumps=%llu smoothed_max=%.3f\n",
                which == 0 ? "local_view " : "remote_view",
                static_cast<unsigned long long>(probe.frames),
                static_cast<unsigned long long>(probe.jumps),
                static_cast<unsigned long long>(probe.missing),
                probe.max_jump,
                probe.jumps > 0
                    ? probe.sum_jump / static_cast<double>(probe.jumps)
                    : 0.0,
                probe.frames > 0
                    ? probe.sum_abs_err / static_cast<double>(probe.frames)
                    : 0.0,
                static_cast<unsigned long long>(probe.smooth_jumps),
                probe.smooth_max_jump);
        }
        std::printf(
            "client0_steps adv0=%llu adv1=%llu adv2=%llu adv3=%llu adv4+=%llu\n",
            static_cast<unsigned long long>(advance_hist[0]),
            static_cast<unsigned long long>(advance_hist[1]),
            static_cast<unsigned long long>(advance_hist[2]),
            static_cast<unsigned long long>(advance_hist[3]),
            static_cast<unsigned long long>(advance_hist[4]));
        std::printf(
            "client0_tick_us avg=%llu max=%llu slow(>4ms)=%llu remote_corr=%u covered=%u\n",
            static_cast<unsigned long long>(
                steps > 0 ? probe_tick_us_total /
                        static_cast<std::uint64_t>(steps)
                          : 0),
            static_cast<unsigned long long>(probe_tick_us_max),
            static_cast<unsigned long long>(probe_slow_ticks),
            clients[0].stats().remote_snapshot_corrections,
            clients[0].stats().covered_inputs);
    }

    bool ok = true;
    for (int slot = 0; slot < humans; ++slot) {
        const RealClient& client = clients[static_cast<std::size_t>(slot)];
        const auto& stats = client.stats();
        const auto& transport = client.transport_stats();
        std::printf(
            "client%d slot=%u phase=%u match=%u tick=%u ack=%u corrections=%u rollbacks=%u desyncs=%u sm=%u smm=%u smp=%u sma=%u sr=%u erb=%u cov=%u rbt=%llu rtt=%ums sts=%d slag=%u elag=%u rttc=%u est=%u steps=%u skips=%u debt=%u target=%u celag=%u lead=%u queued=%u aligned=%d ended=%d snapshots=%llu snap_avg=%lluB rx=%lluB\n",
            slot,
            static_cast<unsigned>(stats.slot),
            static_cast<unsigned>(stats.phase),
            stats.match_id,
            stats.predicted_tick,
            stats.ack_tick,
            stats.corrections,
            stats.rollbacks,
            stats.desyncs,
            stats.snapshot_mismatches,
            stats.snapshot_missing_predictions,
            stats.snapshot_periodic_mismatches,
            stats.snapshot_authoritative_mismatches,
            stats.snapshot_restores,
            stats.event_rollbacks,
            stats.covered_inputs,
            static_cast<unsigned long long>(stats.total_rollback_ticks),
            stats.rtt_ms,
            static_cast<int>(stats.sync_time_to_step_ms),
            static_cast<unsigned>(stats.server_lag_ticks),
            static_cast<unsigned>(stats.effective_server_lag_ticks),
            static_cast<unsigned>(stats.rtt_compensation_ticks),
            stats.estimated_server_tick,
            static_cast<unsigned>(stats.scheduler_steps),
            static_cast<unsigned>(stats.scheduler_skips),
            static_cast<unsigned>(stats.scheduler_debt_ticks),
            static_cast<unsigned>(stats.scheduler_target_tick),
            static_cast<unsigned>(stats.client_event_lag_ticks),
            static_cast<unsigned>(stats.input_lead_ticks),
            static_cast<unsigned>(stats.snapshots_queued),
            client.aligned() ? 1 : 0,
            client.match_end().match_id != 0 ? 1 : 0,
            static_cast<unsigned long long>(stats.snapshot_packets),
            static_cast<unsigned long long>(
                stats.snapshot_packets > 0
                    ? stats.snapshot_bytes / stats.snapshot_packets
                    : 0),
            static_cast<unsigned long long>(transport.bytes_received));
        ok = ok && client.state() == NetClientState::Connected;
        ok = ok && stats.ack_tick > 0;
        if (drop_at <= 0) {
            ok = ok && stats.corrections == 0;
            ok = ok && stats.desyncs == 0;
        }
        if (sync_time_to_step_ms != 0) {
            ok = ok && stats.sync_time_to_step_ms ==
                sync_time_to_step_ms;
            ok = ok && stats.effective_server_lag_ticks ==
                effective_server_lag_ticks(
                    server_config.server_lag_ticks,
                    static_cast<std::int16_t>(sync_time_to_step_ms),
                    60);
        }
        if (original_sync) {
            const std::uint32_t expected_lead = client_lag
                ? static_cast<std::uint32_t>(
                      server_config.client_event_lag_ticks)
                : std::max(
                      static_cast<std::uint32_t>(
                          server_config.client_event_lag_ticks),
                      rtt_compensation_ticks(stats.rtt_ms, 60) + 4U);
            ok = ok && stats.server_lag_ticks ==
                server_config.server_lag_ticks;
            ok = ok && stats.client_event_lag_ticks ==
                server_config.client_event_lag_ticks;
            ok = ok && stats.input_lead_ticks >= expected_lead;
            ok = ok && stats.input_lead_ticks <= 60U;
            ok = ok && client.aligned();
            if (!lobby && rtt_ms == 0) {
                const std::uint32_t lag_slack =
                    client_lag
                        ? static_cast<std::uint32_t>(stats.server_lag_ticks) +
                            4U
                        : 8U;
                ok = ok && client.world().tick() + lag_slack >=
                    server.tick();
            }
        }
    }
    if (mode != nsync::core::GameMode::Tdm) {
        ok = ok && server.world().game_mode() == mode;
        for (int slot = 0; slot < humans; ++slot) {
            ok = ok &&
                clients[static_cast<std::size_t>(slot)].world().game_mode() ==
                    mode;
        }
    }
    if (lobby) {
        ok = ok && server.stats().matches_started > 0;
        ok = ok && server.world().players().size() ==
            static_cast<std::size_t>(players);
        ok = ok && server.stats().matches_ended > 0;
        if (drop_at > 0) {
            ok = ok && server.stats().bot_takeovers > 0;
        }
        if (!auto_rematch && match_ticks > 0 && ticks >= match_ticks) {
            for (const RealClient& client : clients) {
                ok = ok && client.match_end().match_id != 0;
            }
        }
    }
    const auto& server_stats = server.stats();
    if (humans > 0) {
        ok = ok && server_stats.action_packets > 0U;
        ok = ok && server_stats.action_events > 0U;
        ok = ok && server_stats.action_event_frames > 0U;
        ok = ok && server_stats.action_events_applied > 0U;
        if (!client_lag) {
            ok = ok && server_stats.action_driven_inputs > 0U;
        } else {
            ok = ok &&
                (server_stats.late_action_driven_inputs > 0U ||
                 server_stats.action_driven_inputs > 0U);
        }
    }
    const std::uint64_t snapshot_avg_bytes =
        server_stats.snapshots_sent > 0
            ? server_stats.snapshot_bytes / server_stats.snapshots_sent
            : 0;
    const std::uint64_t snapshot_gen_avg_us =
        server_stats.snapshot_broadcasts > 0
            ? server_stats.snapshot_generation_us_total /
                server_stats.snapshot_broadcasts
            : 0;
    const std::uint64_t snapshot_avg_us =
        server_stats.snapshot_broadcasts > 0
            ? server_stats.snapshot_broadcast_us_total /
                server_stats.snapshot_broadcasts
            : 0;
    const double snapshot_client_kb_s =
        humans > 0
            ? static_cast<double>(server_stats.snapshot_bytes) * 1000.0 /
                (static_cast<double>(elapsed_ms) *
                 static_cast<double>(humans) *
                 1024.0)
            : 0.0;
    std::printf(
        "server tick=%u clients=%d inputs=%llu rejected=%llu late_replays=%llu late_replay_batches=%llu late_replay_failures=%llu late_clamped=%llu snapshots=%llu matches=%llu ended=%llu bots=%llu\n",
        server.tick(),
        server.connected_clients(),
        static_cast<unsigned long long>(server_stats.input_packets),
        static_cast<unsigned long long>(server_stats.rejected_inputs),
        static_cast<unsigned long long>(server_stats.late_replays),
        static_cast<unsigned long long>(server_stats.late_replay_batches),
        static_cast<unsigned long long>(server_stats.late_replay_failures),
        static_cast<unsigned long long>(server_stats.late_inputs),
        static_cast<unsigned long long>(server_stats.snapshots_sent),
        static_cast<unsigned long long>(server_stats.matches_started),
        static_cast<unsigned long long>(server_stats.matches_ended),
        static_cast<unsigned long long>(server_stats.bot_takeovers));
    std::printf(
        "server rejects post_match=%llu phase=%llu malformed=%llu slot=%llu future=%llu late=%llu\n",
        static_cast<unsigned long long>(
            server_stats.rejected_post_match_inputs),
        static_cast<unsigned long long>(server_stats.rejected_phase_inputs),
        static_cast<unsigned long long>(
            server_stats.rejected_malformed_inputs),
        static_cast<unsigned long long>(server_stats.rejected_slot_inputs),
        static_cast<unsigned long long>(server_stats.rejected_future_inputs),
        static_cast<unsigned long long>(server_stats.rejected_late_inputs));
    std::printf(
        "server actions packets=%llu events=%llu toggles=%llu "
        "duplicates=%llu frames=%llu applied=%llu mismatches=%llu "
        "driven=%llu fallback=%llu late_driven=%llu late_fallback=%llu\n",
        static_cast<unsigned long long>(server_stats.action_packets),
        static_cast<unsigned long long>(server_stats.action_events),
        static_cast<unsigned long long>(
            server_stats.action_toggle_events),
        static_cast<unsigned long long>(
            server_stats.action_duplicate_packets),
        static_cast<unsigned long long>(server_stats.action_event_frames),
        static_cast<unsigned long long>(server_stats.action_events_applied),
        static_cast<unsigned long long>(server_stats.action_mismatches),
        static_cast<unsigned long long>(server_stats.action_driven_inputs),
        static_cast<unsigned long long>(server_stats.action_fallback_inputs),
        static_cast<unsigned long long>(
            server_stats.late_action_driven_inputs),
        static_cast<unsigned long long>(
            server_stats.late_action_fallback_inputs));
    std::printf(
        "server acks mismatches=%llu rewritten=%llu\n",
        static_cast<unsigned long long>(server_stats.ack_mismatches),
        static_cast<unsigned long long>(
            server_stats.rewritten_ack_mismatches));
    std::printf(
        "server replay_fail base=%llu window=%llu restore=%llu tick=%llu\n",
        static_cast<unsigned long long>(
            server_stats.late_replay_failure_base),
        static_cast<unsigned long long>(
            server_stats.late_replay_failure_window),
        static_cast<unsigned long long>(
            server_stats.late_replay_failure_restore),
        static_cast<unsigned long long>(
            server_stats.late_replay_failure_tick));
    std::printf(
        "server perf snap_avg=%lluB snap_gen_us_avg=%llu snap_gen_us_max=%u snap_us_avg=%llu snap_us_max=%u tx=%lluB elapsed=%ums snap_client_kb_s=%.2f\n",
        static_cast<unsigned long long>(snapshot_avg_bytes),
        static_cast<unsigned long long>(snapshot_gen_avg_us),
        server_stats.snapshot_generation_us_max,
        static_cast<unsigned long long>(snapshot_avg_us),
        server_stats.snapshot_broadcast_us_max,
        static_cast<unsigned long long>(server_tx_bytes),
        elapsed_ms,
        snapshot_client_kb_s);
    for (RealClient& client : clients) {
        client.stop();
    }
    server.stop();
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    const nsync::shared::HighResolutionTimer timer_resolution;
    bool loopback = false;
    bool client_mode = false;
    const char* host = "127.0.0.1";
    int port = nsync::net::kDefaultPort;
    int ticks = 600;
    int players = 2;
    int humans = 0;
    int input_delay = 16;
    bool original_sync = true;
    int reconnect_at = -1;
    int match_ticks = 0;
    int drop_at = 0;
    int resume_at = 0;
    int rtt_ms = 0;
    int jitter_ms = 0;
    int loss_percent = 0;
    int duplicate_percent = 0;
    int burst_length = 1;
    int spike_ms = 0;
    int spike_interval_ms = 0;
    int spike_duration_ms = 0;
    bool lobby = false;
    bool auto_rematch = false;
    nsync::core::GameMode mode = nsync::core::GameMode::Tdm;
    bool client_lag = false;
    bool fixed_step_scheduler = false;
    int sync_time_to_step_ms = 0;
    const char* map_path = nullptr;
    std::string trace_path;

    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--help") == 0 ||
            std::strcmp(argv[index], "-h") == 0) {
            print_usage();
            return 0;
        }
        if (std::strcmp(argv[index], "--loopback") == 0) {
            loopback = true;
            continue;
        }
        if (std::strcmp(argv[index], "--client") == 0) {
            client_mode = true;
            continue;
        }
        if (std::strcmp(argv[index], "--host") == 0 && index + 1 < argc) {
            host = argv[++index];
            continue;
        }
        if (std::strcmp(argv[index], "--port") == 0 && index + 1 < argc) {
            if (!parse_int(argv[++index], port)) {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--ticks") == 0 && index + 1 < argc) {
            if (!parse_int(argv[++index], ticks)) {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--players") == 0 && index + 1 < argc) {
            if (!parse_int(argv[++index], players)) {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--humans") == 0 && index + 1 < argc) {
            if (!parse_int(argv[++index], humans)) {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--lobby") == 0) {
            lobby = true;
            continue;
        }
        if (std::strcmp(argv[index], "--match-ticks") == 0 && index + 1 < argc) {
            if (!parse_int(argv[++index], match_ticks)) {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--drop-at") == 0 && index + 1 < argc) {
            if (!parse_int(argv[++index], drop_at)) {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--resume-at") == 0 && index + 1 < argc) {
            if (!parse_int(argv[++index], resume_at)) {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--auto-rematch") == 0) {
            auto_rematch = true;
            continue;
        }
        if (std::strcmp(argv[index], "--map") == 0 && index + 1 < argc) {
            map_path = argv[++index];
            continue;
        }
        if (std::strcmp(argv[index], "--client-lag") == 0) {
            client_lag = true;
            continue;
        }
        if (std::strcmp(argv[index], "--fixed-step-scheduler") == 0) {
            fixed_step_scheduler = true;
            continue;
        }
        if (std::strcmp(argv[index], "--sync-time-to-step-ms") == 0 &&
            index + 1 < argc) {
            if (!parse_int(argv[++index], sync_time_to_step_ms)) {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--mode") == 0 && index + 1 < argc) {
            const char* value = argv[++index];
            if (std::strcmp(value, "control") == 0) {
                mode = nsync::core::GameMode::Control;
            } else if (std::strcmp(value, "crystal") == 0) {
                mode = nsync::core::GameMode::Crystal;
            } else if (std::strcmp(value, "tdm") == 0) {
                mode = nsync::core::GameMode::Tdm;
            } else {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--input-delay") == 0 && index + 1 < argc) {
            if (!parse_int(argv[++index], input_delay)) {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--input-sync-model") == 0 &&
            index + 1 < argc) {
            const char* model = argv[++index];
            if (std::strcmp(model, "original") == 0) {
                original_sync = true;
            } else if (std::strcmp(model, "legacy") == 0) {
                original_sync = false;
            } else {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--reconnect-at") == 0 && index + 1 < argc) {
            if (!parse_int(argv[++index], reconnect_at)) {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--rtt-ms") == 0 && index + 1 < argc) {
            if (!parse_int(argv[++index], rtt_ms)) {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--jitter-ms") == 0 && index + 1 < argc) {
            if (!parse_int(argv[++index], jitter_ms)) {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--loss-percent") == 0 && index + 1 < argc) {
            if (!parse_int(argv[++index], loss_percent)) {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--duplicate-percent") == 0 &&
            index + 1 < argc) {
            if (!parse_int(argv[++index], duplicate_percent)) {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--burst-length") == 0 &&
            index + 1 < argc) {
            if (!parse_int(argv[++index], burst_length)) {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--spike-ms") == 0 && index + 1 < argc) {
            if (!parse_int(argv[++index], spike_ms)) {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--spike-interval-ms") == 0 &&
            index + 1 < argc) {
            if (!parse_int(argv[++index], spike_interval_ms)) {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--spike-duration-ms") == 0 &&
            index + 1 < argc) {
            if (!parse_int(argv[++index], spike_duration_ms)) {
                print_usage();
                return 2;
            }
            continue;
        }
        if (std::strcmp(argv[index], "--trace-file") == 0 &&
            index + 1 < argc) {
            trace_path = argv[++index];
            continue;
        }
        if (std::strcmp(argv[index], "--log-level") == 0 && index + 1 < argc) {
            nsync::shared::LogLevel level = nsync::shared::LogLevel::Info;
            if (!nsync::shared::parse_log_level(argv[++index], level)) {
                print_usage();
                return 2;
            }
            nsync::shared::set_log_level(level);
            continue;
        }
        print_usage();
        return 2;
    }

    if (ticks < 1) {
        std::fprintf(stderr, "invalid --ticks\n");
        return 2;
    }
    if (players < 1 || players > nsync::core::kMaxPlayers) {
        std::fprintf(stderr, "invalid --players\n");
        return 2;
    }
    if (humans == 0) {
        humans = players;
    }
    if (humans < 1 || humans > players) {
        std::fprintf(stderr, "invalid --humans\n");
        return 2;
    }
    if (input_delay < 2 || input_delay > 60) {
        std::fprintf(stderr, "invalid --input-delay\n");
        return 2;
    }
    if (rtt_ms < 0 || rtt_ms > 1000) {
        std::fprintf(stderr, "invalid --rtt-ms\n");
        return 2;
    }
    if (jitter_ms < 0 || jitter_ms > 1000 ||
        loss_percent < 0 || loss_percent > 100 ||
        duplicate_percent < 0 || duplicate_percent > 100 ||
        burst_length < 1 || burst_length > 256 ||
        spike_ms < 0 || spike_ms > 5000 ||
        spike_interval_ms < 0 || spike_interval_ms > 60000 ||
        spike_duration_ms < 0 || spike_duration_ms > spike_interval_ms) {
        std::fprintf(stderr, "invalid WAN simulation options\n");
        return 2;
    }
    if (spike_ms > 0 &&
        (spike_interval_ms == 0 || spike_duration_ms == 0)) {
        std::fprintf(stderr, "invalid latency spike options\n");
        return 2;
    }
    std::vector<nsync::net::WanTraceSample> network_trace;
    if (!trace_path.empty()) {
        if (rtt_ms != 0 || jitter_ms != 0 || loss_percent != 0 ||
            duplicate_percent != 0 || burst_length != 1 ||
            spike_ms != 0 || spike_interval_ms != 0 ||
            spike_duration_ms != 0) {
            std::fprintf(
                stderr,
                "--trace-file cannot be combined with other WAN options\n");
            return 2;
        }
        std::string trace_error;
        if (!nsync::net::load_wan_trace(
                trace_path, network_trace, &trace_error)) {
            std::fprintf(stderr, "%s\n", trace_error.c_str());
            return 2;
        }
    }
    if (sync_time_to_step_ms < -2000 || sync_time_to_step_ms > 2000) {
        std::fprintf(stderr, "invalid --sync-time-to-step-ms\n");
        return 2;
    }
    if (match_ticks < 0) {
        std::fprintf(stderr, "invalid --match-ticks\n");
        return 2;
    }
    if (drop_at < 0 || resume_at < 0 ||
        (resume_at > 0 && resume_at <= drop_at)) {
        std::fprintf(stderr, "invalid --drop-at/--resume-at\n");
        return 2;
    }

    if (loopback) {
        return run_loopback(
            ticks,
            players,
            humans,
            input_delay,
            rtt_ms,
            original_sync,
            lobby,
            match_ticks,
            drop_at,
            resume_at,
            auto_rematch,
            mode,
            map_path,
            client_lag,
            fixed_step_scheduler,
            sync_time_to_step_ms,
            network_trace);
    }
    if (client_mode) {
        if (port < 1 || port > 65535) {
            std::fprintf(stderr, "invalid --port\n");
            return 2;
        }
        return run_client(
            host,
            static_cast<std::uint16_t>(port),
            ticks,
            input_delay,
            reconnect_at,
            rtt_ms,
            jitter_ms,
            loss_percent,
            duplicate_percent,
            burst_length,
            spike_ms,
            spike_interval_ms,
            spike_duration_ms,
            network_trace,
            map_path,
            client_lag,
            fixed_step_scheduler,
            auto_rematch);
    }
    print_usage();
    return 2;
}


