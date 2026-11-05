#pragma once

#include "core/world.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nsync::server {

struct ServerOptions {
    core::GameMode game_mode = core::GameMode::Tdm;
    int tick_rate = 60;
    int max_catchup_steps = 5;
    int max_ticks = 0;
    bool net = false;
    int port = 40100;
    int players = 2;
    std::string map = "maps/arena_01.json";
    std::vector<std::string> map_rotation;
    std::uint64_t seed = 1;
    int snapshot_interval = 3;
    bool original_input_sync = true;
    int server_lag_ticks = 8;
    int client_event_lag_ticks = 3;
    int sync_period_ticks = 30;
    int sync_time_to_step_ms = 0;
    bool late_input_replay = false;
    int late_input_replay_window_ticks = 120;
    int match_duration_ticks = 18000;
    int score_limit = 30;
    bool lobby = true;
    int lobby_countdown_ms = 3000;
    int match_end_delay_ms = 3000;
    int lobby_min_humans = 1;
};

int run_server(const ServerOptions& options);

} // namespace nsync::server

