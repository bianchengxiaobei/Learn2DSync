#include "server/server_app.h"

#include "core/fixed_step.h"
#include "core/map.h"
#include "net/real_server.h"
#include "shared/paths.h"
#include "shared/log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace nsync::server {
namespace {

std::atomic_bool g_stop{false};

void handle_signal(int) {
    g_stop.store(true);
}

bool load_server_map(
    const std::string& requested,
    core::Map& map,
    std::string* error) {
    namespace fs = std::filesystem;
    fs::path path;
    std::error_code fs_error;
    if (fs::is_regular_file(requested, fs_error)) {
        path = requested;
    } else if (const auto found =
                   shared::paths::find_file("assets/" + requested)) {
        path = *found;
    } else if (const auto found = shared::paths::find_file(requested)) {
        path = *found;
    }

    if (path.empty()) {
        if (error) {
            *error = "map not found: " + requested;
        }
        return false;
    }
    std::string load_error;
    if (!core::load_map_file(path, map, &load_error)) {
        if (error) {
            *error = path.string() + ": " + load_error;
        }
        return false;
    }
    if (map.id.empty() || map.id.size() > net::kMaxMapIdLength) {
        if (error) {
            *error = path.string() + ": invalid map id";
        }
        return false;
    }
    NSYNC_LOG_INFO(
        "server",
        "loaded map: ",
        path.string(),
        " id=",
        map.id,
        " fingerprint=",
        core::map_fingerprint(map));
    if (error) {
        error->clear();
    }
    return true;
}

int run_network_server(const ServerOptions& options) {
    if (const auto path =
            shared::paths::find_file("assets/data/content.json")) {
        std::string content_error;
        if (!core::content().load_file(*path, &content_error)) {
            NSYNC_LOG_ERROR(
                "server",
                "content load failed: ",
                content_error);
            return 1;
        }
        NSYNC_LOG_INFO(
            "server",
            "loaded content: heroes=",
            core::content().hero_count(),
            " weapons=",
            core::content().weapon_count(),
            " fingerprint=",
            core::content().fingerprint());
    } else {
        NSYNC_LOG_WARN(
            "server",
            "assets/data/content.json missing; using built-in content");
    }

    net::NetServerConfig config;
    config.game_mode = options.game_mode;
    config.port = static_cast<std::uint16_t>(
        std::clamp(options.port, 1, 65535));
    config.player_count = std::clamp(options.players, 1, core::kMaxPlayers);
    config.seed = options.seed;
    config.tick_rate = options.tick_rate;
    config.max_catchup_steps = options.max_catchup_steps;
    config.snapshot_interval = static_cast<std::uint32_t>(
        std::clamp(options.snapshot_interval, 1, 60));
    config.original_input_sync = options.original_input_sync;
    config.server_lag_ticks = static_cast<std::uint16_t>(
        std::clamp(options.server_lag_ticks, 0, 60));
    config.client_event_lag_ticks = static_cast<std::uint16_t>(
        std::clamp(options.client_event_lag_ticks, 0, 10));
    config.sync_period_ticks = static_cast<std::uint16_t>(
        std::clamp(options.sync_period_ticks, 0, 240));
    config.sync_time_to_step_ms = static_cast<std::int16_t>(
        std::clamp(options.sync_time_to_step_ms, -2000, 2000));
    config.late_input_replay = options.late_input_replay;
    config.late_input_replay_window_ticks = static_cast<std::uint32_t>(
        std::clamp(options.late_input_replay_window_ticks, 1, 600));
    config.match_duration_ticks = static_cast<std::uint32_t>(
        std::clamp(options.match_duration_ticks, 1, 10000000));
    config.score_limit = static_cast<std::uint16_t>(
        std::clamp(options.score_limit, 1, 1000));
    config.lobby_enabled = options.lobby;
    config.lobby_countdown_ms = static_cast<std::uint32_t>(
        std::clamp(options.lobby_countdown_ms, 0, 60000));
    config.match_end_delay_ms = static_cast<std::uint32_t>(
        std::clamp(options.match_end_delay_ms, 0, 60000));
    config.lobby_min_humans = static_cast<std::uint32_t>(
        std::clamp(options.lobby_min_humans, 1, config.player_count));

    const std::vector<std::string> map_paths =
        options.map_rotation.empty()
            ? std::vector<std::string>{options.map}
            : options.map_rotation;
    std::vector<core::Map> maps;
    maps.reserve(map_paths.size());
    for (const std::string& map_path : map_paths) {
        core::Map map;
        std::string map_error;
        if (!load_server_map(map_path, map, &map_error)) {
            NSYNC_LOG_ERROR(
                "server",
                "map load failed: ",
                map_error);
            return 1;
        }
        if (config.game_mode == core::GameMode::Control &&
            !map.has_control_objective()) {
            NSYNC_LOG_ERROR(
                "server",
                "map ",
                map.id,
                " has no control objective");
            return 1;
        }
        if (config.game_mode == core::GameMode::Crystal &&
            (map.drop_depots.empty() || map.drop_spawns.empty())) {
            NSYNC_LOG_ERROR(
                "server",
                "map ",
                map.id,
                " has no crystal drops or depots");
            return 1;
        }
        maps.push_back(std::move(map));
    }

    net::RealServer server;
    std::string error;
    if (!server.start(maps, config, &error)) {
        NSYNC_LOG_ERROR("server", "network server failed: ", error);
        return 1;
    }
    if (options.max_ticks > 0) {
        NSYNC_LOG_INFO(
            "server",
            "network server running for ",
            options.max_ticks,
            " ticks");
    }

    const auto start = core::FixedStepClock::Clock::now();
    while (!g_stop.load(std::memory_order_relaxed)) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            core::FixedStepClock::Clock::now() - start);
        server.update(static_cast<std::uint32_t>(elapsed.count()));
        if (options.max_ticks > 0 &&
            server.tick() >= static_cast<std::uint32_t>(options.max_ticks)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    NSYNC_LOG_INFO(
        "server",
        "network server stopped: tick=",
        server.tick(),
        " clients=",
        server.connected_clients(),
        " inputs=",
        server.stats().input_packets,
        " rejected=",
        server.stats().rejected_inputs,
        " late_replays=",
        server.stats().late_replays,
        " late_replay_batches=",
        server.stats().late_replay_batches,
        " late_replay_failures=",
        server.stats().late_replay_failures,
        " late_clamped=",
        server.stats().late_inputs,
        " rate_limited=",
        server.stats().rate_limited_inputs,
        " packets_rate_limited=",
        server.stats().rate_limited_packets,
        " snapshots=",
        server.stats().snapshots_sent,
        " snapshot_bytes=",
        server.stats().snapshot_bytes,
        " snapshot_broadcasts=",
        server.stats().snapshot_broadcasts,
        " snapshot_gen_avg_us=",
        server.stats().snapshot_broadcasts > 0
            ? server.stats().snapshot_generation_us_total /
                server.stats().snapshot_broadcasts
            : 0,
        " snapshot_gen_max_us=",
        server.stats().snapshot_generation_us_max,
        " snapshot_avg_us=",
        server.stats().snapshot_broadcasts > 0
            ? server.stats().snapshot_broadcast_us_total /
                server.stats().snapshot_broadcasts
            : 0,
        " snapshot_max_us=",
        server.stats().snapshot_broadcast_us_max,
        " rtt=",
        server.transport_stats().rtt_ms,
        " retrans=",
        server.transport_stats().retransmissions,
        " sessions=",
        server.transport_stats().sessions,
        " matches=",
        server.stats().matches_started,
        " ended=",
        server.stats().matches_ended,
        " bots=",
        server.stats().bot_takeovers);
    server.stop();
    return 0;
}

} // namespace

int run_server(const ServerOptions& options) {
    if (options.net) {
        return run_network_server(options);
    }
    std::signal(SIGINT, handle_signal);
#ifdef SIGTERM
    std::signal(SIGTERM, handle_signal);
#endif
    g_stop.store(false);

    const int tick_rate = options.tick_rate > 0 ? options.tick_rate : 60;
    core::FixedStepClock clock(tick_rate, options.max_catchup_steps);
    const auto start = core::FixedStepClock::Clock::now();
    clock.reset(start);

    NSYNC_LOG_INFO(
        "server",
        "headless loop started: tick_rate=",
        clock.tick_rate(),
        " max_catchup=",
        clock.max_steps(),
        " max_ticks=",
        options.max_ticks);

    std::uint64_t tick = 0;
    double dropped_seconds = 0.0;

    while (!g_stop.load(std::memory_order_relaxed) &&
           (options.max_ticks <= 0 ||
            tick < static_cast<std::uint64_t>(options.max_ticks))) {
        const auto now = core::FixedStepClock::Clock::now();
        const core::StepAdvance advance = clock.advance(now);
        dropped_seconds += advance.dropped_seconds;

        for (int i = 0; i < advance.steps; ++i) {
            ++tick;
            if (tick % static_cast<std::uint64_t>(clock.tick_rate()) == 0) {
                const double simulated =
                    static_cast<double>(tick) / static_cast<double>(clock.tick_rate());
                NSYNC_LOG_INFO(
                    "server",
                    "tick=",
                    tick,
                    " simulated=",
                    simulated,
                    "s dropped=",
                    dropped_seconds,
                    "s");
            }
            if (options.max_ticks > 0 &&
                tick >= static_cast<std::uint64_t>(options.max_ticks)) {
                break;
            }
        }

        if (advance.steps == 0) {
            const auto wait = clock.time_until_next_step();
            if (wait > std::chrono::nanoseconds::zero()) {
                std::this_thread::sleep_for(wait);
            }
        }
    }

    NSYNC_LOG_INFO("server", "stopped at tick=", tick);
    return 0;
}

} // namespace nsync::server



